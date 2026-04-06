#!/usr/bin/env python3
"""
fbnet_client.py — Python client for the fbcurses UDP remote-rendering protocol.

See fbnet.h for the full protocol specification.

Usage:
    from fbnet_client import FbNet, Color, Border, Align, Font, Toast

    with FbNet("192.168.1.10", 9876) as fb:
        win = fb.win_new(2, 2, 60, 14)
        fb.title_bar(win, "Hello from Python", Border.DOUBLE,
                     Color.CYAN, Color.BLACK, Color.CYAN)
        fb.colors(win, Color.WHITE, Color.BLACK)
        fb.print_at(win, 4, 4, "Rendered remotely over UDP!")
        fb.win_refresh(win)
        fb.flush()

Requirements: Python 3.6+, no external dependencies.
"""

import socket
import struct
import time
import threading
from enum import IntEnum
from typing import Optional, List, Tuple

__all__ = [
    "FbNet", "Color", "Border", "Align", "Font", "Toast", "Attr",
]


# ── Named colours ───────────────────────────────────────────────────

class Color:
    """Common colour values as 0xRRGGBB integers."""
    BLACK          = 0x000000
    WHITE          = 0xFFFFFF
    RED            = 0xCD3131
    GREEN          = 0x0DBC79
    BLUE           = 0x2472C8
    YELLOW         = 0xE5E510
    MAGENTA        = 0xBC3FBC
    CYAN           = 0x11A8CD
    GRAY           = 0x767676
    BRIGHT_RED     = 0xF14C4C
    BRIGHT_GREEN   = 0x23D18B
    BRIGHT_BLUE    = 0x3B8EEA
    BRIGHT_YELLOW  = 0xF5F543
    BRIGHT_MAGENTA = 0xD670D6
    BRIGHT_CYAN    = 0x29B8DB
    TRANSPARENT    = 0x000000

    @staticmethod
    def rgb(r: int, g: int, b: int) -> int:
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)

    @staticmethod
    def to_str(c: int) -> str:
        return f"#{c:06X}"


# ── Enumerations ────────────────────────────────────────────────────

class Border(IntEnum):
    NONE    = 0
    SINGLE  = 1
    DOUBLE  = 2
    ROUNDED = 3
    THICK   = 4
    DASHED  = 5

    def __str__(self):
        return self.name.lower()


class Align(IntEnum):
    LEFT   = 0
    CENTER = 1
    RIGHT  = 2

    def __str__(self):
        return self.name.lower()


class Font:
    VGA     = "vga"
    BOLD8   = "bold8"
    THIN5   = "thin5"
    NARROW6 = "narrow6"
    BLOCK8  = "block8"
    LCD7    = "lcd7"
    CGA8    = "cga8"
    THIN6X12 = "thin6x12"
    TALL8X14 = "tall8x14"
    WIDE    = "wide"


class Attr:
    NONE      = 0x00
    BOLD      = 0x01
    UNDERLINE = 0x02
    REVERSE   = 0x04
    DIM       = 0x10

    @staticmethod
    def to_str(attr: int) -> str:
        if attr == 0:
            return "none"
        parts = []
        if attr & Attr.BOLD:      parts.append("bold")
        if attr & Attr.DIM:       parts.append("dim")
        if attr & Attr.UNDERLINE:  parts.append("underline")
        if attr & Attr.REVERSE:   parts.append("reverse")
        return "|".join(parts) if parts else "none"


class Toast(IntEnum):
    INFO    = 0
    SUCCESS = 1
    WARNING = 2
    ERROR   = 3

    def __str__(self):
        return self.name.lower()


# ── Helpers ─────────────────────────────────────────────────────────

def _quote(s: str) -> str:
    """Wrap a string in quotes, escaping special chars."""
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{s}"'


def _color(c: int) -> str:
    return f"#{c:06X}"


# ── Main client class ────────────────────────────────────────────────

class FbNet:
    """
    UDP client for the fbcurses remote-rendering protocol.

    Supports both immediate-send mode and batched mode (accumulates
    commands and sends them in one datagram for efficiency).
    """

    MAX_BATCH = 3800  # stay under typical ethernet MTU

    def __init__(self, host: str, port: int, timeout: float = 0.2):
        self._host    = host
        self._port    = port
        self._timeout = timeout
        self._sock    = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)
        self._addr    = (host, port)
        self._batch   : Optional[List[str]] = None
        self._lock    = threading.Lock()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        self._sock.close()

    # ── Low-level ──────────────────────────────────────────────────

    def send(self, cmd: str):
        """Send a raw command string (or queue it if batching)."""
        with self._lock:
            if self._batch is not None:
                line = cmd if cmd.endswith("\n") else cmd + "\n"
                pending = sum(len(c) for c in self._batch)
                if pending + len(line) < self.MAX_BATCH:
                    self._batch.append(line)
                    return
                # Batch full — flush and start new
                self._flush_batch()
                self._batch = [line]
            else:
                self._sock.sendto(cmd.encode(), self._addr)

    def _flush_batch(self):
        """Internal: send accumulated batch (must hold _lock)."""
        if self._batch:
            data = "".join(self._batch).encode()
            self._sock.sendto(data, self._addr)
            self._batch = []

    def send_recv(self, cmd: str) -> Optional[str]:
        """Send a command and wait for a reply. Never batches."""
        with self._lock:
            self._sock.sendto(cmd.encode(), self._addr)
        try:
            data, _ = self._sock.recvfrom(4096)
            return data.decode().strip()
        except socket.timeout:
            return None

    # ── Batching ───────────────────────────────────────────────────

    def batch_begin(self):
        """Start accumulating commands for a single datagram."""
        with self._lock:
            self._batch = []

    def batch_end(self) -> int:
        """Send the accumulated batch. Returns number of commands."""
        with self._lock:
            n = len(self._batch) if self._batch else 0
            self._flush_batch()
            self._batch = None
            return n

    def batch_discard(self):
        with self._lock:
            self._batch = None

    def __call__(self, *cmds: str):
        """
        Context-manager-style batch: with fb(...) sends all at once.

        Usage:
            with fb.batch():
                fb.colors(win, Color.WHITE, Color.BLACK)
                fb.print_at(win, 2, 2, "hello")
                fb.win_refresh(win)
                fb.flush()
        """
        return _BatchContext(self)

    def batch(self):
        """Return a context manager that batches commands in its body."""
        return _BatchContext(self)

    # ── Ping ───────────────────────────────────────────────────────

    def ping(self) -> Optional[float]:
        """Ping the server. Returns round-trip time in ms, or None."""
        t0 = time.monotonic()
        reply = self.send_recv("ping")
        if reply and reply.startswith("ok,pong"):
            return (time.monotonic() - t0) * 1000.0
        return None

    # ── Screen ─────────────────────────────────────────────────────

    def flush(self):
        self.send("flush")

    def clear(self, color: int = Color.BLACK):
        self.send(f"clear,{_color(color)}")

    def cursor(self, visible: bool):
        self.send(f"cursor,{1 if visible else 0}")

    def screen_size(self) -> Optional[Tuple[int, int, int, int]]:
        """Returns (pixel_w, pixel_h, cols, rows) or None."""
        r = self.send_recv("screen_size")
        if r and r.startswith("ok,screen_size,"):
            parts = r.split(",")[2:]
            if len(parts) >= 4:
                return tuple(int(p) for p in parts[:4])
        return None

    # ── Windows ────────────────────────────────────────────────────

    def win_new(self, col: int, row: int, cols: int, rows: int) -> int:
        """Create a remote window. Returns handle (≥1) or -1."""
        r = self.send_recv(f"win_new,{col},{row},{cols},{rows}")
        if r and r.startswith("ok,win_new,"):
            return int(r.split(",")[2])
        return -1

    def win_del(self, win: int):
        self.send(f"win_del,{win}")

    def win_move(self, win: int, col: int, row: int):
        self.send(f"win_move,{win},{col},{row}")

    def win_resize(self, win: int, cols: int, rows: int):
        self.send(f"win_resize,{win},{cols},{rows}")

    def win_clear(self, win: int, bg: int = Color.BLACK):
        self.send(f"win_clear,{win},{_color(bg)}")

    def win_refresh(self, win: int):
        self.send(f"win_refresh,{win}")

    def win_font(self, win: int, font: str):
        self.send(f"win_font,{win},{font}")

    def win_size(self, win: int) -> Optional[Tuple[int, int]]:
        """Returns (cols, rows) or None."""
        r = self.send_recv(f"win_size,{win}")
        if r and r.startswith("ok,win_size,"):
            parts = r.split(",")[2:]
            return (int(parts[0]), int(parts[1])) if len(parts) >= 2 else None
        return None

    def list_windows(self) -> List[int]:
        r = self.send_recv("list_windows")
        if r and r.startswith("ok,list_windows"):
            parts = r.split(",")[2:]
            return [int(p) for p in parts if p.strip().isdigit()]
        return []

    def refresh_all(self, flush: bool = False):
        self.send(f"refresh_all,{1 if flush else 0}")

    # ── Text state ─────────────────────────────────────────────────

    def move(self, win: int, col: int, row: int):
        self.send(f"move,{win},{col},{row}")

    def colors(self, win: int, fg: int, bg: int):
        self.send(f"colors,{win},{_color(fg)},{_color(bg)}")

    def attr(self, win: int, a: int):
        self.send(f"attr,{win},{Attr.to_str(a)}")

    # ── Text output ────────────────────────────────────────────────

    def print_(self, win: int, text: str):
        self.send(f"print,{win},{_quote(text)}")

    def print_at(self, win: int, col: int, row: int, text: str):
        self.send(f"print_at,{win},{col},{row},{_quote(text)}")

    def print_align(self, win: int, row: int, align: Align, text: str):
        self.send(f"print_align,{win},{row},{align},{_quote(text)}")

    def print_px(self, x: int, y: int, text: str,
                 fg: int = Color.WHITE, bg: int = Color.BLACK,
                 a: int = Attr.NONE, font: str = Font.VGA):
        self.send(f"print_px,{x},{y},{_quote(text)},"
                  f"{_color(fg)},{_color(bg)},{Attr.to_str(a)},{font}")

    # ── Drawing ────────────────────────────────────────────────────

    def pixel(self, x: int, y: int, color: int):
        self.send(f"pixel,{x},{y},{_color(color)}")

    def line(self, x0: int, y0: int, x1: int, y1: int, color: int):
        self.send(f"line,{x0},{y0},{x1},{y1},{_color(color)}")

    def rect(self, x: int, y: int, w: int, h: int, color: int):
        self.send(f"rect,{x},{y},{w},{h},{_color(color)}")

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int):
        self.send(f"fill_rect,{x},{y},{w},{h},{_color(color)}")

    def circle(self, cx: int, cy: int, r: int, color: int):
        self.send(f"circle,{cx},{cy},{r},{_color(color)}")

    def fill_circle(self, cx: int, cy: int, r: int, color: int):
        self.send(f"fill_circle,{cx},{cy},{r},{_color(color)}")

    # ── Borders ────────────────────────────────────────────────────

    def border(self, win: int, style: Border, color: int):
        self.send(f"border,{win},{style},{_color(color)}")

    def box(self, win: int, col: int, row: int,
            cols: int, rows: int, style: Border, color: int):
        self.send(f"box,{win},{col},{row},{cols},{rows},{style},{_color(color)}")

    def title_bar(self, win: int, title: str, style: Border,
                  border_color: int, title_fg: int, title_bg: int):
        self.send(f"title_bar,{win},{_quote(title)},{style},"
                  f"{_color(border_color)},{_color(title_fg)},{_color(title_bg)}")

    # ── Widgets ────────────────────────────────────────────────────

    def progress(self, win: int, col: int, row: int, width: int,
                 pct: int, fg: int, bg: int, show_pct: bool = True):
        self.send(f"progress,{win},{col},{row},{width},{pct},"
                  f"{_color(fg)},{_color(bg)},{1 if show_pct else 0}")

    def spinner(self, win: int, col: int, row: int,
                tick: int, fg: int, bg: int):
        self.send(f"spinner,{win},{col},{row},{tick},{_color(fg)},{_color(bg)}")

    def tick(self, win: int, col: int, row: int, fg: int, bg: int):
        """Auto-incrementing spinner (server tracks the tick counter)."""
        self.send(f"tick,{win},{col},{row},{_color(fg)},{_color(bg)}")

    def gauge(self, win: int, col: int, row: int,
              height: int, value: int, max_val: int,
              fg: int, bg: int):
        self.send(f"gauge,{win},{col},{row},{height},{value},{max_val},"
                  f"{_color(fg)},{_color(bg)}")

    def sparkline(self, win: int, col: int, row: int, width: int,
                  fg: int, bg: int, values: List[float]):
        vals = ",".join(f"{v:.3f}" for v in values)
        self.send(f"sparkline,{win},{col},{row},{width},"
                  f"{_color(fg)},{_color(bg)},{vals}")

    def scroll_up(self, win: int, n: int, bg: int = Color.BLACK):
        self.send(f"scroll_up,{win},{n},{_color(bg)}")

    def scroll_down(self, win: int, n: int, bg: int = Color.BLACK):
        self.send(f"scroll_down,{win},{n},{_color(bg)}")

    # ── Notifications ─────────────────────────────────────────────

    def toast(self, kind: Toast, ms: int, msg: str):
        self.send(f"toast,{kind},{ms},{_quote(msg)}")

    # ── Convenience ───────────────────────────────────────────────

    def refresh_flush(self, win: int):
        """win_refresh + flush in one batch datagram."""
        with self.batch():
            self.win_refresh(win)
            self.flush()

    def refresh_all_flush(self):
        """refresh_all + flush in one batch datagram."""
        with self.batch():
            self.refresh_all()
            self.flush()


    # ── Table ──────────────────────────────────────────────────────

    def table(self, win: int, start_col: int, start_row: int,
              cols: list, rows: list, sel_row: int = -1,
              header_fg: int = Color.BRIGHT_CYAN, header_bg: int = 0x0A1E28,
              cell_fg:   int = Color.WHITE,       cell_bg:   int = 0x0A0F19,
              sel_fg:    int = Color.BLACK,        sel_bg:    int = Color.BRIGHT_GREEN):
        """
        Render a data table in a remote window.

        cols: list of (header, width, align) tuples.
              width=0 means auto-size; align is Align.LEFT/CENTER/RIGHT.
        rows: list of lists of strings.

        Example:
            fb.table(win, 2, 4,
                cols=[("Name", 12, Align.LEFT), ("CPU%", 6, Align.RIGHT)],
                rows=[["fbcurses", "0.2"], ["python3", "1.4"]],
                sel_row=0)
        """
        parts = [f"table,{win},{start_col},{start_row}",
                 _color(header_fg), _color(header_bg),
                 _color(cell_fg),   _color(cell_bg),
                 _color(sel_fg),    _color(sel_bg),
                 str(sel_row)]
        for hdr, width, align in cols:
            parts += [_quote(hdr), str(width), str(align)]
        parts.append("|")
        for row in rows:
            for cell in row:
                parts.append(_quote(str(cell)))
        self.send(",".join(parts))

    # ── Colour math ────────────────────────────────────────────────

    def _color_reply(self, cmd: str) -> Optional[int]:
        r = self.send_recv(cmd)
        if not r: return None
        import re
        m = re.search(r'#([0-9A-Fa-f]{6})', r)
        if not m: return None
        return int(m.group(1), 16)

    def blend(self, dst: int, src: int) -> Optional[int]:
        """Alpha-composite src over dst. Returns result colour."""
        return self._color_reply(f"blend,{_color(dst)},{_color(src)}")

    def lerp(self, a: int, b: int, t: float) -> Optional[int]:
        """Linearly interpolate between colours a and b."""
        return self._color_reply(f"lerp,{_color(a)},{_color(b)},{t:.3f}")

    def darken(self, c: int, factor: float) -> Optional[int]:
        return self._color_reply(f"darken,{_color(c)},{factor:.3f}")

    def lighten(self, c: int, factor: float) -> Optional[int]:
        return self._color_reply(f"lighten,{_color(c)},{factor:.3f}")

    def grayscale(self, c: int) -> Optional[int]:
        return self._color_reply(f"grayscale,{_color(c)}")

    # ── Custom border ──────────────────────────────────────────────

    def custom_border(self, win: int,
                      tl: int, tr: int, bl: int, br: int,
                      h: int, v: int, color: int):
        """
        Draw a border with custom Unicode characters.
        tl/tr/bl/br are corner codepoints; h/v are line chars.
        Pass ord() of a character, e.g. ord('*') or 0x250C.
        """
        self.send(f"custom_border,{win},{tl},{tr},{bl},{br},{h},{v},{_color(color)}")

    # ── Interactive dialogs ────────────────────────────────────────

    def menu_dlg(self, col: int, row: int,
                 items: list,
                 fg: int = Color.WHITE,   bg: int = 0x14143C,
                 fg_sel: int = Color.BLACK, bg_sel: int = Color.CYAN,
                 border: Border = Border.ROUNDED) -> int:
        """
        Show a pop-up menu on the server display.
        items: list of (label, id) tuples.
        Returns the selected id, or -1 if cancelled.
        Blocks until the user makes a selection.

        Example:
            result = fb.menu_dlg(20, 10, [
                ("New",  1), ("Open", 2), ("Save", 3), ("Quit", 99)
            ])
        """
        parts = [f"menu,{col},{row}",
                 _color(fg), _color(bg), _color(fg_sel), _color(bg_sel),
                 str(border)]
        for label, item_id in items:
            parts += [_quote(label), str(item_id)]
        r = self.send_recv(",".join(parts))
        if r and r.startswith("ok,menu,"):
            return int(r.split(",")[2])
        return -1

    def msgbox(self, title: str, msg: str,
               buttons: str = "ok", kind: str = "info") -> str:
        """
        Show a modal message box on the server display.
        buttons: "ok" | "ok_cancel" | "yes_no" | "yes_no_cancel"
        kind:    "info" | "success" | "warning" | "error"
        Returns: "ok" | "cancel" | "yes" | "no"
        Blocks until the user presses a button.
        """
        cmd = f"msgbox,{_quote(title)},{_quote(msg)},{buttons},{kind}"
        r = self.send_recv(cmd)
        if r and r.startswith("ok,msgbox,"):
            return r.split(",")[2].strip()
        return "cancel"

    def file_pick(self, start_dir: str = "") -> Optional[str]:
        """
        Open a file picker dialog on the server.
        Returns the selected file path, or None if cancelled.
        Blocks until the user selects a file or cancels.
        """
        cmd = f"file_pick,{_quote(start_dir)}" if start_dir else "file_pick"
        r = self.send_recv(cmd)
        if not r or not r.startswith("ok,file_pick,"):
            return None
        path = r[len("ok,file_pick,"):].strip()
        return path if path else None

    def color_pick(self, initial: int = Color.BLACK) -> Optional[int]:
        """
        Open a colour picker dialog on the server.
        Returns the selected colour, or None if cancelled.
        Blocks until the user picks a colour or cancels.
        """
        return self._color_reply(f"color_pick,{_color(initial)}")

    # ── Server info ────────────────────────────────────────────────

    def version(self) -> Optional[str]:
        """Return the server's fbcurses version string."""
        r = self.send_recv("version")
        if r and r.startswith("ok,version,"):
            return r[len("ok,version,"):]
        return None

    def fonts(self) -> list:
        """Return list of font names available on the server."""
        r = self.send_recv("fonts")
        if r and r.startswith("ok,fonts,"):
            return r[len("ok,fonts,"):].split(",")
        return []


class _BatchContext:
    def __init__(self, fb: FbNet):
        self._fb = fb

    def __enter__(self):
        self._fb.batch_begin()
        return self._fb

    def __exit__(self, exc_type, *_):
        if exc_type:
            self._fb.batch_discard()
        else:
            self._fb.batch_end()



class FbNetMulticast(FbNet):
    """
    UDP multicast client — sends commands to ALL servers in a group.

    Every server that has joined the multicast group will receive and
    execute every command sent by this client simultaneously.

    Replies are NOT received (multicast is one-way for rendering).
    Use a regular FbNet() for commands that need replies (win_new, ping),
    then use FbNetMulticast() for bulk rendering.

    IPv4 multicast groups must be in 224.0.0.0 – 239.255.255.255.
    The 239.x.x.x range is recommended for site-local use.

    Usage:
        # Each server joins the group:
        #   fbNetJoinMulticast(srv, "239.76.66.49")  (or via "subscribe" command)
        #
        # Client sends to all of them at once:
        with FbNetMulticast("239.76.66.49", 9876) as fb:
            fb.colors(1, Color.WHITE, Color.BLACK)
            fb.print_at(1, 4, 5, "Hello every display!")
            fb.win_refresh(1)
            fb.flush()

    Predefined group addresses:
        FB_NET_MCAST_ALL   = "239.76.66.49"  — all fbcurses displays
        FB_NET_MCAST_ZONE1 = "239.76.66.50"
        FB_NET_MCAST_ZONE2 = "239.76.66.51"
        FB_NET_MCAST_ZONE3 = "239.76.66.52"
    """

    FB_NET_MCAST_ALL   = "239.76.66.49"
    FB_NET_MCAST_ZONE1 = "239.76.66.50"
    FB_NET_MCAST_ZONE2 = "239.76.66.51"
    FB_NET_MCAST_ZONE3 = "239.76.66.52"

    def __init__(self, group: str, port: int,
                 ttl: int = 1, loopback: bool = False,
                 iface: Optional[str] = None):
        """
        @param group     Multicast group address, e.g. "239.76.66.49"
        @param port      Destination port (must match servers)
        @param ttl       IP TTL: 1 = subnet only (default), up to 255
        @param loopback  True = also deliver to local host (for testing)
        @param iface     Outgoing interface IP (None = OS default)
        """
        import socket as _socket
        import struct

        # Validate
        octets = group.split(".")
        if len(octets) != 4:
            raise ValueError(f"Invalid multicast address: {group!r}")
        first = int(octets[0])
        if not (224 <= first <= 239):
            raise ValueError(
                f"Not a multicast address: {group!r} "
                f"(must be 224.0.0.0 – 239.255.255.255)"
            )

        self._group    = group
        self._port     = port
        self._ttl      = ttl
        self._loopback = loopback
        self._iface    = iface
        self._is_mcast = True

        self._timeout = 0.2
        self._addr    = (group, port)
        self._batch   = None
        self._lock    = __import__('threading').Lock()

        # Create UDP socket with multicast options
        self._sock = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM,
                                    _socket.IPPROTO_UDP)
        self._sock.settimeout(self._timeout)

        # TTL
        self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_TTL,
                               struct.pack('B', ttl))

        # Loopback
        self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_LOOP,
                               struct.pack('B', 1 if loopback else 0))

        # Interface binding
        if iface:
            self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_IF,
                                   _socket.inet_aton(iface))

    def set_ttl(self, ttl: int):
        """Change the multicast TTL (1 = subnet only)."""
        import socket as _socket, struct
        self._ttl = ttl
        self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_TTL,
                               struct.pack('B', ttl))

    def set_loopback(self, enable: bool):
        """Enable/disable local loopback."""
        import socket as _socket, struct
        self._loopback = enable
        self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_LOOP,
                               struct.pack('B', 1 if enable else 0))

    def set_interface(self, ip: Optional[str]):
        """Bind outgoing multicast to a specific interface by IP."""
        import socket as _socket
        addr = _socket.inet_aton(ip) if ip else _socket.inet_aton("0.0.0.0")
        self._sock.setsockopt(_socket.IPPROTO_IP, _socket.IP_MULTICAST_IF, addr)

    # Override send_recv — multicast is send-only; replies won't arrive
    def send_recv(self, cmd: str, *args, **kwargs) -> Optional[str]:
        """Multicast clients cannot receive replies. Sends and returns None."""
        self.send(cmd)
        return None

    # win_new via multicast makes no sense (reply needed) — warn instead
    def win_new(self, *args, **kwargs) -> int:
        raise NotImplementedError(
            "win_new() requires a unicast connection (needs reply). "
            "Use FbNet() for setup, then FbNetMulticast() for rendering."
        )

    def ping(self, *args, **kwargs):
        raise NotImplementedError("Multicast clients cannot receive ping replies.")

    @property
    def group(self) -> str:
        return self._group

    @property
    def is_multicast(self) -> bool:
        return True


def multicast_subscribe(cl: FbNet, group: str) -> bool:
    """
    Ask a unicast server to join a multicast group at runtime.

    After this call, the server will also receive packets sent to @group.

    Example:
        # Connect to one server via unicast
        with FbNet("192.168.1.10", 9876) as cl:
            multicast_subscribe(cl, FbNetMulticast.FB_NET_MCAST_ALL)

        # Now broadcast to it (and any others in the group) via multicast
        with FbNetMulticast(FbNetMulticast.FB_NET_MCAST_ALL, 9876) as mc:
            mc.clear(Color.BLACK)
            mc.flush()
    """
    reply = cl.send_recv(f"subscribe,{group}")
    return bool(reply and reply.startswith("ok,subscribe"))


def multicast_unsubscribe(cl: FbNet, group: str) -> bool:
    """Ask a unicast server to leave a multicast group."""
    reply = cl.send_recv(f"unsubscribe,{group}")
    return bool(reply and reply.startswith("ok,unsubscribe"))


def multicast_groups(cl: FbNet) -> list:
    """Return the multicast groups a server has joined."""
    reply = cl.send_recv("groups")
    if not reply or not reply.startswith("ok,groups,"):
        return []
    rest = reply[len("ok,groups,"):]
    return [g for g in rest.split(",") if g]


# ── Command-line quick-send tool ────────────────────────────────────

if __name__ == "__main__":
    import sys
    import argparse

    parser = argparse.ArgumentParser(
        description="Send fbcurses UDP commands from the command line.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Ping the server
  python3 fbnet_client.py 192.168.1.10 9876 ping

  # Create a window and write text
  python3 fbnet_client.py 127.0.0.1 9876 win_new,2,2,60,10
  python3 fbnet_client.py 127.0.0.1 9876 'print_at,1,4,3,"Hello from Python!"'
  python3 fbnet_client.py 127.0.0.1 9876 win_refresh,1 flush

  # Multiple commands in one call (batched into one datagram)
  python3 fbnet_client.py 127.0.0.1 9876 \\
    'colors,1,#00FF88,black' \\
    'print_at,1,2,5,"Python is driving the framebuffer!"' \\
    win_refresh,1 flush
        """,
    )
    parser.add_argument("host",    help="Server hostname or IP")
    parser.add_argument("port",    type=int, help="UDP port")
    parser.add_argument("cmds",    nargs="+", help="Command(s) to send")
    parser.add_argument("--timeout", type=float, default=0.3,
                        help="Reply timeout in seconds (default 0.3)")
    args = parser.parse_args()

    with FbNet(args.host, args.port, timeout=args.timeout) as fb:
        if len(args.cmds) == 1:
            # Single command — might expect a reply
            cmd = args.cmds[0]
            reply = fb.send_recv(cmd)
            if reply:
                print(reply)
        else:
            # Multiple commands — batch them
            with fb.batch():
                for cmd in args.cmds:
                    fb.send(cmd)
