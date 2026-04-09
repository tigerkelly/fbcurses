#ifndef FBCURSES_INTERNAL_H
#define FBCURSES_INTERNAL_H

#include "fbcurses.h"
#include "fonts.h"

#include <linux/fb.h>
#include <termios.h>
#include <wchar.h>

/* ─── Error codes ────────────────────────────────────────────────── */
#define FB_ERR_NONE   0
#define FB_ERR_OPEN   1
#define FB_ERR_IOCTL  2
#define FB_ERR_MMAP   3
#define FB_ERR_ALLOC  4
#define FB_ERR_PARAM  5
#define FB_ERR_IO     6

void _fbSetError(int code, const char *msg);

/* ─── Legacy VGA 8×16 raw bitmap table (font8x16.c) ─────────────── */
#define FB_FONT_W  8
#define FB_FONT_H 16
extern const uint8_t _fbFont[256][16];

/* ─── Cell (one character position in a window) ──────────────────── */
typedef struct {
    wchar_t  ch;
    fbColor  fg;
    fbColor  bg;
    uint8_t  attr;
    bool     dirty;
} fbCell;

/* ─── Screen context ─────────────────────────────────────────────── */
struct fbScreen {
    int      fd;            /* framebuffer device fd                  */
    int      ttyFd;         /* controlling console tty fd             */
    uint8_t *fbMem;
    uint8_t *savedFb;       /* snapshot of framebuffer taken at fbInit */
    uint32_t *backBuf;
    size_t   memLen;

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;

    int  pixelW, pixelH;
    int  bpp, lineLen;
    int  cols, rows;        /* character grid at the default VGA font */

    struct termios origTermios;
    bool rawMode;
    bool cursorWasVisible;  /* cursor state before fbInit              */
    int  kdMode;            /* original KD_TEXT/KD_GRAPHICS mode       */

    int  vtNum;             /* VT number this process is running on   */
};

/* ─── Window context ─────────────────────────────────────────────── */
struct fbWindow {
    fbScreen     *scr;
    int           col, row;    /* position in character cells          */
    int           cols, rows;  /* size in character cells              */
    fbCell       *cells;

    int           cursorCol, cursorRow;
    fbColor       fg, bg;
    uint8_t       attr;
    const fbFont *font;        /* active font; never NULL              */
};

/* ─── Inline pixel helpers ───────────────────────────────────────── */
static inline void
_fbPutPixelBack(fbScreen *scr, int x, int y, fbColor color)
{
    if (x < 0 || y < 0 || x >= scr->pixelW || y >= scr->pixelH) return;
    scr->backBuf[y * scr->pixelW + x] = color;
}

static inline fbColor
_fbGetPixelBack(const fbScreen *scr, int x, int y)
{
    if (x < 0 || y < 0 || x >= scr->pixelW || y >= scr->pixelH)
        return FB_BLACK;
    return scr->backBuf[y * scr->pixelW + x];
}

static inline fbCell *
_fbGetCell(fbWindow *win, int col, int row)
{
    return &win->cells[row * win->cols + col];
}

/* ─── Renderers ──────────────────────────────────────────────────── */
/* Main glyph dispatcher: tries box-draw first, falls back to bitmap. */
void _fbRenderGlyph(fbScreen *scr, int pixX, int pixY,
                    wchar_t ch, fbColor fg, fbColor bg, uint8_t attr,
                    const fbFont *font);

/* Vector renderer for box/block/braille codepoints. */
bool _fbDrawBoxChar(fbScreen *scr, int px0, int py0,
                    wchar_t ch, fbColor fg, fbColor bg,
                    int fw, int fh);

/* ─── Border character tables ────────────────────────────────────── */
typedef struct {
    wchar_t tl, tr, bl, br, h, v;
} _fbBorderChars;

extern const _fbBorderChars _fbBorderTable[];

#endif /* FBCURSES_INTERNAL_H */
