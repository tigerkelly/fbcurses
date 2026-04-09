/*
 * fbnet_client.c — C client implementation for the fbcurses UDP protocol.
 *
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

#define _POSIX_C_SOURCE 200809L
#include "fbnet_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

/* ── Border name table ───────────────────────────────────────────── */
static const char *_borderName(fbncBorder b) {
    switch (b) {
        case FBNC_BORDER_NONE:    return "none";
        case FBNC_BORDER_SINGLE:  return "single";
        case FBNC_BORDER_DOUBLE:  return "double";
        case FBNC_BORDER_ROUNDED: return "rounded";
        case FBNC_BORDER_THICK:   return "thick";
        case FBNC_BORDER_DASHED:  return "dashed";
        default:                  return "single";
    }
}

static const char *_alignName(fbncAlign a) {
    switch (a) {
        case FBNC_ALIGN_CENTER: return "center";
        case FBNC_ALIGN_RIGHT:  return "right";
        default:                return "left";
    }
}

static const char *_toastName(fbncToastKind k) {
    switch (k) {
        case FBNC_TOAST_SUCCESS: return "success";
        case FBNC_TOAST_WARNING: return "warning";
        case FBNC_TOAST_ERROR:   return "error";
        default:                 return "info";
    }
}

/* ── Colour formatting ───────────────────────────────────────────── */
static void _colorStr(char *buf, fbncColor c) {
    /* FBNC_TRANSPARENT sentinel — send the named keyword, not a hex value */
    if (c == 0xFF000000u) {
        memcpy(buf, "transparent", 12);   /* 11 chars + NUL */
        return;
    }
    snprintf(buf, 8, "#%02X%02X%02X",
             (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* ── Attribute string ────────────────────────────────────────────── */
static void _attrStr(char *buf, uint8_t attr) {
    if (attr == FBNC_ATTR_NONE) { strcpy(buf, "none"); return; }
    buf[0] = '\0';
    if (attr & FBNC_ATTR_BOLD)      strcat(buf, "bold|");
    if (attr & FBNC_ATTR_DIM)       strcat(buf, "dim|");
    if (attr & FBNC_ATTR_UNDERLINE)  strcat(buf, "underline|");
    if (attr & FBNC_ATTR_REVERSE)   strcat(buf, "reverse|");
    int len = (int)strlen(buf);
    if (len > 0 && buf[len-1] == '|') buf[len-1] = '\0';
    if (!buf[0]) strcpy(buf, "none");
}

/* ═══════════════════════════════════════════════════════════════════
 *  Client context
 * ═══════════════════════════════════════════════════════════════════ */

#define FBNC_BATCH_CAP  3800   /* keep under typical MTU          */

struct fbNetClient {
    int    fd;
    struct sockaddr_in srv;
    int    timeoutMs;

    /* Multicast */
    bool   is_multicast;

    /* Batching */
    bool   batching;
    char   batchBuf[FBNC_BATCH_CAP + 16];
    int    batchLen;
    int    batchCmds;
};

/* ═══════════════════════════════════════════════════════════════════
 *  Connection
 * ═══════════════════════════════════════════════════════════════════ */

fbNetClient *fbncOpen(const char *host, uint16_t port)
{
    fbNetClient *cl = calloc(1, sizeof(fbNetClient));
    if (!cl) return NULL;

    cl->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (cl->fd < 0) { free(cl); return NULL; }

    /* Resolve host */
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);
    if (getaddrinfo(host, portStr, &hints, &res) != 0) {
        close(cl->fd); free(cl); return NULL;
    }
    memcpy(&cl->srv, res->ai_addr, sizeof(cl->srv));
    freeaddrinfo(res);

    /* Bind so we can receive replies */
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    bind(cl->fd, (struct sockaddr *)&local, sizeof(local));

    cl->timeoutMs = 200;
    return cl;
}

void fbncClose(fbNetClient *cl)
{
    if (!cl) return;
    close(cl->fd);
    free(cl);
}

void fbncSetTimeout(fbNetClient *cl, int ms)
{
    if (cl) cl->timeoutMs = ms;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Low-level send / receive
 * ═══════════════════════════════════════════════════════════════════ */

void fbncSend(fbNetClient *cl, const char *cmd)
{
    if (!cl || !cmd) return;

    if (cl->batching) {
        int len = (int)strlen(cmd);
        /* Ensure newline separator */
        int needNl = (len > 0 && cmd[len-1] != '\n') ? 1 : 0;
        if (cl->batchLen + len + needNl < FBNC_BATCH_CAP) {
            memcpy(cl->batchBuf + cl->batchLen, cmd, (size_t)len);
            cl->batchLen += len;
            if (needNl) cl->batchBuf[cl->batchLen++] = '\n';
            cl->batchBuf[cl->batchLen] = '\0';
            cl->batchCmds++;
        }
        return;
    }

    sendto(cl->fd, cmd, strlen(cmd), 0,
           (struct sockaddr *)&cl->srv, sizeof(cl->srv));
}

bool fbncSendRecv(fbNetClient *cl, const char *cmd,
                  char *replyBuf, int replyLen)
{
    if (!cl) return false;

    /* Never buffer reply-expecting calls */
    bool wasBatching = cl->batching;
    cl->batching = false;
    fbncSend(cl, cmd);
    cl->batching = wasBatching;

    if (!replyBuf || replyLen <= 0) return true;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(cl->fd, &fds);
    struct timeval tv = { cl->timeoutMs / 1000,
                         (cl->timeoutMs % 1000) * 1000 };
    if (select(cl->fd + 1, &fds, NULL, NULL, &tv) <= 0) return false;

    ssize_t n = recv(cl->fd, replyBuf, (size_t)(replyLen - 1), 0);
    if (n <= 0) return false;
    replyBuf[n] = '\0';
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Batching
 * ═══════════════════════════════════════════════════════════════════ */

void fbncBatchBegin(fbNetClient *cl)
{
    if (!cl) return;
    cl->batching  = true;
    cl->batchLen  = 0;
    cl->batchCmds = 0;
    cl->batchBuf[0] = '\0';
}

int fbncBatchEnd(fbNetClient *cl)
{
    if (!cl || !cl->batching) return -1;
    cl->batching = false;
    if (cl->batchLen == 0) return 0;

    sendto(cl->fd, cl->batchBuf, (size_t)cl->batchLen, 0,
           (struct sockaddr *)&cl->srv, sizeof(cl->srv));

    int cmds = cl->batchCmds;
    cl->batchLen = 0;
    cl->batchCmds = 0;
    return cmds;
}

void fbncBatchDiscard(fbNetClient *cl)
{
    if (!cl) return;
    cl->batching = false;
    cl->batchLen = 0;
    cl->batchCmds = 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Internal command builder
 * ═══════════════════════════════════════════════════════════════════ */

static void _send(fbNetClient *cl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void _send(fbNetClient *cl, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fbncSend(cl, buf);
}

/* Quote a string so commas and special chars survive tokenisation */
static void _quoted(const char *in, char *out, int outLen)
{
    int o = 0;
    out[o++] = '"';
    for (const char *p = in; *p && o < outLen - 3; p++) {
        if (*p == '"')  { out[o++] = '\\'; out[o++] = '"'; }
        else if (*p == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else if (*p == '\n') { out[o++] = '\\'; out[o++] = 'n';  }
        else out[o++] = *p;
    }
    out[o++] = '"';
    out[o]   = '\0';
}

/* ═══════════════════════════════════════════════════════════════════
 *  Screen
 * ═══════════════════════════════════════════════════════════════════ */

void fbncFlush(fbNetClient *cl)  { fbncSend(cl, "flush"); }
void fbncCursor(fbNetClient *cl, bool v)
    { _send(cl, "cursor,%d", v ? 1 : 0); }

void fbncClear(fbNetClient *cl, fbncColor color)
{
    char c[16]; _colorStr(c, color);
    _send(cl, "clear,%s", c);
}

bool fbncScreenSize(fbNetClient *cl, int *pw, int *ph, int *pc, int *pr)
{
    char reply[128];
    if (!fbncSendRecv(cl, "screen_size", reply, sizeof(reply))) return false;
    int w,h,c,r;
    if (sscanf(reply, "ok,screen_size,%d,%d,%d,%d", &w,&h,&c,&r) != 4)
        return false;
    if (pw) *pw=w;
    if (ph) *ph=h;
    if (pc) *pc=c;
    if (pr) *pr=r;
    return true;
}

int fbncPing(fbNetClient *cl)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    char reply[64];
    if (!fbncSendRecv(cl, "ping", reply, sizeof(reply))) return -1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (int)((t1.tv_sec - t0.tv_sec) * 1000 +
                 (t1.tv_nsec - t0.tv_nsec) / 1000000);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Windows
 * ═══════════════════════════════════════════════════════════════════ */

int fbncWinNew(fbNetClient *cl, int col, int row, int cols, int rows)
{
    char cmd[64], reply[64];
    snprintf(cmd, sizeof(cmd), "win_new,%d,%d,%d,%d", col, row, cols, rows);
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return -1;
    int id;
    if (sscanf(reply, "ok,win_new,%d", &id) != 1) return -1;
    return id;
}

void fbncWinDel    (fbNetClient *cl, int w) { _send(cl,"win_del,%d",w); }
void fbncWinMove   (fbNetClient *cl, int w, int c, int r)
    { _send(cl,"win_move,%d,%d,%d",w,c,r); }
void fbncWinResize (fbNetClient *cl, int w, int c, int r)
    { _send(cl,"win_resize,%d,%d,%d",w,c,r); }
void fbncWinRefresh(fbNetClient *cl, int w) { _send(cl,"win_refresh,%d",w); }
void fbncWinFont   (fbNetClient *cl, int w, const char *fn)
    { _send(cl,"win_font,%d,%s",w,fn?fn:"vga"); }

void fbncWinClear(fbNetClient *cl, int w, fbncColor bg)
{
    char c[16]; _colorStr(c, bg);
    _send(cl, "win_clear,%d,%s", w, c);
}

bool fbncWinSize(fbNetClient *cl, int win, int *cols, int *rows)
{
    char cmd[32], reply[64];
    snprintf(cmd, sizeof(cmd), "win_size,%d", win);
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return false;
    int c, r;
    if (sscanf(reply, "ok,win_size,%d,%d", &c, &r) != 2) return false;
    if (cols) *cols = c;
    if (rows) *rows = r;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Text
 * ═══════════════════════════════════════════════════════════════════ */

void fbncMove(fbNetClient *cl, int w, int c, int r)
    { _send(cl,"move,%d,%d,%d",w,c,r); }

void fbncColors(fbNetClient *cl, int w, fbncColor fg, fbncColor bg)
{
    char f[16], b[8];
    _colorStr(f, fg); _colorStr(b, bg);
    _send(cl, "colors,%d,%s,%s", w, f, b);
}

void fbncAttr(fbNetClient *cl, int w, uint8_t attr)
{
    char a[64]; _attrStr(a, attr);
    _send(cl, "attr,%d,%s", w, a);
}

void fbncPrint(fbNetClient *cl, int w, const char *str)
{
    char q[4096]; _quoted(str, q, sizeof(q));
    _send(cl, "print,%d,%s", w, q);
}

void fbncPrintAt(fbNetClient *cl, int w, int col, int row, const char *str)
{
    char q[4096]; _quoted(str, q, sizeof(q));
    _send(cl, "print_at,%d,%d,%d,%s", w, col, row, q);
}

void fbncPrintFmt(fbNetClient *cl, int w, const char *fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fbncPrint(cl, w, buf);
}

void fbncPrintAtFmt(fbNetClient *cl, int w, int col, int row,
                    const char *fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fbncPrintAt(cl, w, col, row, buf);
}

void fbncPrintAlign(fbNetClient *cl, int w, int row, fbncAlign align,
                    const char *str)
{
    char q[4096]; _quoted(str, q, sizeof(q));
    _send(cl, "print_align,%d,%d,%s,%s", w, row, _alignName(align), q);
}

void fbncPrintPx(fbNetClient *cl, int x, int y, const char *str,
                 fbncColor fg, fbncColor bg, uint8_t attr,
                 const char *font)
{
    char q[2048], f[8], b[8], a[64];
    _quoted(str, q, sizeof(q));
    _colorStr(f, fg); _colorStr(b, bg); _attrStr(a, attr);
    _send(cl, "print_px,%d,%d,%s,%s,%s,%s,%s",
          x, y, q, f, b, a, font ? font : "vga");
}

/* ═══════════════════════════════════════════════════════════════════
 *  Drawing
 * ═══════════════════════════════════════════════════════════════════ */

void fbncPixel(fbNetClient *cl, int x, int y, fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"pixel,%d,%d,%s",x,y,s);
}

void fbncLine(fbNetClient *cl, int x0,int y0,int x1,int y1,fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"line,%d,%d,%d,%d,%s",x0,y0,x1,y1,s);
}

void fbncRect(fbNetClient *cl, int x,int y,int w,int h,fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"rect,%d,%d,%d,%d,%s",x,y,w,h,s);
}

void fbncFillRect(fbNetClient *cl, int x,int y,int w,int h,fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"fill_rect,%d,%d,%d,%d,%s",x,y,w,h,s);
}

void fbncCircle(fbNetClient *cl, int cx,int cy,int r,fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"circle,%d,%d,%d,%s",cx,cy,r,s);
}

void fbncFillCircle(fbNetClient *cl, int cx,int cy,int r,fbncColor c)
{
    char s[16]; _colorStr(s,c);
    _send(cl,"fill_circle,%d,%d,%d,%s",cx,cy,r,s);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Borders
 * ═══════════════════════════════════════════════════════════════════ */

void fbncDrawBorder(fbNetClient *cl, int w, fbncBorder style, fbncColor color)
{
    char c[16]; _colorStr(c,color);
    _send(cl,"border,%d,%s,%s",w,_borderName(style),c);
}

void fbncBox(fbNetClient *cl, int w, int col, int row,
             int cols, int rows, fbncBorder style, fbncColor color)
{
    char c[16]; _colorStr(c,color);
    _send(cl,"box,%d,%d,%d,%d,%d,%s,%s",w,col,row,cols,rows,_borderName(style),c);
}

void fbncTitleBar(fbNetClient *cl, int w, const char *title,
                  fbncBorder style, fbncColor bc, fbncColor tfg, fbncColor tbg)
{
    char q[1024], sc[8], sf[8], sb[8];
    _quoted(title,q,sizeof(q)); _colorStr(sc,bc);
    _colorStr(sf,tfg); _colorStr(sb,tbg);
    _send(cl,"title_bar,%d,%s,%s,%s,%s,%s",w,q,_borderName(style),sc,sf,sb);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Widgets
 * ═══════════════════════════════════════════════════════════════════ */

void fbncProgress(fbNetClient *cl, int w, int col, int row,
                  int width, int pct, fbncColor fg, fbncColor bg, bool show)
{
    char f[16],b[8]; _colorStr(f,fg); _colorStr(b,bg);
    _send(cl,"progress,%d,%d,%d,%d,%d,%s,%s,%d",
          w,col,row,width,pct,f,b,show?1:0);
}

void fbncSpinner(fbNetClient *cl, int w, int col, int row,
                 int tick, fbncColor fg, fbncColor bg)
{
    char f[16],b[8]; _colorStr(f,fg); _colorStr(b,bg);
    _send(cl,"spinner,%d,%d,%d,%d,%s,%s",w,col,row,tick,f,b);
}

void fbncTick(fbNetClient *cl, int w, int col, int row,
              fbncColor fg, fbncColor bg)
{
    char f[16],b[8]; _colorStr(f,fg); _colorStr(b,bg);
    _send(cl,"tick,%d,%d,%d,%s,%s",w,col,row,f,b);
}

void fbncGauge(fbNetClient *cl, int w, int col, int row,
               int height, int value, int maxVal,
               fbncColor fg, fbncColor bg)
{
    char f[16],b[8]; _colorStr(f,fg); _colorStr(b,bg);
    _send(cl,"gauge,%d,%d,%d,%d,%d,%d,%s,%s",
          w,col,row,height,value,maxVal,f,b);
}

void fbncSparkline(fbNetClient *cl, int w, int col, int row,
                   int width, fbncColor fg, fbncColor bg,
                   const float *values, int nValues)
{
    char f[16],b[8]; _colorStr(f,fg); _colorStr(b,bg);
    char buf[2048];
    int len = snprintf(buf, sizeof(buf),
                       "sparkline,%d,%d,%d,%d,%s,%s",
                       w, col, row, width, f, b);
    for (int i = 0; i < nValues && len < (int)sizeof(buf)-20; i++)
        len += snprintf(buf+len, sizeof(buf)-(size_t)len, ",%.3f", values[i]);
    fbncSend(cl, buf);
}

void fbncScrollUp(fbNetClient *cl, int w, int n, fbncColor bg)
{
    char b[16]; _colorStr(b,bg);
    _send(cl,"scroll_up,%d,%d,%s",w,n,b);
}

void fbncScrollDown(fbNetClient *cl, int w, int n, fbncColor bg)
{
    char b[16]; _colorStr(b,bg);
    _send(cl,"scroll_down,%d,%d,%s",w,n,b);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Notifications
 * ═══════════════════════════════════════════════════════════════════ */

void fbncToast(fbNetClient *cl, fbncToastKind kind, int ms, const char *msg)
{
    char q[1024]; _quoted(msg, q, sizeof(q));
    _send(cl, "toast,%s,%d,%s", _toastName(kind), ms, q);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Convenience batch helpers
 * ═══════════════════════════════════════════════════════════════════ */

void fbncRefreshFlush(fbNetClient *cl, int win)
{
    fbncBatchBegin(cl);
    fbncWinRefresh(cl, win);
    fbncFlush(cl);
    fbncBatchEnd(cl);
}

void fbncRefreshAllFlush(fbNetClient *cl)
{
    fbncBatchBegin(cl);
    fbncSend(cl, "refresh_all,1");
    fbncBatchEnd(cl);
}


/* ═══════════════════════════════════════════════════════════════════
 *  Table
 * ═══════════════════════════════════════════════════════════════════ */

void fbncTable(fbNetClient *cl, int win, int startCol, int startRow,
               const fbncTableCol *cols,
               const char * const *const *rows, int nRows,
               int selRow,
               fbncColor hfg, fbncColor hbg,
               fbncColor cfg, fbncColor cbg,
               fbncColor sfg, fbncColor sbg)
{
    if (!cl || !cols) return;
    char hfgS[8],hbgS[8],cfgS[8],cbgS[8],sfgS[8],sbgS[8];
    _colorStr(hfgS,hfg); _colorStr(hbgS,hbg);
    _colorStr(cfgS,cfg); _colorStr(cbgS,cbg);
    _colorStr(sfgS,sfg); _colorStr(sbgS,sbg);

    /* Build: table,win,sc,sr,hfg,hbg,cfg,cbg,sfg,sbg,sel,hdr0,w0,a0,...,|,d00,... */
    char buf[3800];
    int len = snprintf(buf, sizeof(buf),
        "table,%d,%d,%d,%s,%s,%s,%s,%s,%s,%d",
        win, startCol, startRow,
        hfgS, hbgS, cfgS, cbgS, sfgS, sbgS, selRow);

    int nCols = 0;
    for (; cols[nCols].header; nCols++) {
        char q[128]; _quoted(cols[nCols].header, q, sizeof(q));
        len += snprintf(buf+len, sizeof(buf)-(size_t)len, ",%s,%d,%s",
                        q, cols[nCols].width, _alignName(cols[nCols].align));
    }
    /* Separator */
    len += snprintf(buf+len, sizeof(buf)-(size_t)len, ",|");

    /* Data */
    for (int r = 0; r < nRows && len < 3700; r++)
        for (int c2 = 0; c2 < nCols; c2++) {
            const char *cell = rows[r][c2] ? rows[r][c2] : "";
            char q[128]; _quoted(cell, q, sizeof(q));
            len += snprintf(buf+len, sizeof(buf)-(size_t)len, ",%s", q);
        }

    fbncSend(cl, buf);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Colour math
 * ═══════════════════════════════════════════════════════════════════ */

static fbncColor _colorReply(fbNetClient *cl, const char *cmd)
{
    char reply[64];
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return 0;
    /* reply: ok,<cmd>,#RRGGBB */
    char *hash = strchr(reply, '#');
    if (!hash) return 0;
    unsigned r2,g2,b2;
    if (sscanf(hash+1, "%2x%2x%2x", &r2, &g2, &b2) != 3) return 0;
    return FBNC_RGB(r2, g2, b2);
}

fbncColor fbncBlend(fbNetClient *cl, fbncColor dst, fbncColor src)
{
    char cmd[32]; snprintf(cmd,sizeof(cmd),"blend,%s,%s",
        (char[8]){0}, (char[8]){0});
    char d[8], s2[8]; _colorStr(d,dst); _colorStr(s2,src);
    snprintf(cmd,sizeof(cmd),"blend,%s,%s",d,s2);
    return _colorReply(cl, cmd);
}
fbncColor fbncLerp(fbNetClient *cl, fbncColor a, fbncColor b, float t)
{
    char ca[8],cb[8]; _colorStr(ca,a); _colorStr(cb,b);
    char cmd[48]; snprintf(cmd,sizeof(cmd),"lerp,%s,%s,%.3f",ca,cb,t);
    return _colorReply(cl, cmd);
}
fbncColor fbncDarken(fbNetClient *cl, fbncColor c, float factor)
{
    char cs[8]; _colorStr(cs,c);
    char cmd[32]; snprintf(cmd,sizeof(cmd),"darken,%s,%.3f",cs,factor);
    return _colorReply(cl, cmd);
}
fbncColor fbncLighten(fbNetClient *cl, fbncColor c, float factor)
{
    char cs[8]; _colorStr(cs,c);
    char cmd[32]; snprintf(cmd,sizeof(cmd),"lighten,%s,%.3f",cs,factor);
    return _colorReply(cl, cmd);
}
fbncColor fbncGrayscale(fbNetClient *cl, fbncColor c)
{
    char cs[8]; _colorStr(cs,c);
    char cmd[24]; snprintf(cmd,sizeof(cmd),"grayscale,%s",cs);
    return _colorReply(cl, cmd);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Custom border
 * ═══════════════════════════════════════════════════════════════════ */

void fbncCustomBorder(fbNetClient *cl, int win,
                      int tl, int tr, int bl, int br,
                      int h,  int v,  fbncColor color)
{
    char c[16]; _colorStr(c, color);
    _send(cl, "custom_border,%d,%d,%d,%d,%d,%d,%d,%s",
          win, tl, tr, bl, br, h, v, c);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Interactive dialogs
 * ═══════════════════════════════════════════════════════════════════ */

int fbncMenuDlg(fbNetClient *cl, int col, int row,
                fbncColor fg,    fbncColor bg,
                fbncColor fgSel, fbncColor bgSel,
                fbncBorder border,
                const char **labels, const int *ids, int nItems)
{
    char f[16],b[8],fs[8],bs[8];
    _colorStr(f,fg); _colorStr(b,bg); _colorStr(fs,fgSel); _colorStr(bs,bgSel);

    char buf[2048];
    int len = snprintf(buf,sizeof(buf),"menu,%d,%d,%s,%s,%s,%s,%s",
                       col,row,f,b,fs,bs,_borderName(border));
    for (int i = 0; i < nItems; i++) {
        char q[256]; _quoted(labels[i], q, sizeof(q));
        len += snprintf(buf+len, sizeof(buf)-(size_t)len, ",%s,%d", q, ids[i]);
    }

    char reply[64];
    if (!fbncSendRecv(cl, buf, reply, sizeof(reply))) return -1;
    int result = -1;
    sscanf(reply, "ok,menu,%d", &result);
    return result;
}

const char *fbncMsgBox(fbNetClient *cl,
                       const char *title, const char *msg,
                       const char *buttons, const char *kind)
{
    static char reply[64];
    char qt[512], qm[1024];
    _quoted(title, qt, sizeof(qt));
    _quoted(msg,   qm, sizeof(qm));
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "msgbox,%s,%s,%s,%s",
             qt, qm,
             buttons ? buttons : "ok",
             kind    ? kind    : "info");
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return "cancel";
    /* reply: ok,msgbox,<result> */
    char *comma = strrchr(reply, ',');
    return comma ? comma+1 : "cancel";
}

bool fbncFilePick(fbNetClient *cl, const char *startDir,
                  char *outPath, int outLen)
{
    char cmd[640];
    if (startDir) {
        char q[512]; _quoted(startDir, q, sizeof(q));
        snprintf(cmd, sizeof(cmd), "file_pick,%s", q);
    } else {
        snprintf(cmd, sizeof(cmd), "file_pick");
    }
    char reply[768];
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return false;
    /* reply: ok,file_pick,/path */
    char *comma = strstr(reply, "ok,file_pick,");
    if (!comma) return false;
    const char *path = comma + strlen("ok,file_pick,");
    if (!path[0]) return false;
    snprintf(outPath, (size_t)outLen, "%s", path);
    return true;
}

fbncColor fbncColorPick(fbNetClient *cl, fbncColor initial)
{
    char cs[8]; _colorStr(cs, initial);
    char cmd[32]; snprintf(cmd, sizeof(cmd), "color_pick,%s", cs);
    char reply[32];
    if (!fbncSendRecv(cl, cmd, reply, sizeof(reply))) return 0;
    char *hash = strchr(reply, '#');
    if (!hash) return 0;
    unsigned r2,g2,b2;
    if (sscanf(hash+1, "%2x%2x%2x", &r2, &g2, &b2) != 3) return 0;
    return FBNC_RGB(r2, g2, b2);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Server info
 * ═══════════════════════════════════════════════════════════════════ */

static char _infoReplyBuf[512];

const char *fbncVersion(fbNetClient *cl)
{
    if (!fbncSendRecv(cl, "version", _infoReplyBuf, sizeof(_infoReplyBuf)))
        return "unknown";
    char *comma = strstr(_infoReplyBuf, "ok,version,");
    return comma ? comma + strlen("ok,version,") : _infoReplyBuf;
}

const char *fbncFonts(fbNetClient *cl)
{
    if (!fbncSendRecv(cl, "fonts", _infoReplyBuf, sizeof(_infoReplyBuf)))
        return "";
    char *comma = strstr(_infoReplyBuf, "ok,fonts,");
    return comma ? comma + strlen("ok,fonts,") : _infoReplyBuf;
}


/* ═══════════════════════════════════════════════════════════════════
 *  Multicast client
 * ═══════════════════════════════════════════════════════════════════ */

fbNetClient *fbncOpenMulticast(const char *group, uint16_t port,
                                uint8_t ttl, bool loopback)
{
    fbNetClient *cl = calloc(1, sizeof(fbNetClient));
    if (!cl) return NULL;

    cl->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (cl->fd < 0) { free(cl); return NULL; }

    /* Destination: the multicast group address */
    memset(&cl->srv, 0, sizeof(cl->srv));
    cl->srv.sin_family = AF_INET;
    cl->srv.sin_port   = htons(port);
    if (inet_pton(AF_INET, group, &cl->srv.sin_addr) != 1) {
        close(cl->fd); free(cl); return NULL;
    }

    /* Validate multicast range */
    uint32_t addr_h = ntohl(cl->srv.sin_addr.s_addr);
    if ((addr_h >> 28) != 14) {
        close(cl->fd); free(cl); return NULL;
    }

    /* Set TTL */
    if (setsockopt(cl->fd, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl, sizeof(ttl)) < 0) {
        close(cl->fd); free(cl); return NULL;
    }

    /* Loopback: allow local host to receive its own multicast packets */
    uint8_t loop = loopback ? 1 : 0;
    setsockopt(cl->fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    cl->timeoutMs   = 200;
    cl->is_multicast = true;
    return cl;
}

bool fbncSetMulticastTTL(fbNetClient *cl, uint8_t ttl)
{
    if (!cl || !cl->is_multicast) return false;
    return setsockopt(cl->fd, IPPROTO_IP, IP_MULTICAST_TTL,
                      &ttl, sizeof(ttl)) == 0;
}

bool fbncSetMulticastLoopback(fbNetClient *cl, bool enable)
{
    if (!cl || !cl->is_multicast) return false;
    uint8_t loop = enable ? 1 : 0;
    return setsockopt(cl->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                      &loop, sizeof(loop)) == 0;
}

bool fbncSetMulticastInterface(fbNetClient *cl, const char *ifAddr)
{
    if (!cl || !cl->is_multicast) return false;
    struct in_addr iface;
    if (!ifAddr) {
        iface.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ifAddr, &iface) != 1) return false;
    }
    return setsockopt(cl->fd, IPPROTO_IP, IP_MULTICAST_IF,
                      &iface, sizeof(iface)) == 0;
}

bool fbncIsMulticast(const fbNetClient *cl)
{
    return cl ? cl->is_multicast : false;
}
