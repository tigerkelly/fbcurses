/*
 * Copyright (c) 2025 Richard Kelly Wiles (rkwiles@twc.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * examples/videoplayer.c -- framebuffer video player with window support.
 *
 * Plays one or more video files (MP4, AVI, MKV, WebM, ...) on the
 * framebuffer inside an fbWindow of any size.  The window defaults to
 * full-screen but can be placed and sized with -wp (pixels) or -w (cells).
 * All rendering and the HUD are window-relative.
 *
 * Usage:
 *   sudo ./videoplayer [options] file1 [file2 ...]
 *
 *   -wp X Y W H           window position and size in PIXELS (recommended)
 *   -w  COL ROW COLS ROWS window position and size in character cells
 *   -f  FONT              font for HUD chrome  (vga, bold8, 16x32, ...)
 *   -l                    loop playlist
 *   -s SPEED              playback speed multiplier (default 1.0)
 *   -t SECS               start each file at SECS seconds
 *
 * Keys during playback:
 *   Space / p       pause / resume
 *   Right arrow     seek forward  10 s
 *   Left arrow      seek backward 10 s
 *   Up / Down       speed up / slow down (+-25%)
 *   n               next file in playlist
 *   r               restart current file
 *   h               show / hide HUD
 *   q / Esc         quit
 *
 * Build from the fbcurses source directory:
 *   gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
 *       examples/videoplayer.c -L. -lfbcurses -lm \
 *       $(pkg-config --libs libavcodec libavformat libswscale \
 *                    libavutil libpng libjpeg) \
 *       -o videoplayer
 *   sudo ./videoplayer movie.mp4
 *   sudo ./videoplayer -wp 0 0 800 600 movie.mp4
 *   sudo ./videoplayer -wp 100 100 640 480 -s 1.5 clip.mp4
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fbimage.h"
#include "fbvideo.h"
#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Signal handling                                                     */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/*  Timing                                                              */
/* ------------------------------------------------------------------ */
static long long _nowUs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
static void _sleepUs(long long us) {
    if (us <= 0) return;
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/*  HUD overlay                                                         */
/*                                                                      */
/*  All coordinates are relative to the window's pixel origin so the   */
/*  HUD always sits inside the window regardless of position or size.  */
/* ------------------------------------------------------------------ */
static void formatTime(char *buf, int bufsz, double secs) {
    int h = (int)(secs / 3600);
    int m = (int)(secs / 60) % 60;
    int s = (int)secs % 60;
    if (h > 0) snprintf(buf, (size_t)bufsz, "%d:%02d:%02d", h, m, s);
    else        snprintf(buf, (size_t)bufsz, "%02d:%02d", m, s);
}

static void drawHUD(fbWindow *win, const char *filename,
                    double elapsed, double duration,
                    double fps, double speed,
                    bool paused, int frameNum)
{
    fbScreen *scr = fbWindowGetScreen(win);

    int winX = fbWindowPixelX(win);
    int winY = fbWindowPixelY(win);
    int winW = fbWindowPixelW(win);
    int winH = fbWindowPixelH(win);

    /* ---- Top bar -------------------------------------------------- */
    int topH = 26;
    for (int y = winY; y < winY + topH; y++)
        for (int x = winX; x < winX + winW; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Filename on the left */
    char topLeft[256];
    snprintf(topLeft, sizeof(topLeft), "%s", filename);
    fbDrawTextPx(scr, winX + 10, winY + 5,
                 topLeft, FB_WHITE, 0, FB_ATTR_NONE, &fbVga);

    /* Speed / paused on the right */
    char topRight[64];
    if (paused)
        snprintf(topRight, sizeof(topRight), "PAUSED  %.2fx", speed);
    else
        snprintf(topRight, sizeof(topRight), "%.2fx  %.1f fps", speed, fps);
    int trX = winX + winW - (int)strlen(topRight) * fbVga.w - 10;
    fbDrawTextPx(scr, trX, winY + 5, topRight,
                 paused ? FB_BRIGHT_YELLOW : FB_BRIGHT_GREEN, 0,
                 FB_ATTR_NONE, &fbVga);

    /* ---- Bottom bar ----------------------------------------------- */
    int barH = 28;
    int barY = winY + winH - barH;

    for (int y = barY; y < winY + winH; y++)
        for (int x = winX; x < winX + winW; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Time elapsed / duration */
    char tElapsed[32], tDur[32];
    formatTime(tElapsed, sizeof(tElapsed), elapsed);
    formatTime(tDur,     sizeof(tDur),     duration > 0 ? duration : elapsed);

    char timeStr[80];
    snprintf(timeStr, sizeof(timeStr), "%s / %s", tElapsed, tDur);
    fbDrawTextPx(scr, winX + 10, barY + 6,
                 timeStr, FB_WHITE, 0, FB_ATTR_NONE, &fbVga);

    /* Frame counter on the right */
    char frameStr[32];
    snprintf(frameStr, sizeof(frameStr), "frame %d", frameNum);
    int frX = winX + winW - (int)strlen(frameStr) * fbVga.w - 10;
    fbDrawTextPx(scr, frX, barY + 6,
                 frameStr, FB_GRAY, 0, FB_ATTR_NONE, &fbVga);

    /* Progress bar between time and frame counter */
    if (duration > 0) {
        int timeW = (int)strlen(timeStr)  * fbVga.w + 20;
        int frW   = (int)strlen(frameStr) * fbVga.w + 20;
        int pbX   = winX + 10 + timeW;
        int pbW   = winW - 20 - timeW - frW;
        int pbY   = barY + 10;
        int pbH   = 6;

        if (pbW > 10) {
            /* Track */
            for (int y = pbY; y < pbY + pbH; y++)
                for (int x = pbX; x < pbX + pbW; x++)
                    fbDrawPixel(scr, x, y, FB_RGB(60, 60, 60));

            /* Filled */
            int filled = (int)((elapsed / duration) * pbW);
            if (filled > pbW) filled = pbW;
            for (int y = pbY; y < pbY + pbH; y++)
                for (int x = pbX; x < pbX + filled; x++)
                    fbDrawPixel(scr, x, y, FB_BRIGHT_CYAN);

            /* Playhead */
            int dotX = pbX + filled;
            for (int y = pbY - 2; y < pbY + pbH + 2; y++)
                fbDrawPixel(scr, dotX, y, FB_WHITE);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Window border (drawn when -w or -wp is used)                       */
/* ------------------------------------------------------------------ */
static void drawBorder(fbWindow *win)
{
    fbScreen *scr = fbWindowGetScreen(win);
    int wx = fbWindowPixelX(win);
    int wy = fbWindowPixelY(win);
    int ww = fbWindowPixelW(win);
    int wh = fbWindowPixelH(win);
    fbColor bc = FB_BRIGHT_CYAN;
    for (int x = wx; x < wx + ww; x++) {
        fbDrawPixel(scr, x, wy,          bc);
        fbDrawPixel(scr, x, wy + wh - 1, bc);
    }
    for (int y = wy; y < wy + wh; y++) {
        fbDrawPixel(scr, wx,          y, bc);
        fbDrawPixel(scr, wx + ww - 1, y, bc);
    }
}

/* ------------------------------------------------------------------ */
/*  Play one file                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    bool   loopPlaylist;
    bool   windowed;        /* true when -w or -wp was given           */
    double startSecs;
    double speed;
} PlayerOpts;

/* Returns true to continue to next file, false to quit entirely */
static bool playFile(fbWindow *win, const char *path,
                     const PlayerOpts *opts)
{
    fbScreen *scr = fbWindowGetScreen(win);

    int winX = fbWindowPixelX(win);
    int winY = fbWindowPixelY(win);
    int winW = fbWindowPixelW(win);
    int winH = fbWindowPixelH(win);

    fbVideo *vid = fbVideoOpen(path);
    if (!vid) {
        fbClear(scr, FB_BLACK);
        char msg[512];
        snprintf(msg, sizeof(msg), "Cannot open: %s", path);
        fbDrawTextPx(scr, winX + 20, winY + winH / 2 - 8,
                     msg, FB_RED, FB_BLACK, FB_ATTR_NONE, &fbVga);
        fbDrawTextPx(scr, winX + 20, winY + winH / 2 + 12,
                     "Press any key to continue...",
                     FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);
        fbFlush(scr);
        fbGetKey(scr);
        return true;
    }

    double duration = fbVideoDuration(vid);
    double fps      = fbVideoFPS(vid);
    double speed    = (opts->speed > 0.05) ? opts->speed : 1.0;

    if (opts->startSecs > 0)
        fbVideoSeek(vid, opts->startSecs);

    /* Compute the frame decode size: fit video inside window, keep AR */
    float scaleW = (float)winW / (float)fbVideoWidth(vid);
    float scaleH = (float)winH / (float)fbVideoHeight(vid);
    float scale  = (scaleW < scaleH) ? scaleW : scaleH;
    int dstW = (int)(fbVideoWidth(vid)  * scale);
    int dstH = (int)(fbVideoHeight(vid) * scale);
    int dstX = winX + (winW - dstW) / 2;
    int dstY = winY + (winH - dstH) / 2;

    fprintf(stderr, "Playing : %s\n", path);
    fprintf(stderr, "  Video : %dx%d  %.2f fps  %.1fs\n",
            fbVideoWidth(vid), fbVideoHeight(vid), fps, duration);
    fprintf(stderr, "  Window: (%d,%d) %dx%d px\n", winX, winY, winW, winH);
    fprintf(stderr, "  Frame : (%d,%d) %dx%d px\n", dstX, dstY, dstW, dstH);

    fbImage frame   = {0};
    bool    paused  = false;
    bool    showHUD = true;
    int     frameNum = 0;
    double  elapsed  = opts->startSecs;
    bool    nextFile = false;

    long long frameInterval = (fps > 0)
        ? (long long)(1.0e6 / (fps * speed)) : 40000LL;

    fbClear(scr, FB_BLACK);
    fbFlush(scr);

    while (_running) {

        int key = fbGetKeyTimeout(scr, 0);

        switch (key) {
        case 'q':
        case FB_KEY_ESC:
            _running = 0;
            goto done;

        case ' ':
        case 'p':
            paused = !paused;
            break;

        case FB_KEY_RIGHT:
            elapsed += 10.0;
            if (duration > 0 && elapsed > duration) elapsed = duration - 1;
            fbVideoSeek(vid, elapsed);
            break;

        case FB_KEY_LEFT:
            elapsed -= 10.0;
            if (elapsed < 0) elapsed = 0;
            fbVideoSeek(vid, elapsed);
            break;

        case FB_KEY_UP:
            speed *= 1.25;
            if (speed > 8.0) speed = 8.0;
            frameInterval = (fps > 0)
                ? (long long)(1.0e6 / (fps * speed)) : 40000LL;
            break;

        case FB_KEY_DOWN:
            speed /= 1.25;
            if (speed < 0.1) speed = 0.1;
            frameInterval = (fps > 0)
                ? (long long)(1.0e6 / (fps * speed)) : 40000LL;
            break;

        case 'n':
            nextFile = true;
            goto done;

        case 'r':
            fbVideoSeek(vid, 0);
            elapsed  = 0;
            frameNum = 0;
            break;

        case 'h':
            showHUD = !showHUD;
            break;

        default:
            break;
        }

        if (paused) {
            if (frame.pixels && showHUD) {
                drawHUD(win, path, elapsed, duration, fps, speed,
                        true, frameNum);
                if (opts->windowed) drawBorder(win);
                fbFlush(scr);
            }
            _sleepUs(20000);
            continue;
        }

        long long t0 = _nowUs();

        if (!fbVideoNextFrame(vid, &frame, dstW, dstH))
            break;

        frameNum++;
        elapsed = (fps > 0)
            ? (double)frameNum / fps + opts->startSecs
            : elapsed + frameInterval / 1.0e6;

        /* Blit frame at its letterboxed position inside the window */
        fbImageDraw(scr, &frame, dstX, dstY);

        if (showHUD)
            drawHUD(win, path, elapsed, duration, fps, speed,
                    false, frameNum);

        if (opts->windowed) drawBorder(win);

        fbFlush(scr);

        long long spent = _nowUs() - t0;
        long long wait  = frameInterval - spent;
        if (wait > 500) _sleepUs(wait);
    }

done:
    free(frame.pixels);
    fbVideoClose(vid);
    return nextFile || _running;
}

/* ------------------------------------------------------------------ */
/*  Font lookup                                                         */
/* ------------------------------------------------------------------ */
static const fbFont *lookupFont(const char *name)
{
    if (!name) return &fbVga;
    for (int i = 0; i < fbFontCount; i++)
        if (strcasecmp(fbFontList[i]->name, name) == 0)
            return fbFontList[i];
    fprintf(stderr, "Unknown font '%s', using vga\n", name);
    return &fbVga;
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: videoplayer [options] file1 [file2 ...]\n"
            "  -wp X Y W H           window position and size in PIXELS\n"
            "  -w  COL ROW COLS ROWS window position and size in cells\n"
            "  -f  FONT              HUD font (vga, bold8, 16x32, ...)\n"
            "  -l                    loop playlist\n"
            "  -s SPEED              playback speed (default 1.0)\n"
            "  -t SECS               start position in seconds\n"
            "\n"
            "Keys: Space=pause  Arrows=seek/speed  n=next  r=restart"
            "  h=HUD  q=quit\n");
        return 1;
    }

    _installSigHandlers();

    /* ---- Parse options -------------------------------------------- */
    int winCol = -1, winRow = -1, winCols = -1, winRows = -1;
    int wpX    = -1, wpY   = -1, wpW    = -1, wpH    = -1;
    const fbFont *font = &fbVga;
    PlayerOpts opts = { .loopPlaylist = false, .windowed = false,
                        .startSecs = 0.0, .speed = 1.0 };
    int firstFile = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-wp") == 0 && i + 4 < argc) {
            wpX = atoi(argv[++i]);
            wpY = atoi(argv[++i]);
            wpW = atoi(argv[++i]);
            wpH = atoi(argv[++i]);
            firstFile = i + 1;
        } else if (strcmp(argv[i], "-w") == 0 && i + 4 < argc) {
            winCol  = atoi(argv[++i]);
            winRow  = atoi(argv[++i]);
            winCols = atoi(argv[++i]);
            winRows = atoi(argv[++i]);
            firstFile = i + 1;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            font = lookupFont(argv[++i]);
            firstFile = i + 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            opts.loopPlaylist = true;
            firstFile = i + 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            opts.speed = atof(argv[++i]);
            firstFile = i + 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            opts.startSecs = atof(argv[++i]);
            firstFile = i + 1;
        } else {
            firstFile = i;
            break;
        }
    }

    if (firstFile >= argc) {
        fprintf(stderr, "No input files specified.\n");
        return 1;
    }

    /* ---- Initialise framebuffer ------------------------------------ */
    fbScreen *scr = fbInit(NULL);
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    /* ---- Convert pixel coords to cells if -wp was given ----------- */
    if (wpX >= 0) {
        int cellW = font->w;
        int bpr   = (font->w + 7) / 8;
        int cellH = font->h / bpr;

        winCol  = wpX / cellW;
        winRow  = wpY / cellH;
        winCols = wpW / cellW;
        winRows = wpH / cellH;

        if (winCols < 1) winCols = 1;
        if (winRows < 1) winRows = 1;

        fprintf(stderr,
                "Pixel window  : (%d,%d) %dx%d px\n"
                "Cell window   : (%d,%d) %dx%d cells  (font %dx%d)\n"
                "Actual pixels : (%d,%d) %dx%d px\n",
                wpX, wpY, wpW, wpH,
                winCol, winRow, winCols, winRows, cellW, cellH,
                winCol * cellW, winRow * cellH,
                winCols * cellW, winRows * cellH);
    }

    /* ---- Default: full screen ------------------------------------- */
    opts.windowed = (winCol >= 0);
    if (!opts.windowed) {
        winCol  = 0;
        winRow  = 0;
        winCols = fbCols(scr);
        winRows = fbRows(scr);
    }

    fbWindow *win = fbNewWindow(scr, winCol, winRow, winCols, winRows);
    if (!win) {
        fprintf(stderr, "fbNewWindow failed\n");
        fbShutdown(scr);
        return 1;
    }
    fbSetFont(win, font);

    if (!opts.windowed) {
        fprintf(stderr,
                "Video window: cells (%d,%d) %dx%d  =>  pixels (%d,%d) %dx%d\n"
                "  Tip: use -wp X Y W H for pixel-accurate placement\n",
                winCol, winRow, winCols, winRows,
                fbWindowPixelX(win), fbWindowPixelY(win),
                fbWindowPixelW(win), fbWindowPixelH(win));
    }

    /* ---- Playlist loop -------------------------------------------- */
    int total = argc - firstFile;
    int idx   = 0;

    do {
        for (idx = 0; idx < total && _running; idx++) {
            const char *path = argv[firstFile + idx];
            bool cont = playFile(win, path, &opts);
            if (!cont) goto quit;
        }
    } while (opts.loopPlaylist && _running);

quit:
    fbDelWindow(win);
    fbShutdown(scr);
    return 0;
}
