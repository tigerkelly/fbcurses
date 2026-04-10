#ifndef FBCURSES_H
#define FBCURSES_H

/*
 * fbcurses.h — Linux Framebuffer TUI Library
 * A ncurses-style API for direct framebuffer rendering.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fonts.h"

/* ─── Versioning ─────────────────────────────────────────────────── */
#define FBCURSES_VERSION_MAJOR 1
#define FBCURSES_VERSION_MINOR 0
#define FBCURSES_VERSION_PATCH 0

/* ─── Colour (32-bit ARGB) ───────────────────────────────────────── */
typedef uint32_t fbColor;

#define FB_RGB(r, g, b)        ((fbColor)(0xFF000000u | ((r) << 16) | ((g) << 8) | (b)))
#define FB_RGBA(r, g, b, a)    ((fbColor)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define FB_COLOR_R(c)          (((c) >> 16) & 0xFF)
#define FB_COLOR_G(c)          (((c) >>  8) & 0xFF)
#define FB_COLOR_B(c)          (((c)      ) & 0xFF)
#define FB_COLOR_A(c)          (((c) >> 24) & 0xFF)

/* Named colours */
#define FB_BLACK               FB_RGB(  0,   0,   0)
#define FB_WHITE               FB_RGB(255, 255, 255)
#define FB_RED                 FB_RGB(205,  49,  49)
#define FB_GREEN               FB_RGB( 13, 188, 121)
#define FB_YELLOW              FB_RGB(229, 229,  16)
#define FB_BLUE                FB_RGB( 36, 114, 200)
#define FB_MAGENTA             FB_RGB(188,  63, 188)
#define FB_CYAN                FB_RGB( 17, 168, 205)
#define FB_GRAY                FB_RGB(118, 118, 118)
#define FB_BRIGHT_RED          FB_RGB(241,  76,  76)
#define FB_BRIGHT_GREEN        FB_RGB( 35, 209, 139)
#define FB_BRIGHT_YELLOW       FB_RGB(245, 245,  67)
#define FB_BRIGHT_BLUE         FB_RGB( 59, 142, 234)
#define FB_BRIGHT_MAGENTA      FB_RGB(214, 112, 214)
#define FB_BRIGHT_CYAN         FB_RGB( 41, 184, 219)
#define FB_TRANSPARENT         ((fbColor)0x00000000u)

/* ─── Text attributes (bit flags) ───────────────────────────────── */
#define FB_ATTR_NONE           0x00u
#define FB_ATTR_BOLD           0x01u
#define FB_ATTR_UNDERLINE      0x02u
#define FB_ATTR_REVERSE        0x04u
#define FB_ATTR_BLINK          0x08u   /* best-effort; flicker via timer */
#define FB_ATTR_DIM            0x10u

/* ─── Border styles ─────────────────────────────────────────────── */
typedef enum {
    FB_BORDER_NONE = 0,
    FB_BORDER_SINGLE,      /* ┌─┐│└─┘ */
    FB_BORDER_DOUBLE,      /* ╔═╗║╚═╝ */
    FB_BORDER_ROUNDED,     /* ╭─╮│╰─╯ */
    FB_BORDER_THICK,       /* ┏━┓┃┗━┛ */
    FB_BORDER_DASHED,      /* ┌╌┐╎└╌┘ */
    FB_BORDER_CUSTOM
} fbBorderStyle;

typedef struct {
    fbBorderStyle style;
    wchar_t       tl, tr, bl, br;   /* corners: top-left, top-right, …  */
    wchar_t       horiz, vert;       /* horizontal / vertical bar        */
    fbColor       color;
} fbBorder;

/* ─── Key codes (returned by fbGetKey) ──────────────────────────── */
#define FB_KEY_NONE            0x0000
#define FB_KEY_UP              0x0100
#define FB_KEY_DOWN            0x0101
#define FB_KEY_LEFT            0x0102
#define FB_KEY_RIGHT           0x0103
#define FB_KEY_HOME            0x0104
#define FB_KEY_END             0x0105
#define FB_KEY_PAGE_UP         0x0106
#define FB_KEY_PAGE_DOWN       0x0107
#define FB_KEY_F1              0x0201
#define FB_KEY_F2              0x0202
#define FB_KEY_F3              0x0203
#define FB_KEY_F4              0x0204
#define FB_KEY_F5              0x0205
#define FB_KEY_F6              0x0206
#define FB_KEY_F7              0x0207
#define FB_KEY_F8              0x0208
#define FB_KEY_F9              0x0209
#define FB_KEY_F10             0x020A
#define FB_KEY_F11             0x020B
#define FB_KEY_F12             0x020C
#define FB_KEY_BACKSPACE       0x007F
#define FB_KEY_ENTER           0x000D
#define FB_KEY_ESC             0x001B
#define FB_KEY_TAB             0x0009
#define FB_KEY_DELETE          0x0300
#define FB_KEY_INSERT          0x0301
#define FB_KEY_RESIZE          0xFFFF   /* terminal/fb size changed      */

/* ─── Screen / window types ─────────────────────────────────────── */
typedef struct fbScreen fbScreen;
typedef struct fbWindow fbWindow;

/* ─── Alignment ─────────────────────────────────────────────────── */
typedef enum {
    FB_ALIGN_LEFT   = 0,
    FB_ALIGN_CENTER = 1,
    FB_ALIGN_RIGHT  = 2
} fbAlign;

/* ═══════════════════════════════════════════════════════════════════
 *  Screen (global context, one per program)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbInit — open the framebuffer and prepare the library.
 *
 * @param  device   framebuffer device path, e.g. "/dev/fb0" (NULL → auto)
 * @return pointer to the screen context, or NULL on failure
 */
fbScreen *fbInit(const char *device);

/**
 * fbShutdown — restore state and close the framebuffer.
 */
void fbShutdown(fbScreen *scr);

/** fbWidth / fbHeight — screen dimensions in *pixels*. */
int fbWidth(const fbScreen *scr);
int fbHeight(const fbScreen *scr);

/** fbColsRows — character columns/rows (using the built-in 8×16 font). */
int fbCols(const fbScreen *scr);
int fbRows(const fbScreen *scr);

/**
 * fbClear — fill the entire screen with a colour.
 */
void fbClear(fbScreen *scr, fbColor color);

/**
 * fbFlush — blit the back-buffer to the real framebuffer.
 * Call once per frame after all drawing is done.
 */
void fbFlush(fbScreen *scr);

/**
 * fbSetCursor — show/hide the hardware text cursor (if supported).
 */
void fbSetCursor(fbScreen *scr, bool visible);

/* ═══════════════════════════════════════════════════════════════════
 *  Window (a rectangular sub-region with its own coordinate space)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbNewWindow — create a window in character-cell coordinates.
 *
 * @param scr    parent screen
 * @param col    left edge  (character columns from screen left)
 * @param row    top edge   (character rows  from screen top)
 * @param cols   width  in character columns
 * @param rows   height in character rows
 * @return new window, or NULL on failure
 */
fbWindow *fbNewWindow(fbScreen *scr, int col, int row, int cols, int rows);

/** fbDelWindow — destroy a window and free its resources. */
void fbDelWindow(fbWindow *win);

/** fbMoveWindow — reposition a window (character coordinates). */
void fbMoveWindow(fbWindow *win, int col, int row);

/** fbResizeWindow — resize a window (character coordinates). */
void fbResizeWindow(fbWindow *win, int cols, int rows);

/** fbWindowGetScreen — return the fbScreen this window belongs to. */
fbScreen *fbWindowGetScreen(const fbWindow *win);

/** fbWindowCols / fbWindowRows — window dimensions in character cells. */
int fbWindowCols(const fbWindow *win);
int fbWindowRows(const fbWindow *win);

/** fbWindowPixelX / fbWindowPixelY — top-left corner of a window in pixels. */
int fbWindowPixelX(const fbWindow *win);
int fbWindowPixelY(const fbWindow *win);

/** fbWindowPixelW / fbWindowPixelH — window size in pixels.
 *  Accounts for the active font's cell size (including large fonts). */
int fbWindowPixelW(const fbWindow *win);
int fbWindowPixelH(const fbWindow *win);

/**
 * fbClearWindow — fill the window background with a colour.
 */
void fbClearWindow(fbWindow *win, fbColor bg);

/**
 * fbRefresh — render a window's content to the screen back-buffer.
 * Does NOT call fbFlush; you control when to flip.
 */
void fbRefresh(fbWindow *win);

/* ═══════════════════════════════════════════════════════════════════
 *  Drawing primitives (pixel-level, relative to screen)
 * ═══════════════════════════════════════════════════════════════════ */

void fbDrawPixel (fbScreen *scr, int x, int y, fbColor color);
void fbDrawLine  (fbScreen *scr, int x0, int y0, int x1, int y1, fbColor color);
void fbDrawRect  (fbScreen *scr, int x, int y, int w, int h, fbColor color);
void fbFillRect  (fbScreen *scr, int x, int y, int w, int h, fbColor color);
void fbDrawCircle(fbScreen *scr, int cx, int cy, int r, fbColor color);
void fbFillCircle(fbScreen *scr, int cx, int cy, int r, fbColor color);

/* ═══════════════════════════════════════════════════════════════════
 *  Text rendering (character-cell, relative to window)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbMoveCursor — move the window's internal text cursor.
 */
void fbMoveCursor(fbWindow *win, int col, int row);

/**
 * fbSetColors — set the active fg/bg colour pair for the window.
 */
void fbSetColors(fbWindow *win, fbColor fg, fbColor bg);

/**
 * fbSetAttr — set text attribute flags (FB_ATTR_*).
 */
void fbSetAttr(fbWindow *win, uint8_t attr);

/**
 * fbAddChar — draw a single character at the cursor, then advance.
 */
void fbAddChar(fbWindow *win, char ch);

/**
 * fbAddWchar — draw a single wide (Unicode) character at the cursor.
 * Handles box-drawing, block elements, braille, and any wchar_t codepoint.
 */
void fbAddWchar(fbWindow *win, wchar_t ch);

/**
 * fbAddStr — draw a NUL-terminated string at the cursor.
 */
void fbAddStr(fbWindow *win, const char *str);

/**
 * fbPrint — printf-style formatted text at the cursor.
 */
void fbPrint(fbWindow *win, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * fbPrintAt — move then print (convenience wrapper).
 */
void fbPrintAt(fbWindow *win, int col, int row, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * fbPrintAligned — print text aligned within the window's width.
 */
void fbPrintAligned(fbWindow *win, int row, fbAlign align, const char *str);

/* ═══════════════════════════════════════════════════════════════════
 *  Borders & boxes
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbDrawBorder — draw a border around the entire window using a preset style.
 */
void fbDrawBorder(fbWindow *win, fbBorderStyle style, fbColor color);

/**
 * fbDrawCustomBorder — draw a border using a fully custom fbBorder descriptor.
 */
void fbDrawCustomBorder(fbWindow *win, const fbBorder *border);

/**
 * fbDrawBox — draw a box at an arbitrary cell position within the window.
 */
void fbDrawBox(fbWindow *win, int col, int row, int cols, int rows,
               fbBorderStyle style, fbColor color);

/**
 * fbDrawTitleBar — draw the border *and* a centred title string.
 */
void fbDrawTitleBar(fbWindow *win, const char *title,
                    fbBorderStyle style, fbColor borderColor,
                    fbColor titleFg, fbColor titleBg);

/* ═══════════════════════════════════════════════════════════════════
 *  Progress & status widgets
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbDrawProgressBar — horizontal progress bar inside the window.
 *
 * @param col, row   top-left character position within the window
 * @param width      bar width in character columns
 * @param percent    0–100
 * @param fg         filled-section colour
 * @param bg         empty-section colour
 * @param showPct    if true, render "NN%" text in the bar
 */
void fbDrawProgressBar(fbWindow *win, int col, int row, int width,
                       int percent, fbColor fg, fbColor bg, bool showPct);

/**
 * fbDrawSpinner — animated spinner at (col, row).
 * Call repeatedly; the frame advances automatically each call.
 *
 * @param tick   monotonically increasing counter driving the animation
 */
void fbDrawSpinner(fbWindow *win, int col, int row, int tick,
                   fbColor fg, fbColor bg);

/* ═══════════════════════════════════════════════════════════════════
 *  Scrolling & panning
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbScrollUp / fbScrollDown — scroll the window content by n rows.
 * Vacated rows are filled with bg.
 */
void fbScrollUp  (fbWindow *win, int n, fbColor bg);
void fbScrollDown(fbWindow *win, int n, fbColor bg);

/* ═══════════════════════════════════════════════════════════════════
 *  Input
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbGetKey — blocking read of one keypress.
 * Returns an FB_KEY_* constant or a plain ASCII value.
 */
int  fbGetKey(fbScreen *scr);

/**
 * fbGetKeyTimeout — like fbGetKey but returns FB_KEY_NONE after ms milliseconds.
 */
int  fbGetKeyTimeout(fbScreen *scr, int ms);

/**
 * fbGetStr — read a line of text into buf (max len-1 chars + NUL).
 * Displays input at the current window cursor with basic editing.
 * Returns the number of characters read.
 */
int  fbGetStr(fbWindow *win, char *buf, int len);

/* ═══════════════════════════════════════════════════════════════════
 *  Colour utilities
 * ═══════════════════════════════════════════════════════════════════ */

/** fbBlend — alpha-composite src over dst. */
fbColor fbBlend(fbColor dst, fbColor src);

/** fbDarken / fbLighten — adjust brightness by factor (0.0–1.0). */
fbColor fbDarken (fbColor c, float factor);
fbColor fbLighten(fbColor c, float factor);

/** fbGrayscale — convert colour to grayscale. */
fbColor fbGrayscale(fbColor c);

/** fbLerp — linear interpolate between two colours (t = 0.0–1.0). */
fbColor fbLerp(fbColor a, fbColor b, float t);


/* ═══════════════════════════════════════════════════════════════════
 *  Pixel-level text blitting (font-aware, bypasses cell grid)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbDrawTextPx — render a string directly at pixel coordinates (x, y)
 * using any font, without involving a window or cell grid.
 *
 * This is the correct way to display text in multiple different fonts
 * on the same screen — each call can use a different font and the
 * glyphs are placed at exact pixel positions.
 *
 * After drawing, call fbFlush() to push to hardware.
 *
 * @param scr   target screen
 * @param x, y  top-left pixel position
 * @param str   NUL-terminated ASCII string
 * @param fg    foreground colour
 * @param bg    background colour (use FB_TRANSPARENT to skip bg fill)
 * @param attr  FB_ATTR_* flags (bold, underline, etc.)
 * @param font  which font to use (NULL → fbVga)
 * @returns     pixel x coordinate immediately after the last glyph
 */
int fbDrawTextPx(fbScreen *scr, int x, int y, const char *str,
                 fbColor fg, fbColor bg, uint8_t attr,
                 const fbFont *font);

/**
 * fbDrawWTextPx — same as fbDrawTextPx but accepts a wide string,
 * rendering Unicode / box-drawing characters correctly.
 */
int fbDrawWTextPx(fbScreen *scr, int x, int y, const wchar_t *str,
                  fbColor fg, fbColor bg, uint8_t attr,
                  const fbFont *font);

/* ═══════════════════════════════════════════════════════════════════
 *  Font selection
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbSetFont — change the active bitmap font for a window.
 *
 * The window's character-cell grid (cols, rows) is recalculated based on
 * the new font's pixel dimensions.  All existing cells are preserved and
 * re-marked dirty so the next fbRefresh() redraws everything correctly.
 *
 * Pass NULL to reset to the default VGA 8×16 font.
 *
 * @param win  target window
 * @param font pointer to an fbFont (e.g. &fbBold8, &fbThin5, …)
 */
void fbSetFont(fbWindow *win, const fbFont *font);

/**
 * fbGetFont — return the font currently active on a window.
 */
const fbFont *fbGetFont(const fbWindow *win);

/* ═══════════════════════════════════════════════════════════════════
 *  Error handling
 * ═══════════════════════════════════════════════════════════════════ */

/** fbGetError — return a human-readable description of the last error. */
const char *fbGetError(void);

/** fbErrorCode — numeric error code (0 = no error). */
int fbErrorCode(void);

/* ═══════════════════════════════════════════════════════════════════
 *  Mouse input
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    int     x, y;          /* pixel position                           */
    int     col, row;      /* character-cell position (VGA font grid)  */
    bool    left, middle, right;
    bool    moved;         /* position changed since last event        */
    int     wheelDelta;    /* +1 scroll up, -1 scroll down             */
} fbMouseEvent;

/**
 * fbMouseInit  — enable mouse tracking on the active terminal.
 * Call once after fbInit().  Disable with fbMouseShutdown().
 */
void fbMouseInit    (fbScreen *scr);
void fbMouseShutdown(fbScreen *scr);

/**
 * fbMousePoll — non-blocking read of one mouse event.
 * Returns true if an event was available; sets *ev.
 */
bool fbMousePoll(fbScreen *scr, fbMouseEvent *ev);

/* ═══════════════════════════════════════════════════════════════════
 *  Menu widget
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *label;     /* menu item text (NULL = separator line)   */
    int         id;        /* value returned when item is selected     */
    bool        disabled;
} fbMenuItem;

/**
 * fbMenu — display a pop-up menu anchored at (col, row) in screen
 * character coordinates and run a blocking event loop until the user
 * selects an item or presses Escape.
 *
 * Returns the selected item's id, or -1 if cancelled.
 *
 * @param items   array of fbMenuItem, terminated by {NULL, 0, false}
 * @param fgSel   foreground colour for the highlighted item
 * @param bgSel   background colour for the highlighted item
 * @param fg      foreground colour for normal items
 * @param bg      background colour for normal items
 * @param border  border style for the menu box
 */
int fbMenu(fbScreen *scr, int col, int row,
           const fbMenuItem *items,
           fbColor fg,    fbColor bg,
           fbColor fgSel, fbColor bgSel,
           fbBorderStyle border);

/* ═══════════════════════════════════════════════════════════════════
 *  Text-input widget (single-line editor with cursor)
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct fbTextInput fbTextInput;

/**
 * fbTextInputNew  — create a single-line editor.
 *
 * @param win     host window
 * @param col,row position within window (character cells)
 * @param width   visible width in character cells
 * @param maxLen  maximum string length (not counting NUL)
 * @param initial initial content (may be NULL)
 */
fbTextInput *fbTextInputNew (fbWindow *win, int col, int row,
                             int width, int maxLen,
                             const char *initial);
void         fbTextInputFree(fbTextInput *ti);

/** fbTextInputDraw — render the widget into the parent window. */
void         fbTextInputDraw(fbTextInput *ti,
                             fbColor fg, fbColor bg, fbColor cursorFg);

/**
 * fbTextInputKey — feed a key (from fbGetKey) into the editor.
 * Returns true if the key was consumed; false if it should propagate
 * (e.g. Tab, Enter, Escape — the caller decides what these mean).
 */
bool         fbTextInputKey(fbTextInput *ti, int key);

/** fbTextInputGet — return the current text (pointer into internal buf). */
const char  *fbTextInputGet(const fbTextInput *ti);

/** fbTextInputSet — replace the content programmatically. */
void         fbTextInputSet(fbTextInput *ti, const char *text);

/* ═══════════════════════════════════════════════════════════════════
 *  Toast / notification popup
 * ═══════════════════════════════════════════════════════════════════ */

typedef enum {
    FB_TOAST_INFO    = 0,
    FB_TOAST_SUCCESS = 1,
    FB_TOAST_WARNING = 2,
    FB_TOAST_ERROR   = 3,
} fbToastKind;

/**
 * fbToast — display a floating notification for durationMs milliseconds,
 * then erase it and restore what was underneath.
 *
 * @param kind      visual style (info/success/warning/error)
 * @param msg       message text (one line)
 * @param durationMs  display time; ≤0 = wait for any keypress
 */
void fbToast(fbScreen *scr, fbToastKind kind,
             const char *msg, int durationMs);

/* ═══════════════════════════════════════════════════════════════════
 *  Table / data-grid widget
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char  *header;   /* column header text                       */
    int          width;    /* column width in chars (0 = auto-size)    */
    fbAlign      align;
} fbTableCol;

/**
 * fbDrawTable — render a data table inside a window.
 *
 * @param win        target window
 * @param startCol   left edge (character column within window)
 * @param startRow   top edge
 * @param cols       column descriptors array, terminated by {NULL,0,0}
 * @param rows       2-D array of strings: rows[r][c]
 * @param nRows      number of data rows
 * @param selRow     highlighted row index (-1 = none)
 * @param headerFg/Bg   header colours
 * @param cellFg/Bg     cell colours
 * @param selFg/Bg      selected-row colours
 */
void fbDrawTable(fbWindow *win, int startCol, int startRow,
                 const fbTableCol *cols,
                 const char * const * const *rows, int nRows,
                 int selRow,
                 fbColor headerFg, fbColor headerBg,
                 fbColor cellFg,   fbColor cellBg,
                 fbColor selFg,    fbColor selBg);

/* ═══════════════════════════════════════════════════════════════════
 *  Gauge / sparkline widgets
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbDrawGauge — vertical bar gauge (like a VU meter) at (col,row).
 *
 * @param height   gauge height in rows
 * @param value    current value  (0–maxVal)
 * @param maxVal   maximum value
 * @param fg       filled bar colour
 * @param bg       empty background colour
 */
void fbDrawGauge(fbWindow *win, int col, int row,
                 int height, int value, int maxVal,
                 fbColor fg, fbColor bg);

/**
 * fbDrawSparkline — mini line chart using ▁▂▃▄▅▆▇█ block chars.
 *
 * @param values   array of nValues floats (0.0–1.0 normalised)
 * @param nValues  number of data points (should equal width)
 * @param width    width in character cells
 * @param fg       bar colour
 * @param bg       background colour
 */
void fbDrawSparkline(fbWindow *win, int col, int row,
                     const float *values, int nValues, int width,
                     fbColor fg, fbColor bg);

/* ═══════════════════════════════════════════════════════════════════
 *  Wide-string text output
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbAddWStr — draw a NUL-terminated wide string at the cursor.
 * Handles any Unicode codepoint including box-drawing characters.
 */
void fbAddWStr(fbWindow *win, const wchar_t *str);

/**
 * fbAddUtf8 — draw a UTF-8 encoded string at the cursor.
 * Decodes multi-byte sequences and renders each codepoint correctly.
 */
void fbAddUtf8(fbWindow *win, const char *utf8);

/* ═══════════════════════════════════════════════════════════════════
 *  File picker widget
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbFilePicker — interactive file browser dialog.
 *
 * Displays a scrollable file listing rooted at startDir.
 * The user can navigate with arrow keys, Enter to descend into
 * directories or select a file, Backspace/Left to go up, Esc to cancel.
 *
 * @param startDir  initial directory (NULL = current working dir)
 * @param outPath   buffer to receive the selected path
 * @param outLen    size of outPath buffer
 * @returns  true if a file was selected, false if cancelled
 */
bool fbFilePicker(fbScreen *scr, const char *startDir,
                  char *outPath, int outLen);

/* ═══════════════════════════════════════════════════════════════════
 *  Colour picker widget
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbColorPicker — interactive 256-colour swatch picker.
 *
 * Displays a palette grid; user navigates with arrow keys.
 * Enter selects, Esc cancels.
 *
 * @param initial   initially highlighted colour (or FB_BLACK)
 * @returns  selected fbColor, or FB_TRANSPARENT on cancel
 */
fbColor fbColorPicker(fbScreen *scr, fbColor initial);

/* ═══════════════════════════════════════════════════════════════════
 *  Message box
 * ═══════════════════════════════════════════════════════════════════ */

typedef enum {
    FB_MSGBOX_OK          = 0x01,
    FB_MSGBOX_OK_CANCEL   = 0x03,
    FB_MSGBOX_YES_NO      = 0x06,
    FB_MSGBOX_YES_NO_CANCEL = 0x07,
} fbMsgBoxButtons;

#define FB_MSGBOX_RESULT_OK      1
#define FB_MSGBOX_RESULT_CANCEL  2
#define FB_MSGBOX_RESULT_YES     4
#define FB_MSGBOX_RESULT_NO      8

/**
 * fbMsgBox — display a modal message box with configurable buttons.
 *
 * @param title    title bar text
 * @param msg      message body (may contain newlines)
 * @param buttons  which buttons to show (FB_MSGBOX_* flags)
 * @param kind     visual style (FB_TOAST_INFO / WARNING / ERROR / SUCCESS)
 * @returns  FB_MSGBOX_RESULT_* for the button the user pressed
 */
int fbMsgBox(fbScreen *scr, const char *title, const char *msg,
             fbMsgBoxButtons buttons, fbToastKind kind);


/* ═══════════════════════════════════════════════════════════════════
 *  Virtual Console (VT) switching
 *
 *  Linux supports up to 63 virtual consoles (VT1–VT63).  These
 *  functions let your program switch VTs programmatically rather than
 *  requiring the user to press Alt-Fn or Ctrl-Alt-Fn.
 *
 *  Typical use:
 *    int myVt = fbVtCurrent(scr);          // which VT am I on?
 *    fbVtSwitch(scr, myVt + 1);            // move to next VT
 *    fbVtSwitch(scr, myVt);               // come back
 *
 *    int free = fbVtOpenFree(scr);         // allocate a fresh VT
 *    fbVtSwitch(scr, free);               // switch to it
 *    // ... draw something ...
 *    fbVtSwitch(scr, myVt);               // return home
 *    fbVtClose(scr, free);                // release the VT
 *
 *  Notes:
 *    - You normally need to be root or in the 'tty' group.
 *    - VT numbers are 1-based (VT1 = tty1, VT2 = tty2, …).
 *    - fbVtSwitch() calls fbFlush() first so the screen is clean on
 *      the new VT.
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbVtCurrent — return the VT number this process is running on.
 * Returns 0 if the VT number could not be determined.
 */
int fbVtCurrent(const fbScreen *scr);

/**
 * fbVtSwitch — switch the active display to VT number @vt.
 *
 * Immediately changes the visible console; your program continues
 * running on the new VT.  Pass @waitActive=true to block until the
 * VT is actually foregrounded (useful before drawing).
 *
 * Returns true on success.
 */
bool fbVtSwitch(fbScreen *scr, int vt, bool waitActive);

/**
 * fbVtOpenFree — allocate the next unused VT and return its number.
 * Returns -1 if no free VT is available or the ioctl fails.
 */
int fbVtOpenFree(fbScreen *scr);

/**
 * fbVtClose — deallocate a VT that was previously opened with
 * fbVtOpenFree().  Do not close the VT you are currently on.
 * Returns true on success.
 */
bool fbVtClose(fbScreen *scr, int vt);

/**
 * fbVtCount — return the total number of VTs configured in the kernel
 * (NR_CONSOLES, typically 63).  Returns -1 on error.
 */
int fbVtCount(fbScreen *scr);

#endif /* FBCURSES_H */
