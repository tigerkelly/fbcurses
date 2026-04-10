# fbcurses

A **ncurses-style TUI library for the Linux framebuffer** (`/dev/fb*`), written in C11.
Renders directly to `/dev/fb*` — no X11, no Wayland, no ncurses.

Also includes **image loading and display** (BMP, PNG, JPEG), **video playback**
(any format FFmpeg supports), and a **UDP remote-rendering server** so any machine
on the network can drive the display using a simple CSV-over-UDP protocol, with
client libraries in C and Python.

---

## Quick start

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "fbcurses.h"

int main(void) {
    fbScreen *scr = fbInit(NULL);
    fbWindow *win = fbNewWindow(scr, 2, 2, 60, 14);
    fbDrawTitleBar(win, "Hello, framebuffer!",
                   FB_BORDER_DOUBLE, FB_CYAN, FB_BLACK, FB_CYAN);
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbPrintAt(win, 4, 4, "Press any key");
    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
    fbShutdown(scr);
}
```

```sh
make && sudo ./demo
```

Ctrl-C or pressing `q`/`Esc` exits cleanly from any demo — the terminal and
framebuffer state are always restored, even on crash.

---

## Build

Install dependencies first (see [Requirements](#requirements)), then:

```sh
make              # builds libfbcurses.a + demo, font_demo, net_demo
make install      # installs headers and library to /usr/local
make clean
```

Link your own program:

```sh
gcc -O2 -I. myapp.c -L. -lfbcurses -lm -o myapp

# With image support:
gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
    myapp.c -L. -lfbcurses -lm -lpng -ljpeg -o myapp

# With image + video support:
gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
    myapp.c -L. -lfbcurses -lm \
    $(pkg-config --libs libavcodec libavformat libswscale libavutil) \
    -lpng -ljpeg -o myapp
```

---

## Fonts (13 built-in)

| Symbol | Size | Best for |
|---|---|---|
| `fbVga` | 8×16 | Default, general text |
| `fbBold8` | 8×16 | Headings, emphasis |
| `fbThin5` | 5×8 | Dense data (~192 cols on 1080p) |
| `fbNarrow6` | 6×12 | Narrow panels |
| `fbBlock8` | 8×16 | Retro / game UI |
| `fbLcd7` | 8×16 | Clocks, counters, dashboards |
| `fbCga8` | 8×8 | Ultra-compact (~240 cols) |
| `fbThin6x12` | 6×12 | Condensed |
| `fbTall8x14` | 8×14 | Tall with serif hints |
| `fbWide` | 8×16 | Thick-stroke display / title |
| `fbFont12x24` | 12×24 | Large — scaled VGA with smoothing |
| `fbFont16x32` | 16×32 | Large — scaled VGA with smoothing |
| `fbFont24x48` | 24×48 | Extra large — 3× scaled VGA |

Switch per-window at runtime:

```c
fbSetFont(win, &fbFont16x32);   // switch to large font
fbSetFont(win, NULL);           // reset to VGA default
```

Pixel-accurate multi-font rendering (bypasses the cell grid):

```c
int x2 = fbDrawTextPx(scr, x, y, "12:34:56", FB_CYAN, FB_BLACK,
                       FB_ATTR_NONE, &fbLcd7);
// x2 is the pixel coordinate just past the last character — chain
// multiple fonts on one line by passing x2 as the next x.
```

For the large fonts, `font->h` stores bytes-per-glyph rather than pixel height.
Use the helper macros from `fonts.h`:

```c
int px_h = FB_FONT_PX_H(&fbFont24x48);  // true pixel height = 48
int bpr   = FB_FONT_BPR(&fbFont24x48);  // bytes per row       =  3
```

---

## Core API

### Screen
```c
fbScreen *fbInit(const char *device);   // NULL => /dev/fb0
void      fbShutdown(fbScreen *scr);

void fbClear(fbScreen *scr, fbColor color);
void fbFlush(fbScreen *scr);
void fbSetCursor(fbScreen *scr, bool visible);

int fbWidth(const fbScreen *scr);    // pixels
int fbHeight(const fbScreen *scr);
int fbCols(const fbScreen *scr);     // character cells (VGA grid)
int fbRows(const fbScreen *scr);
```

### Windows
```c
fbWindow *fbNewWindow(fbScreen *scr, int col, int row, int cols, int rows);
void fbDelWindow(fbWindow *win);
void fbMoveWindow(fbWindow *win, int col, int row);
void fbResizeWindow(fbWindow *win, int cols, int rows);
void fbClearWindow(fbWindow *win, fbColor bg);
void fbRefresh(fbWindow *win);

// Character-cell dimensions
int fbWindowCols(const fbWindow *win);
int fbWindowRows(const fbWindow *win);

// Pixel dimensions — account for the active font's cell size
fbScreen *fbWindowGetScreen(const fbWindow *win);
int fbWindowPixelX(const fbWindow *win);   // left edge in pixels
int fbWindowPixelY(const fbWindow *win);   // top edge in pixels
int fbWindowPixelW(const fbWindow *win);   // width in pixels
int fbWindowPixelH(const fbWindow *win);   // height in pixels

void fbSetFont(fbWindow *win, const fbFont *font);
const fbFont *fbGetFont(const fbWindow *win);
```

### Text
```c
void fbMoveCursor(fbWindow *win, int col, int row);
void fbSetColors(fbWindow *win, fbColor fg, fbColor bg);
void fbSetAttr(fbWindow *win, uint8_t attr);
     // FB_ATTR_NONE | FB_ATTR_BOLD | FB_ATTR_DIM
     // FB_ATTR_UNDERLINE | FB_ATTR_REVERSE

void fbAddChar(fbWindow *win, char ch);
void fbAddWchar(fbWindow *win, wchar_t wch);
void fbAddStr(fbWindow *win, const char *str);
void fbAddUtf8(fbWindow *win, const char *utf8);
void fbPrint(fbWindow *win, const char *fmt, ...);
void fbPrintAt(fbWindow *win, int col, int row, const char *fmt, ...);
void fbPrintAligned(fbWindow *win, int row, fbAlign align, const char *str);
     // FB_ALIGN_LEFT | FB_ALIGN_CENTER | FB_ALIGN_RIGHT

// Pixel-coordinate rendering — any font, any position
int fbDrawTextPx(fbScreen *scr, int x, int y, const char *str,
                 fbColor fg, fbColor bg, uint8_t attr, const fbFont *font);
```

### Drawing
```c
void fbDrawPixel(fbScreen *scr, int x, int y, fbColor color);
void fbDrawLine(fbScreen *scr, int x0, int y0, int x1, int y1, fbColor color);
void fbDrawRect(fbScreen *scr, int x, int y, int w, int h, fbColor color);
void fbFillRect(fbScreen *scr, int x, int y, int w, int h, fbColor color);
void fbDrawCircle(fbScreen *scr, int cx, int cy, int r, fbColor color);
void fbFillCircle(fbScreen *scr, int cx, int cy, int r, fbColor color);
```

### Borders
```c
// Styles: FB_BORDER_NONE/SINGLE/DOUBLE/ROUNDED/THICK/DASHED
void fbDrawBorder(fbWindow *win, fbBorderStyle style, fbColor color);
void fbDrawBox(fbWindow *win, int col, int row, int cols, int rows,
               fbBorderStyle style, fbColor color);
void fbDrawTitleBar(fbWindow *win, const char *title,
                    fbBorderStyle style,
                    fbColor border_col, fbColor title_fg, fbColor title_bg);
void fbDrawCustomBorder(fbWindow *win, const fbBorder *border);
```

### Widgets
```c
void fbDrawProgressBar(fbWindow *win, int col, int row, int width,
                       int pct, fbColor fg, fbColor bg, bool showPct);
void fbDrawSpinner(fbWindow *win, int col, int row, int tick,
                   fbColor fg, fbColor bg);
void fbDrawGauge(fbWindow *win, int col, int row, int height,
                 int value, int maxVal, fbColor fg, fbColor bg);
void fbDrawSparkline(fbWindow *win, int col, int row,
                     const float *values, int nValues, int width,
                     fbColor fg, fbColor bg);
void fbDrawTable(fbWindow *win, ...);
void fbScrollUp(fbWindow *win, int n, fbColor bg);
void fbScrollDown(fbWindow *win, int n, fbColor bg);
```

### Pop-ups & dialogs
```c
int fbMenu(fbScreen *scr, int col, int row, fbMenuItem items[],
           fbColor fg, fbColor bg, fbColor fgSel, fbColor bgSel,
           fbBorderStyle border);

fbTextInput *fbTextInputNew(fbWindow *win, int col, int row,
                            int width, int maxLen, const char *initial);
void        fbTextInputDraw(fbTextInput *ti, fbColor fg, fbColor bg, fbColor cur);
bool        fbTextInputKey(fbTextInput *ti, int key);
const char *fbTextInputGet(const fbTextInput *ti);
void        fbTextInputFree(fbTextInput *ti);

void    fbToast(fbScreen *scr, fbToastKind kind, const char *msg, int ms);
        // kind: FB_TOAST_INFO/SUCCESS/WARNING/ERROR
bool    fbFilePicker(fbScreen *scr, const char *dir, char *outPath, int outLen);
fbColor fbColorPicker(fbScreen *scr, fbColor initial);
int     fbMsgBox(fbScreen *scr, const char *title, const char *msg,
                 fbMsgBoxButtons btns, fbToastKind kind);
        // btns: FB_MSGBOX_OK/OK_CANCEL/YES_NO/...
```

### Input
```c
int  fbGetKey(fbScreen *scr);                        // blocking
int  fbGetKeyTimeout(fbScreen *scr, int ms);         // FB_KEY_NONE on timeout
int  fbGetStr(fbWindow *win, char *buf, int len);    // line editor

void fbMouseInit(fbScreen *scr);
bool fbMousePoll(fbScreen *scr, fbMouseEvent *ev);   // non-blocking
void fbMouseShutdown(fbScreen *scr);
```

### Colour
```c
fbColor FB_RGB(uint8_t r, uint8_t g, uint8_t b);
fbColor fbBlend(fbColor dst, fbColor src);
fbColor fbLerp(fbColor a, fbColor b, float t);
fbColor fbDarken(fbColor c, float factor);
fbColor fbLighten(fbColor c, float factor);
fbColor fbGrayscale(fbColor c);
```

### Virtual console switching

Switch VTs programmatically rather than requiring the user to press Alt-Fn:

```c
int  fbVtCurrent(const fbScreen *scr);               // VT we are on (1-based)
bool fbVtSwitch(fbScreen *scr, int vt, bool wait);   // switch to VT number vt
int  fbVtOpenFree(fbScreen *scr);                    // allocate next unused VT
bool fbVtClose(fbScreen *scr, int vt);               // release an allocated VT
int  fbVtCount(fbScreen *scr);                       // total VTs (typically 63)
```

> Requires root or membership in the `tty` group.

---

## Image support (`fbimage.h`)

Load and display BMP, PNG, and JPEG images directly on the framebuffer.
The format is detected from the file's magic bytes, not the filename extension.

Build with `-DFBIMAGE_PNG -DFBIMAGE_JPEG -lpng -ljpeg` to enable all formats.
BMP works with no extra dependencies.

### Loading
```c
fbImage *fbImageLoad(const char *path);          // auto-detect BMP/PNG/JPEG
fbImage *fbImageLoadBMP(const char *path);       // BMP only (no dependencies)
fbImage *fbImageLoadPNG(const char *path);       // PNG  (requires -DFBIMAGE_PNG)
fbImage *fbImageLoadJPEG(const char *path);      // JPEG (requires -DFBIMAGE_JPEG)
fbImage *fbImageFromRGBA(int w, int h,
                          const uint8_t *pixels); // create from existing RGBA data
void     fbImageFree(fbImage *img);
```

If loading fails, `fbImageLoad` prints the reason to stderr (file not found,
unsupported format, missing compile-time flags, etc.) and returns NULL.

### Drawing
```c
// Draw at native size
void fbImageDraw(fbScreen *scr, const fbImage *img, int x, int y);

// Draw scaled to (dstW x dstH) — bilinear interpolation
// Pass 0 for one dimension to scale proportionally
void fbImageDrawScaled(fbScreen *scr, const fbImage *img,
                       int x, int y, int dstW, int dstH);

// Draw a sub-region, optionally scaled
void fbImageDrawRegion(fbScreen *scr, const fbImage *img,
                       int srcX, int srcY, int srcW, int srcH,
                       int dstX, int dstY, int dstW, int dstH);

// Draw scaled to fill an fbWindow — position and size computed automatically
void fbImageDrawInWindow(fbWindow *win, const fbImage *img, bool keepAspect);
void fbImageDrawInWindowRegion(fbWindow *win, const fbImage *img,
                                int srcX, int srcY, int srcW, int srcH,
                                bool keepAspect);
```

`fbImageDrawInWindow` with `keepAspect=true` letterboxes or pillarboxes the
image to fit the window while preserving the aspect ratio, centred within
the window's pixel bounds. With `keepAspect=false` it stretches to fill exactly.

### Scaling helpers
```c
fbImage *fbImageScale(const fbImage *src, int newW, int newH);
fbImage *fbImageScaleFit(const fbImage *src, int maxW, int maxH);
```

### Example
```c
#include "fbimage.h"

fbScreen *scr = fbInit(NULL);

// Full-screen, aspect-ratio preserved
fbImage *img = fbImageLoad("photo.jpg");
fbImageDrawScaled(scr, img, 0, 0, fbWidth(scr), fbHeight(scr));
fbFlush(scr);
fbImageFree(img);

// Fit into an fbWindow
fbWindow *win = fbNewWindow(scr, 5, 3, 70, 35);
fbImage  *img2 = fbImageLoad("banner.png");
fbImageDrawInWindow(win, img2, /*keepAspect=*/true);
fbFlush(scr);
fbImageFree(img2);

fbShutdown(scr);
```

---

## Video support (`fbvideo.h`)

Play any video format FFmpeg supports (MP4, AVI, MKV, WebM, MOV, FLV, …)
directly on the framebuffer. Requires libavcodec, libavformat, libswscale,
and libavutil.

### Simple one-call playback
```c
// Full-screen, aspect-ratio preserved; q/Esc stops playback
fbVideoPlay(scr, "movie.mp4", 0, 0, 0, 0);

// In a specific rectangle (pixels)
fbVideoPlay(scr, "clip.mp4", 100, 100, 800, 450);

// Play in an fbWindow
fbVideoPlayInWindow(win, "clip.mp4", /*keepAspect=*/true);

// With options
fbVideoOpts opts = {
    .x = 0, .y = 0, .width = 0, .height = 0,
    .loop = true, .keepAspect = true, .speedFactor = 1.0
};
fbVideoPlayOpts(scr, "loop.mp4", &opts);
```

### Frame-by-frame interface
```c
fbVideo *vid = fbVideoOpen("movie.mp4");
printf("%.1f fps, %.1fs\n", fbVideoFPS(vid), fbVideoDuration(vid));

fbImage frame = {0};   // reused across frames — no per-frame alloc

// Decode and scale to 1280x720
while (fbVideoNextFrame(vid, &frame, 1280, 720)) {
    fbImageDraw(scr, &frame, 0, 0);
    fbFlush(scr);
    // ... overlay text, react to keys ...
}
free(frame.pixels);
fbVideoClose(vid);

// Or decode directly into a window
fbVideo *vid2 = fbVideoOpen("clip.mp4");
fbImage  frame2 = {0};
while (fbVideoNextFrameInWindow(vid2, &frame2, win, true)) {
    fbRefresh(overlay_win);   // draw HUD on top
    fbFlush(scr);
}
free(frame2.pixels);
fbVideoClose(vid2);
```

### API reference
```c
fbVideoResult fbVideoPlay(fbScreen *scr, const char *path,
                           int x, int y, int width, int height);
fbVideoResult fbVideoPlayOpts(fbScreen *scr, const char *path,
                               const fbVideoOpts *opts);
fbVideoResult fbVideoPlayInWindow(fbWindow *win, const char *path,
                                   bool keepAspect);

fbVideo *fbVideoOpen(const char *path);
int      fbVideoWidth(const fbVideo *vid);
int      fbVideoHeight(const fbVideo *vid);
double   fbVideoFPS(const fbVideo *vid);
double   fbVideoDuration(const fbVideo *vid);
bool     fbVideoNextFrame(fbVideo *vid, fbImage *img, int dstW, int dstH);
bool     fbVideoNextFrameInWindow(fbVideo *vid, fbImage *frame,
                                   fbWindow *win, bool keepAspect);
bool     fbVideoSeek(fbVideo *vid, double seconds);
void     fbVideoClose(fbVideo *vid);
```

Return codes: `FB_VIDEO_OK` (played to end), `FB_VIDEO_STOPPED` (user quit),
`FB_VIDEO_ERR_OPEN`, `FB_VIDEO_ERR_CODEC`, `FB_VIDEO_ERR_MEM`.

---

## UDP Remote Rendering

Start the server on the display machine:

```sh
sudo ./net_demo [port]   # default port 9876
# q or Esc on the console quits; Ctrl-C also works
```

The server joins the multicast group `239.76.66.49` on startup so it also
receives packets sent to that group address.

Each UDP datagram holds one or more `\n`-separated commands:

```
COMMAND,arg1,arg2,...\n
```

Arguments may be bare tokens or single/double quoted strings (quotes are
stripped). CRLF line endings are handled transparently.

### Command reference

| Category | Commands |
|---|---|
| Screen | `flush`, `clear,COLOR`, `cursor,BOOL` |
| Windows | `win_new,col,row,cols,rows` → `ok,win_new,ID`; `win_del`, `win_move`, `win_resize`, `win_clear`, `win_refresh`, `win_font`, `win_size`, `list_windows`, `refresh_all` |
| Text | `move`, `colors`, `attr`, `print`, `print_at`, `print_align`, `print_px` |
| Drawing | `pixel`, `line`, `rect`, `fill_rect`, `circle`, `fill_circle` |
| Borders | `border`, `box`, `title_bar`, `custom_border` |
| Widgets | `progress`, `spinner`, `tick`, `gauge`, `sparkline`, `table`, `scroll_up`, `scroll_down` |
| Notifications | `toast` |
| Colour math | `blend`, `lerp`, `darken`, `lighten`, `grayscale` |
| Interactive | `menu`, `msgbox`, `file_pick`, `color_pick` ¹ |
| Multicast | `subscribe,GROUP`, `unsubscribe,GROUP`, `groups` |
| Info | `ping`, `screen_size`, `version`, `fonts`, `stats` |

Font names in the protocol: `vga`, `bold8`, `thin5`, `narrow6`, `block8`,
`lcd7`, `cga8`, `thin6x12`, `tall8x14`, `wide`, `12x24`, `16x32`, `24x48`.

> ¹ Interactive commands (`menu`, `msgbox`, `file_pick`, `color_pick`) require
> local user input and are **not available via UDP** while the server event loop
> is running.  Sending them returns `err,<cmd>,blocking command not supported
> via UDP`.

### Shell one-liners

```sh
echo "ping" | nc -u -q1 127.0.0.1 9876

printf "win_new,2,2,60,10\ncolors,1,#00FF88,black\nprint_at,1,4,3,\"Hello!\"\nwin_refresh,1\nflush\n" \
  | nc -u -q1 127.0.0.1 9876

echo "print_px,50,100,\"Hello\",bright_cyan,black,none,16x32" \
  | nc -u -q1 127.0.0.1 9876
echo "flush" | nc -u -q1 127.0.0.1 9876
```

### Multicast — one packet, many displays

```sh
echo "clear,black" | nc -u 239.76.66.49 9876
printf "print_px,50,100,\"All displays!\",bright_yellow,black,none,16x32\nflush\n" \
  | nc -u 239.76.66.49 9876

echo "subscribe,239.76.66.49" | nc -u -q1 192.168.1.10 9876
```

| Constant | Address | Purpose |
|---|---|---|
| `FB_NET_MCAST_ALL` / `FBNC_MCAST_ALL` | `239.76.66.49` | All fbcurses displays |
| `FB_NET_MCAST_ZONE1` / `FBNC_MCAST_ZONE1` | `239.76.66.50` | Zone 1 |
| `FB_NET_MCAST_ZONE2` / `FBNC_MCAST_ZONE2` | `239.76.66.51` | Zone 2 |
| `FB_NET_MCAST_ZONE3` / `FBNC_MCAST_ZONE3` | `239.76.66.52` | Zone 3 |

### Python client

```python
from fbnet_client import FbNet, FbNetMulticast, Color, Border, Font, Attr

with FbNet("127.0.0.1", 9876) as fb:
    win = fb.win_new(2, 2, 60, 14)
    fb.title_bar(win, "Remote UI", Border.DOUBLE,
                 Color.CYAN, Color.BLACK, Color.CYAN)
    fb.print_at(win, 4, 4, "Driven over UDP from Python!")
    fb.print_px(50, 200, "Big text", Color.BRIGHT_CYAN,
                Color.BLACK, Attr.NONE, Font.F16X32)
    with fb.batch():
        fb.win_refresh(win)
        fb.flush()

with FbNetMulticast(FbNetMulticast.FB_NET_MCAST_ALL, 9876) as mc:
    mc.clear(Color.BLACK)
    mc.print_px(50, 100, "Hello everyone", Color.BRIGHT_YELLOW,
                Color.BLACK, Attr.NONE, Font.F16X32)
    mc.flush()
```

### C client

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "fbcurses.h"
#include "fbnet.h"
#include "fbnet_client.h"

// Unicast
int main(int argc, char *argv[]) {
	char ip[18];
    unsigned short port;

    strcpy(ip, "127.0.0.1");
    port = 9876;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-a", 2) == 0) {
            strcpy(ip, argv[i+1]);
        } else if (strncmp(argv[i], "-p", 2) == 0) {
            port = (unsigned short)atoi(argv[i+1]);
        }
    }

	fbNetClient *cl = fbncOpen(ip, port);
	int win = fbncWinNew(cl, 2, 2, 60, 14);
	fbncTitleBar(cl, win, "Hello", FBNC_BORDER_DOUBLE,
             FBNC_CYAN, FBNC_BLACK, FBNC_CYAN);
	fbncPrintAtFmt(cl, win, 4, 4, "Hello from C!");
	fbncPrintPx(cl, 50, 200, "Big text", FBNC_BRIGHT_CYAN, FBNC_BLACK,
            FBNC_ATTR_NONE, "16x32");
	fbncRefreshFlush(cl, win);
	fbncClose(cl);

	fbNetClient *mc = fbncOpenMcast(FBNC_MCAST_ALL, port);
	fbncClear(mc, 0x000000);
	fbncRefreshAllFlush(mc);
	fbncClose(mc);

	return 0;
}
```

---

## Tokeniser utilities

**`strqtok_r`** — quote-aware re-entrant tokeniser (R. K. Wiles, 1988/2015).
Like `strtok_r` but treats `'…'` or `"…"` as a single token, stripping quotes.

```c
#include "strqtok_r.h"

int main(void) {
	char buf[] = "hello,\"world, foo\",bar";
	char *save, *tok = strqtok_r(buf, ",", &save);
	// tok = "hello", then "world, foo", then "bar"
}
```

**`qparse`** — tokenises directly into a `char*` array:

```c
#include "qparse.h"

int main(void) {
	char buf[] = "print_at,1,4,4,\"Hello, world!\"";
	char *args[16];
	int n = qparse(buf, "\t\n ,", args, 16);
	// n=5: "print_at","1","4","4","Hello, world!"  args[5]=NULL
}
```

---

## Examples

```sh
# TUI examples (no extra libs needed)
gcc -O2 -I. examples/clock.c   -L. -lfbcurses -lm -o clock
gcc -O2 -I. examples/sysmon.c  -L. -lfbcurses -lm -o sysmon
gcc -O2 -I. examples/logview.c -L. -lfbcurses -lm -o logview

# Image viewer
gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
    examples/imageview.c -L. -lfbcurses -lm -lpng -ljpeg -o imageview

# Video player
gcc -O2 -I. -DFBIMAGE_PNG -DFBIMAGE_JPEG \
    examples/videoplayer.c -L. -lfbcurses -lm \
    $(pkg-config --libs libavcodec libavformat libswscale libavutil) \
    -lpng -ljpeg -o videoplayer

# Python examples
python3 examples/remote_dashboard.py 127.0.0.1 9876
python3 examples/multicast_demo.py
```

| Example | Description |
|---|---|
| `examples/clock.c` | Real-time LCD clock with second progress bar |
| `examples/sysmon.c` | CPU/memory/network monitor with gauges and sparklines |
| `examples/logview.c` | Live log tail, colour-coded severity, keyword filter |
| `examples/imageview.c` | Image viewer (BMP/PNG/JPEG) with zoom, pan, and window support |
| `examples/videoplayer.c` | Video player with HUD, seek, speed control, and window support |
| `examples/remote_dashboard.py` | Animated Python UDP dashboard |
| `examples/multicast_demo.py` | 5-slide broadcast demo sent to all displays at once |

### imageview

```sh
sudo ./imageview photo.jpg                         # full screen
sudo ./imageview -wp 0 0 800 600 photo.jpg         # 800x600 pixel window
sudo ./imageview -wp 100 100 640 480 photo.jpg     # offset window
sudo ./imageview -w 0 0 80 30 photo.jpg            # cell-based window
sudo ./imageview photo1.jpg photo2.jpg photo3.jpg  # slideshow
```

Keys: `Right`/`Space` next, `Left` prev, `f` fit/1:1, `z`/`Z` zoom, arrows pan, `i` info bar, `q` quit.

### videoplayer

```sh
sudo ./videoplayer movie.mp4                       # full screen
sudo ./videoplayer -wp 0 0 800 600 movie.mp4       # pixel window
sudo ./videoplayer -wp 100 100 640 480 clip.mp4    # offset window
sudo ./videoplayer -s 1.5 -t 30 movie.mp4          # 1.5× speed, start at 30s
sudo ./videoplayer -l movie.mp4                    # loop
sudo ./videoplayer clip1.mp4 clip2.mp4 clip3.mp4   # playlist
```

Keys: `Space`/`p` pause, `←`/`→` seek ±10s, `↑`/`↓` speed ±25%, `n` next, `r` restart, `h` HUD, `q` quit.

---

## Requirements

```sh
sudo apt install -y \
    build-essential \
    libpng-dev \
    libjpeg-dev \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    libavutil-dev
```

| Package | Used for |
|---|---|
| `build-essential` | `gcc`, `make`, `ar` |
| `libpng-dev` | PNG image loading |
| `libjpeg-dev` | JPEG image loading |
| `libavcodec-dev` | FFmpeg video decoding |
| `libavformat-dev` | FFmpeg container/demuxer (MP4, MKV, AVI…) |
| `libswscale-dev` | FFmpeg pixel format conversion / scaling |
| `libavutil-dev` | FFmpeg shared utilities |

The framebuffer itself (`/dev/fb0`) needs no extra package — just access permission:

```sh
sudo usermod -aG video $USER   # allow /dev/fb0 without sudo; re-login after
```

Run on a virtual console, not inside X11 or Wayland:

```sh
Ctrl-Alt-F2    # switch to VT2
Ctrl-Alt-F1    # return to desktop
```

Or switch programmatically: `fbVtSwitch(scr, 2, true)` (requires root or `tty` group).

Tested on: desktop VTs, Raspberry Pi, QEMU virtio-vga, KMS/DRM fbdev.

---

## Signal handling

`fbInit` automatically installs `SIGINT`, `SIGTERM`, and `SIGHUP` handlers
plus an `atexit` callback.  If the process is killed or crashes, terminal raw
mode is restored, the cursor is re-enabled, and the original screen contents
are restored — the console is always left in a usable state.

---

## File overview

| File | Purpose |
|---|---|
| `fbcurses.h` / `fbcurses.c` | Core library: screen, windows, text, drawing, input, VT switching, pixel window accessors |
| `fbcurses_internal.h` | Internal structs (not part of public API) |
| `fbimage.h` / `fbimage.c` | Image loading (BMP/PNG/JPEG) and drawing — screen and window-aware |
| `fbvideo.h` / `fbvideo.c` | Video playback via FFmpeg — simple and frame-by-frame APIs |
| `boxdraw.c` | Unicode box/block/braille character renderer |
| `widgets.c` | Progress bars, gauges, sparklines, tables, toasts, menus, text input |
| `dialogs.c` | File picker, colour picker, message box |
| `fonts.h` / `fonts.c` | Font registry (`fbFontList[]`, `fbFontCount = 13`) |
| `font_*.c` | Bitmap glyph data for each of the 13 fonts |
| `strqtok_r.h` / `strqtok_r.c` | Quote-aware `strtok_r` (R. K. Wiles, 1988/2015) |
| `qparse.h` / `qparse.c` | CSV tokeniser built on `strqtok_r` |
| `fbnet.h` / `fbnet.c` | UDP server: protocol dispatcher, multicast, event loop |
| `fbnet_client.h` / `fbnet_client.c` | C client library |
| `fbnet_client.py` | Python 3 client library |
| `demo.c` | Feature showcase |
| `font_demo.c` | Font and widget showcase (6 pages, including large-font page) |
| `net_demo.c` | UDP server with built-in self-test |

---

## License

MIT — see copyright headers in each source file.  
`strqtok_r.c` / `strqtok_r.h` are © 2015 Richard Kelly Wiles, MIT licensed.
