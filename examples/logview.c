/*
 * examples/logview.c — Live log file viewer with colour highlighting.
 *
 * Tails a file (default /var/log/syslog) and displays new lines as
 * they arrive, with colour-coded severity keywords.
 *
 * Usage:
 *   sudo ./logview [/path/to/logfile]
 *
 * Keys:
 *   q / Esc    quit
 *   Space      toggle auto-scroll (follow tail)
 *   f          filter: show only lines containing a search term
 *   c          clear filter
 */

#include "fbcurses.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>

#define MAX_LINES   2000
#define MAX_LINE_LEN 512

static char  lines[MAX_LINES][MAX_LINE_LEN];
static int   nLines = 0;
static int   scrollPos = 0;
static bool  autoScroll = true;
static char  filter[128] = "";

/* Classify a log line → colour */
static fbColor lineColor(const char *line)
{
    char lower[MAX_LINE_LEN];
    int i;
    for (i = 0; line[i] && i < MAX_LINE_LEN - 1; i++)
        lower[i] = (char)tolower((unsigned char)line[i]);
    lower[i] = '\0';

    if (strstr(lower, "error") || strstr(lower, "fail") ||
        strstr(lower, "fatal") || strstr(lower, "crit"))
        return FB_BRIGHT_RED;
    if (strstr(lower, "warn"))
        return FB_BRIGHT_YELLOW;
    if (strstr(lower, "debug") || strstr(lower, "trace"))
        return FB_GRAY;
    if (strstr(lower, "ok") || strstr(lower, "success") ||
        strstr(lower, "start") || strstr(lower, "listen"))
        return FB_BRIGHT_GREEN;
    if (strstr(lower, "info") || strstr(lower, "notice"))
        return FB_WHITE;
    return FB_RGB(180, 180, 180);
}

static void addLine(const char *line)
{
    if (nLines < MAX_LINES) {
        strncpy(lines[nLines], line, MAX_LINE_LEN - 1);
        lines[nLines][MAX_LINE_LEN - 1] = '\0';
        /* strip trailing newline */
        int l = (int)strlen(lines[nLines]);
        while (l > 0 && (lines[nLines][l-1] == '\n' || lines[nLines][l-1] == '\r'))
            lines[nLines][--l] = '\0';
        nLines++;
    } else {
        /* Rotate buffer */
        memmove(lines[0], lines[1], (MAX_LINES - 1) * MAX_LINE_LEN);
        strncpy(lines[MAX_LINES - 1], line, MAX_LINE_LEN - 1);
        lines[MAX_LINES - 1][MAX_LINE_LEN - 1] = '\0';
    }
}

static void drawLog(fbWindow *win, int contentRows, int wCols)
{
    /* Collect visible lines (apply filter) */
    int visible[MAX_LINES];
    int nVisible = 0;
    for (int i = 0; i < nLines; i++) {
        if (!filter[0] || strcasestr(lines[i], filter))
            visible[nVisible++] = i;
    }

    if (autoScroll) scrollPos = (nVisible > contentRows)
                                ? nVisible - contentRows : 0;

    for (int row = 0; row < contentRows; row++) {
        int li = scrollPos + row;
        /* Clear the row */
        fbSetColors(win, FB_BLACK, FB_BLACK);
        fbMoveCursor(win, 1, row + 2);
        for (int c = 1; c < wCols - 1; c++) fbAddChar(win, ' ');

        if (li >= nVisible) continue;

        const char *line = lines[visible[li]];
        fbColor col = lineColor(line);

        /* Highlight filter match */
        if (filter[0]) {
            const char *match = strcasestr(line, filter);
            if (match) {
                int preLen = (int)(match - line);
                int matchLen = (int)strlen(filter);
                /* Pre-match */
                fbSetColors(win, col, FB_BLACK);
                fbSetAttr(win, FB_ATTR_NONE);
                fbMoveCursor(win, 1, row + 2);
                char tmp[MAX_LINE_LEN];
                strncpy(tmp, line, (size_t)preLen);
                tmp[preLen] = '\0';
                fbAddStr(win, tmp);
                /* Match highlighted */
                fbSetColors(win, FB_BLACK, FB_BRIGHT_YELLOW);
                fbSetAttr(win, FB_ATTR_BOLD);
                strncpy(tmp, match, (size_t)matchLen);
                tmp[matchLen] = '\0';
                fbAddStr(win, tmp);
                /* Post-match */
                fbSetColors(win, col, FB_BLACK);
                fbSetAttr(win, FB_ATTR_NONE);
                fbAddStr(win, match + matchLen);
                continue;
            }
        }

        fbSetColors(win, col, FB_BLACK);
        fbSetAttr(win, FB_ATTR_NONE);
        fbPrintAt(win, 1, row + 2, "%-*.*s",
                  wCols - 2, wCols - 2, line);
    }
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


int main(int argc, char *argv[])
{
    const char *logFile = (argc > 1) ? argv[1] : "/var/log/syslog";

    FILE *fp = fopen(logFile, "r");
    if (!fp) {
        /* Try common alternatives */
        fp = fopen("/var/log/messages", "r");
        if (!fp) { perror("fopen"); return 1; }
        logFile = "/var/log/messages";
    }

    /* Pre-load last 200 lines */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    long readFrom = fsize > 20000 ? fsize - 20000 : 0;
    fseek(fp, readFrom, SEEK_SET);

    char buf[MAX_LINE_LEN];
    if (readFrom > 0) fgets(buf, sizeof(buf), fp); /* skip partial line */
    while (fgets(buf, sizeof(buf), fp)) addLine(buf);

    fbScreen *scr = fbInit(NULL);
    _installSigHandlers();
    if (!scr) { fprintf(stderr, "fbInit: %s\n", fbGetError()); return 1; }

    int W = fbCols(scr), H = fbRows(scr);
    fbSetCursor(scr, false);

    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    int contentRows = H - 4;
    bool needRedraw = true;
    int tick = 0;

    while (_running) {
        /* Read new lines from file */
        bool gotNew = false;
        while (fgets(buf, sizeof(buf), fp)) { addLine(buf); gotNew = true; }
        if (gotNew) needRedraw = true;

        int key = fbGetKeyTimeout(scr, 250);

        if (key == FB_KEY_ESC || key == 'q') break;

        if (key == FB_KEY_UP   || key == 'k') { scrollPos--; autoScroll = false; needRedraw = true; }
        if (key == FB_KEY_DOWN || key == 'j') { scrollPos++; needRedraw = true; }
        if (key == FB_KEY_PAGE_UP)   { scrollPos -= contentRows; autoScroll = false; needRedraw = true; }
        if (key == FB_KEY_PAGE_DOWN) { scrollPos += contentRows; needRedraw = true; }
        if (key == FB_KEY_HOME) { scrollPos = 0; autoScroll = false; needRedraw = true; }
        if (key == FB_KEY_END)  { autoScroll = true; needRedraw = true; }
        if (key == ' ') { autoScroll = !autoScroll; needRedraw = true; }
        if (key == 'c') { filter[0] = '\0'; needRedraw = true; }

        if (key == 'f') {
            /* Inline filter input */
            fbSetColors(win, FB_WHITE, FB_RGB(20,20,40));
            fbPrintAt(win, 2, H - 2, "Filter: %-*s", W - 12, "");
            fbTextInput *ti = fbTextInputNew(win, 10, H - 2, W - 14, 64, filter);
            if (ti) {
                fbRefresh(win); fbFlush(scr);
                fbGetStr(win, filter, sizeof(filter));
                fbTextInputFree(ti);
            }
            needRedraw = true;
        }

        if (scrollPos < 0) scrollPos = 0;

        if (needRedraw || (tick % 4 == 0)) {
            /* Title bar */
            char title[128];
            snprintf(title, sizeof(title), "Log: %s%s%s",
                     logFile,
                     filter[0] ? "  | filter: " : "",
                     filter[0] ? filter : "");
            fbClearWindow(win, FB_BLACK);
            fbDrawTitleBar(win, title, FB_BORDER_SINGLE,
                           FB_CYAN, FB_BLACK, FB_CYAN);

            drawLog(win, contentRows, W);

            /* Status bar */
            fbSetColors(win, FB_GRAY, FB_BLACK);
            fbSetAttr(win, FB_ATTR_NONE);
            fbPrintAt(win, 1, H - 2,
                      " ↑↓/PgUp/PgDn  Space=auto-scroll[%s]  f=filter  c=clear  q=quit",
                      autoScroll ? "ON" : "OFF");

            /* Spinner shows live activity */
            fbDrawSpinner(win, W - 2, H - 2, tick, FB_CYAN, FB_BLACK);

            fbRefresh(win);
            fbFlush(scr);
            needRedraw = false;
        }
        tick++;
    }

    fclose(fp);
    fbSetCursor(scr, true);
    fbDelWindow(win);
    fbShutdown(scr);
    return 0;
}
