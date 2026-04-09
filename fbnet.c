/*
 * fbnet.c — UDP remote-rendering server for fbcurses.
 *
 * See fbnet.h for the full protocol specification.
 *
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

#define _POSIX_C_SOURCE 200809L
#include "fbnet.h"
#include "qparse.h"
#include "fbcurses_internal.h"
#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════
 *  Server context
 * ═══════════════════════════════════════════════════════════════════ */

struct fbNetServer {
    fbScreen  *scr;
    int        fd;                             /* UDP socket             */
    uint16_t   port;
    fbWindow  *wins[FB_NET_MAX_WINDOWS];       /* handle → window*       */
    volatile bool stop;

    uint64_t   pktsIn;
    uint64_t   pktsErr;

    fbNetLogLevel logLevel;
    FILE         *logFile;

    /* Multicast groups this server has joined */
    struct in_addr mcastGroups[FB_NET_MAX_GROUPS];
    int            nMcastGroups;

    /* Set true while inside fbNetRun() so the dispatcher refuses
       blocking commands (toast/menu/msgbox/file_pick/color_pick).
       Blocking inside fbNetRun would freeze the server and swallow
       keypresses (including the q/Esc quit key).                   */
    bool       inEventLoop;
};

/* ═══════════════════════════════════════════════════════════════════
 *  Logging helpers
 * ═══════════════════════════════════════════════════════════════════ */

static void _netLog(fbNetServer *srv, fbNetLogLevel level,
                    const char *fmt, ...)
    __attribute__((format(printf,3,4)));

static void _netLog(fbNetServer *srv, fbNetLogLevel level,
                    const char *fmt, ...)
{
    if (!srv || level > srv->logLevel) return;
    FILE *fp = srv->logFile ? srv->logFile : stderr;
    fprintf(fp, "[fbnet] ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fflush(fp);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Argument parser
 * ═══════════════════════════════════════════════════════════════════ */

/* Split a command line into tokens in-place.
   Handles "quoted strings" with \" and \\ escapes.
   Returns number of tokens. */
/* qparse() is implemented in qparse.c — see qparse.h */

/* ── Colour parsing ──────────────────────────────────────────────── */

typedef struct { const char *name; fbColor color; } _NamedColor;
static const _NamedColor _namedColors[] = {
    { "black",          FB_BLACK         },
    { "white",          FB_WHITE         },
    { "red",            FB_RED           },
    { "green",          FB_GREEN         },
    { "blue",           FB_BLUE          },
    { "yellow",         FB_YELLOW        },
    { "magenta",        FB_MAGENTA       },
    { "cyan",           FB_CYAN          },
    { "gray",           FB_GRAY          },
    { "grey",           FB_GRAY          },
    { "bright_red",     FB_BRIGHT_RED    },
    { "bright_green",   FB_BRIGHT_GREEN  },
    { "bright_blue",    FB_BRIGHT_BLUE   },
    { "bright_yellow",  FB_BRIGHT_YELLOW },
    { "bright_magenta", FB_BRIGHT_MAGENTA},
    { "bright_cyan",    FB_BRIGHT_CYAN   },
    { "transparent",    FB_TRANSPARENT   },
    { NULL, 0 }
};

static bool _parseColor(const char *s, fbColor *out)
{
    if (!s || !*s) return false;

    /* Named colour */
    for (int i = 0; _namedColors[i].name; i++) {
        if (strcasecmp(s, _namedColors[i].name) == 0) {
            *out = _namedColors[i].color;
            return true;
        }
    }

    /* #RRGGBB */
    if (s[0] == '#' && strlen(s) == 7) {
        unsigned r, g, b;
        if (sscanf(s+1, "%2x%2x%2x", &r, &g, &b) == 3) {
            *out = FB_RGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
            return true;
        }
    }

    /* RRGGBB without # */
    if (strlen(s) == 6) {
        unsigned r, g, b;
        if (sscanf(s, "%2x%2x%2x", &r, &g, &b) == 3) {
            *out = FB_RGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
            return true;
        }
    }

    /* 0xRRGGBB */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned long v = strtoul(s+2, NULL, 16);
        *out = FB_RGB((v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
        return true;
    }

    return false;
}

/* ── Border style parsing ────────────────────────────────────────── */
static bool _parseBorder(const char *s, fbBorderStyle *out)
{
    if (!s) return false;
    if (strcasecmp(s, "none")    == 0) { *out = FB_BORDER_NONE;    return true; }
    if (strcasecmp(s, "single")  == 0) { *out = FB_BORDER_SINGLE;  return true; }
    if (strcasecmp(s, "double")  == 0) { *out = FB_BORDER_DOUBLE;  return true; }
    if (strcasecmp(s, "rounded") == 0) { *out = FB_BORDER_ROUNDED; return true; }
    if (strcasecmp(s, "thick")   == 0) { *out = FB_BORDER_THICK;   return true; }
    if (strcasecmp(s, "dashed")  == 0) { *out = FB_BORDER_DASHED;  return true; }
    return false;
}

/* ── Attribute flag parsing ──────────────────────────────────────── */
static uint8_t _parseAttr(const char *s)
{
    if (!s || strcasecmp(s, "none") == 0) return FB_ATTR_NONE;
    uint8_t attr = FB_ATTR_NONE;
    /* s may be a single token like "bold" or "bold|underline" or "bold,underline" */
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s", s);
    char *p = tmp, *tok;
    while ((tok = strsep(&p, "|+, ")) != NULL) {
        if (!*tok) continue;
        if (strcasecmp(tok, "bold")      == 0) attr |= FB_ATTR_BOLD;
        if (strcasecmp(tok, "dim")       == 0) attr |= FB_ATTR_DIM;
        if (strcasecmp(tok, "underline") == 0) attr |= FB_ATTR_UNDERLINE;
        if (strcasecmp(tok, "reverse")   == 0) attr |= FB_ATTR_REVERSE;
        if (strcasecmp(tok, "blink")     == 0) attr |= FB_ATTR_BLINK;
    }
    return attr;
}

/* ── Font lookup ─────────────────────────────────────────────────── */
static const fbFont *_parseFont(const char *s)
{
    if (!s || strcasecmp(s, "vga")     == 0) return &fbVga;
    if (strcasecmp(s, "bold8")   == 0) return &fbBold8;
    if (strcasecmp(s, "thin5")   == 0) return &fbThin5;
    if (strcasecmp(s, "narrow6") == 0) return &fbNarrow6;
    if (strcasecmp(s, "block8")  == 0) return &fbBlock8;
    if (strcasecmp(s, "lcd7")    == 0) return &fbLcd7;
    if (strcasecmp(s, "cga8")    == 0) return &fbCga8;
    if (strcasecmp(s, "thin6x12") == 0) return &fbThin6x12;
    if (strcasecmp(s, "tall8x14") == 0) return &fbTall8x14;
    if (strcasecmp(s, "wide")     == 0) return &fbWide;
    if (strcasecmp(s, "12x24")   == 0) return &fbFont12x24;
    if (strcasecmp(s, "16x32")   == 0) return &fbFont16x32;
    if (strcasecmp(s, "24x48")   == 0) return &fbFont24x48;
    return &fbVga;
}

/* ── Align parsing ───────────────────────────────────────────────── */
static fbAlign _parseAlign(const char *s)
{
    if (!s) return FB_ALIGN_LEFT;
    if (strcasecmp(s, "center") == 0 || strcasecmp(s, "centre") == 0)
        return FB_ALIGN_CENTER;
    if (strcasecmp(s, "right") == 0) return FB_ALIGN_RIGHT;
    return FB_ALIGN_LEFT;
}

/* ── Integer and float helpers ───────────────────────────────────── */
#define NEED(n) do { if (argc < (n)) { \
    snprintf(reply, replyLen, "err,%s,too few args (need %d got %d)", cmd0, (n), argc); \
    return false; } } while(0)

#define INT(i)   atoi(toks[(i)])
#define FLT(i)   ((float)atof(toks[(i)]))

#define COL(i, var) do { if (!_parseColor(toks[(i)], &(var))) { \
    snprintf(reply, replyLen, "err,%s,bad color '%s'", cmd0, toks[(i)]); \
    return false; } } while(0)

#define BORDER(i, var) do { if (!_parseBorder(toks[(i)], &(var))) { \
    snprintf(reply, replyLen, "err,%s,bad border '%s'", cmd0, toks[(i)]); \
    return false; } } while(0)

/* Window handle lookup — slot 0 is reserved (NULL), valid IDs are 1..MAX-1 */
static fbWindow *_getWin(fbWindow **wins, int id, char *reply, int replyLen,
                         const char *cmd0)
{
    if (id <= 0 || id >= FB_NET_MAX_WINDOWS || !wins[id]) {
        snprintf(reply, replyLen, "err,%s,invalid window %d", cmd0, id);
        return NULL;
    }
    return wins[id];
}

/* Allocate the next free window slot, returns the id (1-based) or -1 */
static int _allocWin(fbWindow **wins, fbWindow *w)
{
    for (int i = 1; i < FB_NET_MAX_WINDOWS; i++) {
        if (!wins[i]) { wins[i] = w; return i; }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Core dispatcher
 * ═══════════════════════════════════════════════════════════════════ */

bool fbNetDispatch(fbNetServer *srv, fbScreen *scr, fbWindow **wins,
                   char *cmd, char *replyBuf, int replyLen)
{
    /* Default: empty reply */
    if (replyBuf && replyLen > 0) replyBuf[0] = '\0';
    char *reply  = replyBuf  ? replyBuf  : (char[256]){0};
    int   rLen   = replyLen  ? replyLen  : 256;

    /* Tokenise */
    char *tok_arr[64];
    int   argc = qparse(cmd, "\t\n ,", tok_arr, 64);
    if (argc == 0) return true;   /* empty packet — silently OK */

    const char *cmd0 = tok_arr[0]; /* command name */
    char      **toks = tok_arr + 1;/* shift: toks[0] is first argument */
    argc--;                        /* one fewer arg after removing cmd  */          /* shift so toks[0] is first arg   */

/* ── Screen commands ──────────────────────────────────────────── */

    if (strcasecmp(cmd0, "flush") == 0) {
        fbFlush(scr);
        snprintf(reply, rLen, "ok,flush");
        return true;
    }

    if (strcasecmp(cmd0, "clear") == 0) {
        fbColor col = FB_BLACK;
        if (argc >= 1) COL(0, col);
        fbClear(scr, col);
        return true;
    }

    if (strcasecmp(cmd0, "cursor") == 0) {
        NEED(1);
        bool vis = (INT(0) != 0) ||
                   (strcasecmp(toks[0], "true") == 0) ||
                   (strcasecmp(toks[0], "on")   == 0);
        fbSetCursor(scr, vis);
        return true;
    }

/* ── Window commands ─────────────────────────────────────────── */

    if (strcasecmp(cmd0, "win_new") == 0) {
        NEED(4);
        fbWindow *w = fbNewWindow(scr, INT(0), INT(1), INT(2), INT(3));
        if (!w) {
            snprintf(reply, rLen, "err,win_new,%s", fbGetError());
            return false;
        }
        int id = _allocWin(wins, w);
        if (id < 0) {
            fbDelWindow(w);
            snprintf(reply, rLen, "err,win_new,window table full");
            return false;
        }
        snprintf(reply, rLen, "ok,win_new,%d", id);
        return true;
    }

    if (strcasecmp(cmd0, "win_del") == 0) {
        NEED(1);
        int id = INT(0);
        fbWindow *w = _getWin(wins, id, reply, rLen, cmd0);
        if (!w) return false;
        fbDelWindow(w);
        wins[id] = NULL;
        return true;
    }

    if (strcasecmp(cmd0, "win_move") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbMoveWindow(w, INT(1), INT(2));
        return true;
    }

    if (strcasecmp(cmd0, "win_resize") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbResizeWindow(w, INT(1), INT(2));
        return true;
    }

    if (strcasecmp(cmd0, "win_clear") == 0) {
        NEED(2);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor col = FB_BLACK;
        COL(1, col);
        fbClearWindow(w, col);
        return true;
    }

    if (strcasecmp(cmd0, "win_refresh") == 0) {
        NEED(1);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbRefresh(w);
        return true;
    }

    if (strcasecmp(cmd0, "win_font") == 0) {
        NEED(2);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbSetFont(w, _parseFont(toks[1]));
        return true;
    }

/* ── Text state ──────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "move") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbMoveCursor(w, INT(1), INT(2));
        return true;
    }

    if (strcasecmp(cmd0, "colors") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg;
        COL(1, fg); COL(2, bg);
        fbSetColors(w, fg, bg);
        return true;
    }

    if (strcasecmp(cmd0, "attr") == 0) {
        NEED(2);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbSetAttr(w, _parseAttr(toks[1]));
        return true;
    }

/* ── Text output ─────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "print") == 0) {
        NEED(2);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbAddStr(w, toks[1]);
        return true;
    }

    if (strcasecmp(cmd0, "print_at") == 0) {
        NEED(4);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbPrintAt(w, INT(1), INT(2), "%s", toks[3]);
        return true;
    }

    if (strcasecmp(cmd0, "print_align") == 0) {
        NEED(4);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbPrintAligned(w, INT(1), _parseAlign(toks[2]), toks[3]);
        return true;
    }

    if (strcasecmp(cmd0, "print_px") == 0) {
        /* print_px,x,y,STR,fg,bg,attr,font */
        NEED(4);
        int     x    = INT(0);
        int     y    = INT(1);
        const char *str = toks[2];
        fbColor fg   = FB_WHITE, bg = FB_BLACK;
        uint8_t attr = FB_ATTR_NONE;
        const fbFont *font = &fbVga;
        if (argc >= 4) COL(3, fg);
        if (argc >= 5) COL(4, bg);
        if (argc >= 6) attr = _parseAttr(toks[5]);
        if (argc >= 7) font = _parseFont(toks[6]);
        fbDrawTextPx(scr, x, y, str, fg, bg, attr, font);
        return true;
    }

/* ── Drawing primitives ──────────────────────────────────────── */

    if (strcasecmp(cmd0, "pixel") == 0) {
        NEED(3);
        fbColor col; COL(2, col);
        fbDrawPixel(scr, INT(0), INT(1), col);
        return true;
    }

    if (strcasecmp(cmd0, "line") == 0) {
        NEED(5);
        fbColor col; COL(4, col);
        fbDrawLine(scr, INT(0), INT(1), INT(2), INT(3), col);
        return true;
    }

    if (strcasecmp(cmd0, "rect") == 0) {
        NEED(5);
        fbColor col; COL(4, col);
        fbDrawRect(scr, INT(0), INT(1), INT(2), INT(3), col);
        return true;
    }

    if (strcasecmp(cmd0, "fill_rect") == 0) {
        NEED(5);
        fbColor col; COL(4, col);
        fbFillRect(scr, INT(0), INT(1), INT(2), INT(3), col);
        return true;
    }

    if (strcasecmp(cmd0, "circle") == 0) {
        NEED(4);
        fbColor col; COL(3, col);
        fbDrawCircle(scr, INT(0), INT(1), INT(2), col);
        return true;
    }

    if (strcasecmp(cmd0, "fill_circle") == 0) {
        NEED(4);
        fbColor col; COL(3, col);
        fbFillCircle(scr, INT(0), INT(1), INT(2), col);
        return true;
    }

/* ── Borders ─────────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "border") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbBorderStyle style; BORDER(1, style);
        fbColor col;         COL(2, col);
        fbDrawBorder(w, style, col);
        return true;
    }

    if (strcasecmp(cmd0, "box") == 0) {
        NEED(7);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbBorderStyle style; BORDER(5, style);
        fbColor col;         COL(6, col);
        fbDrawBox(w, INT(1), INT(2), INT(3), INT(4), style, col);
        return true;
    }

    if (strcasecmp(cmd0, "title_bar") == 0) {
        /* title_bar,WIN,STR,BORDER,border_color,title_fg,title_bg */
        NEED(6);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbBorderStyle style;   BORDER(2, style);
        fbColor bcol, tfg, tbg;
        COL(3, bcol); COL(4, tfg); COL(5, tbg);
        fbDrawTitleBar(w, toks[1], style, bcol, tfg, tbg);
        return true;
    }

/* ── Widgets ─────────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "progress") == 0) {
        /* progress,WIN,col,row,width,pct,fg,bg,showpct */
        NEED(8);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg; COL(5, fg); COL(6, bg);
        bool showPct = (argc >= 8) ? (INT(7) != 0) : true;
        fbDrawProgressBar(w, INT(1), INT(2), INT(3), INT(4), fg, bg, showPct);
        return true;
    }

    if (strcasecmp(cmd0, "spinner") == 0) {
        /* spinner,WIN,col,row,tick,fg,bg */
        NEED(6);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg; COL(4, fg); COL(5, bg);
        fbDrawSpinner(w, INT(1), INT(2), INT(3), fg, bg);
        return true;
    }

    if (strcasecmp(cmd0, "gauge") == 0) {
        /* gauge,WIN,col,row,height,val,maxval,fg,bg */
        NEED(8);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg; COL(6, fg); COL(7, bg);
        fbDrawGauge(w, INT(1), INT(2), INT(3), INT(4), INT(5), fg, bg);
        return true;
    }

    if (strcasecmp(cmd0, "sparkline") == 0) {
        /* sparkline,WIN,col,row,width,fg,bg,f0,f1,f2,... */
        NEED(6);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg; COL(4, fg); COL(5, bg);
        int width = INT(3);
        int nVals = argc - 6;
        if (nVals < 0) nVals = 0;
        float *vals = NULL;
        if (nVals > 0) {
            vals = malloc((size_t)nVals * sizeof(float));
            for (int i = 0; i < nVals; i++)
                vals[i] = FLT(6 + i);
        }
        fbDrawSparkline(w, INT(1), INT(2), (const float*)vals, nVals, width, fg, bg);
        free(vals);
        return true;
    }

    if (strcasecmp(cmd0, "scroll_up") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor col; COL(2, col);
        fbScrollUp(w, INT(1), col);
        return true;
    }

    if (strcasecmp(cmd0, "scroll_down") == 0) {
        NEED(3);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor col; COL(2, col);
        fbScrollDown(w, INT(1), col);
        return true;
    }

/* ── Notifications ───────────────────────────────────────────── */

    if (strcasecmp(cmd0, "toast") == 0) {
        /* Refuse to block inside fbNetRun — would freeze the server
           and swallow keypresses (q/Esc). Return an error instead. */
        if (srv && srv->inEventLoop) {
            snprintf(reply, rLen,
                     "err,toast,not allowed in server event loop (would block)");
            return false;
        }

        /* toast,kind,ms,STR */
        NEED(3);
        fbToastKind kind = FB_TOAST_INFO;
        if (strcasecmp(toks[0], "success") == 0) kind = FB_TOAST_SUCCESS;
        if (strcasecmp(toks[0], "warning") == 0) kind = FB_TOAST_WARNING;
        if (strcasecmp(toks[0], "error")   == 0) kind = FB_TOAST_ERROR;
        fbToast(scr, kind, toks[2], INT(1));
        return true;
    }

/* ── Query / info ────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "screen_size") == 0) {
        snprintf(reply, rLen, "ok,screen_size,%d,%d,%d,%d",
                 fbWidth(scr), fbHeight(scr),
                 fbCols(scr),  fbRows(scr));
        return true;
    }

    if (strcasecmp(cmd0, "win_size") == 0) {
        NEED(1);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        snprintf(reply, rLen, "ok,win_size,%d,%d",
                 fbWindowCols(w), fbWindowRows(w));
        return true;
    }

    if (strcasecmp(cmd0, "ping") == 0) {
        snprintf(reply, rLen, "ok,pong");
        return true;
    }

    if (strcasecmp(cmd0, "table") == 0) {
        /* table,WIN,sc,sr,hfg,hbg,cfg,cbg,sfg,sbg,sel,hdr0,w0,a0,...,|,d00,d01,...
           Column defs terminated by "|" token; data follows as flat list.  */
        NEED(10);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        int startCol = INT(1), startRow = INT(2);
        fbColor hfg,hbg,cfg,cbg,sfg,sbg;
        COL(3,hfg); COL(4,hbg); COL(5,cfg);
        COL(6,cbg); COL(7,sfg); COL(8,sbg);
        int selRow = INT(9);

        int ai = 10;
        fbTableCol cols[32];
        int nCols = 0;
        while (ai < argc && nCols < 31) {
            if (strcmp(toks[ai], "|") == 0) { ai++; break; }
            cols[nCols].header = toks[ai++];
            cols[nCols].width  = (ai < argc) ? atoi(toks[ai++]) : 0;
            const char *astr   = (ai < argc) ? toks[ai++] : "l";
            cols[nCols].align  = _parseAlign(astr);
            nCols++;
        }
        cols[nCols].header = NULL;

        int nDataRows = 0;
        int dataStart = ai;
        if (nCols > 0) nDataRows = (argc - dataStart) / nCols;
        if (nDataRows < 0) nDataRows = 0;

        const char ***rows = NULL;
        if (nDataRows > 0) {
            rows = malloc((size_t)nDataRows * sizeof(const char **));
            for (int r = 0; r < nDataRows; r++) {
                const char **row = malloc((size_t)nCols * sizeof(const char *));
                for (int c = 0; c < nCols; c++) {
                    int idx = dataStart + r*nCols + c;
                    row[c] = (idx < argc) ? toks[idx] : "";
                }
                rows[r] = row;
            }
        }

        fbDrawTable(w, startCol, startRow, cols,
                    (const char * const * const *)rows, nDataRows,
                    selRow, hfg, hbg, cfg, cbg, sfg, sbg);

        if (rows) {
            for (int r = 0; r < nDataRows; r++) free((void*)rows[r]);
            free(rows);
        }
        return true;
    }

    if (strcasecmp(cmd0, "tick") == 0) {
        /* tick,WIN,col,row,fg,bg  — spinner at auto-incrementing tick */
        NEED(5);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;
        fbColor fg, bg; COL(3, fg); COL(4, bg);
        static int _tick = 0;
        fbDrawSpinner(w, INT(1), INT(2), _tick++, fg, bg);
        return true;
    }

    if (strcasecmp(cmd0, "list_windows") == 0) {
        char buf[512] = "ok,list_windows";
        int  blen = (int)strlen(buf);
        for (int i = 1; i < FB_NET_MAX_WINDOWS; i++)
            if (wins[i])
                blen += snprintf(buf+blen, sizeof(buf)-(size_t)blen, ",%d", i);
        snprintf(reply, rLen, "%s", buf);
        return true;
    }

    if (strcasecmp(cmd0, "refresh_all") == 0) {
        /* refresh_all[,1]  — refresh all windows; flush if arg=1 */
        for (int i = 1; i < FB_NET_MAX_WINDOWS; i++)
            if (wins[i]) fbRefresh(wins[i]);
        if (argc >= 1 && INT(0)) fbFlush(scr);
        return true;
    }

    if (strcasecmp(cmd0, "text_px") == 0) {
        /* Alias for print_px */
        NEED(4);
        int x = INT(0), y = INT(1);
        fbColor fg = FB_WHITE, bg = FB_BLACK;
        uint8_t attr = FB_ATTR_NONE;
        const fbFont *font = &fbVga;
        if (argc >= 4) COL(3, fg);
        if (argc >= 5) COL(4, bg);
        if (argc >= 6) attr = _parseAttr(toks[5]);
        if (argc >= 7) font = _parseFont(toks[6]);
        fbDrawTextPx(scr, x, y, toks[2], fg, bg, attr, font);
        return true;
    }


    /* ── Colour math ─────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "blend") == 0) {
        /* blend,dst_color,src_color  -> ok,blend,#RRGGBB */
        NEED(2);
        fbColor dst, src;
        COL(0, dst); COL(1, src);
        fbColor result = fbBlend(dst, src);
        snprintf(reply, rLen, "ok,blend,#%02X%02X%02X",
                 FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    if (strcasecmp(cmd0, "lerp") == 0) {
        /* lerp,color_a,color_b,t  -> ok,lerp,#RRGGBB  (t is 0.0-1.0) */
        NEED(3);
        fbColor a, b;
        COL(0, a); COL(1, b);
        float t = FLT(2);
        fbColor result = fbLerp(a, b, t);
        snprintf(reply, rLen, "ok,lerp,#%02X%02X%02X",
                 FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    if (strcasecmp(cmd0, "darken") == 0) {
        /* darken,color,factor  -> ok,darken,#RRGGBB */
        NEED(2);
        fbColor c; COL(0, c);
        fbColor result = fbDarken(c, FLT(1));
        snprintf(reply, rLen, "ok,darken,#%02X%02X%02X",
                 FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    if (strcasecmp(cmd0, "lighten") == 0) {
        /* lighten,color,factor  -> ok,lighten,#RRGGBB */
        NEED(2);
        fbColor c; COL(0, c);
        fbColor result = fbLighten(c, FLT(1));
        snprintf(reply, rLen, "ok,lighten,#%02X%02X%02X",
                 FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    if (strcasecmp(cmd0, "grayscale") == 0) {
        /* grayscale,color  -> ok,grayscale,#RRGGBB */
        NEED(1);
        fbColor c; COL(0, c);
        fbColor result = fbGrayscale(c);
        snprintf(reply, rLen, "ok,grayscale,#%02X%02X%02X",
                 FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    /* ── Custom border ───────────────────────────────────────────── */

    if (strcasecmp(cmd0, "custom_border") == 0) {
        /* custom_border,WIN,tl,tr,bl,br,horiz,vert,COLOR
           Corner/line chars given as decimal Unicode codepoints or
           single ASCII characters.                                   */
        NEED(8);
        fbWindow *w = _getWin(wins, INT(0), reply, rLen, cmd0);
        if (!w) return false;

        fbBorder brd;
        brd.style = FB_BORDER_CUSTOM;
        /* Parse codepoints */
        #define _WC(i) ((toks[i][0]>='0'&&toks[i][0]<='9') ?                         (wchar_t)atoi(toks[i]) : (wchar_t)(unsigned char)toks[i][0])
        brd.tl    = _WC(1);
        brd.tr    = _WC(2);
        brd.bl    = _WC(3);
        brd.br    = _WC(4);
        brd.horiz = _WC(5);
        brd.vert  = _WC(6);
        #undef _WC
        COL(7, brd.color);
        fbDrawCustomBorder(w, &brd);
        return true;
    }

    /* ── Interactive dialogs (blocking — reply contains result) ──── */

    if (strcasecmp(cmd0, "menu") == 0) {
        /* Refuse to block inside fbNetRun — would freeze the server
           and swallow keypresses (q/Esc). Return an error instead. */
        if (srv && srv->inEventLoop) {
            snprintf(reply, rLen,
                     "err,menu,not allowed in server event loop (would block)");
            return false;
        }

        /* menu,col,row,fg,bg,fgsel,bgsel,BORDER,label0,id0,label1,id1,...
           Returns: ok,menu,selected_id   or  ok,menu,-1 (cancelled)  */
        NEED(8);
        int col = INT(0), row = INT(1);
        fbColor fg, bg, fgsel, bgsel;
        COL(2, fg); COL(3, bg); COL(4, fgsel); COL(5, bgsel);
        fbBorderStyle bstyle; BORDER(6, bstyle);

        /* Parse item pairs: label, id */
        int ai = 7;
        int maxItems = (argc - ai) / 2 + 1;
        if (maxItems <= 0) maxItems = 1;
        fbMenuItem *items = calloc((size_t)(maxItems + 1), sizeof(fbMenuItem));
        int nItems = 0;
        while (ai + 1 < argc && nItems < maxItems) {
            items[nItems].label    = toks[ai++];
            items[nItems].id       = atoi(toks[ai++]);
            items[nItems].disabled = false;
            nItems++;
        }
        items[nItems].label = NULL;

        int result = fbMenu(scr, col, row, items, fg, bg, fgsel, bgsel, bstyle);
        free(items);
        snprintf(reply, rLen, "ok,menu,%d", result);
        return true;
    }

    if (strcasecmp(cmd0, "msgbox") == 0) {
        /* Refuse to block inside fbNetRun — would freeze the server
           and swallow keypresses (q/Esc). Return an error instead. */
        if (srv && srv->inEventLoop) {
            snprintf(reply, rLen,
                     "err,msgbox,not allowed in server event loop (would block)");
            return false;
        }

        /* msgbox,title,msg,buttons,kind
           buttons: ok | ok_cancel | yes_no | yes_no_cancel
           kind:    info | success | warning | error
           Returns: ok,msgbox,ok|cancel|yes|no                        */
        NEED(4);
        const char *title = toks[0];
        const char *msg   = toks[1];

        fbMsgBoxButtons btns = FB_MSGBOX_OK;
        if (strcasecmp(toks[2], "ok_cancel")      == 0) btns = FB_MSGBOX_OK_CANCEL;
        if (strcasecmp(toks[2], "yes_no")         == 0) btns = FB_MSGBOX_YES_NO;
        if (strcasecmp(toks[2], "yes_no_cancel")  == 0) btns = FB_MSGBOX_YES_NO_CANCEL;

        fbToastKind kind = FB_TOAST_INFO;
        if (strcasecmp(toks[3], "success") == 0) kind = FB_TOAST_SUCCESS;
        if (strcasecmp(toks[3], "warning") == 0) kind = FB_TOAST_WARNING;
        if (strcasecmp(toks[3], "error")   == 0) kind = FB_TOAST_ERROR;

        int result = fbMsgBox(scr, title, msg, btns, kind);
        const char *resStr = "ok";
        if (result == FB_MSGBOX_RESULT_CANCEL) resStr = "cancel";
        if (result == FB_MSGBOX_RESULT_YES)    resStr = "yes";
        if (result == FB_MSGBOX_RESULT_NO)     resStr = "no";
        snprintf(reply, rLen, "ok,msgbox,%s", resStr);
        return true;
    }

    if (strcasecmp(cmd0, "file_pick") == 0) {
        /* Refuse to block inside fbNetRun — would freeze the server
           and swallow keypresses (q/Esc). Return an error instead. */
        if (srv && srv->inEventLoop) {
            snprintf(reply, rLen,
                     "err,file_pick,not allowed in server event loop (would block)");
            return false;
        }

        /* file_pick[,start_dir]
           Returns: ok,file_pick,/path/to/selected/file
               or:  ok,file_pick, (empty = cancelled)               */
        const char *startDir = (argc >= 1 && toks[0][0]) ? toks[0] : NULL;
        char outPath[768] = {0};
        bool picked = fbFilePicker(scr, startDir, outPath, sizeof(outPath));
        if (picked)
            snprintf(reply, rLen, "ok,file_pick,%s", outPath);
        else
            snprintf(reply, rLen, "ok,file_pick,");
        return true;
    }

    if (strcasecmp(cmd0, "color_pick") == 0) {
        /* Refuse to block inside fbNetRun — would freeze the server
           and swallow keypresses (q/Esc). Return an error instead. */
        if (srv && srv->inEventLoop) {
            snprintf(reply, rLen,
                     "err,color_pick,not allowed in server event loop (would block)");
            return false;
        }

        /* color_pick[,initial_color]
           Returns: ok,color_pick,#RRGGBB  or  ok,color_pick, (cancelled) */
        fbColor initial = FB_BLACK;
        if (argc >= 1) _parseColor(toks[0], &initial);
        fbColor result = fbColorPicker(scr, initial);
        if (result == FB_TRANSPARENT)
            snprintf(reply, rLen, "ok,color_pick,");
        else
            snprintf(reply, rLen, "ok,color_pick,#%02X%02X%02X",
                     FB_COLOR_R(result), FB_COLOR_G(result), FB_COLOR_B(result));
        return true;
    }

    /* ── Server info ─────────────────────────────────────────────── */

    if (strcasecmp(cmd0, "fonts") == 0) {
        /* fonts  -> ok,fonts,vga,bold8,thin5,...  (all registered fonts) */
        char buf[512] = "ok,fonts";
        int blen = (int)strlen(buf);
        for (int i = 0; fbFontList[i] != NULL; i++) {
            blen += snprintf(buf + blen, sizeof(buf) - (size_t)blen,
                             ",%s", fbFontList[i]->name);
        }
        snprintf(reply, rLen, "%s", buf);
        return true;
    }

    if (strcasecmp(cmd0, "version") == 0) {
        snprintf(reply, rLen, "ok,version,%d.%d.%d",
                 FBCURSES_VERSION_MAJOR,
                 FBCURSES_VERSION_MINOR,
                 FBCURSES_VERSION_PATCH);
        return true;
    }

    if (strcasecmp(cmd0, "stats") == 0) {
        /* Sent only when called through server context — reuse ping path */
        /* Count active windows */
        int nActive = 0;
        if (wins) for (int _i = 1; _i < FB_NET_MAX_WINDOWS; _i++) if (wins[_i]) nActive++;
        snprintf(reply, rLen, "ok,stats,windows=%d", nActive);
        return true;
    }

    if (strcasecmp(cmd0, "subscribe") == 0) {
        /* subscribe,GROUP_ADDR  — join a multicast group at runtime */
        NEED(1);
        if (fbNetJoinMulticast(srv, toks[0]))
            snprintf(reply, rLen, "ok,subscribe,%s", toks[0]);
        else
            snprintf(reply, rLen, "err,subscribe,failed to join %s", toks[0]);
        return true;
    }

    if (strcasecmp(cmd0, "unsubscribe") == 0) {
        /* unsubscribe,GROUP_ADDR  — leave a multicast group */
        NEED(1);
        if (fbNetLeaveMulticast(srv, toks[0]))
            snprintf(reply, rLen, "ok,unsubscribe,%s", toks[0]);
        else
            snprintf(reply, rLen, "err,unsubscribe,not a member of %s", toks[0]);
        return true;
    }

    if (strcasecmp(cmd0, "groups") == 0) {
        /* groups  — list joined multicast groups */
        char gbuf[512] = {0};
        int n = fbNetListGroups(srv, gbuf, sizeof(gbuf));
        if (n > 0)
            snprintf(reply, rLen, "ok,groups,%s", gbuf);
        else
            snprintf(reply, rLen, "ok,groups,");
        return true;
    }

/* ── Unknown command ─────────────────────────────────────────── */

    snprintf(reply, rLen, "err,unknown,%s", cmd0);
    return false;
}


/* ═══════════════════════════════════════════════════════════════════
 *  Multicast group management
 * ═══════════════════════════════════════════════════════════════════ */

bool fbNetJoinMulticast(fbNetServer *srv, const char *groupAddr)
{
    if (!srv || !groupAddr) return false;

    struct in_addr grp;
    if (inet_pton(AF_INET, groupAddr, &grp) != 1) {
        _netLog(srv, FB_NET_LOG_ERRORS,
                "fbNetJoinMulticast: invalid address '%s'", groupAddr);
        return false;
    }

    /* Validate it is actually a multicast address (224.0.0.0/4) */
    uint32_t addr_h = ntohl(grp.s_addr);
    if ((addr_h >> 28) != 14) {   /* 0xE = 1110 in top 4 bits */
        _netLog(srv, FB_NET_LOG_ERRORS,
                "fbNetJoinMulticast: '%s' is not a multicast address "
                "(must be 224.0.0.0 – 239.255.255.255)", groupAddr);
        return false;
    }

    /* Check not already joined */
    for (int i = 0; i < srv->nMcastGroups; i++) {
        if (srv->mcastGroups[i].s_addr == grp.s_addr) {
            _netLog(srv, FB_NET_LOG_ERRORS,
                    "fbNetJoinMulticast: already in group %s", groupAddr);
            return true;  /* not an error */
        }
    }

    if (srv->nMcastGroups >= FB_NET_MAX_GROUPS) {
        _netLog(srv, FB_NET_LOG_ERRORS,
                "fbNetJoinMulticast: already in %d groups (max %d)",
                srv->nMcastGroups, FB_NET_MAX_GROUPS);
        return false;
    }

    /* SO_REUSEADDR + SO_REUSEPORT needed so multiple servers on the
       same host can all join the same group on the same port         */
    int yes = 1;
    setsockopt(srv->fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

    struct ip_mreq mreq;
    mreq.imr_multiaddr = grp;
    mreq.imr_interface.s_addr = INADDR_ANY;  /* all local interfaces */

    if (setsockopt(srv->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        _netLog(srv, FB_NET_LOG_ERRORS,
                "fbNetJoinMulticast: IP_ADD_MEMBERSHIP %s: %s",
                groupAddr, strerror(errno));
        return false;
    }

    srv->mcastGroups[srv->nMcastGroups++] = grp;
    _netLog(srv, FB_NET_LOG_ERRORS,
            "Joined multicast group %s", groupAddr);
    return true;
}

bool fbNetLeaveMulticast(fbNetServer *srv, const char *groupAddr)
{
    if (!srv || !groupAddr) return false;

    struct in_addr grp;
    if (inet_pton(AF_INET, groupAddr, &grp) != 1) return false;

    for (int i = 0; i < srv->nMcastGroups; i++) {
        if (srv->mcastGroups[i].s_addr != grp.s_addr) continue;

        struct ip_mreq mreq;
        mreq.imr_multiaddr = grp;
        mreq.imr_interface.s_addr = INADDR_ANY;
        setsockopt(srv->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   &mreq, sizeof(mreq));

        /* Remove from list (swap with last) */
        srv->mcastGroups[i] = srv->mcastGroups[--srv->nMcastGroups];
        _netLog(srv, FB_NET_LOG_ERRORS, "Left multicast group %s", groupAddr);
        return true;
    }
    return false;  /* was not a member */
}

int fbNetListGroups(const fbNetServer *srv, char *buf, int bufLen)
{
    if (!srv || !buf || bufLen <= 0) return 0;
    buf[0] = '\0';
    int len = 0;
    for (int i = 0; i < srv->nMcastGroups; i++) {
        char tmp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &srv->mcastGroups[i], tmp, sizeof(tmp));
        len += snprintf(buf + len, (size_t)(bufLen - len),
                        "%s%s", i ? "," : "", tmp);
    }
    return srv->nMcastGroups;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Server lifecycle
 * ═══════════════════════════════════════════════════════════════════ */

fbNetServer *fbNetOpen(fbScreen *scr, uint16_t port)
{
    fbNetServer *srv = calloc(1, sizeof(fbNetServer));
    if (!srv) return NULL;

    srv->scr      = scr;
    srv->logLevel = FB_NET_LOG_ERRORS;
    srv->logFile  = stderr;

    /* Create non-blocking UDP socket */
    srv->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (srv->fd < 0) {
        _netLog(srv, FB_NET_LOG_ERRORS, "socket: %s", strerror(errno));
        free(srv);
        return NULL;
    }

    /* Allow address reuse */
    int yes = 1;
    setsockopt(srv->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        _netLog(srv, FB_NET_LOG_ERRORS, "bind port %u: %s", port, strerror(errno));
        close(srv->fd);
        free(srv);
        return NULL;
    }

    /* Read back the actual port (useful when port=0) */
    socklen_t addrLen = sizeof(addr);
    getsockname(srv->fd, (struct sockaddr *)&addr, &addrLen);
    srv->port = ntohs(addr.sin_port);

    /* Set socket non-blocking */
    int flags = fcntl(srv->fd, F_GETFL, 0);
    fcntl(srv->fd, F_SETFL, flags | O_NONBLOCK);

    _netLog(srv, FB_NET_LOG_ERRORS, "listening on UDP port %u", srv->port);
    return srv;
}

void fbNetClose(fbNetServer *srv)
{
    if (!srv) return;

    /* Leave all multicast groups gracefully */
    for (int i = srv->nMcastGroups - 1; i >= 0; i--) {
        char tmp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &srv->mcastGroups[i], tmp, sizeof(tmp));
        fbNetLeaveMulticast(srv, tmp);
    }

    /* Destroy all network-created windows */
    for (int i = 1; i < FB_NET_MAX_WINDOWS; i++) {
        if (srv->wins[i]) {
            fbDelWindow(srv->wins[i]);
            srv->wins[i] = NULL;
        }
    }
    close(srv->fd);
    free(srv);
}

uint16_t fbNetPort(const fbNetServer *srv) { return srv ? srv->port : 0; }
uint64_t fbNetPacketsIn (const fbNetServer *srv) { return srv ? srv->pktsIn  : 0; }
uint64_t fbNetPacketsErr(const fbNetServer *srv) { return srv ? srv->pktsErr : 0; }

void fbNetSetLog(fbNetServer *srv, fbNetLogLevel level, void *file)
{
    if (!srv) return;
    srv->logLevel = level;
    srv->logFile  = file ? (FILE *)file : stderr;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Packet receive + dispatch
 * ═══════════════════════════════════════════════════════════════════ */

bool fbNetProcess(fbNetServer *srv)
{
    if (!srv) return false;

    char pkt[FB_NET_MAX_PACKET];
    struct sockaddr_in sender;
    socklen_t senderLen = sizeof(sender);

    ssize_t n = recvfrom(srv->fd, pkt, sizeof(pkt) - 1, 0,
                         (struct sockaddr *)&sender, &senderLen);
    if (n <= 0) return false;   /* EAGAIN / EWOULDBLOCK — no packet */

    pkt[n] = '\0';
    srv->pktsIn++;

    _netLog(srv, FB_NET_LOG_ALL, "recv %zd bytes from %s:%u",
            n, inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));

    /* A single datagram may contain multiple newline-separated commands.
       Split on '\n' and dispatch each one; collect all replies.         */
    char allReplies[2048] = {0};
    int  allLen = 0;

    char *line = pkt;
    char *next;
    bool anyErr = false;

    while (line && *line) {
        /* Find end of this line */
        next = strchr(line, '\n');
        if (next) *next++ = '\0';

        /* Skip leading whitespace / CR on this line.
           Pass the trimmed pointer 'p' to the dispatcher so that
           leading \r (CRLF line endings) does not contaminate cmd0. */
        char *p = line;
        while (*p == ' ' || *p == '\r' || *p == '\t') p++;
        if (!*p) { line = next; continue; }

        char reply[512] = {0};
        bool ok = fbNetDispatch(srv, srv->scr, srv->wins, p, reply, sizeof(reply));

        if (!ok) {
            anyErr = true;
            srv->pktsErr++;
            _netLog(srv, FB_NET_LOG_ERRORS, "%s", reply[0] ? reply : "(dispatch failed)");
        } else {
            _netLog(srv, FB_NET_LOG_COMMANDS, "ok: %s", p);
        }

        /* Accumulate replies */
        if (reply[0] && allLen < (int)sizeof(allReplies) - 2) {
            int rLen = (int)strlen(reply);
            if (allLen + rLen + 2 < (int)sizeof(allReplies)) {
                memcpy(allReplies + allLen, reply, (size_t)rLen);
                allLen += rLen;
                allReplies[allLen++] = '\n';
                allReplies[allLen]   = '\0';
            }
        }

        line = next;
    }

    /* Send consolidated reply */
    if (allLen > 0) {
        sendto(srv->fd, allReplies, (size_t)allLen, 0,
               (struct sockaddr *)&sender, senderLen);
    }

    (void)anyErr;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Blocking event loop
 * ═══════════════════════════════════════════════════════════════════ */

void fbNetStop(fbNetServer *srv) { if (srv) srv->stop = true; }

void fbNetRun(fbNetServer *srv, bool autoFlush)
{
    if (!srv) return;
    srv->stop = false;

    _netLog(srv, FB_NET_LOG_ERRORS,
            "fbNetRun started (port %u, autoFlush=%s)",
            srv->port, autoFlush ? "yes" : "no");

    srv->inEventLoop = true;

    while (!srv->stop) {
        /* Wait up to 50ms for a UDP packet.
           We do NOT add STDIN to the select() set here — instead we
           poll for keypresses unconditionally every iteration with a
           0ms timeout.  This avoids the race where a UDP packet and a
           keypress both arrive simultaneously but select() only marks
           the UDP fd ready, causing the keypress check to be skipped. */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv->fd, &rfds);

        struct timeval tv = { 0, 50000 };   /* 50ms */
        int sel = select(srv->fd + 1, &rfds, NULL, NULL, &tv);

        if (sel < 0 && errno != EINTR) break;

        /* Drain all pending UDP packets */
        bool didDraw = false;
        while (fbNetProcess(srv)) {
            didDraw = true;
        }

        if (autoFlush && didDraw) fbFlush(srv->scr);

        /* Always poll for local keypress — non-blocking, 0ms timeout.
           fbGetKeyTimeout returns FB_KEY_NONE immediately if no key is
           waiting, so this adds negligible overhead.                   */
        int key = fbGetKeyTimeout(srv->scr, 0);
        if (key == FB_KEY_ESC || key == 'q' || key == 'Q') {
            _netLog(srv, FB_NET_LOG_ERRORS, "local ESC/q — stopping");
            break;
        }
    }

    srv->inEventLoop = false;

    _netLog(srv, FB_NET_LOG_ERRORS,
            "fbNetRun stopped. packets in=%llu err=%llu",
            (unsigned long long)srv->pktsIn,
            (unsigned long long)srv->pktsErr);
}
