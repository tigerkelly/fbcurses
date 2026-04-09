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

#include "fbimage.h"
#include "fbcurses_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Optional format libraries ────────────────────────────────────── */
#ifdef FBIMAGE_PNG
#  include <png.h>
#endif
#ifdef FBIMAGE_JPEG
#  include <jpeglib.h>
#  include <jerror.h>
#endif

/* ═══════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════ */

static fbImage *_imgAlloc(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    fbImage *img = calloc(1, sizeof(fbImage));
    if (!img) return NULL;
    img->width  = w;
    img->height = h;
    img->pixels = calloc((size_t)(w * h), 4);
    if (!img->pixels) { free(img); return NULL; }
    return img;
}

/* Bilinear interpolation: sample src at fractional (fx, fy) */
static void _bilinear(const fbImage *src, float fx, float fy, uint8_t *out)
{
    int x0 = (int)fx,  y0 = (int)fy;
    int x1 = x0 + 1,  y1 = y0 + 1;
    float tx = fx - x0, ty = fy - y0;

    if (x1 >= src->width)  x1 = src->width  - 1;
    if (y1 >= src->height) y1 = src->height - 1;

    const uint8_t *p00 = src->pixels + (y0 * src->width + x0) * 4;
    const uint8_t *p10 = src->pixels + (y0 * src->width + x1) * 4;
    const uint8_t *p01 = src->pixels + (y1 * src->width + x0) * 4;
    const uint8_t *p11 = src->pixels + (y1 * src->width + x1) * 4;

    float w00 = (1.0f - tx) * (1.0f - ty);
    float w10 = tx           * (1.0f - ty);
    float w01 = (1.0f - tx) * ty;
    float w11 = tx           * ty;

    for (int c = 0; c < 4; c++)
        out[c] = (uint8_t)(p00[c]*w00 + p10[c]*w10 + p01[c]*w01 + p11[c]*w11);
}

/* ═══════════════════════════════════════════════════════════════════
 *  BMP loader (no external dependencies)
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageLoadBMP(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* BMP file header (14 bytes) */
    uint8_t fhdr[14];
    if (fread(fhdr, 1, 14, f) != 14) { fclose(f); return NULL; }
    if (fhdr[0] != 'B' || fhdr[1] != 'M') { fclose(f); return NULL; }

    uint32_t dataOff = (uint32_t)(fhdr[10] | (fhdr[11]<<8) |
                                  (fhdr[12]<<16) | (fhdr[13]<<24));

    /* DIB header — read BITMAPINFOHEADER (40 bytes minimum) */
    uint8_t ihdr[40];
    if (fread(ihdr, 1, 40, f) != 40) { fclose(f); return NULL; }

    int32_t  w       = (int32_t)(ihdr[4]|(ihdr[5]<<8)|(ihdr[6]<<16)|(ihdr[7]<<24));
    int32_t  h       = (int32_t)(ihdr[8]|(ihdr[9]<<8)|(ihdr[10]<<16)|(ihdr[11]<<24));
    uint16_t bpp     = (uint16_t)(ihdr[14] | (ihdr[15]<<8));
    uint32_t compr   = (uint32_t)(ihdr[16]|(ihdr[17]<<8)|(ihdr[18]<<16)|(ihdr[19]<<24));

    /* We support 24-bpp and 32-bpp uncompressed */
    if ((bpp != 24 && bpp != 32) || compr != 0) {
        fclose(f); return NULL;
    }

    bool flipped = (h > 0);   /* positive height = bottom-up */
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) { fclose(f); return NULL; }

    fbImage *img = _imgAlloc(w, h);
    if (!img) { fclose(f); return NULL; }

    fseek(f, (long)dataOff, SEEK_SET);

    int bytesPerPx  = bpp / 8;
    int rowBytes    = (w * bytesPerPx + 3) & ~3;   /* 4-byte aligned */
    uint8_t *row = malloc((size_t)rowBytes);
    if (!row) { fbImageFree(img); fclose(f); return NULL; }

    for (int y = 0; y < h; y++) {
        if (fread(row, 1, (size_t)rowBytes, f) != (size_t)rowBytes) break;
        int dstY = flipped ? (h - 1 - y) : y;
        uint8_t *dst = img->pixels + dstY * w * 4;
        const uint8_t *src = row;
        for (int x = 0; x < w; x++, src += bytesPerPx, dst += 4) {
            dst[0] = src[2];   /* R */
            dst[1] = src[1];   /* G */
            dst[2] = src[0];   /* B */
            dst[3] = (bpp == 32) ? src[3] : 255;   /* A */
        }
    }
    free(row);
    fclose(f);
    return img;
}

/* ═══════════════════════════════════════════════════════════════════
 *  PNG loader (requires libpng, compiled in if FBIMAGE_PNG defined)
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageLoadPNG(const char *path)
{
#ifndef FBIMAGE_PNG
    (void)path;
    return NULL;   /* PNG support not compiled in */
#else
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* Check PNG signature */
    uint8_t sig[8];
    if (fread(sig, 1, 8, f) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(f); return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) { fclose(f); return NULL; }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(f); return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f); return NULL;
    }

    png_init_io(png, f);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    int w   = (int)png_get_image_width(png, info);
    int h   = (int)png_get_image_height(png, info);
    int ct  = png_get_color_type(png, info);
    int bd  = png_get_bit_depth(png, info);

    /* Normalise to 8-bit RGBA */
    if (bd == 16)              png_set_strip_16(png);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ct == PNG_COLOR_TYPE_GRAY && bd < 8)  png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (ct == PNG_COLOR_TYPE_RGB  ||
        ct == PNG_COLOR_TYPE_GRAY ||
        ct == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (ct == PNG_COLOR_TYPE_GRAY ||
        ct == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    fbImage *img = _imgAlloc(w, h);
    if (!img) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f); return NULL;
    }

    png_bytep *rows = malloc((size_t)h * sizeof(png_bytep));
    if (!rows) {
        fbImageFree(img);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f); return NULL;
    }
    for (int y = 0; y < h; y++)
        rows[y] = img->pixels + y * w * 4;

    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(f);
    return img;
#endif
}

/* ═══════════════════════════════════════════════════════════════════
 *  JPEG loader (requires libjpeg, compiled in if FBIMAGE_JPEG defined)
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageLoadJPEG(const char *path)
{
#ifndef FBIMAGE_JPEG
    (void)path;
    return NULL;   /* JPEG support not compiled in */
#else
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;

    fbImage *img = _imgAlloc(w, h);
    if (!img) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f); return NULL;
    }

    JSAMPARRAY rowbuf = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr)&cinfo, JPOOL_IMAGE,
         (JDIMENSION)(w * 3), 1);

    for (int y = 0; y < h; y++) {
        jpeg_read_scanlines(&cinfo, rowbuf, 1);
        uint8_t *dst = img->pixels + y * w * 4;
        uint8_t *src = rowbuf[0];
        for (int x = 0; x < w; x++, src += 3, dst += 4) {
            dst[0] = src[0];   /* R */
            dst[1] = src[1];   /* G */
            dst[2] = src[2];   /* B */
            dst[3] = 255;      /* A */
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
#endif
}

/* ═══════════════════════════════════════════════════════════════════
 *  Auto-detect loader
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageLoad(const char *path)
{
    if (!path) return NULL;

    /* Detect format from the first few bytes */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t magic[8] = {0};
    if (fread(magic, 1, 8, f) < 1) { /* best-effort magic read */ }
    fclose(f);

    /* PNG: \x89 P N G \r \n \x1a \n */
    if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G')
        return fbImageLoadPNG(path);

    /* JPEG: FF D8 FF */
    if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF)
        return fbImageLoadJPEG(path);

    /* BMP: B M */
    if (magic[0] == 'B' && magic[1] == 'M')
        return fbImageLoadBMP(path);

    return NULL;   /* unrecognised format */
}

/* ═══════════════════════════════════════════════════════════════════
 *  Construction / destruction
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageFromRGBA(int width, int height, const uint8_t *pixels)
{
    fbImage *img = _imgAlloc(width, height);
    if (!img) return NULL;
    memcpy(img->pixels, pixels, (size_t)(width * height * 4));
    return img;
}

void fbImageFree(fbImage *img)
{
    if (!img) return;
    free(img->pixels);
    free(img);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Drawing
 * ═══════════════════════════════════════════════════════════════════ */

/* Write one RGBA pixel into the screen back-buffer with alpha blending */
static inline void _blitPixel(fbScreen *scr, int x, int y, const uint8_t *rgba)
{
    if (x < 0 || y < 0 || x >= scr->pixelW || y >= scr->pixelH) return;

    uint8_t a = rgba[3];
    if (a == 0) return;

    if (a == 255) {
        /* Fully opaque — direct write */
        _fbPutPixelBack(scr, x, y, FB_RGB(rgba[0], rgba[1], rgba[2]));
    } else {
        /* Alpha blend over existing back-buffer pixel */
        fbColor bg = _fbGetPixelBack(scr, x, y);
        uint8_t br = FB_COLOR_R(bg);
        uint8_t bg_ = FB_COLOR_G(bg);
        uint8_t bb = FB_COLOR_B(bg);
        uint8_t inv = (uint8_t)(255 - a);
        uint8_t r = (uint8_t)((rgba[0] * a + br  * inv) >> 8);
        uint8_t g = (uint8_t)((rgba[1] * a + bg_ * inv) >> 8);
        uint8_t b = (uint8_t)((rgba[2] * a + bb  * inv) >> 8);
        _fbPutPixelBack(scr, x, y, FB_RGB(r, g, b));
    }
}

void fbImageDraw(fbScreen *scr, const fbImage *img, int x, int y)
{
    if (!scr || !img) return;
    for (int row = 0; row < img->height; row++) {
        const uint8_t *src = img->pixels + row * img->width * 4;
        for (int col = 0; col < img->width; col++, src += 4)
            _blitPixel(scr, x + col, y + row, src);
    }
}

void fbImageDrawRegion(fbScreen *scr, const fbImage *img,
                       int srcX, int srcY, int srcW, int srcH,
                       int dstX, int dstY, int dstW, int dstH)
{
    if (!scr || !img) return;
    if (srcW <= 0) srcW = img->width  - srcX;
    if (srcH <= 0) srcH = img->height - srcY;
    if (dstW <= 0) dstW = srcW;
    if (dstH <= 0) dstH = srcH;

    float sx = (float)srcW / (float)dstW;
    float sy = (float)srcH / (float)dstH;
    uint8_t px[4];

    for (int dy = 0; dy < dstH; dy++) {
        float fy = srcY + dy * sy;
        for (int dx = 0; dx < dstW; dx++) {
            float fx = srcX + dx * sx;
            /* Clamp to source bounds */
            if (fx < 0) fx = 0;
            if (fy < 0) fy = 0;
            if (fx >= img->width  - 1) fx = (float)(img->width  - 1);
            if (fy >= img->height - 1) fy = (float)(img->height - 1);
            _bilinear(img, fx, fy, px);
            _blitPixel(scr, dstX + dx, dstY + dy, px);
        }
    }
}

void fbImageDrawScaled(fbScreen *scr, const fbImage *img,
                       int x, int y, int dstW, int dstH)
{
    if (!scr || !img) return;

    /* Compute missing dimension preserving aspect ratio */
    if (dstW <= 0 && dstH <= 0) {
        dstW = img->width;
        dstH = img->height;
    } else if (dstW <= 0) {
        dstW = img->width * dstH / img->height;
        if (dstW < 1) dstW = 1;
    } else if (dstH <= 0) {
        dstH = img->height * dstW / img->width;
        if (dstH < 1) dstH = 1;
    }

    fbImageDrawRegion(scr, img, 0, 0, img->width, img->height,
                      x, y, dstW, dstH);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Scaling to a new fbImage
 * ═══════════════════════════════════════════════════════════════════ */

fbImage *fbImageScale(const fbImage *src, int newW, int newH)
{
    if (!src || newW <= 0 || newH <= 0) return NULL;
    fbImage *dst = _imgAlloc(newW, newH);
    if (!dst) return NULL;

    float sx = (float)src->width  / (float)newW;
    float sy = (float)src->height / (float)newH;

    for (int y = 0; y < newH; y++) {
        float fy = y * sy;
        if (fy > src->height - 1) fy = (float)(src->height - 1);
        uint8_t *row = dst->pixels + y * newW * 4;
        for (int x = 0; x < newW; x++) {
            float fx = x * sx;
            if (fx > src->width - 1) fx = (float)(src->width - 1);
            _bilinear(src, fx, fy, row + x * 4);
        }
    }
    return dst;
}

fbImage *fbImageScaleFit(const fbImage *src, int maxW, int maxH)
{
    if (!src || maxW <= 0 || maxH <= 0) return NULL;

    float scaleW = (float)maxW / (float)src->width;
    float scaleH = (float)maxH / (float)src->height;
    float scale  = (scaleW < scaleH) ? scaleW : scaleH;

    int newW = (int)(src->width  * scale);
    int newH = (int)(src->height * scale);
    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;

    return fbImageScale(src, newW, newH);
}
