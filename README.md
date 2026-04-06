# fbcurses

A **ncurses-style TUI library for the Linux framebuffer** (`/dev/fb*`), written in C11.
Renders directly to `/dev/fb*` — no X11, no Wayland, no ncurses.

Also includes a **UDP remote-rendering server** so any machine on the network
can drive the display using a simple CSV-over-UDP protocol, with client libraries
in C and Python.

---

## Quick start

```c
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

---

## Build

```sh
make              # builds libfbcurses.a + demo, font_demo, net_demo
make install      # installs to /usr/local
make clean
```

Link your own program:

```sh
gcc -O2 myapp.c -lfbcurses -lm -o myapp
```

---

## Fonts (10 built-in)

| Symbol | Size | Best for |
|---|---|---|
| `fbVga` | 8x16 | Default, general text |
| `fbBold8` | 8x16 | Headings, emphasis |
| `fbThin5` | 5x8 | Dense data (~192 cols on 1080p) |
| `fbNarrow6` | 6x12 | Narrow panels |
| `fbBlock8` | 8x16 | Retro/game UI |
| `fbLcd7` | 8x16 | Clocks, counters, dashboards |
| `fbCga8` | 8x8 | Ultra-compact (~240 cols) |
| `fbThin6x12` | 6x12 | Condensed |
| `fbTall8x14` | 8x14 | Tall with serif hints |
| `fbWide` | 8x16 | Thick-stroke display/title |

Switch per-window at runtime:

```c
fbSetFont(win, &fbLcd7);   // switch to LCD font
fbSetFont(win, NULL);      // reset to VGA default
```

Pixel-accurate multi-font rendering (bypasses cell grid):

```c
int x2 = fbDrawTextPx(scr, x, y, "12:34:56", FB_CYAN, FB_BLACK,
                       FB_ATTR_NONE, &fbLcd7);
```

---

## Core API

### Screen
```c
fbScreen *fbInit(const char *device);   // NULL => /dev/fb0
void      fbShutdown(fbScreen *scr);
void fbClear(scr, color);  void fbFlush(scr);  void fbSetCursor(scr, bool);
int fbWidth(scr), fbHeight(scr);  // pixels
int fbCols(scr),  fbRows(scr);    // character cells
```

### Windows
```c
fbWindow *fbNewWindow(scr, col, row, cols, rows);
void fbDelWindow(win);
void fbMoveWindow(win, col, row);   void fbResizeWindow(win, cols, rows);
void fbClearWindow(win, bg);        void fbRefresh(win);
int  fbWindowCols(win);             int  fbWindowRows(win);
void fbSetFont(win, font);          const fbFont *fbGetFont(win);
```

### Text
```c
void fbMoveCursor(win, col, row);
void fbSetColors(win, fg, bg);         // use FB_RGB(r,g,b)
void fbSetAttr(win, attr);             // FB_ATTR_BOLD|UNDERLINE|REVERSE|DIM
void fbAddChar(win, ch);
void fbAddWchar(win, wch);             // Unicode / box-drawing characters
void fbAddStr(win, str);
void fbAddUtf8(win, utf8);             // UTF-8 string
void fbPrint(win, fmt, ...);
void fbPrintAt(win, col, row, fmt, ...);
void fbPrintAligned(win, row, align, str);  // FB_ALIGN_LEFT/CENTER/RIGHT
int  fbDrawTextPx(scr, x, y, str, fg, bg, attr, font);  // pixel coords
int  fbDrawWTextPx(scr, x, y, wstr, fg, bg, attr, font);
```

### Drawing
```c
void fbDrawPixel(scr, x, y, color);
void fbDrawLine(scr, x0, y0, x1, y1, color);
void fbDrawRect(scr, x, y, w, h, color);   void fbFillRect(scr, x, y, w, h, color);
void fbDrawCircle(scr, cx, cy, r, color);  void fbFillCircle(scr, cx, cy, r, color);
```

### Borders
```c
// Styles: FB_BORDER_NONE/SINGLE/DOUBLE/ROUNDED/THICK/DASHED
void fbDrawBorder(win, style, color);
void fbDrawBox(win, col, row, cols, rows, style, color);
void fbDrawTitleBar(win, title, style, border_col, title_fg, title_bg);
void fbDrawCustomBorder(win, &fbBorder);  // fully custom characters
```

### Widgets
```c
void fbDrawProgressBar(win, col, row, width, pct, fg, bg, showPct);
void fbDrawSpinner(win, col, row, tick, fg, bg);
void fbDrawGauge(win, col, row, height, value, maxVal, fg, bg);
void fbDrawSparkline(win, col, row, values[], nValues, width, fg, bg);
void fbDrawTable(win, sc, sr, cols[], rows[][], nRows, selRow,
                 hdrFg, hdrBg, cellFg, cellBg, selFg, selBg);
void fbScrollUp(win, n, bg);  void fbScrollDown(win, n, bg);
```

### Pop-ups & dialogs
```c
int  fbMenu(scr, col, row, items[], fg, bg, fgSel, bgSel, border);

fbTextInput *fbTextInputNew(win, col, row, width, maxLen, initial);
void         fbTextInputDraw(ti, fg, bg, cursorFg);
bool         fbTextInputKey(ti, key);
const char  *fbTextInputGet(ti);
void         fbTextInputFree(ti);

void    fbToast(scr, FB_TOAST_INFO/SUCCESS/WARNING/ERROR, msg, ms);
bool    fbFilePicker(scr, dir, outPath, outLen);
fbColor fbColorPicker(scr, initial);
int     fbMsgBox(scr, title, msg, FB_MSGBOX_OK/OK_CANCEL/YES_NO/..., kind);
```

### Input
```c
int  fbGetKey(scr);                     // blocking
int  fbGetKeyTimeout(scr, ms);          // FB_KEY_NONE on timeout
int  fbGetStr(win, buf, len);           // line editor
void fbMouseInit(scr);
bool fbMousePoll(scr, &fbMouseEvent);   // non-blocking
void fbMouseShutdown(scr);
```

### Colour
```c
fbColor FB_RGB(r, g, b);
fbColor fbBlend(dst, src);     fbColor fbLerp(a, b, t);
fbColor fbDarken(c, factor);   fbColor fbLighten(c, factor);
fbColor fbGrayscale(c);
```

---

## UDP Remote Rendering

```sh
sudo ./net_demo [port]   # start server (default port 9876)
```

Each UDP datagram holds one or more newline-separated commands:

```
COMMAND,arg1,arg2,...\n
```

**Full command list:**

| Category | Commands |
|---|---|
| Screen | `flush`, `clear,COLOR`, `cursor,BOOL` |
| Windows | `win_new`, `win_del`, `win_move`, `win_resize`, `win_clear`, `win_refresh`, `win_font`, `win_size`, `list_windows`, `refresh_all` |
| Text | `move`, `colors`, `attr`, `print`, `print_at`, `print_align`, `print_px` |
| Drawing | `pixel`, `line`, `rect`, `fill_rect`, `circle`, `fill_circle` |
| Borders | `border`, `box`, `title_bar`, `custom_border` |
| Widgets | `progress`, `spinner`, `tick`, `gauge`, `sparkline`, `table`, `scroll_up`, `scroll_down` |
| Notifications | `toast` |
| Colour math | `blend`, `lerp`, `darken`, `lighten`, `grayscale` |
| Interactive | `menu`, `msgbox`, `file_pick`, `color_pick` |
| Info | `ping`, `screen_size`, `version`, `fonts`, `stats` |

### Shell one-liners

```sh
echo "ping" | nc -u -q1 127.0.0.1 9876

printf "win_new,2,2,60,10\ncolors,1,#00FF88,black\nprint_at,1,4,3,\"Hello!\"\nwin_refresh,1\nflush\n" \
  | nc -u -q1 127.0.0.1 9876
```

### Python client

```python
from fbnet_client import FbNet, Color, Border, Align, Font, Toast, Attr

with FbNet("127.0.0.1", 9876) as fb:
    print(f"RTT: {fb.ping():.1f} ms  |  fonts: {fb.fonts()}")

    win = fb.win_new(2, 2, 60, 14)
    fb.title_bar(win, "Remote UI", Border.DOUBLE,
                 Color.CYAN, Color.BLACK, Color.CYAN)
    fb.colors(win, Color.WHITE, Color.BLACK)
    fb.print_at(win, 4, 4, "Driven over UDP from Python!")

    # Batch for efficiency
    with fb.batch():
        fb.win_refresh(win)
        fb.flush()

    # Interactive dialog
    result = fb.msgbox("Confirm", "Delete all files?",
                       buttons="yes_no", kind="warning")
    print(f"User chose: {result}")
```

```sh
python3 fbnet_client.py 127.0.0.1 9876 ping
python3 fbnet_client.py 127.0.0.1 9876 version fonts
```

### C client

```c
#include "fbnet_client.h"

fbNetClient *cl = fbncOpen("127.0.0.1", 9876);
printf("RTT: %d ms\n", fbncPing(cl));

int win = fbncWinNew(cl, 2, 2, 60, 14);
fbncTitleBar(cl, win, "Hello", FBNC_BORDER_DOUBLE,
             FBNC_CYAN, FBNC_BLACK, FBNC_CYAN);
fbncPrintAtFmt(cl, win, 4, 4, "Hello from %s!", "C client");

fbncRefreshFlush(cl, win);           // batched win_refresh + flush

const char *btn = fbncMsgBox(cl, "Exit?", "Really quit?",
                              "yes_no", "warning");
fbncClose(cl);
```

---

## Examples

```sh
gcc -O2 -o clock   examples/clock.c   -L. -lfbcurses -lm
gcc -O2 -o sysmon  examples/sysmon.c  -L. -lfbcurses -lm
gcc -O2 -o logview examples/logview.c -L. -lfbcurses -lm
python3 examples/remote_dashboard.py 127.0.0.1 9876
```

| Example | Description |
|---|---|
| `examples/clock.c` | Real-time LCD clock with second progress bar |
| `examples/sysmon.c` | CPU/memory/network monitor with gauges and sparklines |
| `examples/logview.c` | Live log tail, colour-coded severity, keyword filter |
| `examples/remote_dashboard.py` | Animated Python UDP dashboard |

---

## Requirements

- Linux kernel with `CONFIG_FB` (framebuffer support)
- `/dev/fb0` read/write access: `sudo usermod -aG video $USER`
- Switch to a virtual console: **Ctrl-Alt-F2**, return: **Ctrl-Alt-F1**
- Works on: desktop VTs, Raspberry Pi, QEMU virtio-vga, any KMS/DRM fbdev

---

## License

MIT
