/*
 * widgets.c — Extended widget implementations for fbcurses.
 *
 * Implements:
 *   Mouse input (GPM-style xterm escape parsing)
 *   Pop-up menu
 *   Single-line text-input editor
 *   Toast / notification popup
 *   Table / data-grid
 *   Vertical gauge
 *   Sparkline chart
 *
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>

/* ── Internal timing helper ─────────────────────────────────────── */
static long _msNow(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ── Read one byte with timeout (ms=-1 = block) ─────────────────── */
static int _readByteMs(int ms)
{
    if (ms >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
        if (select(1, &fds, NULL, NULL, &tv) <= 0) return -1;
    }
    unsigned char c;
    return (read(STDIN_FILENO, &c, 1) == 1) ? (int)c : -1;
}

/* ══════════════════════════════════════════════════════════════════
 *  MOUSE
 * ══════════════════════════════════════════════════════════════════ */

void fbMouseInit(fbScreen *scr)
{
    (void)scr;
    /* Enable xterm extended mouse tracking (SGR mode) */
    { ssize_t _wr = write(STDOUT_FILENO, "\033[?1003h", 8); (void)_wr; }   /* any-event tracking    */
    { ssize_t _wr = write(STDOUT_FILENO, "\033[?1006h", 8); (void)_wr; }   /* SGR extended mode     */
}

void fbMouseShutdown(fbScreen *scr)
{
    (void)scr;
    { ssize_t _wr = write(STDOUT_FILENO, "\033[?1003l", 8); (void)_wr; }
    { ssize_t _wr = write(STDOUT_FILENO, "\033[?1006l", 8); (void)_wr; }
}

bool fbMousePoll(fbScreen *scr, fbMouseEvent *ev)
{
    /* Peek for ESC [ < (SGR mouse) */
    int c = _readByteMs(0);
    if (c < 0) return false;
    if (c != 0x1B) { /* not a mouse escape — push-back not available, discard */ return false; }

    int a = _readByteMs(20); if (a != '[') return false;
    int b = _readByteMs(20); if (b != '<') return false;

    /* Read: Cb ; Cx ; Cy M/m */
    char buf[64];
    int  pos = 0;
    int  ch2;
    while (pos < 60) {
        ch2 = _readByteMs(20);
        if (ch2 < 0) break;
        buf[pos++] = (char)ch2;
        if (ch2 == 'M' || ch2 == 'm') break;
    }
    buf[pos] = '\0';

    int cb, cx, cy;
    char release;
    if (sscanf(buf, "%d;%d;%d%c", &cb, &cx, &cy, &release) != 4) return false;

    memset(ev, 0, sizeof(*ev));
    ev->col = cx - 1;
    ev->row = cy - 1;
    ev->x   = ev->col * FB_FONT_W;
    ev->y   = ev->row * FB_FONT_H;

    int btn = cb & 0x03;
    bool pressed = (release == 'M');
    bool moved   = (cb & 0x20) != 0;
    ev->moved    = moved;

    if (!moved) {
        if (btn == 0) ev->left   = pressed;
        if (btn == 1) ev->middle = pressed;
        if (btn == 2) ev->right  = pressed;
        if (cb == 64) ev->wheelDelta = +1;
        if (cb == 65) ev->wheelDelta = -1;
    }

    (void)scr;
    return true;
}

/* ══════════════════════════════════════════════════════════════════
 *  MENU
 * ══════════════════════════════════════════════════════════════════ */

int fbMenu(fbScreen *scr, int col, int row,
           const fbMenuItem *items,
           fbColor fg,    fbColor bg,
           fbColor fgSel, fbColor bgSel,
           fbBorderStyle border)
{
    /* Count items, find the widest label */
    int n = 0, maxW = 0;
    for (int i = 0; items[i].label != NULL; i++) {
        n++;
        int l = (int)strlen(items[i].label);
        if (l > maxW) maxW = l;
    }
    if (n == 0) return -1;

    int wCols = maxW + 4;          /* 1 border + 1 space each side + border */
    int wRows = n    + 2;          /* top/bottom border                      */

    /* Clamp to screen */
    int scrC = fbCols(scr), scrR = fbRows(scr);
    if (col + wCols > scrC) col = scrC - wCols;
    if (row + wRows > scrR) row = scrR - wRows;
    if (col < 0) col = 0;
    if (row < 0) row = 0;

    fbWindow *win = fbNewWindow(scr, col, row, wCols, wRows);
    if (!win) return -1;

    int sel = 0;
    /* Skip any initially disabled items */
    while (sel < n && items[sel].disabled) sel++;

    for (;;) {
        /* Draw */
        fbClearWindow(win, bg);
        fbDrawBorder(win, border, fg);

        for (int i = 0; i < n; i++) {
            int r = i + 1;
            if (items[i].label[0] == '\0') {
                /* Separator */
                fbCell *cell;
                for (int c2 = 1; c2 < wCols - 1; c2++) {
                    cell = _fbGetCell(win, c2, r);
                    cell->ch = L'─'; cell->fg = fg; cell->bg = bg;
                    cell->attr = FB_ATTR_NONE; cell->dirty = true;
                }
            } else {
                bool isSel = (i == sel) && !items[i].disabled;
                fbColor cfg = items[i].disabled ? fbLerp(fg, bg, 0.5f)
                            : (isSel ? fgSel : fg);
                fbColor cbg = isSel ? bgSel : bg;

                /* Highlight bar */
                if (isSel) {
                    for (int c2 = 1; c2 < wCols - 1; c2++) {
                        fbCell *cell = _fbGetCell(win, c2, r);
                        cell->ch = L' '; cell->fg = cfg; cell->bg = cbg;
                        cell->dirty = true;
                    }
                }

                fbSetColors(win, cfg, cbg);
                fbSetAttr(win, isSel ? FB_ATTR_BOLD : FB_ATTR_NONE);
                fbPrintAt(win, 2, r, "%s", items[i].label);
            }
        }

        fbRefresh(win);
        fbFlush(scr);

        int key = fbGetKey(scr);
        if (key == FB_KEY_UP || key == 'k') {
            do { sel = (sel - 1 + n) % n; }
            while (items[sel].disabled || items[sel].label[0] == '\0');
        } else if (key == FB_KEY_DOWN || key == 'j') {
            do { sel = (sel + 1) % n; }
            while (items[sel].disabled || items[sel].label[0] == '\0');
        } else if (key == FB_KEY_ENTER || key == '\r' || key == ' ') {
            int id = items[sel].id;
            fbDelWindow(win);
            fbFlush(scr);
            return id;
        } else if (key == FB_KEY_ESC || key == 'q') {
            fbDelWindow(win);
            fbFlush(scr);
            return -1;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  TEXT-INPUT WIDGET
 * ══════════════════════════════════════════════════════════════════ */

struct fbTextInput {
    fbWindow *win;
    int       col, row;
    int       width;
    int       maxLen;
    char     *buf;
    int       len;        /* strlen(buf)             */
    int       cursor;     /* insertion point [0,len] */
    int       scroll;     /* first visible char      */
};

fbTextInput *fbTextInputNew(fbWindow *win, int col, int row,
                            int width, int maxLen,
                            const char *initial)
{
    fbTextInput *ti = calloc(1, sizeof(fbTextInput));
    if (!ti) return NULL;
    ti->buf = calloc(1, (size_t)maxLen + 1);
    if (!ti->buf) { free(ti); return NULL; }

    ti->win    = win;
    ti->col    = col;
    ti->row    = row;
    ti->width  = width;
    ti->maxLen = maxLen;

    if (initial) {
        strncpy(ti->buf, initial, (size_t)maxLen);
        ti->buf[maxLen] = '\0';
        ti->len    = (int)strlen(ti->buf);
        ti->cursor = ti->len;
    }
    return ti;
}

void fbTextInputFree(fbTextInput *ti)
{
    if (ti) { free(ti->buf); free(ti); }
}

void fbTextInputDraw(fbTextInput *ti, fbColor fg, fbColor bg, fbColor cursorFg)
{
    if (!ti) return;
    fbWindow *win = ti->win;

    /* Ensure cursor is visible */
    if (ti->cursor < ti->scroll)
        ti->scroll = ti->cursor;
    if (ti->cursor >= ti->scroll + ti->width)
        ti->scroll = ti->cursor - ti->width + 1;

    for (int i = 0; i < ti->width; i++) {
        int  charIdx = ti->scroll + i;
        int  wcol    = ti->col + i;
        int  wrow    = ti->row;
        if (wcol >= fbWindowCols(win)) break;

        fbCell *cell = _fbGetCell(win, wcol, wrow);
        bool isCursor = (charIdx == ti->cursor);

        cell->ch    = (charIdx < ti->len) ? (wchar_t)(unsigned char)ti->buf[charIdx] : L' ';
        cell->fg    = isCursor ? cursorFg : fg;
        cell->bg    = isCursor ? fg       : bg;
        cell->attr  = isCursor ? FB_ATTR_REVERSE : FB_ATTR_NONE;
        cell->dirty = true;
    }
}

bool fbTextInputKey(fbTextInput *ti, int key)
{
    if (!ti) return false;

    switch (key) {
    case FB_KEY_LEFT:
        if (ti->cursor > 0) ti->cursor--;
        return true;
    case FB_KEY_RIGHT:
        if (ti->cursor < ti->len) ti->cursor++;
        return true;
    case FB_KEY_HOME:  case 1 /* Ctrl-A */:
        ti->cursor = 0;
        return true;
    case FB_KEY_END:   case 5 /* Ctrl-E */:
        ti->cursor = ti->len;
        return true;
    case FB_KEY_BACKSPACE:  /* 0x7F */
        if (ti->cursor > 0) {
            memmove(ti->buf + ti->cursor - 1,
                    ti->buf + ti->cursor,
                    (size_t)(ti->len - ti->cursor + 1));
            ti->cursor--;
            ti->len--;
        }
        return true;
    case FB_KEY_DELETE: case 4 /* Ctrl-D */:
        if (ti->cursor < ti->len) {
            memmove(ti->buf + ti->cursor,
                    ti->buf + ti->cursor + 1,
                    (size_t)(ti->len - ti->cursor));
            ti->len--;
        }
        return true;
    case 11 /* Ctrl-K */:
        ti->buf[ti->cursor] = '\0';
        ti->len = ti->cursor;
        return true;
    case FB_KEY_ENTER:  /* 0x0D == '\r' */
    case '\n':
    case FB_KEY_ESC:   case '\t':
        return false;   /* let caller handle these */
    default:
        if (key >= 0x20 && key < 0x100 && ti->len < ti->maxLen) {
            memmove(ti->buf + ti->cursor + 1,
                    ti->buf + ti->cursor,
                    (size_t)(ti->len - ti->cursor + 1));
            ti->buf[ti->cursor] = (char)key;
            ti->cursor++;
            ti->len++;
            return true;
        }
        return false;
    }
}

const char *fbTextInputGet(const fbTextInput *ti)
{
    return ti ? ti->buf : "";
}

void fbTextInputSet(fbTextInput *ti, const char *text)
{
    if (!ti || !text) return;
    strncpy(ti->buf, text, (size_t)ti->maxLen);
    ti->buf[ti->maxLen] = '\0';
    ti->len    = (int)strlen(ti->buf);
    ti->cursor = ti->len;
    ti->scroll = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  TOAST NOTIFICATION
 * ══════════════════════════════════════════════════════════════════ */

static const struct {
    fbColor border;
    fbColor fg;
    fbColor bg;
    const char *icon;
} _toastStyle[] = {
    [FB_TOAST_INFO]    = { FB_CYAN,          FB_WHITE, FB_RGB(20,40,60),   " ℹ " },
    [FB_TOAST_SUCCESS] = { FB_BRIGHT_GREEN,  FB_WHITE, FB_RGB(15,45,25),   " ✓ " },
    [FB_TOAST_WARNING] = { FB_BRIGHT_YELLOW, FB_WHITE, FB_RGB(50,40,10),   " ⚠ " },
    [FB_TOAST_ERROR]   = { FB_BRIGHT_RED,    FB_WHITE, FB_RGB(55,15,15),   " ✗ " },
};

void fbToast(fbScreen *scr, fbToastKind kind, const char *msg, int durationMs)
{
    if (kind < 0 || kind > FB_TOAST_ERROR) kind = FB_TOAST_INFO;
    const char *icon = _toastStyle[kind].icon;
    int msgLen  = (int)strlen(msg);
    int iconLen = 3;                        /* icon is always 3 chars */
    int wCols   = iconLen + msgLen + 4;     /* borders + padding      */
    int scrC    = fbCols(scr);
    if (wCols > scrC - 2) wCols = scrC - 2;

    int col = (scrC - wCols) / 2;
    int row = 1;                             /* near the top           */

    fbWindow *win = fbNewWindow(scr, col, row, wCols, 3);
    if (!win) return;

    fbColor bg     = _toastStyle[kind].bg;
    fbColor fg     = _toastStyle[kind].fg;
    fbColor border = _toastStyle[kind].border;

    fbClearWindow(win, bg);
    fbDrawBorder(win, FB_BORDER_ROUNDED, border);

    fbSetColors(win, border, bg);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 1, 1, "%s", icon);

    fbSetColors(win, fg, bg);
    fbSetAttr(win, FB_ATTR_NONE);
    fbPrint(win, "%.*s", wCols - iconLen - 3, msg);

    fbRefresh(win);
    fbFlush(scr);

    if (durationMs <= 0) {
        fbGetKey(scr);
    } else {
        long end = _msNow() + durationMs;
        while (_msNow() < end) {
            int rem = (int)(end - _msNow());
            if (rem < 0) rem = 0;
            int key = fbGetKeyTimeout(scr, rem < 50 ? rem : 50);
            if (key != FB_KEY_NONE) break;
        }
    }

    /* Erase: mark all cells dirty with bg and re-render blank */
    fbClearWindow(win, FB_BLACK);
    fbRefresh(win);
    fbFlush(scr);
    fbDelWindow(win);
}

/* ══════════════════════════════════════════════════════════════════
 *  TABLE / DATA-GRID
 * ══════════════════════════════════════════════════════════════════ */

void fbDrawTable(fbWindow *win, int startCol, int startRow,
                 const fbTableCol *cols,
                 const char * const * const *rows, int nRows,
                 int selRow,
                 fbColor headerFg, fbColor headerBg,
                 fbColor cellFg,   fbColor cellBg,
                 fbColor selFg,    fbColor selBg)
{
    if (!win || !cols) return;

    /* Count columns and compute widths */
    int nCols = 0;
    while (cols[nCols].header) nCols++;
    if (nCols == 0) return;

    int *colW = calloc((size_t)nCols, sizeof(int));
    if (!colW) return;

    for (int c = 0; c < nCols; c++) {
        colW[c] = cols[c].width > 0 ? cols[c].width
                                     : (int)strlen(cols[c].header);
        /* Auto-size: also check data */
        if (cols[c].width == 0) {
            for (int r = 0; r < nRows; r++) {
                int l = rows[r][c] ? (int)strlen(rows[r][c]) : 0;
                if (l > colW[c]) colW[c] = l;
            }
        }
    }

    int wW = fbWindowCols(win);
    int wH = fbWindowRows(win);

    /* ── Header row ── */
    int cc = startCol;
    fbSetColors(win, headerFg, headerBg);
    fbSetAttr(win, FB_ATTR_BOLD);
    for (int c = 0; c < nCols && cc < wW; c++) {
        /* Pad/truncate header */
        char hbuf[128];
        snprintf(hbuf, sizeof(hbuf), "%-*.*s",
                 colW[c], colW[c], cols[c].header);
        fbPrintAt(win, cc, startRow, "%s", hbuf);
        cc += colW[c] + 1;  /* +1 for column separator */
    }

    /* ── Header underline ── */
    fbSetAttr(win, FB_ATTR_NONE);
    cc = startCol;
    for (int c = 0; c < nCols && cc < wW; c++) {
        for (int x = cc; x < cc + colW[c] && x < wW; x++) {
            fbCell *cell = _fbGetCell(win, x, startRow + 1);
            cell->ch = L'─'; cell->fg = headerFg; cell->bg = headerBg;
            cell->dirty = true;
        }
        if (cc + colW[c] < wW) {
            fbCell *sep = _fbGetCell(win, cc + colW[c], startRow + 1);
            sep->ch = L'┬'; sep->fg = headerFg; sep->bg = headerBg;
            sep->dirty = true;
        }
        cc += colW[c] + 1;
    }

    /* ── Data rows ── */
    for (int r = 0; r < nRows; r++) {
        int dataRow = startRow + 2 + r;
        if (dataRow >= wH) break;

        bool isSel = (r == selRow);
        fbColor rfg = isSel ? selFg : cellFg;
        fbColor rbg = isSel ? selBg : (r % 2 == 0 ? cellBg : fbDarken(cellBg, 0.08f));

        /* Highlight entire row background */
        for (int x = startCol; x < wW; x++) {
            fbCell *cell = _fbGetCell(win, x, dataRow);
            cell->bg = rbg; cell->dirty = true;
        }

        cc = startCol;
        fbSetAttr(win, isSel ? FB_ATTR_BOLD : FB_ATTR_NONE);
        for (int c = 0; c < nCols && cc < wW; c++) {
            const char *txt = rows[r][c] ? rows[r][c] : "";
            int avail = colW[c];
            int len   = (int)strlen(txt);

            char cbuf[256];
            if (cols[c].align == FB_ALIGN_RIGHT) {
                snprintf(cbuf, sizeof(cbuf), "%*.*s", avail, avail, txt);
            } else if (cols[c].align == FB_ALIGN_CENTER) {
                int pad = (avail - len) / 2;
                snprintf(cbuf, sizeof(cbuf), "%*s%-*.*s",
                         pad, "", avail - pad, avail - pad, txt);
            } else {
                snprintf(cbuf, sizeof(cbuf), "%-*.*s", avail, avail, txt);
            }

            fbSetColors(win, rfg, rbg);
            fbPrintAt(win, cc, dataRow, "%s", cbuf);
            cc += colW[c] + 1;
        }
    }

    fbSetAttr(win, FB_ATTR_NONE);
    free(colW);
}

/* ══════════════════════════════════════════════════════════════════
 *  VERTICAL GAUGE
 * ══════════════════════════════════════════════════════════════════ */

/* Unicode vertical-eighth blocks from bottom to top */
static const wchar_t _vblocks[] = {
    L' ', L'▁', L'▂', L'▃', L'▄', L'▅', L'▆', L'▇', L'█'
};

void fbDrawGauge(fbWindow *win, int col, int row,
                 int height, int value, int maxVal,
                 fbColor fg, fbColor bg)
{
    if (!win || height <= 0 || maxVal <= 0) return;
    if (value < 0) value = 0;
    if (value > maxVal) value = maxVal;

    /* Total "eighths" filled */
    int total8 = (int)((long)value * height * 8 / maxVal);

    for (int r = 0; r < height; r++) {
        int displayRow = row + height - 1 - r;  /* bottom-up */
        if (displayRow < 0 || displayRow >= fbWindowRows(win)) continue;
        if (col < 0 || col >= fbWindowCols(win)) continue;

        int rowFilled = total8 - r * 8;
        wchar_t glyph;
        fbColor gfg, gbg;

        if (rowFilled >= 8) {
            glyph = L'█'; gfg = fg; gbg = bg;
        } else if (rowFilled > 0) {
            glyph = _vblocks[rowFilled]; gfg = fg; gbg = bg;
        } else {
            glyph = L' '; gfg = bg; gbg = bg;
        }

        fbCell *cell = _fbGetCell(win, col, displayRow);
        cell->ch    = glyph;
        cell->fg    = gfg;
        cell->bg    = gbg;
        cell->attr  = FB_ATTR_NONE;
        cell->dirty = true;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  SPARKLINE
 * ══════════════════════════════════════════════════════════════════ */

void fbDrawSparkline(fbWindow *win, int col, int row,
                     const float *values, int nValues, int width,
                     fbColor fg, fbColor bg)
{
    if (!win || !values || nValues == 0 || width == 0) return;

    for (int i = 0; i < width; i++) {
        int c = col + i;
        if (c >= fbWindowCols(win)) break;

        /* Map data index proportionally to width */
        int  di  = (nValues > width) ? i * nValues / width : i;
        if (di >= nValues) di = nValues - 1;

        float v = values[di];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;

        int  eighth = (int)(v * 8.0f);
        if (eighth > 8) eighth = 8;
        wchar_t glyph = _vblocks[eighth];

        /* Colour: gradient from cool to hot */
        fbColor gfg = fbLerp(fg, FB_BRIGHT_RED, v * 0.6f);

        fbCell *cell = _fbGetCell(win, c, row);
        cell->ch    = glyph;
        cell->fg    = gfg;
        cell->bg    = bg;
        cell->attr  = FB_ATTR_NONE;
        cell->dirty = true;
    }
}
