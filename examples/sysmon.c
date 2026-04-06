/*
 * examples/sysmon.c — System resource monitor.
 *
 * Reads /proc/stat and /proc/meminfo every second, displays CPU cores
 * as vertical gauges, memory as a progress bar, and rolling sparklines
 * for CPU and network I/O.
 *
 * Build:
 *   gcc -O2 -o sysmon examples/sysmon.c -L. -lfbcurses -lm
 *   sudo ./sysmon
 */

#include "fbcurses.h"
#include "fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#define MAX_CPUS    16
#define HISTORY_LEN 60

/* ── /proc/stat CPU reader ──────────────────────────────────────── */
typedef struct { long long user, nice, sys, idle, iowait, irq, softirq; } CpuStat;

static int readCpuStats(CpuStat *stats, int maxCpus)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n <= maxCpus) {
        if (strncmp(line, "cpu", 3) != 0 || line[3] < '0' || line[3] > '9')
            continue;
        sscanf(line + 3, "%*d %lld %lld %lld %lld %lld %lld %lld",
               &stats[n].user, &stats[n].nice, &stats[n].sys,
               &stats[n].idle, &stats[n].iowait, &stats[n].irq, &stats[n].softirq);
        n++;
    }
    fclose(f);
    return n;
}

static float cpuPercent(const CpuStat *a, const CpuStat *b)
{
    long long da = (b->user + b->nice + b->sys + b->irq + b->softirq)
                 - (a->user + a->nice + a->sys + a->irq + a->softirq);
    long long di = (b->idle + b->iowait) - (a->idle + a->iowait);
    long long dt = da + di;
    return (dt > 0) ? (float)da / (float)dt : 0.0f;
}

/* ── /proc/meminfo reader ───────────────────────────────────────── */
static void readMem(long long *total, long long *avail)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { *total = *avail = 0; return; }
    *total = *avail = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        long long v; char unit[8];
        if (sscanf(line, "MemTotal: %lld %s", &v, unit) == 2) *total = v;
        if (sscanf(line, "MemAvailable: %lld %s", &v, unit) == 2) *avail = v;
    }
    fclose(f);
}

/* ── /proc/net/dev reader ───────────────────────────────────────── */
static long long readNetBytes(void)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return 0;
    char line[256]; long long total = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, ':')) continue;
        char *colon = strchr(line, ':');
        long long rx, tx;
        /* Skip interface name, read rx_bytes, skip 7 fields, read tx_bytes */
        if (sscanf(colon + 1, "%lld %*d %*d %*d %*d %*d %*d %*d %lld",
                   &rx, &tx) == 2)
            total += rx + tx;
    }
    fclose(f);
    return total;
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
    if (!scr) { fprintf(stderr, "fbInit: %s\n", fbGetError()); return 1; }

    int W = fbCols(scr), H = fbRows(scr);
    fbSetCursor(scr, false);

    /* Outer window */
    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "fbcurses System Monitor  — Esc/q to quit",
                   FB_BORDER_DOUBLE, FB_GREEN, FB_BLACK, FB_GREEN);

    /* Columns layout */
    int gaugeCol = 2;
    int gaugeTop = 3;
    int gaugeH   = 10;
    int sparkRow = gaugeTop + gaugeH + 2;
    int sparkW   = W - 6;

    /* History buffers */
    float cpuHist[MAX_CPUS][HISTORY_LEN];
    float memHist[HISTORY_LEN];
    float netHist[HISTORY_LEN];
    memset(cpuHist, 0, sizeof(cpuHist));
    memset(memHist, 0, sizeof(memHist));
    memset(netHist, 0, sizeof(netHist));
    int histPos = 0;

    CpuStat prev[MAX_CPUS], curr[MAX_CPUS];
    int nCpus = readCpuStats(prev, MAX_CPUS);
    long long prevNet = readNetBytes();

    int tick = 0;

    while (_running) {
        int key = fbGetKeyTimeout(scr, 1000);
        if (key == FB_KEY_ESC || key == 'q') break;

        /* Read stats */
        nCpus = readCpuStats(curr, MAX_CPUS);
        if (nCpus > MAX_CPUS) nCpus = MAX_CPUS;

        long long memTotal, memAvail;
        readMem(&memTotal, &memAvail);
        float memPct = (memTotal > 0)
            ? (float)(memTotal - memAvail) / (float)memTotal : 0.0f;

        long long netNow = readNetBytes();
        long long netDelta = netNow - prevNet;
        prevNet = netNow;
        /* Normalise to 0–1 against 125 MB/s (1 Gbit) */
        float netPct = (float)netDelta / (125.0f * 1024.0f * 1024.0f);
        if (netPct > 1.0f) netPct = 1.0f;

        /* Update history */
        float avgCpu = 0.0f;
        for (int i = 0; i < nCpus; i++) {
            cpuHist[i][histPos] = cpuPercent(&prev[i], &curr[i]);
            avgCpu += cpuHist[i][histPos];
            prev[i] = curr[i];
        }
        if (nCpus > 0) avgCpu /= nCpus;
        memHist[histPos] = memPct;
        netHist[histPos] = netPct;
        histPos = (histPos + 1) % HISTORY_LEN;

        /* Draw CPU section header */
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, gaugeCol, gaugeTop - 1, "CPU cores:");

        /* Gauge per CPU core (up to 16) */
        int gapBetween = 3;
        for (int i = 0; i < nCpus && i < 16; i++) {
            float pct = cpuHist[i][(histPos - 1 + HISTORY_LEN) % HISTORY_LEN];
            fbColor fg = fbLerp(FB_GREEN, FB_RED, pct);
            int col = gaugeCol + i * (1 + gapBetween);
            fbDrawGauge(win, col, gaugeTop, gaugeH,
                        (int)(pct * 100), 100, fg, FB_RGB(15,15,15));
            fbSetColors(win, FB_GRAY, FB_BLACK);
            fbSetAttr(win, FB_ATTR_NONE);
            fbPrintAt(win, col, gaugeTop + gaugeH, "%2d", i);
        }

        /* Memory bar */
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        int memRow = sparkRow - 2;
        fbPrintAt(win, gaugeCol, memRow, "MEM: ");
        fbDrawProgressBar(win, gaugeCol + 5, memRow,
                          sparkW - 5, (int)(memPct * 100),
                          fbLerp(FB_BLUE, FB_RED, memPct),
                          FB_RGB(15,15,25), true);

        /* CPU sparkline */
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, gaugeCol, sparkRow, "CPU avg: ");
        /* Reorder history for sparkline (oldest first) */
        float sparkBuf[HISTORY_LEN];
        for (int i = 0; i < HISTORY_LEN; i++)
            sparkBuf[i] = memHist[(histPos + i) % HISTORY_LEN];
        float cpuSparkBuf[HISTORY_LEN];
        for (int i = 0; i < HISTORY_LEN; i++) {
            float sum = 0;
            for (int c = 0; c < nCpus; c++)
                sum += cpuHist[c][(histPos + i) % HISTORY_LEN];
            cpuSparkBuf[i] = nCpus > 0 ? sum / nCpus : 0;
        }
        fbDrawSparkline(win, gaugeCol + 9, sparkRow,
                        cpuSparkBuf, HISTORY_LEN, sparkW - 9,
                        FB_CYAN, FB_BLACK);

        /* Memory sparkline */
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, gaugeCol, sparkRow + 1, "MEM:     ");
        fbDrawSparkline(win, gaugeCol + 9, sparkRow + 1,
                        sparkBuf, HISTORY_LEN, sparkW - 9,
                        FB_GREEN, FB_BLACK);

        /* Network sparkline */
        float netSparkBuf[HISTORY_LEN];
        for (int i = 0; i < HISTORY_LEN; i++)
            netSparkBuf[i] = netHist[(histPos + i) % HISTORY_LEN];
        fbSetColors(win, FB_WHITE, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        fbPrintAt(win, gaugeCol, sparkRow + 2, "NET:     ");
        fbDrawSparkline(win, gaugeCol + 9, sparkRow + 2,
                        netSparkBuf, HISTORY_LEN, sparkW - 9,
                        FB_YELLOW, FB_BLACK);

        /* Status line */
        fbSetColors(win, FB_GRAY, FB_BLACK);
        fbSetAttr(win, FB_ATTR_NONE);
        time_t now = time(NULL);
        char tbuf[32]; strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime(&now));
        fbPrintAt(win, gaugeCol, H - 2,
                  "%s  |  %d CPUs  |  MEM: %lldMB / %lldMB  |  NET: %lld KB/s",
                  tbuf, nCpus,
                  (memTotal - memAvail) / 1024, memTotal / 1024,
                  netDelta / 1024);

        /* Spinner */
        fbDrawSpinner(win, W - 3, H - 2, tick++, FB_CYAN, FB_BLACK);

        fbRefresh(win);
        fbFlush(scr);
    }

    fbSetCursor(scr, true);
    fbDelWindow(win);
    fbShutdown(scr);
    return 0;
}
