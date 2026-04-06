#!/usr/bin/env python3
"""
examples/remote_dashboard.py — Live dashboard driven over UDP.

Connects to an fbcurses net_demo server and draws an animated dashboard
with multiple windows, progress bars, sparklines, a clock, and a table.

Usage:
    # Terminal 1 (on the machine with the framebuffer):
    sudo ./net_demo

    # Terminal 2 (anywhere on the network):
    python3 examples/remote_dashboard.py 192.168.1.10 9876

    # Or locally:
    python3 examples/remote_dashboard.py 127.0.0.1 9876

Press Ctrl-C to stop.
"""

import sys
import time
import math
import random
import signal
from datetime import datetime

# Add parent directory to path so fbnet_client.py can be found
sys.path.insert(0, '.')
from fbnet_client import FbNet, Color, Border, Align, Font, Toast, Attr

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9876

running = True
def _sigint(s, f): global running; running = False
signal.signal(signal.SIGINT, _sigint)


def lerp_color(a: int, b: int, t: float) -> int:
    """Linearly interpolate between two colours."""
    ar, ag, ab_ = (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF
    br, bg, bb_ = (b >> 16) & 0xFF, (b >> 8) & 0xFF, b & 0xFF
    return Color.rgb(
        int(ar + (br - ar) * t),
        int(ag + (bg - ag) * t),
        int(ab_ + (bb_ - ab_) * t),
    )


class Dashboard:
    def __init__(self, fb: FbNet):
        self.fb = fb
        self.tick = 0

        # Query server dimensions
        size = fb.screen_size()
        if not size:
            print("Could not reach server — is net_demo running?")
            sys.exit(1)
        self.pW, self.pH, self.cols, self.rows = size
        print(f"Connected: {self.cols}×{self.rows} chars, "
              f"{self.pW}×{self.pH} pixels")

        # Data history
        self.hist_len = 40
        self.cpu_hist  = [0.0] * self.hist_len
        self.mem_hist  = [0.0] * self.hist_len
        self.net_hist  = [0.0] * self.hist_len
        self.disk_hist = [0.0] * self.hist_len

        self._create_windows()

    def _create_windows(self):
        fb = self.fb
        C, R = self.cols, self.rows

        # Main border
        self.w_outer = fb.win_new(0, 0, C, R)
        fb.win_clear(self.w_outer, 0x040810)
        fb.title_bar(self.w_outer, "fbcurses Remote Dashboard",
                     Border.DOUBLE, Color.CYAN, 0x040810, Color.CYAN)
        fb.win_refresh(self.w_outer)
        fb.flush()

        # Clock panel (top right)
        cw = 20
        self.w_clock = fb.win_new(C - cw - 2, 2, cw, 4)

        # Gauges panel (left side)
        self.w_gauges = fb.win_new(2, 2, 14, R - 4)

        # Main content area
        self.w_main = fb.win_new(17, 2, C - 20, R - 4)

        # Table panel
        table_h = 10
        self.w_table = fb.win_new(17, R - table_h - 2, C - 20, table_h)

        fb.flush()

    def _update_data(self):
        """Simulate sensor data with sine waves and noise."""
        t = self.tick * 0.1
        def wave(freq, phase, noise=0.15):
            v = 0.5 + 0.4 * math.sin(t * freq + phase)
            v += random.gauss(0, noise)
            return max(0.0, min(1.0, v))

        self.cpu_hist.pop(0);  self.cpu_hist.append(wave(1.0, 0.0))
        self.mem_hist.pop(0);  self.mem_hist.append(wave(0.3, 1.2, 0.05))
        self.net_hist.pop(0);  self.net_hist.append(wave(2.5, 2.4))
        self.disk_hist.pop(0); self.disk_hist.append(wave(0.15, 0.8, 0.03))

    def _draw_clock(self):
        fb = self.fb
        now = datetime.now()
        with fb.batch():
            fb.win_clear(self.w_clock, 0x040810)
            fb.colors(self.w_clock, Color.BRIGHT_CYAN, 0x040810)
            fb.attr(self.w_clock, Attr.BOLD)
            fb.print_at(self.w_clock, 1, 1, now.strftime("%H:%M:%S"))
            fb.colors(self.w_clock, Color.GRAY, 0x040810)
            fb.attr(self.w_clock, Attr.NONE)
            fb.print_at(self.w_clock, 1, 2, now.strftime("%a %d %b %Y"))
            fb.win_refresh(self.w_clock)

    def _draw_gauges(self):
        fb = self.fb
        with fb.batch():
            fb.win_clear(self.w_gauges, 0x040810)
            fb.draw_border = lambda: None  # placeholder
            fb.border(self.w_gauges, Border.SINGLE, Color.GRAY)

            labels = ["CPU", "MEM", "NET", "DSK"]
            values = [self.cpu_hist[-1], self.mem_hist[-1],
                      self.net_hist[-1], self.disk_hist[-1]]
            colors = [Color.BRIGHT_CYAN, Color.BRIGHT_GREEN,
                      Color.BRIGHT_YELLOW, Color.BRIGHT_MAGENTA]
            gaugeH = 8

            for i, (lbl, val, col) in enumerate(zip(labels, values, colors)):
                gc = 2 + i * 3
                fb.colors(self.w_gauges, Color.GRAY, 0x040810)
                fb.attr(self.w_gauges, Attr.NONE)
                fb.print_at(self.w_gauges, gc, 1, lbl[:2])
                fb.gauge(self.w_gauges, gc, 2, gaugeH,
                         int(val * 100), 100, col, 0x0A0A14)
                pct_col = lerp_color(Color.GREEN, Color.RED, val)
                fb.colors(self.w_gauges, pct_col, 0x040810)
                fb.print_at(self.w_gauges, gc, gaugeH + 2, f"{int(val*100):2d}")

            fb.win_refresh(self.w_gauges)

    def _draw_main(self):
        fb = self.fb
        W = self.cols - 20
        with fb.batch():
            fb.win_clear(self.w_main, 0x040810)

            row = 1
            sparkW = W - 12

            for name, hist, col in [
                ("CPU avg", self.cpu_hist, Color.BRIGHT_CYAN),
                ("Memory ", self.mem_hist, Color.BRIGHT_GREEN),
                ("Network", self.net_hist, Color.BRIGHT_YELLOW),
                ("Disk   ", self.disk_hist, Color.BRIGHT_MAGENTA),
            ]:
                val = hist[-1]
                pct_col = lerp_color(Color.GREEN, Color.RED, val)
                fb.colors(self.w_main, Color.GRAY, 0x040810)
                fb.print_at(self.w_main, 1, row, name)
                fb.colors(self.w_main, pct_col, 0x040810)
                fb.attr(self.w_main, Attr.BOLD)
                fb.print_at(self.w_main, 9, row, f"{int(val*100):3d}%")
                fb.attr(self.w_main, Attr.NONE)
                fb.sparkline(self.w_main, 14, row, sparkW, col, 0x040810, hist)
                row += 2

            # Animated progress bar
            anim_val = int(50 + 45 * math.sin(self.tick * 0.08))
            anim_col = lerp_color(Color.BLUE, Color.RED, anim_val / 100)
            fb.colors(self.w_main, Color.GRAY, 0x040810)
            fb.print_at(self.w_main, 1, row, "Animated")
            fb.progress(self.w_main, 10, row, W - 12,
                        anim_val, anim_col, 0x0A0A14, True)
            row += 2

            # Spinner row
            fb.colors(self.w_main, Color.GRAY, 0x040810)
            fb.print_at(self.w_main, 1, row, "Active:  ")
            for i in range(8):
                fb.tick(self.w_main, 10 + i * 3, row, Color.CYAN, 0x040810)

            fb.win_refresh(self.w_main)

    def _draw_table(self):
        fb = self.fb
        processes = [
            ["fbcurses",   "1042", f"{self.cpu_hist[-1]*2:.1f}",  "1.1"],
            ["net_demo",   "2001", f"{self.mem_hist[-1]*3:.1f}",  "0.6"],
            ["python3",    "2314", f"{self.net_hist[-1]*5:.1f}",  "2.2"],
            ["Xorg",       "987",  f"{self.disk_hist[-1]*4:.1f}", "8.2"],
            ["systemd",    "1",    "0.0",                          "1.8"],
        ]
        sel = self.tick % len(processes)
        with fb.batch():
            fb.table(
                self.w_table, 1, 1,
                cols=[
                    ("Process", 10, Align.LEFT),
                    ("PID",      6, Align.RIGHT),
                    ("CPU%",     6, Align.RIGHT),
                    ("MEM%",     6, Align.RIGHT),
                ],
                rows=processes,
                sel_row=sel,
                header_fg=Color.BRIGHT_CYAN, header_bg=0x0A1E28,
                cell_fg=Color.WHITE,          cell_bg=0x050C14,
                sel_fg=Color.BLACK,           sel_bg=Color.BRIGHT_GREEN,
            )
            fb.win_refresh(self.w_table)

    def update(self):
        self._update_data()
        self._draw_clock()
        self._draw_gauges()
        self._draw_main()
        self._draw_table()
        self.fb.refresh_all_flush()
        self.tick += 1


def main():
    with FbNet(HOST, PORT, timeout=1.0) as fb:
        rtt = fb.ping()
        if rtt is None:
            print(f"No response from {HOST}:{PORT}")
            sys.exit(1)
        print(f"Server ping: {rtt:.1f} ms")

        ver = fb.version()
        fonts = fb.fonts()
        print(f"Server version: {ver}")
        print(f"Available fonts: {', '.join(fonts)}")

        fb.clear(Color.BLACK)
        fb.flush()

        dash = Dashboard(fb)

        print("Dashboard running. Press Ctrl-C to stop.")
        frame_time = 0.25  # 4 fps

        while running:
            t0 = time.monotonic()
            dash.update()
            elapsed = time.monotonic() - t0
            sleep = frame_time - elapsed
            if sleep > 0:
                time.sleep(sleep)

        # Cleanup
        fb.clear(Color.BLACK)
        fb.flush()
        print(f"Stopped after {dash.tick} frames.")


if __name__ == "__main__":
    main()
