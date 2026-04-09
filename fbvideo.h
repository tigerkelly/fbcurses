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

#ifndef FBVIDEO_H
#define FBVIDEO_H

#include "fbcurses.h"
#include "fbimage.h"
#include <stdbool.h>

/*
 * fbvideo — play video files directly on the Linux framebuffer.
 *
 * Uses FFmpeg (libavcodec / libavformat / libswscale) to decode any
 * format FFmpeg supports: MP4, AVI, MKV, WebM, MOV, FLV, …
 *
 * Build with:
 *   gcc … fbvideo.c $(pkg-config --cflags --libs libavcodec libavformat \
 *                                libswscale libavutil) …
 *
 * Basic usage:
 *   fbVideoPlay(scr, "movie.mp4", 0, 0, 0, 0);   // full-screen, native fps
 *
 * Press q or Esc to stop playback early.
 */

/* ── Playback options ─────────────────────────────────────────────── */
typedef struct {
    int  x, y;              /* top-left corner on screen (pixels)       */
    int  width, height;     /* display size  (0 = fill screen)          */
    bool loop;              /* restart when the video ends              */
    bool keepAspect;        /* letterbox/pillarbox to preserve AR       */
    bool showProgress;      /* print frame/time info to stderr          */
    double speedFactor;     /* playback speed multiplier (1.0 = normal) */
} fbVideoOpts;

/* ── Return codes ─────────────────────────────────────────────────── */
typedef enum {
    FB_VIDEO_OK       = 0,   /* played to completion                    */
    FB_VIDEO_STOPPED  = 1,   /* user pressed q / Esc                    */
    FB_VIDEO_ERR_OPEN = -1,  /* could not open file or find video stream */
    FB_VIDEO_ERR_CODEC= -2,  /* codec not found or could not be opened  */
    FB_VIDEO_ERR_MEM  = -3,  /* memory allocation failure               */
} fbVideoResult;

/* ── Simple one-call interface ────────────────────────────────────── */

/**
 * fbVideoPlay — decode and display a video file on the framebuffer.
 *
 * @scr            fbcurses screen handle
 * @path           path to the video file
 * @x, @y         top-left display position in pixels  (0,0 = top-left)
 * @width,@height  display size in pixels  (0 = fill screen, keep aspect)
 *
 * Timing: honours the video's native frame rate.
 * Input:  checks for q/Esc between frames; returns FB_VIDEO_STOPPED if pressed.
 * Audio:  not implemented (video only).
 *
 * Returns a fbVideoResult code.
 */
fbVideoResult fbVideoPlay(fbScreen *scr, const char *path,
                           int x, int y, int width, int height);

/**
 * fbVideoPlayOpts — like fbVideoPlay but with extended options.
 */
fbVideoResult fbVideoPlayOpts(fbScreen *scr, const char *path,
                               const fbVideoOpts *opts);

/* ── Frame-by-frame interface ─────────────────────────────────────── */

/* Opaque handle for an open video stream */
typedef struct fbVideo fbVideo;

/**
 * fbVideoOpen — open a video file for frame-by-frame decoding.
 * Returns NULL on failure.  Call fbVideoClose() when done.
 */
fbVideo *fbVideoOpen(const char *path);

/**
 * fbVideoWidth / fbVideoHeight — native video dimensions in pixels.
 */
int fbVideoWidth(const fbVideo *vid);
int fbVideoHeight(const fbVideo *vid);

/**
 * fbVideoFPS — native frame rate (frames per second).
 * Returns 0.0 if unknown.
 */
double fbVideoFPS(const fbVideo *vid);

/**
 * fbVideoDuration — total duration in seconds.
 * Returns 0.0 if unknown.
 */
double fbVideoDuration(const fbVideo *vid);

/**
 * fbVideoNextFrame — decode the next video frame into @img.
 *
 * @img must point to an fbImage previously allocated with fbImageFromRGBA()
 * or fbImageLoad(), or to a zero-initialised fbImage — the function will
 * (re)allocate the pixel buffer as needed.
 *
 * Returns true if a frame was decoded, false at end-of-stream or error.
 * Call fbVideoClose() after the last frame.
 *
 * The frame is scaled to (@dstW x @dstH) if those are non-zero,
 * otherwise the native video dimensions are used.
 */
bool fbVideoNextFrame(fbVideo *vid, fbImage *img, int dstW, int dstH);

/**
 * fbVideoSeek — seek to @seconds from the start of the video.
 * Returns true on success.
 */
bool fbVideoSeek(fbVideo *vid, double seconds);

/**
 * fbVideoClose — release all resources for an open video.
 */
void fbVideoClose(fbVideo *vid);

#endif /* FBVIDEO_H */
