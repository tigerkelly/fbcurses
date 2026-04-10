/*
 * fbcurses.c — Screen, window, drawing, text, input implementation.
 */


#include "fbcurses_internal.h"
#include "font8x16.h"
#include "fonts.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <wchar.h>
#include <errno.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════
 *  Error state
 * ═══════════════════════════════════════════════════════════════════ */

static int  _errCode;
static char _errMsg[256];

void _fbSetError(int code, const char *msg) {
    _errCode = code;
    snprintf(_errMsg, sizeof(_errMsg), "%s", msg ? msg : "");
}

const char *fbGetError(void)  { return _errMsg; }
int         fbErrorCode(void) { return _errCode; }

/* ═══════════════════════════════════════════════════════════════════
 *  Border character tables
 * ═══════════════════════════════════════════════════════════════════ */

const _fbBorderChars _fbBorderTable[] = {
    [FB_BORDER_NONE]    = { L' ', L' ', L' ', L' ', L' ', L' ' },
    [FB_BORDER_SINGLE]  = { L'┌', L'┐', L'└', L'┘', L'─', L'│' },
    [FB_BORDER_DOUBLE]  = { L'╔', L'╗', L'╚', L'╝', L'═', L'║' },
    [FB_BORDER_ROUNDED] = { L'╭', L'╮', L'╰', L'╯', L'─', L'│' },
    [FB_BORDER_THICK]   = { L'┏', L'┓', L'┗', L'┛', L'━', L'┃' },
    [FB_BORDER_DASHED]  = { L'┌', L'┐', L'└', L'┘', L'╌', L'╎' },
};

/* ═══════════════════════════════════════════════════════════════════
 *  Glyph renderer — 8×16 bitmap font → back-buffer pixels
 * ═══════════════════════════════════════════════════════════════════ */

void _fbRenderGlyph(fbScreen *scr, int px, int py,
                    wchar_t ch, fbColor fg, fbColor bg, uint8_t attr,
                    const fbFont *font)
{
    /* Colour transforms */
    fbColor drawFg = fg, drawBg = bg;
    if (attr & FB_ATTR_DIM)     drawFg = fbLerp(drawFg, drawBg, 0.5f);
    if (attr & FB_ATTR_REVERSE) { fbColor t = drawFg; drawFg = drawBg; drawBg = t; }

    /* Vector renderer: box-drawing / block elements / braille.
       Pass pixel dimensions; pxH = font->h / ceil(w/8).          */
    int _bpr0 = (font->w + 7) / 8;
    int _pxH0 = font->h / _bpr0;
    if (_fbDrawBoxChar(scr, px, py, ch, drawFg, drawBg, font->w, _pxH0)) {
        if (attr & FB_ATTR_UNDERLINE)
            for (int c = 0; c < font->w; c++)
                _fbPutPixelBack(scr, px + c, py + _pxH0 - 1, drawFg);
        return;
    }

    /* Bitmap font renderer.
       font->w  = pixel width  (may be > 8 for large fonts)
       font->h  = bytes per glyph = pixel_height * ceil(w/8)
       bpr      = bytes per row   = ceil(w/8)
       pxH      = pixel height    = font->h / bpr               */
    uint8_t idx = (ch >= 0x20 && ch < 0x100) ? (uint8_t)ch : (uint8_t)'?';
    int passes = (attr & FB_ATTR_BOLD) ? 2 : 1;
    int bpr = (font->w + 7) / 8;         /* bytes per pixel row   */
    int pxH = font->h / bpr;             /* actual pixel height   */
    const uint8_t *glyph = (const uint8_t *)font->data
                           + (size_t)idx * (size_t)font->h;

    for (int row = 0; row < pxH; row++) {
        const uint8_t *rowBytes = glyph + row * bpr;
        for (int col = 0; col < font->w; col++) {
            /* Extract bit: byte index and bit position within that byte */
            int byteIdx = col / 8;
            int bitPos  = 7 - (col % 8);   /* MSB of each byte = leftmost */
            bool set = (rowBytes[byteIdx] >> bitPos) & 1;
            fbColor pixel = set ? drawFg : drawBg;
            for (int p = 0; p < passes; p++)
                _fbPutPixelBack(scr, px + col + p, py + row, pixel);
        }
    }
    if (attr & FB_ATTR_UNDERLINE)
        for (int col = 0; col < font->w; col++)
            _fbPutPixelBack(scr, px + col, py + pxH - 1, drawFg);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Font selection
 * ═══════════════════════════════════════════════════════════════════ */

void fbSetFont(fbWindow *win, const fbFont *font)
{
    if (!win) return;
    win->font = font ? font : &fbVga;
    /* Mark all cells dirty so fbRefresh() redraws with the new glyph size */
    for (int i = 0; i < win->cols * win->rows; i++)
        win->cells[i].dirty = true;
}

const fbFont *fbGetFont(const fbWindow *win)
{
    return win ? win->font : &fbVga;
}


/* ═══════════════════════════════════════════════════════════════════
 *  Signal / atexit cleanup
 *
 *  fbInit registers an atexit handler and installs SIGINT/SIGTERM
 *  handlers so that Ctrl-C or kill always restores the terminal and
 *  framebuffer state, even if the application forgets to call
 *  fbShutdown.
 * ═══════════════════════════════════════════════════════════════════ */

#include <signal.h>

/* Pointer to the most-recently initialised screen.  Only one screen
   is supported per process; if the application calls fbShutdown the
   pointer is cleared so the atexit handler does nothing.            */
static fbScreen * volatile _g_scr = NULL;

static void _fbEmergencyShutdown(void)
{
    fbScreen *scr = _g_scr;
    if (!scr) return;
    _g_scr = NULL;          /* prevent double-free */
    fbShutdown(scr);
}

static void _fbSignalHandler(int sig)
{
    _fbEmergencyShutdown();
    /* Re-raise with default handler so the process exits with the
       correct status and core dumps work normally.                  */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ═══════════════════════════════════════════════════════════════════
 *  fbInit / fbShutdown
 * ═══════════════════════════════════════════════════════════════════ */

fbScreen *fbInit(const char *device)
{
    _fbSetError(FB_ERR_NONE, "");

    const char *dev = device ? device : "/dev/fb0";
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        _fbSetError(FB_ERR_OPEN, strerror(errno));
        return NULL;
    }

    fbScreen *scr = calloc(1, sizeof(fbScreen));
    if (!scr) {
        close(fd);
        _fbSetError(FB_ERR_ALLOC, "out of memory");
        return NULL;
    }
    scr->fd = fd;

    if (ioctl(fd, FBIOGET_VSCREENINFO, &scr->var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &scr->fix) < 0) {
        _fbSetError(FB_ERR_IOCTL, strerror(errno));
        goto fail;
    }

    scr->pixelW  = (int)scr->var.xres;
    scr->pixelH  = (int)scr->var.yres;
    scr->bpp     = (int)scr->var.bits_per_pixel;
    scr->lineLen = (int)scr->fix.line_length;
    scr->cols    = scr->pixelW / FB_FONT_W;
    scr->rows    = scr->pixelH / FB_FONT_H;
    scr->memLen  = (size_t)scr->lineLen * scr->pixelH;

    scr->fbMem = mmap(NULL, scr->memLen, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (scr->fbMem == MAP_FAILED) {
        _fbSetError(FB_ERR_MMAP, strerror(errno));
        goto fail;
    }

    scr->backBuf = calloc((size_t)scr->pixelW * scr->pixelH, sizeof(uint32_t));
    if (!scr->backBuf) {
        _fbSetError(FB_ERR_ALLOC, "out of memory");
        goto fail;
    }

    /* Save the current framebuffer contents so fbShutdown can restore
       the screen to exactly the state it was in before we started.   */
    scr->savedFb = malloc(scr->memLen);
    if (scr->savedFb)
        memcpy(scr->savedFb, scr->fbMem, scr->memLen);
    /* Non-fatal if malloc fails — we just won't restore on exit.    */


    /* Put terminal into raw / no-echo mode */
    tcgetattr(STDIN_FILENO, &scr->origTermios);
    struct termios raw = scr->origTermios;
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON);  /* keep ISIG: Ctrl-C must still work */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    scr->rawMode = true;

    /* Open the controlling console for VT ioctls.
       /dev/tty always refers to the process's controlling terminal — the
       active VT we're running on — without needing to know its number.  */
    scr->ttyFd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (scr->ttyFd < 0) {
        /* Fall back to STDIN if /dev/tty is unavailable (rare) */
        scr->ttyFd = STDIN_FILENO;
    }

    /* Discover which VT number we are on */
    {
        struct vt_stat vtst;
        if (ioctl(scr->ttyFd, VT_GETSTATE, &vtst) == 0)
            scr->vtNum = vtst.v_active;
    }

    /* Switch to KD_GRAPHICS mode.
       This tells the kernel's fbcon driver to stop drawing on the
       framebuffer entirely — no text, no cursor, no cursor blink.
       Without this, fbcon periodically redraws its blinking cursor
       on top of our rendering.  Every serious framebuffer app (X11,
       Wayland, SDL) does this.  We restore KD_TEXT in fbShutdown.   */
    scr->kdMode = KD_TEXT;   /* safe default if ioctl fails */
    ioctl(scr->ttyFd, KDGETMODE, &scr->kdMode);
    ioctl(scr->ttyFd, KDSETMODE, KD_GRAPHICS);

    /* Query and save the current cursor visibility so fbShutdown can
       restore exactly the state that was in effect before fbInit.
       We probe by checking /sys/class/graphics/fbcon/cursor_blink;
       fall back to assuming it was visible (the safe default).     */
    scr->cursorWasVisible = true;   /* assume visible unless we know otherwise */
    {
        /* Try reading /sys to detect if cursor blink was disabled */
        int cfd = open("/sys/class/graphics/fbcon/cursor_blink", O_RDONLY);
        if (cfd >= 0) {
            char ch = '1';
            if (read(cfd, &ch, 1) == 1)
                scr->cursorWasVisible = (ch != '0');
            close(cfd);
        }
    }

    /* Hide the text cursor */
    fbSetCursor(scr, false);

    fbClear(scr, FB_BLACK);
    fbFlush(scr);

    /* Register cleanup handlers so Ctrl-C / kill always restores terminal */
    _g_scr = scr;
    atexit(_fbEmergencyShutdown);
    signal(SIGINT,  _fbSignalHandler);
    signal(SIGTERM, _fbSignalHandler);
    signal(SIGHUP,  _fbSignalHandler);

    return scr;

fail:
    if (scr->fbMem && scr->fbMem != MAP_FAILED)
        munmap(scr->fbMem, scr->memLen);
    free(scr->backBuf);
    free(scr->savedFb);
    close(fd);
    free(scr);
    return NULL;
}

void fbShutdown(fbScreen *scr)
{
    if (!scr) return;
    /* Prevent the atexit handler from running fbShutdown a second time */
    if (_g_scr == scr) _g_scr = NULL;

    /* Restore the saved screen contents, or clear to black if no
       save was made (malloc failed at init time).                  */
    if (scr->savedFb) {
        memcpy(scr->fbMem, scr->savedFb, scr->memLen);
        free(scr->savedFb);
        scr->savedFb = NULL;
    } else {
        fbClear(scr, FB_BLACK);
        fbFlush(scr);
    }

    if (scr->rawMode) {
        /* Restore KD_TEXT so fbcon takes back the framebuffer and
           the terminal is usable again after we exit.              */
        ioctl(scr->ttyFd, KDSETMODE, scr->kdMode);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &scr->origTermios);
        fbSetCursor(scr, scr->cursorWasVisible);
    }

    if (scr->fbMem && scr->fbMem != MAP_FAILED)
        munmap(scr->fbMem, scr->memLen);
    free(scr->backBuf);
    free(scr->savedFb);   /* NULL-safe: free(NULL) is a no-op */
    close(scr->fd);
    if (scr->ttyFd > STDERR_FILENO)   /* don't close STDIN fallback */
        close(scr->ttyFd);
    free(scr);
}

/* ─── Screen accessors ───────────────────────────────────────────── */
int fbWidth (const fbScreen *scr) { return scr ? scr->pixelW : 0; }
int fbHeight(const fbScreen *scr) { return scr ? scr->pixelH : 0; }
int fbCols  (const fbScreen *scr) { return scr ? scr->cols   : 0; }
int fbRows  (const fbScreen *scr) { return scr ? scr->rows   : 0; }

/* ─── fbSetCursor ────────────────────────────────────────────────── */
void fbSetCursor(fbScreen *scr, bool visible)
{
    /* Send the DECTCEM escape to the tty as a belt-and-suspenders measure.
       This hides the software cursor within fbcon's text renderer.       */
    int fd = (scr && scr->ttyFd >= 0) ? scr->ttyFd : STDOUT_FILENO;
    const char *seq = visible ? "\033[?25h" : "\033[?25l";
    { ssize_t _r = write(fd, seq, strlen(seq)); (void)_r; }
    /* The hardware cursor blink is suppressed by KD_GRAPHICS mode,
       which is set in fbInit and cleared in fbShutdown.               */
}

/* ─── fbClear ────────────────────────────────────────────────────── */
void fbClear(fbScreen *scr, fbColor color)
{
    if (!scr) return;
    int total = scr->pixelW * scr->pixelH;
    for (int i = 0; i < total; i++)
        scr->backBuf[i] = color;
}

/* ─── fbFlush ────────────────────────────────────────────────────── */
void fbFlush(fbScreen *scr)
{
    if (!scr) return;

    int bpp = scr->bpp;
    for (int y = 0; y < scr->pixelH; y++) {
        uint8_t *row = scr->fbMem + y * scr->lineLen;
        if (bpp == 32) {
            memcpy(row, scr->backBuf + y * scr->pixelW,
                   (size_t)scr->pixelW * 4);
        } else if (bpp == 16) {
            uint16_t *dst = (uint16_t *)row;
            const uint32_t *src = scr->backBuf + y * scr->pixelW;
            for (int x = 0; x < scr->pixelW; x++) {
                uint32_t c = src[x];
                uint8_t r = (c >> 16) & 0xFF;
                uint8_t g = (c >>  8) & 0xFF;
                uint8_t b = (c      ) & 0xFF;
                dst[x] = (uint16_t)(((r >> 3) << 11) |
                                    ((g >> 2) <<  5) |
                                    ((b >> 3)      ));
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Window management
 * ═══════════════════════════════════════════════════════════════════ */

fbWindow *fbNewWindow(fbScreen *scr, int col, int row, int cols, int rows)
{
    if (!scr || cols <= 0 || rows <= 0) {
        _fbSetError(FB_ERR_PARAM, "invalid window dimensions");
        return NULL;
    }
    fbWindow *win = calloc(1, sizeof(fbWindow));
    if (!win) { _fbSetError(FB_ERR_ALLOC, "out of memory"); return NULL; }

    win->scr   = scr;
    win->col   = col;
    win->row   = row;
    win->cols  = cols;
    win->rows  = rows;
    win->font  = &fbVga;
    win->fg    = FB_WHITE;
    win->bg    = FB_BLACK;
    win->attr  = FB_ATTR_NONE;

    win->cells = calloc((size_t)cols * rows, sizeof(fbCell));
    if (!win->cells) {
        _fbSetError(FB_ERR_ALLOC, "out of memory");
        free(win);
        return NULL;
    }

    /* Initialise every cell to space */
    for (int i = 0; i < cols * rows; i++) {
        win->cells[i].ch   = L' ';
        win->cells[i].fg   = FB_WHITE;
        win->cells[i].bg   = FB_BLACK;
        win->cells[i].attr = FB_ATTR_NONE;
        win->cells[i].dirty = true;
    }
    return win;
}

void fbDelWindow(fbWindow *win)
{
    if (!win) return;
    free(win->cells);
    free(win);
}

void fbMoveWindow(fbWindow *win, int col, int row)
{
    if (!win) return;
    win->col = col;
    win->row = row;
}

void fbResizeWindow(fbWindow *win, int cols, int rows)
{
    if (!win || cols <= 0 || rows <= 0) return;
    fbCell *nc = realloc(win->cells, (size_t)cols * rows * sizeof(fbCell));
    if (!nc) { _fbSetError(FB_ERR_ALLOC, "out of memory"); return; }
    win->cells = nc;
    win->cols  = cols;
    win->rows  = rows;
    /* Mark all dirty */
    for (int i = 0; i < cols * rows; i++) {
        win->cells[i].ch    = L' ';
        win->cells[i].fg    = win->fg;
        win->cells[i].bg    = win->bg;
        win->cells[i].attr  = FB_ATTR_NONE;
        win->cells[i].dirty = true;
    }
}

fbScreen *fbWindowGetScreen(const fbWindow *win) { return win ? win->scr : NULL; }
int fbWindowCols(const fbWindow *win) { return win ? win->cols : 0; }
int fbWindowRows(const fbWindow *win) { return win ? win->rows : 0; }

int fbWindowPixelX(const fbWindow *win)
{
    if (!win) return 0;
    return win->col * win->font->w;
}

int fbWindowPixelY(const fbWindow *win)
{
    if (!win) return 0;
    int bpr = (win->font->w + 7) / 8;
    int cellH = win->font->h / bpr;
    return win->row * cellH;
}

int fbWindowPixelW(const fbWindow *win)
{
    if (!win) return 0;
    return win->cols * win->font->w;
}

int fbWindowPixelH(const fbWindow *win)
{
    if (!win) return 0;
    int bpr = (win->font->w + 7) / 8;
    int cellH = win->font->h / bpr;
    return win->rows * cellH;
}

void fbClearWindow(fbWindow *win, fbColor bg)
{
    if (!win) return;
    for (int i = 0; i < win->cols * win->rows; i++) {
        win->cells[i].ch    = L' ';
        win->cells[i].fg    = win->fg;
        win->cells[i].bg    = bg;
        win->cells[i].attr  = FB_ATTR_NONE;
        win->cells[i].dirty = true;
    }
    win->bg = bg;
}

/* ─── fbRefresh — flush window cells → back-buffer ──────────────── */
void fbRefresh(fbWindow *win)
{
    if (!win) return;
    fbScreen *scr = win->scr;

    for (int r = 0; r < win->rows; r++) {
        for (int c = 0; c < win->cols; c++) {
            fbCell *cell = _fbGetCell(win, c, r);
            if (!cell->dirty) continue;

            int _bpr = (win->font->w + 7) / 8;
            int px = (win->col + c) * win->font->w;
            int py = (win->row + r) * (win->font->h / _bpr);

            _fbRenderGlyph(scr, px, py,
                           cell->ch, cell->fg, cell->bg, cell->attr,
                           win->font);
            cell->dirty = false;
        }
    }
}


/* ═══════════════════════════════════════════════════════════════════
 *  Pixel-level text blitters
 * ═══════════════════════════════════════════════════════════════════ */

int fbDrawTextPx(fbScreen *scr, int x, int y, const char *str,
                 fbColor fg, fbColor bg, uint8_t attr,
                 const fbFont *font)
{
    if (!scr || !str) return x;
    if (!font) font = &fbVga;
    int cx = x;
    while (*str) {
        wchar_t ch = (wchar_t)(unsigned char)*str++;
        _fbRenderGlyph(scr, cx, y, ch, fg, bg, attr, font);
        cx += font->w;
    }
    return cx;
}

int fbDrawWTextPx(fbScreen *scr, int x, int y, const wchar_t *str,
                  fbColor fg, fbColor bg, uint8_t attr,
                  const fbFont *font)
{
    if (!scr || !str) return x;
    if (!font) font = &fbVga;
    int cx = x;
    while (*str) {
        _fbRenderGlyph(scr, cx, y, *str++, fg, bg, attr, font);
        cx += font->w;
    }
    return cx;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Drawing primitives
 * ═══════════════════════════════════════════════════════════════════ */

void fbDrawPixel(fbScreen *scr, int x, int y, fbColor color)
{
    _fbPutPixelBack(scr, x, y, color);
}

/* Bresenham line */
void fbDrawLine(fbScreen *scr, int x0, int y0, int x1, int y1, fbColor color)
{
    int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        _fbPutPixelBack(scr, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fbDrawRect(fbScreen *scr, int x, int y, int w, int h, fbColor color)
{
    fbDrawLine(scr, x,         y,         x + w - 1, y,         color);
    fbDrawLine(scr, x,         y + h - 1, x + w - 1, y + h - 1, color);
    fbDrawLine(scr, x,         y,         x,         y + h - 1, color);
    fbDrawLine(scr, x + w - 1, y,         x + w - 1, y + h - 1, color);
}

void fbFillRect(fbScreen *scr, int x, int y, int w, int h, fbColor color)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            _fbPutPixelBack(scr, col, row, color);
}

/* Midpoint circle */
void fbDrawCircle(fbScreen *scr, int cx, int cy, int r, fbColor color)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        _fbPutPixelBack(scr, cx+x, cy+y, color);
        _fbPutPixelBack(scr, cx-x, cy+y, color);
        _fbPutPixelBack(scr, cx+x, cy-y, color);
        _fbPutPixelBack(scr, cx-x, cy-y, color);
        _fbPutPixelBack(scr, cx+y, cy+x, color);
        _fbPutPixelBack(scr, cx-y, cy+x, color);
        _fbPutPixelBack(scr, cx+y, cy-x, color);
        _fbPutPixelBack(scr, cx-y, cy-x, color);
        if (d < 0) d += 4*x+6; else { d += 4*(x-y)+10; y--; }
        x++;
    }
}

void fbFillCircle(fbScreen *scr, int cx, int cy, int r, fbColor color)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                _fbPutPixelBack(scr, cx+x, cy+y, color);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Text rendering
 * ═══════════════════════════════════════════════════════════════════ */

void fbMoveCursor(fbWindow *win, int col, int row)
{
    if (!win) return;
    win->cursorCol = col;
    win->cursorRow = row;
}

void fbSetColors(fbWindow *win, fbColor fg, fbColor bg)
{
    if (!win) return;
    win->fg = fg;
    win->bg = bg;
}

void fbSetAttr(fbWindow *win, uint8_t attr)
{
    if (!win) return;
    win->attr = attr;
}

void fbAddChar(fbWindow *win, char ch)
{
    if (!win) return;

    if (ch == '\n') {
        win->cursorCol = 0;
        win->cursorRow++;
        return;
    }
    if (ch == '\r') { win->cursorCol = 0; return; }
    if (ch == '\t') {
        int next = (win->cursorCol + 8) & ~7;
        while (win->cursorCol < next && win->cursorCol < win->cols)
            fbAddChar(win, ' ');
        return;
    }

    if (win->cursorCol < 0 || win->cursorCol >= win->cols ||
        win->cursorRow < 0 || win->cursorRow >= win->rows)
        return;

    fbCell *cell = _fbGetCell(win, win->cursorCol, win->cursorRow);
    cell->ch    = (wchar_t)(unsigned char)ch;
    cell->fg    = win->fg;
    cell->bg    = win->bg;
    cell->attr  = win->attr;
    cell->dirty = true;

    win->cursorCol++;
    if (win->cursorCol >= win->cols) {
        win->cursorCol = 0;
        win->cursorRow++;
    }
}

void fbAddWchar(fbWindow *win, wchar_t ch)
{
    if (!win) return;

    if (ch == L'\n') { win->cursorCol = 0; win->cursorRow++; return; }
    if (ch == L'\r') { win->cursorCol = 0; return; }

    if (win->cursorCol < 0 || win->cursorCol >= win->cols ||
        win->cursorRow < 0 || win->cursorRow >= win->rows) return;

    fbCell *cell = _fbGetCell(win, win->cursorCol, win->cursorRow);
    cell->ch    = ch;
    cell->fg    = win->fg;
    cell->bg    = win->bg;
    cell->attr  = win->attr;
    cell->dirty = true;

    win->cursorCol++;
    if (win->cursorCol >= win->cols) { win->cursorCol = 0; win->cursorRow++; }
}

void fbAddWStr(fbWindow *win, const wchar_t *str)
{
    if (!win || !str) return;
    while (*str) fbAddWchar(win, *str++);
}

/* Decode one UTF-8 codepoint from *p, advance *p, return the codepoint. */
static wchar_t _decodeUtf8(const char **p)
{
    unsigned char c = (unsigned char)**p;
    wchar_t cp;
    int extra;

    if      (c < 0x80) { cp = c; extra = 0; }
    else if (c < 0xC0) { (*p)++; return 0xFFFD; }  /* continuation byte */
    else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
    else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
    else               { cp = c & 0x07; extra = 3; }

    (*p)++;
    for (int i = 0; i < extra; i++) {
        unsigned char b = (unsigned char)**p;
        if ((b & 0xC0) != 0x80) return 0xFFFD;  /* invalid continuation */
        cp = (cp << 6) | (b & 0x3F);
        (*p)++;
    }
    return cp;
}

void fbAddUtf8(fbWindow *win, const char *utf8)
{
    if (!win || !utf8) return;
    const char *p = utf8;
    while (*p) {
        wchar_t cp = _decodeUtf8(&p);
        if (cp != 0xFFFD || *(p-1) != 0)
            fbAddWchar(win, cp);
    }
}

void fbAddStr(fbWindow *win, const char *str)
{
    if (!win || !str) return;
    while (*str) fbAddChar(win, *str++);
}

void fbPrint(fbWindow *win, const char *fmt, ...)
{
    if (!win || !fmt) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fbAddStr(win, buf);
}

void fbPrintAt(fbWindow *win, int col, int row, const char *fmt, ...)
{
    if (!win || !fmt) return;
    fbMoveCursor(win, col, row);
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fbAddStr(win, buf);
}

void fbPrintAligned(fbWindow *win, int row, fbAlign align, const char *str)
{
    if (!win || !str) return;
    int len = (int)strlen(str);
    int col = 0;
    if (align == FB_ALIGN_CENTER) col = (win->cols - len) / 2;
    else if (align == FB_ALIGN_RIGHT)  col = win->cols - len;
    if (col < 0) col = 0;
    fbMoveCursor(win, col, row);
    fbAddStr(win, str);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Borders & boxes
 * ═══════════════════════════════════════════════════════════════════ */

static void _drawBorderChars(fbWindow *win, const _fbBorderChars *bc,
                              fbColor color, int c0, int r0, int cols, int rows)
{
    fbColor savedFg = win->fg;
    fbColor savedBg = win->bg;
    win->fg = color;

    /* Top row */
    fbMoveCursor(win, c0, r0);
    /* Write corners + horizontal bars as wide chars using UTF-8 sequence */
    /* We store them as wchar_t in the cell directly */
    fbCell *cell;

    cell = _fbGetCell(win, c0,           r0);
    cell->ch = bc->tl; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
    cell = _fbGetCell(win, c0+cols-1,    r0);
    cell->ch = bc->tr; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
    cell = _fbGetCell(win, c0,           r0+rows-1);
    cell->ch = bc->bl; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
    cell = _fbGetCell(win, c0+cols-1,    r0+rows-1);
    cell->ch = bc->br; cell->fg = color; cell->bg = savedBg; cell->dirty = true;

    for (int c = c0+1; c < c0+cols-1; c++) {
        cell = _fbGetCell(win, c, r0);
        cell->ch = bc->h; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
        cell = _fbGetCell(win, c, r0+rows-1);
        cell->ch = bc->h; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
    }
    for (int r = r0+1; r < r0+rows-1; r++) {
        cell = _fbGetCell(win, c0, r);
        cell->ch = bc->v; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
        cell = _fbGetCell(win, c0+cols-1, r);
        cell->ch = bc->v; cell->fg = color; cell->bg = savedBg; cell->dirty = true;
    }

    win->fg = savedFg;
    win->bg = savedBg;
}

void fbDrawBorder(fbWindow *win, fbBorderStyle style, fbColor color)
{
    if (!win || style == FB_BORDER_NONE) return;
    const _fbBorderChars *bc = &_fbBorderTable[style];
    _drawBorderChars(win, bc, color, 0, 0, win->cols, win->rows);
}

void fbDrawCustomBorder(fbWindow *win, const fbBorder *border)
{
    if (!win || !border) return;
    _fbBorderChars bc = {
        border->tl, border->tr, border->bl, border->br,
        border->horiz, border->vert
    };
    _drawBorderChars(win, &bc, border->color, 0, 0, win->cols, win->rows);
}

void fbDrawBox(fbWindow *win, int col, int row, int cols, int rows,
               fbBorderStyle style, fbColor color)
{
    if (!win || style == FB_BORDER_NONE) return;
    const _fbBorderChars *bc = &_fbBorderTable[style];
    _drawBorderChars(win, bc, color, col, row, cols, rows);
}

void fbDrawTitleBar(fbWindow *win, const char *title,
                    fbBorderStyle style, fbColor borderColor,
                    fbColor titleFg, fbColor titleBg)
{
    fbDrawBorder(win, style, borderColor);
    if (!title) return;

    int len = (int)strlen(title);
    int avail = win->cols - 4;          /* 2 chars margin each side */
    if (avail <= 0) return;
    if (len > avail) len = avail;

    int startCol = (win->cols - len - 2) / 2;

    fbCell *cell;
    /* Space + title + space, with title colours */
    cell = _fbGetCell(win, startCol, 0);
    cell->ch = L' '; cell->fg = titleFg; cell->bg = titleBg; cell->dirty = true;

    for (int i = 0; i < len; i++) {
        cell = _fbGetCell(win, startCol + 1 + i, 0);
        cell->ch   = (wchar_t)(unsigned char)title[i];
        cell->fg   = titleFg;
        cell->bg   = titleBg;
        cell->attr = FB_ATTR_BOLD;
        cell->dirty = true;
    }
    cell = _fbGetCell(win, startCol + 1 + len, 0);
    cell->ch = L' '; cell->fg = titleFg; cell->bg = titleBg; cell->dirty = true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Progress & spinner widgets
 * ═══════════════════════════════════════════════════════════════════ */

void fbDrawProgressBar(fbWindow *win, int col, int row, int width,
                       int percent, fbColor fg, fbColor bg, bool showPct)
{
    if (!win || width <= 0) return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int filled = (width * percent) / 100;

    for (int i = 0; i < width; i++) {
        fbCell *cell = _fbGetCell(win, col + i, row);
        cell->ch    = L'█';
        cell->fg    = i < filled ? fg : bg;
        cell->bg    = bg;
        cell->attr  = FB_ATTR_NONE;
        cell->dirty = true;
    }

    if (showPct) {
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%3d%%", percent);
        int pLen = (int)strlen(pctBuf);
        int pCol = col + (width - pLen) / 2;
        for (int i = 0; i < pLen && pCol + i < win->cols; i++) {
            fbCell *cell = _fbGetCell(win, pCol + i, row);
            cell->ch    = (wchar_t)(unsigned char)pctBuf[i];
            cell->fg    = FB_WHITE;
            cell->bg    = (pCol + i - col) < filled ? fg : bg;
            cell->dirty = true;
        }
    }
}

static const wchar_t _spinFrames[] = L"⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
#define SPIN_FRAMES 10

void fbDrawSpinner(fbWindow *win, int col, int row, int tick,
                   fbColor fg, fbColor bg)
{
    if (!win) return;
    fbCell *cell = _fbGetCell(win, col, row);
    cell->ch    = _spinFrames[tick % SPIN_FRAMES];
    cell->fg    = fg;
    cell->bg    = bg;
    cell->attr  = FB_ATTR_NONE;
    cell->dirty = true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Scrolling
 * ═══════════════════════════════════════════════════════════════════ */

void fbScrollUp(fbWindow *win, int n, fbColor bg)
{
    if (!win || n <= 0) return;
    if (n >= win->rows) { fbClearWindow(win, bg); return; }

    /* Move rows n..rows-1 up by n */
    memmove(win->cells,
            win->cells + (size_t)n * win->cols,
            (size_t)(win->rows - n) * win->cols * sizeof(fbCell));

    /* Clear the new rows at the bottom */
    for (int r = win->rows - n; r < win->rows; r++)
        for (int c = 0; c < win->cols; c++) {
            fbCell *cell = _fbGetCell(win, c, r);
            cell->ch    = L' ';
            cell->fg    = win->fg;
            cell->bg    = bg;
            cell->dirty = true;
        }

    /* Mark everything dirty */
    for (int i = 0; i < win->cols * win->rows; i++)
        win->cells[i].dirty = true;
}

void fbScrollDown(fbWindow *win, int n, fbColor bg)
{
    if (!win || n <= 0) return;
    if (n >= win->rows) { fbClearWindow(win, bg); return; }

    memmove(win->cells + (size_t)n * win->cols,
            win->cells,
            (size_t)(win->rows - n) * win->cols * sizeof(fbCell));

    for (int r = 0; r < n; r++)
        for (int c = 0; c < win->cols; c++) {
            fbCell *cell = _fbGetCell(win, c, r);
            cell->ch    = L' ';
            cell->fg    = win->fg;
            cell->bg    = bg;
            cell->dirty = true;
        }

    for (int i = 0; i < win->cols * win->rows; i++)
        win->cells[i].dirty = true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Input
 * ═══════════════════════════════════════════════════════════════════ */

/* Read a raw byte with optional timeout (-1 = block).
   Returns -1 on timeout, error, or signal (EINTR) — all treated the
   same so that while(_running) loops re-check their exit condition. */
static int _readByte(int ms)
{
    if (ms >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
        int rc = select(1, &fds, NULL, NULL, &tv);
        if (rc < 0 && errno == EINTR) return -1;  /* signal — not an error */
        if (rc <= 0) return -1;
    }
    uint8_t c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return (int)c;
}

static int _parseEscape(void)
{
    int a = _readByte(50);
    if (a < 0)  return FB_KEY_ESC;
    if (a != '[' && a != 'O') return FB_KEY_ESC;

    int b = _readByte(50);
    if (b < 0)  return FB_KEY_ESC;

    /* CSI sequences */
    if (a == '[') {
        switch (b) {
            case 'A': return FB_KEY_UP;
            case 'B': return FB_KEY_DOWN;
            case 'C': return FB_KEY_RIGHT;
            case 'D': return FB_KEY_LEFT;
            case 'H': return FB_KEY_HOME;
            case 'F': return FB_KEY_END;
            default: break;
        }
        /* Extended: ESC [ N ~ */
        if (b >= '0' && b <= '9') {
            int c2 = _readByte(50);
            int num = b - '0';
            while (c2 >= '0' && c2 <= '9') {
                num = num * 10 + (c2 - '0');
                c2 = _readByte(50);
            }
            /* c2 should be '~' */
            switch (num) {
                case 1:  return FB_KEY_HOME;
                case 2:  return FB_KEY_INSERT;
                case 3:  return FB_KEY_DELETE;
                case 4:  return FB_KEY_END;
                case 5:  return FB_KEY_PAGE_UP;
                case 6:  return FB_KEY_PAGE_DOWN;
                case 11: return FB_KEY_F1;
                case 12: return FB_KEY_F2;
                case 13: return FB_KEY_F3;
                case 14: return FB_KEY_F4;
                case 15: return FB_KEY_F5;
                case 17: return FB_KEY_F6;
                case 18: return FB_KEY_F7;
                case 19: return FB_KEY_F8;
                case 20: return FB_KEY_F9;
                case 21: return FB_KEY_F10;
                case 23: return FB_KEY_F11;
                case 24: return FB_KEY_F12;
            }
        }
    }
    /* SS3 sequences (F1–F4 on some terminals) */
    if (a == 'O') {
        switch (b) {
            case 'P': return FB_KEY_F1;
            case 'Q': return FB_KEY_F2;
            case 'R': return FB_KEY_F3;
            case 'S': return FB_KEY_F4;
        }
    }
    return FB_KEY_ESC;
}

int fbGetKey(fbScreen *scr)
{
    (void)scr;
    int c = _readByte(-1);
    if (c < 0) return FB_KEY_NONE;
    if (c == 0x1B) return _parseEscape();
    return c;
}

int fbGetKeyTimeout(fbScreen *scr, int ms)
{
    (void)scr;
    int c = _readByte(ms);
    if (c < 0) return FB_KEY_NONE;
    if (c == 0x1B) return _parseEscape();
    return c;
}

int fbGetStr(fbWindow *win, char *buf, int len)
{
    if (!win || !buf || len <= 0) return 0;
    int pos = 0;
    buf[0] = '\0';

    int origCol = win->cursorCol;
    int origRow = win->cursorRow;

    while (1) {
        /* Redraw field */
        int c = origCol;
        for (int i = 0; i < len - 1 && c < win->cols; i++, c++) {
            fbCell *cell = _fbGetCell(win, c, origRow);
            cell->ch    = i < pos ? (wchar_t)(unsigned char)buf[i] : L'_';
            cell->fg    = win->fg;
            cell->bg    = win->bg;
            cell->attr  = (i == pos) ? FB_ATTR_REVERSE : FB_ATTR_NONE;
            cell->dirty = true;
        }
        fbRefresh(win);
        fbFlush(win->scr);

        int key = fbGetKey(win->scr);
        if (key == FB_KEY_ENTER || key == '\n' || key == '\r') break;
        if (key == FB_KEY_ESC) { pos = 0; buf[0] = '\0'; break; }
        if ((key == FB_KEY_BACKSPACE || key == 127) && pos > 0) {
            pos--;
            buf[pos] = '\0';
        } else if (key >= 0x20 && key < 0x7F && pos < len - 1) {
            buf[pos++] = (char)key;
            buf[pos]   = '\0';
        }
    }
    return pos;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Colour utilities
 * ═══════════════════════════════════════════════════════════════════ */

fbColor fbBlend(fbColor dst, fbColor src)
{
    uint8_t a = FB_COLOR_A(src);
    if (a == 0xFF) return src;
    if (a == 0x00) return dst;

    uint8_t sr = FB_COLOR_R(src), sg = FB_COLOR_G(src), sb = FB_COLOR_B(src);
    uint8_t dr = FB_COLOR_R(dst), dg = FB_COLOR_G(dst), db = FB_COLOR_B(dst);
    uint8_t ia = 255 - a;

    return FB_RGB(
        (uint8_t)((sr * a + dr * ia) / 255),
        (uint8_t)((sg * a + dg * ia) / 255),
        (uint8_t)((sb * a + db * ia) / 255)
    );
}

fbColor fbDarken(fbColor c, float factor)
{
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    return FB_RGB(
        (uint8_t)(FB_COLOR_R(c) * (1.0f - factor)),
        (uint8_t)(FB_COLOR_G(c) * (1.0f - factor)),
        (uint8_t)(FB_COLOR_B(c) * (1.0f - factor))
    );
}

fbColor fbLighten(fbColor c, float factor)
{
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    uint8_t r = FB_COLOR_R(c), g = FB_COLOR_G(c), b = FB_COLOR_B(c);
    return FB_RGB(
        (uint8_t)(r + (255 - r) * factor),
        (uint8_t)(g + (255 - g) * factor),
        (uint8_t)(b + (255 - b) * factor)
    );
}

fbColor fbGrayscale(fbColor c)
{
    uint8_t lum = (uint8_t)(
        0.2126f * FB_COLOR_R(c) +
        0.7152f * FB_COLOR_G(c) +
        0.0722f * FB_COLOR_B(c)
    );
    return FB_RGB(lum, lum, lum);
}

fbColor fbLerp(fbColor a, fbColor b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    float s = 1.0f - t;
    return FB_RGB(
        (uint8_t)(FB_COLOR_R(a) * s + FB_COLOR_R(b) * t),
        (uint8_t)(FB_COLOR_G(a) * s + FB_COLOR_G(b) * t),
        (uint8_t)(FB_COLOR_B(a) * s + FB_COLOR_B(b) * t)
    );
}

/* ═══════════════════════════════════════════════════════════════════
 *  Virtual Console switching
 * ═══════════════════════════════════════════════════════════════════ */

int fbVtCurrent(const fbScreen *scr)
{
    if (!scr) return 0;
    /* Return the cached value set at fbInit time */
    if (scr->vtNum > 0) return scr->vtNum;
    /* Re-query in case it was not set */
    struct vt_stat vtst;
    if (scr->ttyFd >= 0 && ioctl(scr->ttyFd, VT_GETSTATE, &vtst) == 0)
        return vtst.v_active;
    return 0;
}

bool fbVtSwitch(fbScreen *scr, int vt, bool waitActive)
{
    if (!scr || vt < 1 || vt > 63) return false;

    /* Flush the back-buffer before leaving so the display is clean */
    fbFlush(scr);

    if (ioctl(scr->ttyFd, VT_ACTIVATE, vt) < 0)
        return false;

    if (waitActive) {
        /* Block until the requested VT is actually foregrounded */
        ioctl(scr->ttyFd, VT_WAITACTIVE, vt);
    }

    return true;
}

int fbVtOpenFree(fbScreen *scr)
{
    if (!scr) return -1;
    int freeVt = -1;
    if (ioctl(scr->ttyFd, VT_OPENQRY, &freeVt) < 0)
        return -1;
    return freeVt;   /* -1 if none available */
}

bool fbVtClose(fbScreen *scr, int vt)
{
    if (!scr || vt < 1) return false;
    /* VT_DISALLOCATE releases a VT that is not currently active */
    return ioctl(scr->ttyFd, VT_DISALLOCATE, vt) == 0;
}

int fbVtCount(fbScreen *scr)
{
    if (!scr) return -1;
    /* The kernel supports up to 63 virtual consoles by default */
    (void)scr;
    return 63;
}
