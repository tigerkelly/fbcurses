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
 * examples/imageview.c — framebuffer image viewer.
 *
 * Loads one or more image files (BMP, PNG, JPEG) and displays them full-
 * screen, one at a time.  Demonstrates fbimage.h: loading, fit-scaling,
 * direct pixel drawing, region cropping, and building a simple overlay.
 *
 * Usage:
 *   sudo ./imageview photo1.jpg photo2.png picture.bmp
 *
 * Keys during viewing:
 *   Right / Space     next image
 *   Left              previous image
 *   f                 toggle fit-to-screen / native (1:1) size
 *   z                 zoom in  (+10%)
 *   Z / x             zoom out (-10%)
 *   r                 reset zoom to fit
 *   arrow keys        pan when zoomed in
 *   i                 show / hide info overlay
 *   q / Esc           quit
 *
 * Build from the fbcurses source directory:
 *   gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
 *       examples/imageview.c fbimage.c -L. -lfbcurses \
 *       -lm -lpng -ljpeg -o imageview
 *   sudo ./imageview /path/to/images/photo.jpg photo2.png
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fbimage.h"
#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
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

/* ── Viewer state ────────────────────────────────────────────────── */
typedef struct {
    float  zoom;        /* 1.0 = fit-to-screen                        */
    int    panX, panY;  /* top-left offset of the image on screen     */
    bool   fitMode;     /* true = always fit-to-screen                */
    bool   showInfo;    /* overlay with filename/dimensions           */
} ViewState;

/* Draw a semi-transparent black bar at the bottom with text info */
static void drawInfoBar(fbScreen *scr, const char *filename,
                        int imgW, int imgH,
                        int idx, int total)
{
    int sw = fbWidth(scr);
    int sh = fbHeight(scr);
    int barH = 28;
    int barY = sh - barH;

    /* Draw a semi-opaque dark bar over the bottom strip */
    for (int y = barY; y < sh; y++)
        for (int x = 0; x < sw; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Filename on the left */
    char left[256];
    snprintf(left, sizeof(left), "%s  (%d x %d)", filename, imgW, imgH);
    fbDrawTextPx(scr, 10, barY + 6, left,
                 FB_WHITE, 0, FB_ATTR_NONE, &fbVga);

    /* Index on the right */
    char right[32];
    snprintf(right, sizeof(right), "%d / %d", idx + 1, total);
    int rx = sw - (int)strlen(right) * fbVga.w - 10;
    fbDrawTextPx(scr, rx, barY + 6, right,
                 FB_GRAY, 0, FB_ATTR_NONE, &fbVga);
}

/* Render the current image with the current ViewState */
static void renderImage(fbScreen *scr, fbImage *img,
                        const ViewState *vs,
                        const char *filename, int idx, int total)
{
    int sw = fbWidth(scr);
    int sh = fbHeight(scr);

    fbClear(scr, FB_BLACK);

    if (!img) {
        fbDrawTextPx(scr, 20, sh/2 - 8,
                     "Failed to load image", FB_RED, FB_BLACK,
                     FB_ATTR_NONE, &fbVga);
        fbFlush(scr);
        return;
    }

    int dstW, dstH, dstX, dstY;

    if (vs->fitMode) {
        /* Scale to fill the screen, keeping aspect ratio */
        float scaleW = (float)sw / (float)img->width;
        float scaleH = (float)sh / (float)img->height;
        float scale  = (scaleW < scaleH) ? scaleW : scaleH;
        dstW = (int)(img->width  * scale);
        dstH = (int)(img->height * scale);
        dstX = (sw - dstW) / 2;
        dstY = (sh - dstH) / 2;
        fbImageDrawScaled(scr, img, dstX, dstY, dstW, dstH);
    } else {
        /* Manual zoom + pan */
        dstW = (int)(img->width  * vs->zoom);
        dstH = (int)(img->height * vs->zoom);
        dstX = vs->panX;
        dstY = vs->panY;
        fbImageDrawScaled(scr, img, dstX, dstY, dstW, dstH);
    }

    if (vs->showInfo)
        drawInfoBar(scr, filename, img->width, img->height, idx, total);

    fbFlush(scr);
}

/* Clamp pan so image doesn't scroll completely off screen */
static void clampPan(fbScreen *scr, fbImage *img, ViewState *vs) {
    int sw = fbWidth(scr);
    int sh = fbHeight(scr);
    int dstW = (int)(img->width  * vs->zoom);
    int dstH = (int)(img->height * vs->zoom);
    /* Allow scrolling up to one screen-width past the edge */
    if (vs->panX >  sw / 2) vs->panX =  sw / 2;
    if (vs->panY >  sh / 2) vs->panY =  sh / 2;
    if (vs->panX < -(dstW - sw / 2)) vs->panX = -(dstW - sw / 2);
    if (vs->panY < -(dstH - sh / 2)) vs->panY = -(dstH - sh / 2);
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: imageview <image1> [image2 ...]\n"
                        "Formats: BMP, PNG, JPEG\n");
        return 1;
    }

    _installSigHandlers();

    fbScreen *scr = fbInit(NULL);
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    int total  = argc - 1;
    int idx    = 0;
    fbImage *img = NULL;

    ViewState vs = {
        .zoom     = 1.0f,
        .panX     = 0, .panY = 0,
        .fitMode  = true,
        .showInfo = true,
    };

    /* Initial load */
    img = fbImageLoad(argv[idx + 1]);

    while (_running) {
        /* Render */
        renderImage(scr, img, &vs, argv[idx + 1], idx, total);

        /* Input */
        int key = fbGetKey(scr);
        if (!_running) break;

        int sw = fbWidth(scr);
        int sh = fbHeight(scr);
        int panStep = 40;

        switch (key) {
        /* Navigation */
        case FB_KEY_RIGHT:
        case ' ':
            fbImageFree(img);
            idx = (idx + 1) % total;
            img = fbImageLoad(argv[idx + 1]);
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        case FB_KEY_LEFT:
            fbImageFree(img);
            idx = (idx - 1 + total) % total;
            img = fbImageLoad(argv[idx + 1]);
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        /* Zoom */
        case 'z':
            if (img) {
                vs.fitMode = false;
                if (vs.zoom < 0.1f) vs.zoom = 0.1f;
                vs.zoom *= 1.1f;
                if (vs.zoom > 20.0f) vs.zoom = 20.0f;
                /* Re-centre on zoom */
                int dstW = (int)(img->width  * vs.zoom);
                int dstH = (int)(img->height * vs.zoom);
                vs.panX  = (sw - dstW) / 2;
                vs.panY  = (sh - dstH) / 2;
            }
            break;

        case 'Z':
        case 'x':
            if (img) {
                vs.fitMode = false;
                vs.zoom /= 1.1f;
                if (vs.zoom < 0.05f) vs.zoom = 0.05f;
                int dstW = (int)(img->width  * vs.zoom);
                int dstH = (int)(img->height * vs.zoom);
                vs.panX  = (sw - dstW) / 2;
                vs.panY  = (sh - dstH) / 2;
            }
            break;

        case 'r':
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        /* Pan (only in manual zoom mode) */
        case FB_KEY_UP:
            if (!vs.fitMode && img) {
                vs.panY += panStep;
                clampPan(scr, img, &vs);
            }
            break;
        case FB_KEY_DOWN:
            if (!vs.fitMode && img) {
                vs.panY -= panStep;
                clampPan(scr, img, &vs);
            }
            break;

        /* Toggle fit mode */
        case 'f':
            vs.fitMode = !vs.fitMode;
            if (!vs.fitMode && img) {
                /* Switch to 1:1 zoom centred */
                vs.zoom = 1.0f;
                vs.panX = (sw - img->width)  / 2;
                vs.panY = (sh - img->height) / 2;
            }
            break;

        /* Toggle info overlay */
        case 'i':
            vs.showInfo = !vs.showInfo;
            break;

        /* Quit */
        case 'q':
        case FB_KEY_ESC:
            _running = 0;
            break;

        default:
            break;
        }
    }

    fbImageFree(img);
    fbShutdown(scr);
    return 0;
}
