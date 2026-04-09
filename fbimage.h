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

#ifndef FBIMAGE_H
#define FBIMAGE_H

#include "fbcurses.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * fbImage — an RGBA pixel buffer loaded from a file.
 *
 * Pixels are stored row-major, top-to-bottom, left-to-right.
 * Each pixel is 4 bytes: R, G, B, A  (A=255 fully opaque).
 */
typedef struct {
    int      width;
    int      height;
    uint8_t *pixels;    /* width * height * 4 bytes (RGBA) */
} fbImage;

/* ── Loading ──────────────────────────────────────────────────────── */

/**
 * fbImageLoad — load an image from a file.
 *
 * Supports BMP (no external dependency), PNG (requires libpng), and
 * JPEG (requires libjpeg).  The format is detected from the file header,
 * not the extension.
 *
 * Returns a heap-allocated fbImage on success, or NULL on failure.
 * Call fbImageFree() when done.
 */
fbImage *fbImageLoad(const char *path);

/**
 * fbImageLoadBMP — load a BMP image (no external libraries required).
 * Supports 24-bit and 32-bit uncompressed BMP files.
 */
fbImage *fbImageLoadBMP(const char *path);

/**
 * fbImageLoadPNG — load a PNG image (requires libpng).
 */
fbImage *fbImageLoadPNG(const char *path);

/**
 * fbImageLoadJPEG — load a JPEG image (requires libjpeg).
 */
fbImage *fbImageLoadJPEG(const char *path);

/**
 * fbImageFromRGBA — create an fbImage from an existing RGBA buffer.
 * The buffer is COPIED; the caller retains ownership of @pixels.
 */
fbImage *fbImageFromRGBA(int width, int height, const uint8_t *pixels);

/**
 * fbImageFree — release an fbImage and its pixel data.
 */
void fbImageFree(fbImage *img);

/* ── Drawing ──────────────────────────────────────────────────────── */

/**
 * fbImageDraw — blit an image to the screen at pixel position (x, y).
 *
 * Draws at the image's native resolution.  Pixels outside the screen
 * bounds are clipped.  Alpha blending is applied for semi-transparent
 * pixels.
 */
void fbImageDraw(fbScreen *scr, const fbImage *img, int x, int y);

/**
 * fbImageDrawScaled — draw an image scaled to (dstW x dstH) pixels.
 *
 * Uses bilinear interpolation for smooth scaling.
 * Pass dstW=0 or dstH=0 to use the image's native size for that axis
 * while scaling the other proportionally.
 */
void fbImageDrawScaled(fbScreen *scr, const fbImage *img,
                       int x, int y, int dstW, int dstH);

/**
 * fbImageDrawRegion — draw a sub-region of an image.
 *
 * @srcX, @srcY, @srcW, @srcH  — source rectangle within the image
 * @dstX, @dstY                — destination position on screen
 * @dstW, @dstH                — destination size (0 = same as source)
 */
void fbImageDrawRegion(fbScreen *scr, const fbImage *img,
                       int srcX, int srcY, int srcW, int srcH,
                       int dstX, int dstY, int dstW, int dstH);

/* ── Scaling helpers ──────────────────────────────────────────────── */

/**
 * fbImageScale — create a new fbImage scaled to (newW x newH).
 * The caller owns the returned image and must call fbImageFree().
 */
fbImage *fbImageScale(const fbImage *src, int newW, int newH);

/**
 * fbImageScaleFit — scale an image to fit within (maxW x maxH)
 * while preserving the aspect ratio.
 * The caller owns the returned image and must call fbImageFree().
 */
fbImage *fbImageScaleFit(const fbImage *src, int maxW, int maxH);

#endif /* FBIMAGE_H */
