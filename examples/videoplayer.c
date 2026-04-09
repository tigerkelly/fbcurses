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
 * examples/videoplayer.c — framebuffer video player.
 *
 * Plays one or more video files (MP4, AVI, MKV, WebM, …) full-screen
 * on the framebuffer.  Demonstrates fbvideo.h: simple one-call playback,
 * the frame-by-frame API, seeking, speed control, and a HUD overlay.
 *
 * Usage:
 *   sudo ./videoplayer [options] file1 [file2 ...]
 *
 *   -l          loop playlist
 *   -s SPEED    playback speed (e.g. -s 2.0 for double speed)
 *   -t SECS     start each file at SECS seconds
 *
 * Keys during playback:
 *   Space / p       pause / resume
 *   Right arrow     seek forward  10 s
 *   Left arrow      seek backward 10 s
 *   Up / Down       speed up / slow down (±25%)
 *   n               next file in playlist
 *   r               restart current file
 *   h               show / hide HUD
 *   q / Esc         quit
 *
 * Build from the fbcurses source directory:
 *   gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
 *       examples/videoplayer.c fbvideo.c fbimage.c -L. -lfbcurses -lm \
 *       $(pkg-config --libs libavcodec libavformat libswscale \
 *                    libavutil libpng libjpeg) \
 *       -o videoplayer
 *   sudo ./videoplayer movie.mp4
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fbimage.h"
#include "fbvideo.h"
#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>

/* ── Signal handling ─────────────────────────────────────────────── */
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

/* ── Timing ──────────────────────────────────────────────────────── */
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

/* ── HUD overlay ─────────────────────────────────────────────────── */
static void formatTime(char *buf, int bufsz, double secs) {
    int h = (int)(secs / 3600);
    int m = (int)(secs / 60) % 60;
    int s = (int)secs % 60;
    if (h > 0) snprintf(buf, (size_t)bufsz, "%d:%02d:%02d", h, m, s);
    else        snprintf(buf, (size_t)bufsz, "%02d:%02d", m, s);
}

static void drawHUD(fbScreen *scr, const char *filename,
                    double elapsed, double duration,
                    double fps, double speed,
                    bool paused, int frameNum)
{
    int sw = fbWidth(scr);
    int sh = fbHeight(scr);

    /* ── Top bar: filename + speed + paused indicator ── */
    int topH = 26;
    for (int y = 0; y < topH; y++)
        for (int x = 0; x < sw; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Filename (left) */
    char topLeft[256];
    snprintf(topLeft, sizeof(topLeft), "%s", filename);
    fbDrawTextPx(scr, 10, 5, topLeft,
                 FB_WHITE, 0, FB_ATTR_NONE, &fbVga);

    /* Speed + pause (right) */
    char topRight[64];
    if (paused)
        snprintf(topRight, sizeof(topRight), "PAUSED  %.2fx", speed);
    else
        snprintf(topRight, sizeof(topRight), "%.2fx  %.1f fps", speed, fps);
    int trX = sw - (int)strlen(topRight) * fbVga.w - 10;
    fbDrawTextPx(scr, trX, 5, topRight,
                 paused ? FB_BRIGHT_YELLOW : FB_BRIGHT_GREEN, 0,
                 FB_ATTR_NONE, &fbVga);

    /* ── Bottom bar: progress bar + time ── */
    int barH  = 28;
    int barY  = sh - barH;

    for (int y = barY; y < sh; y++)
        for (int x = 0; x < sw; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Time elapsed / duration */
    char tElapsed[32], tDur[32];
    formatTime(tElapsed, sizeof(tElapsed), elapsed);
    formatTime(tDur,     sizeof(tDur),     duration > 0 ? duration : elapsed);

    char timeStr[80];
    snprintf(timeStr, sizeof(timeStr), "%s / %s", tElapsed, tDur);
    fbDrawTextPx(scr, 10, barY + 6, timeStr,
                 FB_WHITE, 0, FB_ATTR_NONE, &fbVga);

    /* Frame counter (right) */
    char frameStr[32];
    snprintf(frameStr, sizeof(frameStr), "frame %d", frameNum);
    int frX = sw - (int)strlen(frameStr) * fbVga.w - 10;
    fbDrawTextPx(scr, frX, barY + 6, frameStr,
                 FB_GRAY, 0, FB_ATTR_NONE, &fbVga);

    /* Progress bar */
    if (duration > 0) {
        int barPad = 10;
        int timeW  = (int)strlen(timeStr) * fbVga.w + 20;
        int frW    = (int)strlen(frameStr) * fbVga.w + 20;
        int pbX    = barPad + timeW;
        int pbW    = sw - pbX - frW - barPad;
        int pbY    = barY + 10;
        int pbH    = 6;

        /* Background track */
        for (int y = pbY; y < pbY + pbH; y++)
            for (int x = pbX; x < pbX + pbW; x++)
                fbDrawPixel(scr, x, y, FB_RGB(60, 60, 60));

        /* Filled portion */
        int filled = (int)((elapsed / duration) * pbW);
        if (filled > pbW) filled = pbW;
        for (int y = pbY; y < pbY + pbH; y++)
            for (int x = pbX; x < pbX + filled; x++)
                fbDrawPixel(scr, x, y, FB_BRIGHT_CYAN);

        /* Playhead dot */
        int dotX = pbX + filled;
        for (int y = pbY - 2; y < pbY + pbH + 2; y++)
            if (dotX >= 0 && dotX < sw)
                fbDrawPixel(scr, dotX, y, FB_WHITE);
    }
}

/* ── Fit rect (letterbox / pillarbox) ───────────────────────────── */
static void fitRect(int sw, int sh, int vidW, int vidH,
                    int *dstX, int *dstY, int *dstW, int *dstH) {
    float scaleW = (float)sw / (float)vidW;
    float scaleH = (float)sh / (float)vidH;
    float scale  = (scaleW < scaleH) ? scaleW : scaleH;
    *dstW = (int)(vidW * scale);
    *dstH = (int)(vidH * scale);
    *dstX = (sw - *dstW) / 2;
    *dstY = (sh - *dstH) / 2;
}

/* ── Play one file ───────────────────────────────────────────────── */
typedef struct {
    bool   loopPlaylist;
    double startSecs;
    double speed;
} PlayerOpts;

/* Returns true if user wants to continue to next file, false to quit */
static bool playFile(fbScreen *scr, const char *path,
                     const PlayerOpts *opts)
{
    int sw = fbWidth(scr);
    int sh = fbHeight(scr);

    fbVideo *vid = fbVideoOpen(path);
    if (!vid) {
        fbClear(scr, FB_BLACK);
        char msg[512];
        snprintf(msg, sizeof(msg), "Cannot open: %s", path);
        fbDrawTextPx(scr, 20, sh/2 - 8, msg, FB_RED, FB_BLACK,
                     FB_ATTR_NONE, &fbVga);
        fbDrawTextPx(scr, 20, sh/2 + 12,
                     "Press any key to continue...",
                     FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);
        fbFlush(scr);
        fbGetKey(scr);
        return true;
    }

    double duration = fbVideoDuration(vid);
    double fps      = fbVideoFPS(vid);
    double speed    = (opts->speed > 0.05) ? opts->speed : 1.0;

    /* Seek to start position */
    if (opts->startSecs > 0)
        fbVideoSeek(vid, opts->startSecs);

    /* Compute display rectangle (letterboxed) */
    int dstX, dstY, dstW, dstH;
    fitRect(sw, sh, fbVideoWidth(vid), fbVideoHeight(vid),
            &dstX, &dstY, &dstW, &dstH);

    /* Pre-print info on stderr */
    fprintf(stderr, "Playing: %s\n", path);
    fprintf(stderr, "  Size: %dx%d  FPS: %.2f  Duration: %.1fs\n",
            fbVideoWidth(vid), fbVideoHeight(vid), fps, duration);
    fprintf(stderr, "  Display: %dx%d at (%d,%d)\n",
            dstW, dstH, dstX, dstY);

    fbImage frame  = {0};         /* reused for every decoded frame  */
    bool    paused = false;
    bool    showHUD= true;
    int     frameNum = 0;
    double  elapsed  = opts->startSecs;
    bool    nextFile = false;

    long long frameInterval = (fps > 0)
        ? (long long)(1.0e6 / (fps * speed))
        : 40000LL;

    /* Clear once before starting */
    fbClear(scr, FB_BLACK);
    fbFlush(scr);

    while (_running) {

        /* Non-blocking key check */
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
            elapsed = 0;
            frameNum = 0;
            break;

        case 'h':
            showHUD = !showHUD;
            break;

        default:
            break;
        }

        if (paused) {
            /* Redraw last frame + HUD while paused */
            if (frame.pixels && showHUD) {
                drawHUD(scr, path, elapsed, duration, fps, speed,
                        true, frameNum);
                fbFlush(scr);
            }
            _sleepUs(20000);
            continue;
        }

        long long t0 = _nowUs();

        /* Decode next frame */
        if (!fbVideoNextFrame(vid, &frame, dstW, dstH)) {
            /* End of file */
            break;
        }
        frameNum++;
        elapsed = (fps > 0) ? ((double)frameNum / fps) + opts->startSecs
                             : elapsed + (frameInterval / 1e6);

        /* Blit frame pixels */
        fbImageDraw(scr, &frame, dstX, dstY);

        /* HUD overlay */
        if (showHUD)
            drawHUD(scr, path, elapsed, duration, fps, speed,
                    false, frameNum);

        fbFlush(scr);

        /* Frame-rate pacing */
        long long spent = _nowUs() - t0;
        long long wait  = frameInterval - spent;
        if (wait > 500) _sleepUs(wait);
    }

done:
    free(frame.pixels);
    fbVideoClose(vid);
    return nextFile || _running;
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: videoplayer [-l] [-s SPEED] [-t SECS] file ...\n"
                "  -l          loop playlist\n"
                "  -s SPEED    playback speed multiplier (default 1.0)\n"
                "  -t SECS     start position in seconds\n"
                "\n"
                "Keys: Space=pause  Arrows=seek/speed  n=next  r=restart"
                "  h=HUD  q=quit\n");
        return 1;
    }

    _installSigHandlers();

    /* Parse options */
    PlayerOpts opts = { .loopPlaylist = false, .startSecs = 0.0, .speed = 1.0 };
    int firstFile = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
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

    fbScreen *scr = fbInit(NULL);
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    int total = argc - firstFile;
    int idx   = 0;

    /* Playlist loop */
    do {
        for (idx = 0; idx < total && _running; idx++) {
            const char *path = argv[firstFile + idx];
            bool cont = playFile(scr, path, &opts);
            if (!cont) goto quit;
        }
    } while (opts.loopPlaylist && _running);

quit:
    fbShutdown(scr);
    return 0;
}
