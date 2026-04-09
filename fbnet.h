#ifndef FBCURSES_NET_H
#define FBCURSES_NET_H

/*
 * fbnet.h — UDP remote-rendering API for fbcurses
 *
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 *
 * ═══════════════════════════════════════════════════════════════════
 * PROTOCOL
 * ═══════════════════════════════════════════════════════════════════
 *
 * Each UDP datagram contains one or more newline-separated commands:
 *
 *   COMMAND,arg1,arg2,...\n
 *   COMMAND,arg1,arg2,...\n
 *
 * A single \n at the end is optional for single-command datagrams.
 *
 * Arguments are separated by commas.  Whitespace around commas is
 * stripped.  The trailing newline is optional.
 *
 * ── Argument types ──────────────────────────────────────────────
 *
 *   INT        decimal integer, may be negative
 *   WIN        window handle — decimal integer returned by win_new
 *   COLOR      #RRGGBB hex string  OR  a named colour (see below)
 *   STR        string — may contain commas if enclosed in double
 *              quotes: "hello, world"
 *              Escape sequences inside quoted strings:
 *                \\n  newline    \\"  double-quote    \\\\  backslash
 *   BOOL       0/1 or true/false
 *   FONT       font name string: vga, bold8, thin5, narrow6, block8, lcd7
 *   BORDER     none, single, double, rounded, thick, dashed
 *   ALIGN      left, center, right
 *   ATTR       comma-separated subset: bold, dim, underline, reverse
 *              (must be the last argument when used, reads until end)
 *
 * ── Named colours ───────────────────────────────────────────────
 *   black, white, red, green, blue, yellow, magenta, cyan, gray,
 *   bright_red, bright_green, bright_blue, bright_yellow,
 *   bright_magenta, bright_cyan, transparent
 *
 * ═══════════════════════════════════════════════════════════════════
 * COMMAND REFERENCE
 * ═══════════════════════════════════════════════════════════════════
 *
 * ── Screen ──────────────────────────────────────────────────────
 *   flush                          fbFlush
 *   clear,COLOR                    fbClear
 *   cursor,BOOL                    fbSetCursor
 *
 * ── Windows ─────────────────────────────────────────────────────
 *   win_new,col,row,cols,rows      → WIN   fbNewWindow (returns id)
 *   win_del,WIN                    fbDelWindow
 *   win_move,WIN,col,row           fbMoveWindow
 *   win_resize,WIN,cols,rows       fbResizeWindow
 *   win_clear,WIN,COLOR            fbClearWindow
 *   win_refresh,WIN                fbRefresh
 *   win_font,WIN,FONT              fbSetFont
 *
 * ── Text state ──────────────────────────────────────────────────
 *   move,WIN,col,row               fbMoveCursor
 *   colors,WIN,COLOR,COLOR         fbSetColors  (fg, bg)
 *   attr,WIN,ATTR                  fbSetAttr
 *
 * ── Text output ─────────────────────────────────────────────────
 *   print,WIN,STR                  fbAddStr  (at current cursor)
 *   print_at,WIN,col,row,STR       fbPrintAt
 *   print_align,WIN,row,ALIGN,STR  fbPrintAligned
 *   print_px,x,y,STR,COLOR,COLOR,ATTR,FONT  fbDrawTextPx
 *
 * ── Drawing ─────────────────────────────────────────────────────
 *   pixel,x,y,COLOR                fbDrawPixel
 *   line,x0,y0,x1,y1,COLOR        fbDrawLine
 *   rect,x,y,w,h,COLOR            fbDrawRect
 *   fill_rect,x,y,w,h,COLOR       fbFillRect
 *   circle,cx,cy,r,COLOR          fbDrawCircle
 *   fill_circle,cx,cy,r,COLOR     fbFillCircle
 *
 * ── Borders ─────────────────────────────────────────────────────
 *   border,WIN,BORDER,COLOR        fbDrawBorder
 *   box,WIN,col,row,cols,rows,BORDER,COLOR   fbDrawBox
 *   title_bar,WIN,STR,BORDER,COLOR,COLOR,COLOR  fbDrawTitleBar
 *                                  (border_color, title_fg, title_bg)
 *
 * ── Widgets ─────────────────────────────────────────────────────
 *   progress,WIN,col,row,width,pct,COLOR,COLOR,BOOL  fbDrawProgressBar
 *   spinner,WIN,col,row,tick,COLOR,COLOR    fbDrawSpinner
 *   gauge,WIN,col,row,height,val,maxval,COLOR,COLOR  fbDrawGauge
 *   sparkline,WIN,col,row,width,COLOR,COLOR,f0,f1,…  fbDrawSparkline
 *              (floats 0.0–1.0 are appended as extra args)
 *   scroll_up,WIN,n,COLOR          fbScrollUp
 *   scroll_down,WIN,n,COLOR        fbScrollDown
 *
 * ── Notifications ───────────────────────────────────────────────
 *   toast,kind,ms,STR              fbToast
 *              kind: info, success, warning, error
 *
 * ── Query responses (server → client) ───────────────────────────
 *   After win_new the server sends back a UDP reply to the sender:
 *     ok,win_new,WIN_ID\n
 *   After any error:
 *     err,COMMAND,message\n
 *   After flush:
 *     ok,flush\n
 *
 * ═══════════════════════════════════════════════════════════════════
 */

#include "fbcurses.h"
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>   /* struct sockaddr_in */

/* Maximum UDP payload we accept (supports multiple \n-separated commands) */
#define FB_NET_MAX_PACKET  4096
#define FB_NET_MAX_WINDOWS 256

/* ── Server context ─────────────────────────────────────────────── */
typedef struct fbNetServer fbNetServer;

/**
 * fbNetOpen — create a UDP server listening on the given port.
 *
 * @param scr     the fbcurses screen to drive
 * @param port    UDP port to bind (e.g. 9876)
 * @returns       server context, or NULL on failure
 */
fbNetServer *fbNetOpen(fbScreen *scr, uint16_t port);

/**
 * fbNetClose — stop the server and free all resources.
 * Windows created via the network are also destroyed.
 */
void fbNetClose(fbNetServer *srv);

/**
 * fbNetProcess — receive and dispatch one pending UDP packet.
 *
 * Non-blocking: returns immediately if no packet is waiting.
 * Returns true if a packet was processed, false if the queue was empty.
 *
 * Intended for integration into an existing event loop:
 *
 *   while (running) {
 *       while (fbNetProcess(srv)) {}   // drain all pending packets
 *       int key = fbGetKeyTimeout(scr, 16);
 *       if (key == FB_KEY_ESC) running = false;
 *   }
 */
bool fbNetProcess(fbNetServer *srv);

/**
 * fbNetRun — blocking event loop that processes packets until
 * fbNetStop() is called from a signal handler or another thread,
 * or until the escape key is pressed on the local console.
 *
 * @param autoFlush   if true, call fbFlush after every packet that
 *                    modifies screen state (convenient for simple clients)
 */
void fbNetRun(fbNetServer *srv, bool autoFlush);

/**
 * fbNetStop — signal fbNetRun to return on the next iteration.
 * Safe to call from a signal handler.
 */
void fbNetStop(fbNetServer *srv);

/**
 * fbNetPort — return the port the server is actually listening on
 * (useful when port 0 was passed to fbNetOpen, letting the OS assign one).
 */
uint16_t fbNetPort(const fbNetServer *srv);

/**
 * fbNetPacketsIn  — total packets received since fbNetOpen.
 * fbNetPacketsErr — packets that failed to parse or execute.
 */
uint64_t fbNetPacketsIn (const fbNetServer *srv);
uint64_t fbNetPacketsErr(const fbNetServer *srv);

/* ── Packet logging ─────────────────────────────────────────────── */

typedef enum {
    FB_NET_LOG_NONE    = 0,
    FB_NET_LOG_ERRORS  = 1,   /* log parse/dispatch errors only     */
    FB_NET_LOG_COMMANDS= 2,   /* log every successfully dispatched  */
    FB_NET_LOG_ALL     = 3,   /* log raw packet bytes too           */
} fbNetLogLevel;

/**
 * fbNetSetLog — configure logging.
 *
 * @param level   verbosity
 * @param file    FILE* to write to (NULL → stderr)
 */
void fbNetSetLog(fbNetServer *srv, fbNetLogLevel level, void *file);

/* ── Low-level: parse one packet without a server context ───────── */


/* ═══════════════════════════════════════════════════════════════════
 *  Multicast — receive from a group address
 * ═══════════════════════════════════════════════════════════════════
 *
 * IPv4 multicast group addresses: 224.0.0.0 – 239.255.255.255
 *
 * Well-known fbcurses groups (choose any in the 239.x.x.x local-scope
 * range to avoid clashing with routing protocols):
 *
 *   239.76.66.49   "fbcurses-all"    — every display on the subnet
 *   239.76.66.50   "fbcurses-zone1"  — zone 1
 *   239.76.66.51   "fbcurses-zone2"  — zone 2
 *   239.76.66.52   "fbcurses-zone3"  — zone 3
 *
 * A server can join multiple groups simultaneously.
 *
 * Usage (server side):
 *
 *   fbNetServer *srv = fbNetOpen(scr, 9876);
 *   fbNetJoinMulticast(srv, "239.76.66.49");  // join the "all" group
 *   fbNetRun(srv, true);
 *
 * Usage (client side): send to the group address instead of unicast:
 *
 *   // C client
 *   fbNetClient *cl = fbncOpenMulticast("239.76.66.49", 9876);
 *   fbncPrintAt(cl, 1, 4, 5, "Hello every display!");
 *   fbncRefreshFlush(cl, 1);
 *   fbncClose(cl);
 *
 *   // Python client
 *   with FbNetMulticast("239.76.66.49", 9876) as fb:
 *       fb.print_at(1, 4, 5, "Hello every display!")
 *       fb.refresh_flush(1)
 *
 * Multicast packets are one-way — the client does NOT receive replies
 * (replies go back to the individual server, not the group).
 * Use fbncOpen() for commands that need a reply (win_new, ping, etc.),
 * then fbncOpenMulticast() for bulk rendering commands.
 * ═══════════════════════════════════════════════════════════════════ */

/** Default multicast group — "all fbcurses displays on this subnet" */
#define FB_NET_MCAST_ALL    "239.76.66.49"
#define FB_NET_MCAST_ZONE1  "239.76.66.50"
#define FB_NET_MCAST_ZONE2  "239.76.66.51"
#define FB_NET_MCAST_ZONE3  "239.76.66.52"

/** Maximum number of multicast groups one server can join simultaneously */
#define FB_NET_MAX_GROUPS   8

/**
 * fbNetJoinMulticast — join a multicast group on all interfaces.
 *
 * The server will now receive packets sent to @groupAddr in addition
 * to its unicast address.  Call multiple times to join several groups.
 *
 * @param groupAddr  dotted-decimal IPv4 multicast address string
 *                   e.g. "239.76.66.49"
 * @returns  true on success
 */
bool fbNetJoinMulticast(fbNetServer *srv, const char *groupAddr);

/**
 * fbNetLeaveMulticast — leave a previously joined multicast group.
 */
bool fbNetLeaveMulticast(fbNetServer *srv, const char *groupAddr);

/**
 * fbNetListGroups — fill @buf with a comma-separated list of the
 * multicast groups this server has joined.  Returns the number of
 * groups, or 0 if not in any group.
 */
int fbNetListGroups(const fbNetServer *srv, char *buf, int bufLen);

/**
 * fbNetDispatch — parse and execute a single NUL-terminated command
 * string against the given screen and window table directly.
 *
 * Useful for testing or embedding the protocol in other transports
 * (TCP, pipes, Unix sockets, etc.).
 *
 * @param scr      target screen
 * @param wins     array of FB_NET_MAX_WINDOWS fbWindow* (may be NULL slots)
 * @param cmd      the command string (modified in-place during parsing)
 * @param replyBuf output buffer for reply string (may be NULL)
 * @param replyLen size of replyBuf
 * @returns        true on success
 */
bool fbNetDispatch(fbNetServer *srv, fbScreen *scr, fbWindow **wins,
                   char *cmd, char *replyBuf, int replyLen);

#endif /* FBCURSES_NET_H */
