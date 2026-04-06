/*
 * examples/clock.c — Real-time clock using the LCD7 font.
 *
 * Displays HH:MM:SS in large 7-segment style digits, updating every second.
 * Press Esc or 'q' to quit.
 *
 * Build from the fbcurses source directory:
 *   gcc -O2 -o clock examples/clock.c -I. -L. -lfbcurses -lm
 *   sudo ./clock
 */

#include "fbcurses.h"
#include "fonts.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

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
    if (!scr) { fprintf(stderr, "fbInit: %s\n", fbGetError()); return 1; }

    int W = fbCols(scr), H = fbRows(scr);

    /* Outer border window */
    fbWindow *outer = fbNewWindow(scr, 0, 0, W, H);
    fbClearWindow(outer, FB_BLACK);
    fbDrawTitleBar(outer, "fbcurses Clock  — Esc to quit",
                   FB_BORDER_DOUBLE, FB_CYAN, FB_BLACK, FB_CYAN);
    fbSetColors(outer, FB_GRAY, FB_BLACK);
    fbPrintAligned(outer, H - 2, FB_ALIGN_CENTER,
                   "LCD7 font  |  fbDrawTextPx  |  real-time update");
    fbRefresh(outer);

    /* Calculate pixel position to centre the clock */
    /* Time string: "HH:MM:SS" = 8 chars × LCD7 width (8px) = 64px
       We'll use 3× scale by setting a large font window */
    int clockW = 20, clockH = 5;
    int clockCol = (W - clockW) / 2;
    int clockRow = (H - clockH) / 2 - 1;

    fbWindow *clockWin = fbNewWindow(scr, clockCol, clockRow, clockW, clockH);

    char lastTime[16] = "";

    fbSetCursor(scr, false);

    while (_running) {
        /* Check for quit */
        int key = fbGetKeyTimeout(scr, 200);
        if (key == FB_KEY_ESC || key == 'q') break;

        /* Get current time */
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timeBuf[16];
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tm);

        if (strcmp(timeBuf, lastTime) == 0) continue;
        strcpy(lastTime, timeBuf);

        /* Date line */
        char dateBuf[32];
        strftime(dateBuf, sizeof(dateBuf), "%A, %B %d %Y", tm);
        fbSetColors(outer, FB_GRAY, FB_BLACK);
        fbPrintAligned(outer, clockRow + clockH + 1, FB_ALIGN_CENTER, dateBuf);
        fbRefresh(outer);

        /* Draw the time using LCD7 font directly at pixel coords */
        fbClearWindow(clockWin, FB_BLACK);
        fbRefresh(clockWin);   /* clear old digits */

        /* Centre the time string in pixels */
        int strPxW = (int)strlen(timeBuf) * fbFont24x48.w;
        int startX = fbWidth(scr) / 2 - strPxW / 2;
        int startY = fbHeight(scr) / 2 - fbFont24x48.h / 2 - 16;

        /* Draw each character with colour based on position */
        int x = startX;
        for (const char *p = timeBuf; *p; p++) {
            fbColor col;
            if (*p == ':') col = FB_GRAY;
            else if (p - timeBuf < 2) col = FB_BRIGHT_CYAN;   /* hours */
            else if (p - timeBuf < 5) col = FB_BRIGHT_GREEN;   /* minutes */
            else col = FB_BRIGHT_YELLOW;                        /* seconds */

            fbDrawTextPx(scr, x, startY, (char[]){*p, '\0'},
                         col, FB_BLACK, FB_ATTR_NONE, &fbFont24x48);
            x += fbFont24x48.w;
        }

        /* Seconds progress bar below the clock */
        int barY  = startY + fbFont24x48.h + 8;
        int barW  = strPxW;
        int barX  = startX;
        int filled = barW * tm->tm_sec / 59;

        for (int i = 0; i < barW; i++) {
            fbColor c = (i < filled) ? fbLerp(FB_BRIGHT_CYAN, FB_BRIGHT_GREEN,
                                               (float)i / barW)
                                     : FB_RGB(20, 20, 20);
            fbDrawLine(scr, barX + i, barY, barX + i, barY + 2, c);
        }

        fbFlush(scr);
    }

    fbSetCursor(scr, true);
    fbDelWindow(clockWin);
    fbDelWindow(outer);
    fbShutdown(scr);
    return 0;
}
