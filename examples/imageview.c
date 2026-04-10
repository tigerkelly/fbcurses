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
 * examples/imageview.c -- framebuffer image viewer using fbWindow.
 *
 * Loads one or more image files (BMP, PNG, JPEG) and displays them inside
 * an fbWindow of any size.  The window defaults to full-screen but can be
 * placed and sized with -w or -wp.  All rendering -- scaling, panning,
 * zoom, info bar -- is window-relative, using fbWindowPixelX/Y/W/H and
 * fbImageDrawInWindow.
 *
 * Usage:
 *   sudo ./imageview [options] photo1.jpg photo2.png picture.bmp
 *
 *   -wp X Y W H     window position and size in PIXELS (recommended)
 *   -w  COL ROW COLS ROWS  window position and size in character cells
 *   -f  FONT        font for the info bar  (vga, bold8, 16x32, ...)
 *
 * Keys:
 *   Right / Space   next image
 *   Left            previous image
 *   f               toggle fit-to-window / 1:1 native size
 *   z               zoom in  (+10%)
 *   Z / x           zoom out (-10%)
 *   r               reset zoom to fit
 *   Arrow keys      pan when in zoom mode
 *   i               show / hide info bar
 *   q / Esc         quit
 *
 * Build from the fbcurses source directory:
 *   gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
 *       examples/imageview.c -L. -lfbcurses \
 *       -lm -lpng -ljpeg -o imageview
 *   sudo ./imageview photo.jpg
 *   sudo ./imageview -wp 0 0 640 480 photo.jpg
 *   sudo ./imageview -wp 100 100 800 600 -f bold8 photo.jpg
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fbimage.h"
#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <signal.h>

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
/*  Viewer state                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    float zoom;        /* current zoom factor (1.0 = fit-to-window)   */
    int   panX, panY;  /* pan offset in pixels, relative to window    */
    bool  fitMode;     /* true  -> fit image inside window            */
    bool  showInfo;    /* draw the bottom info bar                    */
    bool  windowed;    /* true  -> -w was given; draw window border   */
} ViewState;

/* ------------------------------------------------------------------ */
/*  Info bar                                                            */
/*                                                                      */
/*  Drawn as a solid black strip at the bottom of the window with      */
/*  the filename/dimensions on the left and N/total on the right.      */
/*  All coordinates are window-relative so the bar always sits inside  */
/*  the window regardless of its position or size on screen.           */
/* ------------------------------------------------------------------ */
static void drawInfoBar(fbWindow *win,
                        const char *filename, int imgW, int imgH,
                        int idx, int total)
{
    fbScreen *scr = fbWindowGetScreen(win);

    int winX = fbWindowPixelX(win);
    int winY = fbWindowPixelY(win);
    int winW = fbWindowPixelW(win);
    int winH = fbWindowPixelH(win);

    int barH = 28;
    int barY = winY + winH - barH;

    /* Black bar */
    for (int y = barY; y < winY + winH; y++)
        for (int x = winX; x < winX + winW; x++)
            fbDrawPixel(scr, x, y, FB_RGB(0, 0, 0));

    /* Filename + dimensions on the left */
    char left[256];
    snprintf(left, sizeof(left), "%s  (%d x %d)", filename, imgW, imgH);
    fbDrawTextPx(scr, winX + 8, barY + 6,
                 left, FB_WHITE, FB_RGB(0,0,0), FB_ATTR_NONE, &fbVga);

    /* N / total on the right */
    char right[32];
    snprintf(right, sizeof(right), "%d / %d", idx + 1, total);
    int rx = winX + winW - (int)strlen(right) * fbVga.w - 8;
    if (rx > winX + winW / 2)
        fbDrawTextPx(scr, rx, barY + 6,
                     right, FB_GRAY, FB_RGB(0,0,0), FB_ATTR_NONE, &fbVga);
}

/* ------------------------------------------------------------------ */
/*  Render                                                              */
/*                                                                      */
/*  In fit mode:  fbImageDrawInWindow handles all scaling and centring. */
/*  In zoom mode: fbImageDrawScaled is called with window-relative      */
/*                pixel offsets derived from panX/panY.                 */
/* ------------------------------------------------------------------ */
static void renderImage(fbWindow *win, const fbImage *img,
                        const ViewState *vs,
                        const char *filename, int idx, int total)
{
    fbScreen *scr = fbWindowGetScreen(win);

    int winX = fbWindowPixelX(win);
    int winY = fbWindowPixelY(win);

    /* Clear entire screen to black, then render into the window area */
    fbClear(scr, FB_BLACK);

    if (!img) {
        char errmsg[256];
        snprintf(errmsg, sizeof(errmsg), "Failed to load: %s", filename);
        fbDrawTextPx(scr, winX + 16, winY + fbWindowPixelH(win) / 2 - 8,
                     errmsg, FB_RED, FB_BLACK, FB_ATTR_NONE, &fbVga);
        fbDrawTextPx(scr, winX + 16, winY + fbWindowPixelH(win) / 2 + 12,
                     "(check stderr for details)",
                     FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);
        fbFlush(scr);
        return;
    }

    if (vs->fitMode) {
        /*
         * fbImageDrawInWindow scales the image to fill the window while
         * preserving the aspect ratio, centred inside the window bounds.
         */
        fbImageDrawInWindow(win, img, /*keepAspect=*/true);
    } else {
        /*
         * Manual zoom + pan.
         * vs->panX / vs->panY are pixel offsets relative to the window
         * top-left, so we add winX/winY to get absolute screen coords.
         */
        int dstW = (int)(img->width  * vs->zoom);
        int dstH = (int)(img->height * vs->zoom);
        fbImageDrawScaled(scr, img,
                          winX + vs->panX, winY + vs->panY,
                          dstW, dstH);
    }

    /* Draw a thin border around the window when running in windowed mode
       so the user can see exactly where the window boundaries are.    */
    if (vs->windowed) {
        fbScreen *scr2 = fbWindowGetScreen(win);
        int wx = fbWindowPixelX(win);
        int wy = fbWindowPixelY(win);
        int ww = fbWindowPixelW(win);
        int wh = fbWindowPixelH(win);
        fbColor bc = FB_BRIGHT_CYAN;
        for (int x = wx; x < wx + ww; x++) {
            fbDrawPixel(scr2, x, wy,          bc);
            fbDrawPixel(scr2, x, wy + wh - 1, bc);
        }
        for (int y = wy; y < wy + wh; y++) {
            fbDrawPixel(scr2, wx,          y, bc);
            fbDrawPixel(scr2, wx + ww - 1, y, bc);
        }
    }

    if (vs->showInfo)
        drawInfoBar(win, filename, img->width, img->height, idx, total);

    fbFlush(scr);
}

/* ------------------------------------------------------------------ */
/*  Pan clamping                                                        */
/*                                                                      */
/*  Keeps the zoomed image at least half-visible inside the window.    */
/* ------------------------------------------------------------------ */
static void clampPan(const fbWindow *win, const fbImage *img, ViewState *vs)
{
    int winW = fbWindowPixelW(win);
    int winH = fbWindowPixelH(win);
    int dstW = (int)(img->width  * vs->zoom);
    int dstH = (int)(img->height * vs->zoom);

    if (vs->panX >  winW / 2)          vs->panX =  winW / 2;
    if (vs->panY >  winH / 2)          vs->panY =  winH / 2;
    if (vs->panX < -(dstW - winW / 2)) vs->panX = -(dstW - winW / 2);
    if (vs->panY < -(dstH - winH / 2)) vs->panY = -(dstH - winH / 2);
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
            "Usage: imageview [options] image1 [image2 ...]\n"
            "  -wp X Y W H           window position and size in PIXELS\n"
            "  -w  COL ROW COLS ROWS window position and size in character cells\n"
            "  -f  FONT              font for chrome (vga, bold8, 16x32, ...)\n"
            "Formats: BMP, PNG, JPEG\n"
            "Keys: Right/Space=next  Left=prev  f=fit  z/Z=zoom\n"
            "      Arrows=pan  r=reset  i=info  q=quit\n");
        return 1;
    }

    _installSigHandlers();

    /* ---- Parse options -------------------------------------------- */
    int winCol = -1, winRow = -1, winCols = -1, winRows = -1;
    int wpX    = -1, wpY   = -1, wpW    = -1, wpH    = -1;  /* pixel coords */
    const fbFont *font = &fbVga;
    int firstFile = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-wp") == 0 && i + 4 < argc) {
            /* Pixel-based window: stored and converted to cells after fbInit */
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
        } else {
            firstFile = i;
            break;
        }
    }

    if (firstFile >= argc) {
        fprintf(stderr, "No image files specified.\n");
        return 1;
    }

    /* ---- Initialise ------------------------------------------------ */
    fbScreen *scr = fbInit(NULL);
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    /* Convert pixel coords to character cells.
       Use the active font cell size for the conversion.
       Cell = floor(pixel / cell_size) so the window fits inside the
       requested pixel box without overflowing.                        */
    if (wpX >= 0) {
        /* Pixel mode: derive cell position and size from font metrics */
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

    /* Default: fill the whole screen */
    bool windowed = (winCol >= 0);
    if (!windowed) {
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

    /* Report the exact pixel area the window occupies */
    if (wpX < 0) {
        /* Cell mode: show both cells and derived pixels */
        fprintf(stderr,
                "Image window: cells (%d,%d) %dx%d  =>  pixels (%d,%d) %dx%d\n"
                "  Tip: use -wp X Y W H for pixel-accurate placement\n",
                winCol, winRow, winCols, winRows,
                fbWindowPixelX(win), fbWindowPixelY(win),
                fbWindowPixelW(win), fbWindowPixelH(win));
    }

    /* ---- Viewer loop ---------------------------------------------- */
    int total = argc - firstFile;
    int idx   = 0;

    ViewState vs = {
        .zoom = 1.0f, .panX = 0, .panY = 0,
        .fitMode = true, .showInfo = true,
        .windowed = windowed,
    };

    fbImage *img = fbImageLoad(argv[firstFile]);

    while (_running) {
        renderImage(win, img, &vs, argv[firstFile + idx], idx, total);

        int key = fbGetKey(scr);
        if (!_running) break;

        /* Pan step = 1/16 of the window width, minimum 8 pixels */
        int winW    = fbWindowPixelW(win);
        int winH    = fbWindowPixelH(win);
        int panStep = winW / 16;
        if (panStep < 8) panStep = 8;

        switch (key) {

        /* ---- Navigation ------------------------------------------- */
        case FB_KEY_RIGHT:
        case ' ':
            fbImageFree(img);
            idx = (idx + 1) % total;
            img = fbImageLoad(argv[firstFile + idx]);
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        case FB_KEY_LEFT:
            fbImageFree(img);
            idx = (idx - 1 + total) % total;
            img = fbImageLoad(argv[firstFile + idx]);
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        /* ---- Zoom ------------------------------------------------- */
        case 'z':
            if (img) {
                vs.fitMode = false;
                vs.zoom *= 1.1f;
                if (vs.zoom > 20.0f) vs.zoom = 20.0f;
                /* Re-centre in window */
                vs.panX = (winW - (int)(img->width  * vs.zoom)) / 2;
                vs.panY = (winH - (int)(img->height * vs.zoom)) / 2;
            }
            break;

        case 'Z':
        case 'x':
            if (img) {
                vs.fitMode = false;
                vs.zoom /= 1.1f;
                if (vs.zoom < 0.05f) vs.zoom = 0.05f;
                vs.panX = (winW - (int)(img->width  * vs.zoom)) / 2;
                vs.panY = (winH - (int)(img->height * vs.zoom)) / 2;
            }
            break;

        case 'r':
            vs.zoom = 1.0f; vs.panX = 0; vs.panY = 0;
            vs.fitMode = true;
            break;

        /* ---- Pan (zoom mode only) ---------------------------------- */
        case FB_KEY_UP:
            if (!vs.fitMode && img) { vs.panY += panStep; clampPan(win, img, &vs); }
            break;

        case FB_KEY_DOWN:
            if (!vs.fitMode && img) { vs.panY -= panStep; clampPan(win, img, &vs); }
            break;

        /* ---- Toggle fit ------------------------------------------- */
        case 'f':
            vs.fitMode = !vs.fitMode;
            if (!vs.fitMode && img) {
                vs.zoom = 1.0f;
                vs.panX = (winW - img->width)  / 2;
                vs.panY = (winH - img->height) / 2;
            }
            break;

        /* ---- Toggle info bar -------------------------------------- */
        case 'i':
            vs.showInfo = !vs.showInfo;
            break;

        /* ---- Quit ------------------------------------------------- */
        case 'q':
        case FB_KEY_ESC:
            _running = 0;
            break;

        default: break;
        }
    }

    fbImageFree(img);
    fbDelWindow(win);
    fbShutdown(scr);
    return 0;
}
