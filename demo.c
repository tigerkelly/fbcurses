#define _POSIX_C_SOURCE 200809L
/*
 * demo.c — fbcurses feature showcase
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o demo demo.c fbcurses.c font8x16.c -lm
 *
 * Run (needs /dev/fb0 access):
 *   sudo ./demo
 */

#include "fbcurses.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static void sleepMs(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Page 1: colours & attributes */
static void pageWelcome(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 1, 1, W - 2, H - 2);
    if (!win) return;
    int wH = fbWindowRows(win);

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "fbcurses  -  Linux Framebuffer TUI Library",
                   FB_BORDER_DOUBLE, FB_CYAN, FB_BLACK, FB_CYAN);

    fbSetColors(win, FB_GRAY, FB_BLACK);
    fbPrintAligned(win, 2, FB_ALIGN_CENTER, "A ncurses-style API for /dev/fb*");

    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 3, 4, "Colour palette:");
    fbSetAttr(win, FB_ATTR_NONE);

    static const struct { fbColor col; const char *name; } sw[] = {
        {FB_RED,           "Red    "}, {FB_GREEN,         "Green  "},
        {FB_BLUE,          "Blue   "}, {FB_YELLOW,        "Yellow "},
        {FB_MAGENTA,       "Magenta"}, {FB_CYAN,          "Cyan   "},
        {FB_GRAY,          "Gray   "}, {FB_WHITE,         "White  "},
        {FB_BRIGHT_RED,    "BrtRed "}, {FB_BRIGHT_GREEN,  "BrtGrn "},
        {FB_BRIGHT_BLUE,   "BrtBlu "}, {FB_BRIGHT_YELLOW, "BrtYlw "},
        {FB_BRIGHT_MAGENTA,"BrtMag "}, {FB_BRIGHT_CYAN,   "BrtCyn "},
    };
    int n = (int)(sizeof sw / sizeof sw[0]);
    for (int i = 0; i < n; i++) {
        int col = 3 + (i % 7) * 12;
        int row = 6 + (i / 7) * 2;
        fbSetColors(win, sw[i].col, FB_BLACK);
        fbPrintAt(win, col, row, "### ");
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbPrint(win, "%s", sw[i].name);
    }

    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 3, 11, "Text attributes:");

    fbSetAttr(win, FB_ATTR_BOLD);
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbPrintAt(win, 3, 13, "Bold ");
    fbSetAttr(win, FB_ATTR_DIM);
    fbPrint(win, "Dim ");
    fbSetAttr(win, FB_ATTR_UNDERLINE);
    fbPrint(win, "Underline ");
    fbSetAttr(win, FB_ATTR_REVERSE);
    fbSetColors(win, FB_CYAN, FB_BLACK);
    fbPrint(win, "Reverse");
    fbSetAttr(win, FB_ATTR_NONE);

    fbSetColors(win, FB_GRAY, FB_BLACK);
    fbPrintAligned(win, wH - 2, FB_ALIGN_CENTER, "Press any key for next demo...");

    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* Page 2: border styles */
static void pageBorders(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 1, 1, W - 2, H - 2);
    if (!win) return;
    int wH = fbWindowRows(win);

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Border Styles",
                   FB_BORDER_DOUBLE, FB_YELLOW, FB_BLACK, FB_YELLOW);

    static const struct { fbBorderStyle style; const char *name; fbColor col; } styles[] = {
        {FB_BORDER_SINGLE,  "Single",  FB_WHITE  },
        {FB_BORDER_DOUBLE,  "Double",  FB_CYAN   },
        {FB_BORDER_ROUNDED, "Rounded", FB_GREEN  },
        {FB_BORDER_THICK,   "Thick",   FB_RED    },
        {FB_BORDER_DASHED,  "Dashed",  FB_MAGENTA},
    };
    int ns = (int)(sizeof styles / sizeof styles[0]);

    for (int i = 0; i < ns; i++) {
        int col = 2 + i * 16;
        int bw = 14, bh = 6;
        fbDrawBox(win, col, 4, bw, bh, styles[i].style, styles[i].col);
        int nl = (int)strlen(styles[i].name);
        fbSetColors(win, styles[i].col, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, col + (bw - nl) / 2, 4 + bh / 2, "%s", styles[i].name);
        fbSetAttr(win, FB_ATTR_NONE);
    }

    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 3, 12, "Nested boxes:");
    fbSetAttr(win, FB_ATTR_NONE);

    fbDrawBox(win,  3, 13, 32, 9, FB_BORDER_SINGLE,  FB_WHITE );
    fbDrawBox(win,  5, 14, 28, 7, FB_BORDER_DOUBLE,  FB_CYAN  );
    fbDrawBox(win,  7, 15, 24, 5, FB_BORDER_ROUNDED, FB_GREEN );
    fbDrawBox(win,  9, 16, 20, 3, FB_BORDER_THICK,   FB_YELLOW);
    fbSetColors(win, FB_YELLOW, FB_BLACK);
    fbPrintAt(win, 12, 17, "  Nested!  ");

    fbSetColors(win, FB_GRAY, FB_BLACK);
    fbPrintAligned(win, wH - 2, FB_ALIGN_CENTER, "Press any key...");

    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* Page 3: progress bars & spinner */
static void pageWidgets(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 1, 1, W - 2, H - 2);
    if (!win) return;
    int wW = fbWindowCols(win);
    int wH = fbWindowRows(win);
    int barW = wW - 14;

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Widgets -- Progress Bars & Spinner",
                   FB_BORDER_DOUBLE, FB_GREEN, FB_BLACK, FB_GREEN);

    for (int pct = 0; pct <= 100; pct += 2) {
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, 3,  4, "Default  ");
        fbDrawProgressBar(win, 3,  5, barW, pct, FB_BLUE,   FB_GRAY, true);
        fbPrintAt(win, 3,  7, "Success  ");
        fbDrawProgressBar(win, 3,  8, barW, pct, FB_GREEN,  FB_GRAY, true);
        fbPrintAt(win, 3, 10, "Warning  ");
        fbDrawProgressBar(win, 3, 11, barW, pct, FB_YELLOW, FB_GRAY, true);
        fbPrintAt(win, 3, 13, "Danger   ");
        fbDrawProgressBar(win, 3, 14, barW, pct, FB_RED,    FB_GRAY, true);

        fbSetColors(win, FB_CYAN, FB_BLACK);
        fbPrintAt(win, 3, 16, "Loading... ");
        fbDrawSpinner(win, 15, 16, pct, FB_CYAN, FB_BLACK);

        fbSetColors(win, FB_GRAY, FB_BLACK);
        fbSetAttr(win, FB_ATTR_NONE);
        fbPrintAligned(win, wH - 2, FB_ALIGN_CENTER, "Press any key to skip...");

        fbRefresh(win);
        fbFlush(scr);
        if (fbGetKeyTimeout(scr, 40) != FB_KEY_NONE) break;
    }

    fbGetKey(scr);
    fbDelWindow(win);
}

/* Page 4: pixel drawing */
static void pageDrawing(fbScreen *scr)
{
    fbClear(scr, FB_BLACK);
    int W = fbWidth(scr), H = fbHeight(scr);
    int cx = W / 2, cy = H / 2;

    static const fbColor cc[] = {
        FB_RED, FB_YELLOW, FB_GREEN, FB_CYAN, FB_BLUE, FB_MAGENTA
    };
    int nc = 6;

    for (int i = 0; i < W; i += 32) {
        fbDrawLine(scr, i, 0,     cx, cy, FB_GRAY);
        fbDrawLine(scr, i, H,     cx, cy, FB_GRAY);
        fbDrawLine(scr, 0, i*H/W, cx, cy, FB_GRAY);
        fbDrawLine(scr, W, i*H/W, cx, cy, FB_GRAY);
    }
    for (int r = 20; r <= 200; r += 30)
        fbDrawCircle(scr, cx, cy, r, cc[(r/30) % nc]);

    fbFillCircle(scr, cx, cy, 20, FB_WHITE);
    fbFillCircle(scr, cx, cy, 13, FB_BLUE);
    fbFillCircle(scr, cx, cy,  6, FB_WHITE);
    fbDrawRect(scr, 4, 4, W - 8, H - 8, FB_CYAN);

    int lW = 22, lH = 3;
    fbWindow *lbl = fbNewWindow(scr, fbCols(scr)/2 - lW/2, 2, lW, lH);
    if (lbl) {
        fbClearWindow(lbl, FB_BLACK);
        fbDrawBorder(lbl, FB_BORDER_ROUNDED, FB_CYAN);
        fbSetColors(lbl, FB_WHITE, FB_BLACK);
        fbSetAttr(lbl, FB_ATTR_BOLD);
        fbPrintAligned(lbl, 1, FB_ALIGN_CENTER, "Pixel Drawing API");
        fbRefresh(lbl);
        fbDelWindow(lbl);
    }
    fbFlush(scr);
    fbGetKey(scr);
}

/* Page 5: scrolling log + input */
static void pageInput(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *outer = fbNewWindow(scr, 2, 1, W - 4, H - 2);
    if (!outer) return;
    int oW = fbWindowCols(outer);
    int oH = fbWindowRows(outer);

    fbClearWindow(outer, FB_BLACK);
    fbDrawTitleBar(outer, "Scrolling Log & Text Input",
                   FB_BORDER_DOUBLE, FB_MAGENTA, FB_BLACK, FB_MAGENTA);

    /* Log window positioned inside outer (offset from screen edge: 3, 3) */
    fbWindow *log = fbNewWindow(scr, 3, 3, oW - 2, oH - 6);
    if (!log) { fbDelWindow(outer); return; }
    fbClearWindow(log, FB_BLACK);

    static const char *msgs[] = {
        "fbcurses initialised successfully.",
        "Framebuffer device: /dev/fb0",
        "Resolution: 1920x1080 @ 32bpp",
        "Character grid: 240x67",
        "Double-buffering: enabled",
        "Input: raw terminal mode",
        "Font: 8x16 VGA bitmap",
        "All systems nominal.",
        "Ready for input...",
    };
    int nMsgs = (int)(sizeof msgs / sizeof msgs[0]);
    int logRow = 0;

    for (int i = 0; i < nMsgs; i++) {
        int lH2 = fbWindowRows(log);
        if (logRow >= lH2) {
            fbScrollUp(log, 1, FB_BLACK);
            logRow = lH2 - 1;
        }
        fbSetColors(log, FB_GREEN, FB_BLACK);
        fbMoveCursor(log, 0, logRow);
        fbPrint(log, "[  OK  ] %s", msgs[i]);
        logRow++;
        fbRefresh(log);
        fbRefresh(outer);
        fbFlush(scr);
        sleepMs(120);
    }

    fbSetColors(outer, FB_WHITE, FB_BLACK);
    fbSetAttr(outer, FB_ATTR_BOLD);
    fbPrintAt(outer, 2, oH - 3, "Enter text: ");
    fbSetAttr(outer, FB_ATTR_NONE);
    fbMoveCursor(outer, 14, oH - 3);
    fbRefresh(outer);
    fbFlush(scr);

    char buf[64] = {0};
    fbGetStr(outer, buf, (int)sizeof(buf));

    int lH3 = fbWindowRows(log);
    if (logRow >= lH3) {
        fbScrollUp(log, 1, FB_BLACK);
        logRow = lH3 - 1;
    }
    fbSetColors(log, FB_CYAN, FB_BLACK);
    fbMoveCursor(log, 0, logRow);
    fbPrint(log, "[INPUT ] You typed: \"%s\"", buf[0] ? buf : "(empty)");
    fbRefresh(log);

    fbSetColors(outer, FB_GRAY, FB_BLACK);
    fbPrintAt(outer, 2, oH - 1, "Press any key to exit...");
    fbRefresh(outer);
    fbFlush(scr);
    fbGetKey(scr);

    fbDelWindow(log);
    fbDelWindow(outer);
}

/* ── Ctrl-C / kill handling ──────────────────────────────────────── */
static volatile sig_atomic_t _running = 1;
static void _sigStop(int sig) { (void)sig; _running = 0; }
static void _installSigHandlers(void) {
    struct sigaction sa;
    sa.sa_handler = _sigStop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}


int main(void)
{
    fbScreen *scr = fbInit(NULL);
    _installSigHandlers();
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    pageWelcome(scr);
    pageBorders(scr);
    pageWidgets(scr);
    pageDrawing(scr);
    pageInput(scr);

    fbShutdown(scr);
    return 0;
}
