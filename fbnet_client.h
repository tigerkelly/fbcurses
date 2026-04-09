#ifndef FBCURSES_NET_CLIENT_H
#define FBCURSES_NET_CLIENT_H

/*
 * fbnet_client.h — C client library for the fbcurses UDP protocol.
 *
 * Provides a simple, synchronous API that mirrors the fbcurses drawing
 * functions, translating each call into a UDP packet sent to a remote
 * fbcurses server (see fbnet.h / fbnet.c).
 *
 * Usage:
 *
 *   fbNetClient *cl = fbncOpen("192.168.1.10", 9876);
 *
 *   int win = fbncWinNew(cl, 2, 2, 60, 14);
 *   fbncTitleBar(cl, win, "Hello Remote", FB_BORDER_DOUBLE,
 *                0x00FFFF, 0x000000, 0x00FFFF);
 *   fbncColors(cl, win, 0xFFFFFF, 0x000000);
 *   fbncPrintAt(cl, win, 4, 5, "Rendered remotely over UDP!");
 *   fbncWinRefresh(cl, win);
 *   fbncFlush(cl);
 *
 *   fbncClose(cl);
 *
 * Thread safety: one client per thread; no locking is performed.
 *
 * Colours are plain uint32_t values: 0xRRGGBB (no alpha).
 * Use the FB_RGB() macro from fbcurses.h if available, or construct
 * colours directly as hex literals.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* Opaque client handle */
typedef struct fbNetClient fbNetClient;

/* ── Colour type (0xRRGGBB) ──────────────────────────────────────── */
typedef uint32_t fbncColor;

#define FBNC_RGB(r,g,b)    ((fbncColor)(((r)<<16)|((g)<<8)|(b)))

/* Standard colours — match the server's named colour table exactly */
#define FBNC_BLACK          FBNC_RGB(  0,   0,   0)
#define FBNC_WHITE          FBNC_RGB(255, 255, 255)
#define FBNC_RED            FBNC_RGB(205,  49,  49)
#define FBNC_GREEN          FBNC_RGB( 13, 188, 121)
#define FBNC_BLUE           FBNC_RGB( 36, 114, 200)
#define FBNC_YELLOW         FBNC_RGB(229, 229,  16)
#define FBNC_MAGENTA        FBNC_RGB(188,  63, 188)
#define FBNC_CYAN           FBNC_RGB( 17, 168, 205)
#define FBNC_GRAY           FBNC_RGB(118, 118, 118)
#define FBNC_GREY           FBNC_RGB(118, 118, 118)   /* alias */

/* Bright variants */
#define FBNC_BRIGHT_RED     FBNC_RGB(241,  76,  76)
#define FBNC_BRIGHT_GREEN   FBNC_RGB( 35, 209, 139)
#define FBNC_BRIGHT_BLUE    FBNC_RGB( 59, 142, 234)
#define FBNC_BRIGHT_YELLOW  FBNC_RGB(245, 245,  67)
#define FBNC_BRIGHT_MAGENTA FBNC_RGB(214, 112, 214)
#define FBNC_BRIGHT_CYAN    FBNC_RGB( 41, 184, 219)

/* Transparent — sentinel value; _colorStr() sends "transparent" to server */
#define FBNC_TRANSPARENT    ((fbncColor)0xFF000000u)

/* ── Attribute flags (match FB_ATTR_* in fbcurses.h) ────────────── */
#define FBNC_ATTR_NONE      0x00
#define FBNC_ATTR_BOLD      0x01
#define FBNC_ATTR_UNDERLINE 0x02
#define FBNC_ATTR_REVERSE   0x04
#define FBNC_ATTR_DIM       0x10

/* ── Border styles ───────────────────────────────────────────────── */
typedef enum {
    FBNC_BORDER_NONE    = 0,
    FBNC_BORDER_SINGLE,
    FBNC_BORDER_DOUBLE,
    FBNC_BORDER_ROUNDED,
    FBNC_BORDER_THICK,
    FBNC_BORDER_DASHED,
} fbncBorder;

/* ── Alignment ───────────────────────────────────────────────────── */
typedef enum {
    FBNC_ALIGN_LEFT   = 0,
    FBNC_ALIGN_CENTER = 1,
    FBNC_ALIGN_RIGHT  = 2,
} fbncAlign;

/* ── Toast kinds ─────────────────────────────────────────────────── */
typedef enum {
    FBNC_TOAST_INFO    = 0,
    FBNC_TOAST_SUCCESS = 1,
    FBNC_TOAST_WARNING = 2,
    FBNC_TOAST_ERROR   = 3,
} fbncToastKind;

/* ── Font names ──────────────────────────────────────────────────── */
#define FBNC_FONT_VGA     "vga"
#define FBNC_FONT_BOLD8   "bold8"
#define FBNC_FONT_THIN5   "thin5"
#define FBNC_FONT_NARROW6 "narrow6"
#define FBNC_FONT_BLOCK8  "block8"
#define FBNC_FONT_LCD7    "lcd7"
#define FBNC_FONT_CGA8    "cga8"
#define FBNC_FONT_THIN6X12 "thin6x12"
#define FBNC_FONT_TALL8X14 "tall8x14"
#define FBNC_FONT_WIDE    "wide"
#define FBNC_FONT_12X24  "12x24"
#define FBNC_FONT_16X32  "16x32"
#define FBNC_FONT_24X48  "24x48"

/* ═══════════════════════════════════════════════════════════════════
 *  Connection
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncOpen — connect to an fbcurses UDP server.
 *
 * @param host   hostname or IP address string
 * @param port   UDP port
 * @returns      client handle, or NULL on failure
 */
fbNetClient *fbncOpen(const char *host, uint16_t port);

/**
 * fbncClose — release all resources.
 */
void fbncClose(fbNetClient *cl);

/**
 * fbncSetTimeout — set receive timeout for reply-expecting calls
 * (default 200ms).
 */
void fbncSetTimeout(fbNetClient *cl, int ms);

/**
 * fbncPing — send a ping and wait for the pong reply.
 * Returns round-trip time in milliseconds, or -1 on timeout.
 */
int fbncPing(fbNetClient *cl);

/* ═══════════════════════════════════════════════════════════════════
 *  Batching — queue commands and flush in a single datagram
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncBatchBegin — start accumulating commands instead of sending them.
 * All subsequent fbncXxx calls are queued in a local buffer.
 */
void fbncBatchBegin(fbNetClient *cl);

/**
 * fbncBatchEnd — send all queued commands in a single UDP datagram
 * and process any replies.
 * Returns number of commands sent, or -1 on error.
 */
int fbncBatchEnd(fbNetClient *cl);

/**
 * fbncBatchDiscard — discard the current batch without sending.
 */
void fbncBatchDiscard(fbNetClient *cl);

/* ═══════════════════════════════════════════════════════════════════
 *  Screen
 * ═══════════════════════════════════════════════════════════════════ */

void fbncFlush (fbNetClient *cl);
void fbncClear (fbNetClient *cl, fbncColor color);
void fbncCursor(fbNetClient *cl, bool visible);

/**
 * fbncScreenSize — query server screen dimensions.
 * Returns true on success; fills *pixW, *pixH, *cols, *rows.
 */
bool fbncScreenSize(fbNetClient *cl,
                    int *pixW, int *pixH, int *cols, int *rows);

/* ═══════════════════════════════════════════════════════════════════
 *  Windows
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncWinNew — create a remote window.
 * Returns the server-side window handle (≥1), or -1 on failure.
 */
int  fbncWinNew    (fbNetClient *cl, int col, int row, int cols, int rows);
void fbncWinDel    (fbNetClient *cl, int win);
void fbncWinMove   (fbNetClient *cl, int win, int col, int row);
void fbncWinResize (fbNetClient *cl, int win, int cols, int rows);
void fbncWinClear  (fbNetClient *cl, int win, fbncColor bg);
void fbncWinRefresh(fbNetClient *cl, int win);
void fbncWinFont   (fbNetClient *cl, int win, const char *fontName);

/**
 * fbncWinSize — query remote window dimensions.
 * Returns true on success; fills *cols, *rows.
 */
bool fbncWinSize(fbNetClient *cl, int win, int *cols, int *rows);

/* ═══════════════════════════════════════════════════════════════════
 *  Text state and output
 * ═══════════════════════════════════════════════════════════════════ */

void fbncMove    (fbNetClient *cl, int win, int col, int row);
void fbncColors  (fbNetClient *cl, int win, fbncColor fg, fbncColor bg);
void fbncAttr    (fbNetClient *cl, int win, uint8_t attr);

void fbncPrint   (fbNetClient *cl, int win, const char *str);
void fbncPrintAt (fbNetClient *cl, int win, int col, int row, const char *str);
void fbncPrintFmt(fbNetClient *cl, int win, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void fbncPrintAtFmt(fbNetClient *cl, int win, int col, int row,
                    const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
void fbncPrintAlign(fbNetClient *cl, int win, int row,
                    fbncAlign align, const char *str);

void fbncPrintPx(fbNetClient *cl, int x, int y, const char *str,
                 fbncColor fg, fbncColor bg, uint8_t attr,
                 const char *fontName);

/* ═══════════════════════════════════════════════════════════════════
 *  Drawing
 * ═══════════════════════════════════════════════════════════════════ */

void fbncPixel     (fbNetClient *cl, int x, int y, fbncColor color);
void fbncLine      (fbNetClient *cl, int x0, int y0, int x1, int y1,
                    fbncColor color);
void fbncRect      (fbNetClient *cl, int x, int y, int w, int h,
                    fbncColor color);
void fbncFillRect  (fbNetClient *cl, int x, int y, int w, int h,
                    fbncColor color);
void fbncCircle    (fbNetClient *cl, int cx, int cy, int r, fbncColor color);
void fbncFillCircle(fbNetClient *cl, int cx, int cy, int r, fbncColor color);

/* ═══════════════════════════════════════════════════════════════════
 *  Borders and decorations
 * ═══════════════════════════════════════════════════════════════════ */

void fbncDrawBorder(fbNetClient *cl, int win, fbncBorder style, fbncColor color);
void fbncBox     (fbNetClient *cl, int win, int col, int row,
                  int cols, int rows, fbncBorder style, fbncColor color);
void fbncTitleBar(fbNetClient *cl, int win, const char *title,
                  fbncBorder style, fbncColor borderColor,
                  fbncColor titleFg, fbncColor titleBg);

/* ═══════════════════════════════════════════════════════════════════
 *  Widgets
 * ═══════════════════════════════════════════════════════════════════ */

void fbncProgress (fbNetClient *cl, int win, int col, int row,
                   int width, int pct, fbncColor fg, fbncColor bg, bool showPct);
void fbncSpinner  (fbNetClient *cl, int win, int col, int row,
                   int tick, fbncColor fg, fbncColor bg);
void fbncTick     (fbNetClient *cl, int win, int col, int row,
                   fbncColor fg, fbncColor bg);
void fbncGauge    (fbNetClient *cl, int win, int col, int row,
                   int height, int value, int maxVal,
                   fbncColor fg, fbncColor bg);
void fbncSparkline(fbNetClient *cl, int win, int col, int row,
                   int width, fbncColor fg, fbncColor bg,
                   const float *values, int nValues);
void fbncScrollUp  (fbNetClient *cl, int win, int n, fbncColor bg);
void fbncScrollDown(fbNetClient *cl, int win, int n, fbncColor bg);

/* ═══════════════════════════════════════════════════════════════════
 *  Notifications
 * ═══════════════════════════════════════════════════════════════════ */

void fbncToast(fbNetClient *cl, fbncToastKind kind, int ms, const char *msg);

/* ═══════════════════════════════════════════════════════════════════
 *  Batch helpers (common sequences)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncRefreshFlush — win_refresh + flush in a single batch datagram.
 */
void fbncRefreshFlush(fbNetClient *cl, int win);

/**
 * fbncRefreshAllFlush — refresh_all + flush in a single batch datagram.
 */
void fbncRefreshAllFlush(fbNetClient *cl);

/* ═══════════════════════════════════════════════════════════════════
 *  Low-level: send a raw command string
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncSend — send a raw command string (may contain embedded newlines
 * for multi-command datagrams).  Does NOT wait for a reply.
 */
void fbncSend(fbNetClient *cl, const char *cmd);

/**
 * fbncSendRecv — send a command and wait up to timeout ms for a reply.
 * Returns true if a reply was received; fills replyBuf/replyLen.
 */
bool fbncSendRecv(fbNetClient *cl, const char *cmd,
                  char *replyBuf, int replyLen);

/* ═══════════════════════════════════════════════════════════════════
 *  Table widget
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *header;
    int         width;   /* 0 = auto */
    fbncAlign   align;
} fbncTableCol;

/**
 * fbncTable — render a data table in a remote window.
 *
 * @param cols      column descriptor array, terminated by {NULL,0,0}
 * @param rows      2D array of strings: rows[r][c]
 * @param nRows     number of data rows
 * @param selRow    highlighted row index (-1 = none)
 */
void fbncTable(fbNetClient *cl, int win, int startCol, int startRow,
               const fbncTableCol *cols,
               const char * const *const *rows, int nRows,
               int selRow,
               fbncColor headerFg, fbncColor headerBg,
               fbncColor cellFg,   fbncColor cellBg,
               fbncColor selFg,    fbncColor selBg);

/* ═══════════════════════════════════════════════════════════════════
 *  Colour math (server-side computation, reply contains result)
 * ═══════════════════════════════════════════════════════════════════ */

fbncColor fbncBlend    (fbNetClient *cl, fbncColor dst, fbncColor src);
fbncColor fbncLerp     (fbNetClient *cl, fbncColor a, fbncColor b, float t);
fbncColor fbncDarken   (fbNetClient *cl, fbncColor c, float factor);
fbncColor fbncLighten  (fbNetClient *cl, fbncColor c, float factor);
fbncColor fbncGrayscale(fbNetClient *cl, fbncColor c);

/* ═══════════════════════════════════════════════════════════════════
 *  Custom border
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncCustomBorder — draw a border with fully custom characters.
 * tl/tr/bl/br are corner codepoints; h/v are horizontal/vertical bars.
 * Pass ASCII chars or Unicode codepoints as integers.
 */
void fbncCustomBorder(fbNetClient *cl, int win,
                      int tl, int tr, int bl, int br,
                      int h,  int v,  fbncColor color);

/* ═══════════════════════════════════════════════════════════════════
 *  Interactive dialogs (blocking — wait for server reply)
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncMenu — show a pop-up menu on the server and wait for selection.
 * Returns selected id, or -1 if cancelled / timeout.
 */
int fbncMenuDlg(fbNetClient *cl, int col, int row,
                fbncColor fg,    fbncColor bg,
                fbncColor fgSel, fbncColor bgSel,
                fbncBorder border,
                const char **labels, const int *ids, int nItems);

/**
 * fbncMsgBox — show a modal message box and return the button pressed.
 * Returns "ok", "cancel", "yes", or "no".  Caller must not free result.
 */
const char *fbncMsgBox(fbNetClient *cl,
                       const char *title, const char *msg,
                       const char *buttons, const char *kind);

/**
 * fbncFilePick — open a file browser dialog on the server.
 * Fills outPath with selected path (empty string if cancelled).
 * Returns true if a file was selected.
 */
bool fbncFilePick(fbNetClient *cl, const char *startDir,
                  char *outPath, int outLen);

/**
 * fbncColorPick — open a colour picker on the server.
 * Returns selected colour, or 0 if cancelled.
 */
fbncColor fbncColorPick(fbNetClient *cl, fbncColor initial);

/* ═══════════════════════════════════════════════════════════════════
 *  Server info
 * ═══════════════════════════════════════════════════════════════════ */

/** fbncVersion — returns server version string (static buffer). */
const char *fbncVersion(fbNetClient *cl);

/** fbncFonts — returns comma-separated list of available font names. */
const char *fbncFonts(fbNetClient *cl);


/* ═══════════════════════════════════════════════════════════════════
 *  Multicast client — send to a group address
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * fbncOpenMulticast — create a client that SENDS to a multicast group.
 *
 * All fbncXxx() calls on this client broadcast to every server that
 * has joined the group.  Replies are NOT received (no recv socket),
 * so functions like fbncWinNew() / fbncPing() will not work — use
 * a regular fbncOpen() for those, then switch to multicast for bulk
 * rendering.
 *
 * @param group  multicast group address, e.g. FB_NET_MCAST_ALL
 * @param port   destination port (must match servers)
 * @param ttl    IP TTL for multicast packets:
 *                 1 = local subnet only (default, recommended)
 *                 2-31 = site-local
 *                 32-255 = global (use with care)
 * @param loopback  true = packets are also delivered to the local host
 *                  (useful for testing with a server on the same machine)
 */
fbNetClient *fbncOpenMulticast(const char *group, uint16_t port,
                                uint8_t ttl, bool loopback);

/**
 * fbncSetMulticastTTL — change the TTL of an existing multicast client.
 */
bool fbncSetMulticastTTL(fbNetClient *cl, uint8_t ttl);

/**
 * fbncSetMulticastLoopback — enable/disable local loopback.
 */
bool fbncSetMulticastLoopback(fbNetClient *cl, bool enable);

/**
 * fbncSetMulticastInterface — bind outgoing multicast to a specific
 * local interface by IP address (e.g. "192.168.1.10").
 * Pass NULL to use the default interface.
 */
bool fbncSetMulticastInterface(fbNetClient *cl, const char *ifAddr);

/**
 * fbncIsMulticast — return true if this client targets a multicast address.
 */
bool fbncIsMulticast(const fbNetClient *cl);

/* Convenience: open a multicast client with sensible defaults */
/** Predefined multicast group addresses (mirror of FB_NET_MCAST_* in fbnet.h).
 *  Included here so client-only code need not include fbnet.h. */
#define FBNC_MCAST_ALL    "239.76.66.49"   /**< All fbcurses displays on subnet */
#define FBNC_MCAST_ZONE1  "239.76.66.50"   /**< Zone 1 */
#define FBNC_MCAST_ZONE2  "239.76.66.51"   /**< Zone 2 */
#define FBNC_MCAST_ZONE3  "239.76.66.52"   /**< Zone 3 */

#define fbncOpenMcast(group, port)  fbncOpenMulticast(group, port, 1, false)


#endif /* FBCURSES_NET_CLIENT_H */
