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

#include "fbvideo.h"
#include "fbimage.h"
#include "fbcurses_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>

/* ═══════════════════════════════════════════════════════════════════
 *  fbVideo struct
 * ═══════════════════════════════════════════════════════════════════ */

struct fbVideo {
    AVFormatContext  *fmtCtx;
    AVCodecContext   *codecCtx;
    struct SwsContext *swsCtx;

    AVFrame  *frame;        /* decoded YUV frame                       */
    AVFrame  *rgbFrame;     /* scaled RGB frame                        */
    AVPacket *pkt;

    int       streamIdx;    /* index of the video stream               */
    double    fps;
    double    duration;     /* seconds                                 */
    int       nativeW;
    int       nativeH;

    /* Current swscale output dimensions */
    int       swsW, swsH;
};

/* ═══════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════ */

static long long _nowUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void _sleepUs(long long us)
{
    if (us <= 0) return;
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

/* (Re-)create the swscale context for a given output size */
static bool _initSws(fbVideo *vid, int dstW, int dstH)
{
    if (vid->swsCtx && vid->swsW == dstW && vid->swsH == dstH)
        return true;   /* already correct */

    sws_freeContext(vid->swsCtx);
    vid->swsCtx = sws_getContext(
        vid->nativeW, vid->nativeH, vid->codecCtx->pix_fmt,
        dstW, dstH, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!vid->swsCtx) return false;

    /* Allocate / reallocate the RGB frame buffer */
    av_frame_unref(vid->rgbFrame);
    vid->rgbFrame->format = AV_PIX_FMT_RGBA;
    vid->rgbFrame->width  = dstW;
    vid->rgbFrame->height = dstH;
    if (av_frame_get_buffer(vid->rgbFrame, 32) < 0) return false;

    vid->swsW = dstW;
    vid->swsH = dstH;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Open / close
 * ═══════════════════════════════════════════════════════════════════ */

fbVideo *fbVideoOpen(const char *path)
{
    if (!path) return NULL;

    fbVideo *vid = calloc(1, sizeof(fbVideo));
    if (!vid) return NULL;

    /* Open the file */
    if (avformat_open_input(&vid->fmtCtx, path, NULL, NULL) < 0)
        goto fail;

    if (avformat_find_stream_info(vid->fmtCtx, NULL) < 0)
        goto fail;

    /* Find the best video stream */
    vid->streamIdx = av_find_best_stream(vid->fmtCtx, AVMEDIA_TYPE_VIDEO,
                                          -1, -1, NULL, 0);
    if (vid->streamIdx < 0) goto fail;

    AVStream *stream = vid->fmtCtx->streams[vid->streamIdx];

    /* Find and open the codec */
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) goto fail;

    vid->codecCtx = avcodec_alloc_context3(codec);
    if (!vid->codecCtx) goto fail;

    if (avcodec_parameters_to_context(vid->codecCtx, stream->codecpar) < 0)
        goto fail;

    if (avcodec_open2(vid->codecCtx, codec, NULL) < 0) goto fail;

    /* Video properties */
    vid->nativeW = vid->codecCtx->width;
    vid->nativeH = vid->codecCtx->height;

    if (stream->avg_frame_rate.den > 0)
        vid->fps = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        vid->fps = av_q2d(stream->r_frame_rate);
    else
        vid->fps = 25.0;

    if (vid->fmtCtx->duration != AV_NOPTS_VALUE)
        vid->duration = (double)vid->fmtCtx->duration / AV_TIME_BASE;

    /* Allocate frames and packet */
    vid->frame    = av_frame_alloc();
    vid->rgbFrame = av_frame_alloc();
    vid->pkt      = av_packet_alloc();
    if (!vid->frame || !vid->rgbFrame || !vid->pkt) goto fail;

    return vid;

fail:
    fbVideoClose(vid);
    return NULL;
}

void fbVideoClose(fbVideo *vid)
{
    if (!vid) return;
    sws_freeContext(vid->swsCtx);
    av_frame_free(&vid->frame);
    av_frame_free(&vid->rgbFrame);
    av_packet_free(&vid->pkt);
    avcodec_free_context(&vid->codecCtx);
    avformat_close_input(&vid->fmtCtx);
    free(vid);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Accessors
 * ═══════════════════════════════════════════════════════════════════ */

int    fbVideoWidth   (const fbVideo *v) { return v ? v->nativeW  : 0;   }
int    fbVideoHeight  (const fbVideo *v) { return v ? v->nativeH  : 0;   }
double fbVideoFPS     (const fbVideo *v) { return v ? v->fps       : 0.0; }
double fbVideoDuration(const fbVideo *v) { return v ? v->duration  : 0.0; }

/* ═══════════════════════════════════════════════════════════════════
 *  Frame decode
 * ═══════════════════════════════════════════════════════════════════ */

bool fbVideoNextFrame(fbVideo *vid, fbImage *img, int dstW, int dstH)
{
    if (!vid || !img) return false;

    if (dstW <= 0) dstW = vid->nativeW;
    if (dstH <= 0) dstH = vid->nativeH;

    if (!_initSws(vid, dstW, dstH)) return false;

    /* Read packets until we get a decoded video frame */
    for (;;) {
        int ret = av_read_frame(vid->fmtCtx, vid->pkt);
        if (ret < 0) return false;   /* end of stream or error */

        if (vid->pkt->stream_index != vid->streamIdx) {
            av_packet_unref(vid->pkt);
            continue;
        }

        ret = avcodec_send_packet(vid->codecCtx, vid->pkt);
        av_packet_unref(vid->pkt);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(vid->codecCtx, vid->frame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret < 0) return false;

        /* Got a frame — scale it to RGBA */
        sws_scale(vid->swsCtx,
                  (const uint8_t * const *)vid->frame->data,
                  vid->frame->linesize,
                  0, vid->nativeH,
                  vid->rgbFrame->data,
                  vid->rgbFrame->linesize);

        /* Copy into the fbImage (reallocate if size changed) */
        if (img->width != dstW || img->height != dstH ||
            !img->pixels) {
            free(img->pixels);
            img->width  = dstW;
            img->height = dstH;
            img->pixels = malloc((size_t)(dstW * dstH * 4));
            if (!img->pixels) return false;
        }

        /* swscale output may have padding; copy line by line */
        int lineBytes = dstW * 4;
        for (int y = 0; y < dstH; y++) {
            memcpy(img->pixels + y * lineBytes,
                   vid->rgbFrame->data[0] + y * vid->rgbFrame->linesize[0],
                   (size_t)lineBytes);
        }

        av_frame_unref(vid->frame);
        return true;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Seek
 * ═══════════════════════════════════════════════════════════════════ */

bool fbVideoSeek(fbVideo *vid, double seconds)
{
    if (!vid) return false;
    int64_t ts = (int64_t)(seconds * AV_TIME_BASE);
    if (av_seek_frame(vid->fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(vid->codecCtx);
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  High-level playback
 * ═══════════════════════════════════════════════════════════════════ */

/* Compute display rectangle respecting keepAspect */
static void _fitRect(int scW, int scH, int vidW, int vidH,
                     bool keepAspect,
                     int *outX, int *outY, int *outW, int *outH)
{
    if (!keepAspect) {
        *outX = 0; *outY = 0; *outW = scW; *outH = scH;
        return;
    }
    float scaleW = (float)scW / (float)vidW;
    float scaleH = (float)scH / (float)vidH;
    float scale  = (scaleW < scaleH) ? scaleW : scaleH;
    *outW = (int)(vidW * scale);
    *outH = (int)(vidH * scale);
    *outX = (scW - *outW) / 2;
    *outY = (scH - *outH) / 2;
}

fbVideoResult fbVideoPlayOpts(fbScreen *scr, const char *path,
                               const fbVideoOpts *opts)
{
    if (!scr || !path) return FB_VIDEO_ERR_OPEN;

    fbVideoOpts def = { 0, 0, 0, 0, false, true, false, 1.0 };
    if (!opts) opts = &def;

    fbVideo *vid = fbVideoOpen(path);
    if (!vid) return FB_VIDEO_ERR_OPEN;

    /* Determine display rectangle */
    int dstX, dstY, dstW, dstH;
    int targW = (opts->width  > 0) ? opts->width  : scr->pixelW;
    int targH = (opts->height > 0) ? opts->height : scr->pixelH;

    _fitRect(targW, targH, vid->nativeW, vid->nativeH,
             opts->keepAspect, &dstX, &dstY, &dstW, &dstH);

    dstX += opts->x;
    dstY += opts->y;

    /* Allocate a reusable frame buffer */
    fbImage frame = { 0, 0, NULL };

    double frameInterval = (vid->fps > 0)
                           ? 1.0e6 / (vid->fps * opts->speedFactor)
                           : 40000.0;   /* default 25 fps */

    fbVideoResult result = FB_VIDEO_OK;

    do {
        long long frameStart = _nowUs();

        if (!fbVideoNextFrame(vid, &frame, dstW, dstH)) {
            if (opts->loop) {
                fbVideoSeek(vid, 0.0);
                continue;
            }
            break;
        }

        /* Blit frame and flush */
        fbImageDraw(scr, &frame, dstX, dstY);
        fbFlush(scr);

        /* Check for q/Esc */
        int key = fbGetKeyTimeout(scr, 0);
        if (key == FB_KEY_ESC || key == 'q' || key == 'Q') {
            result = FB_VIDEO_STOPPED;
            break;
        }

        /* Frame-rate pacing */
        long long elapsed = _nowUs() - frameStart;
        long long delay   = (long long)frameInterval - elapsed;
        if (delay > 500) _sleepUs(delay);

    } while (1);

    free(frame.pixels);
    fbVideoClose(vid);
    return result;
}

fbVideoResult fbVideoPlay(fbScreen *scr, const char *path,
                           int x, int y, int width, int height)
{
    fbVideoOpts opts = {
        .x           = x,
        .y           = y,
        .width       = width,
        .height      = height,
        .loop        = false,
        .keepAspect  = true,
        .showProgress= false,
        .speedFactor = 1.0,
    };
    return fbVideoPlayOpts(scr, path, &opts);
}
