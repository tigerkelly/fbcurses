#!/usr/bin/env python3
"""
examples/multicast_demo.py — Drive multiple fbcurses displays simultaneously.

Start net_demo on several machines (or several virtual consoles on one machine).
Each server automatically joins the "239.76.66.49" multicast group on startup.

Then run this script to send the same commands to all of them at once.

Usage:
    # On each display machine:
    sudo ./net_demo

    # On the control machine (or locally):
    python3 examples/multicast_demo.py [group] [port]

    # Override defaults:
    python3 examples/multicast_demo.py 239.76.66.49 9876

Topology:
    Control machine
          |
          | UDP multicast (239.76.66.49:9876)
          |
    ------+-------+-------+-------
          |       |       |
       Display1 Display2 Display3   (all receive the same packets)

Notes:
  - All machines must be on the same subnet (TTL=1)
  - Multicast must be enabled on the switch/router
  - For cross-subnet use: increase TTL with fb.set_ttl(32)
  - For local testing: use loopback=True to see your own packets
"""

import sys
import time
import math
import signal

sys.path.insert(0, '.')
from fbnet_client import (
    FbNet, FbNetMulticast, Color, Border, Align, Font, Toast,
    multicast_subscribe, multicast_groups
)

GROUP = sys.argv[1] if len(sys.argv) > 1 else FbNetMulticast.FB_NET_MCAST_ALL
PORT  = int(sys.argv[2]) if len(sys.argv) > 2 else 9876

running = True
def _stop(s, f): global running; running = False
signal.signal(signal.SIGINT, _stop)


def setup_unicast(host: str, port: int):
    """
    Connect to one known server via unicast to:
    1. Query its screen size (so we know what dimensions to use)
    2. Create windows (win_new needs a reply)
    3. Optionally subscribe it to the multicast group if not already joined
    """
    with FbNet(host, port) as cl:
        rtt = cl.ping()
        if rtt is None:
            print(f"  No response from {host}:{port}")
            return None, None, None

        size = cl.screen_size()
        print(f"  {host}:{port}  RTT={rtt:.0f}ms  size={size}")

        # Check/subscribe to multicast group
        groups = multicast_groups(cl)
        print(f"  Groups: {groups or '(none)'}")
        if GROUP not in groups:
            ok = multicast_subscribe(cl, GROUP)
            print(f"  Subscribed to {GROUP}: {'ok' if ok else 'failed'}")

        # Create the main window via unicast (needs reply for ID)
        win = cl.win_new(0, 0, size[2], size[3])
        print(f"  Created window {win}")

        return size[2], size[3], win  # cols, rows, win_id


def run_slideshow(fb: FbNetMulticast, cols: int, rows: int, win: int):
    """
    Push a series of screens to all displays simultaneously via multicast.
    All displays receive identical commands and render identically.
    """

    def screen(title: str, color_fn):
        """Helper: clear + title bar + content + flush"""
        fb.batch_begin()
        fb.win_clear(win, 0x050A0F)
        fb.title_bar(win, title, Border.DOUBLE,
                     Color.CYAN, Color.BLACK, Color.CYAN)
        color_fn()
        fb.win_refresh(win)
        fb.flush()
        fb.batch_end()

    # ── Slide 1: Welcome ────────────────────────────────────────
    def slide1():
        fb.colors(win, Color.WHITE, 0x050A0F)
        fb.print_align(win, rows // 2 - 1, Align.CENTER,
                       "fbcurses Multicast Broadcasting")
        fb.colors(win, Color.GRAY, 0x050A0F)
        fb.print_align(win, rows // 2,     Align.CENTER,
                       f"Group: {GROUP}  |  Port: {PORT}")
        fb.print_align(win, rows // 2 + 1, Align.CENTER,
                       "All connected displays show this simultaneously")

    screen("Multicast Demo", slide1)
    time.sleep(2.5)
    if not running: return

    # ── Slide 2: Colour fill animation ──────────────────────────
    print("Slide 2: colour animation")
    for i in range(30):
        if not running: return
        hue = i / 30.0 * 360.0
        # HSV to RGB
        h6 = hue / 60.0
        hi = int(h6) % 6
        f_ = h6 - int(h6)
        v = 0.6
        p, q, t = v*0, v*(1-f_), v*f_
        v = v
        rgb = [(v,t,p),(q,v,p),(p,v,t),(p,q,v),(t,p,v),(v,p,q)][hi]
        col = Color.rgb(int(rgb[0]*255), int(rgb[1]*255), int(rgb[2]*255))

        with fb.batch():
            fb.win_clear(win, col)
            fb.colors(win, 0x000000, col)
            fb.print_align(win, rows//2, Align.CENTER, f"Colour {int(hue):3d}deg")
            fb.win_refresh(win)
            fb.flush()
        time.sleep(0.08)

    time.sleep(1.0)
    if not running: return

    # ── Slide 3: Sparkline + progress bars ──────────────────────
    print("Slide 3: live data widgets")
    with fb.batch():
        fb.win_clear(win, 0x050A0F)
        fb.title_bar(win, "Live Data — All Displays", Border.THICK,
                     Color.GREEN, Color.BLACK, Color.GREEN)
        fb.win_refresh(win)
        fb.flush()

    hist = [0.0] * (cols - 10)
    for tick in range(80):
        if not running: return
        val = 0.5 + 0.45 * math.sin(tick * 0.2)
        hist.pop(0); hist.append(val)

        pct_cpu = int(val * 100)
        pct_mem = int((0.5 + 0.3 * math.sin(tick * 0.07 + 1.0)) * 100)
        col_cpu = Color.rgb(int(val*200), int((1-val)*200), 30)
        col_mem = Color.BRIGHT_BLUE

        with fb.batch():
            fb.colors(win, Color.GRAY, 0x050A0F)
            fb.print_at(win, 2, 3, "CPU")
            fb.progress(win, 7, 3, cols - 10, pct_cpu, col_cpu, 0x101010, True)
            fb.print_at(win, 2, 5, "MEM")
            fb.progress(win, 7, 5, cols - 10, pct_mem, col_mem, 0x101010, True)
            fb.print_at(win, 2, 7, "CPU history:")
            fb.sparkline(win, 15, 7, cols - 17, Color.CYAN, 0x050A0F, hist)
            fb.win_refresh(win)
            fb.flush()
        time.sleep(0.1)

    time.sleep(1.0)
    if not running: return

    # ── Slide 4: Text showcase with multiple fonts ───────────────
    print("Slide 4: multi-font text")
    with fb.batch():
        fb.win_clear(win, 0x050A0F)
        fb.title_bar(win, "Pixel-Accurate Multi-Font Rendering",
                     Border.ROUNDED, Color.YELLOW, Color.BLACK, Color.YELLOW)
        y = 40
        for font, col, label in [
            (Font.VGA,     Color.WHITE,          "VGA 8x16 (default)"),
            (Font.BOLD8,   Color.BRIGHT_YELLOW,  "Bold 8x16"),
            (Font.THIN5,   Color.BRIGHT_CYAN,    "Thin 5x8"),
            (Font.NARROW6, Color.BRIGHT_GREEN,   "Narrow 6x12"),
            (Font.BLOCK8,  Color.BRIGHT_RED,     "Block 8x16"),
            (Font.LCD7,    Color.BRIGHT_CYAN,    "LCD7 0123456789"),
            (Font.CGA8,    Color.GRAY,           "CGA 8x8 compact"),
        ]:
            fb.print_px(40, y, label, col, 0x050A0F, 0, font)
            y += 22
        fb.win_refresh(win)
        fb.flush()
    time.sleep(3.0)
    if not running: return

    # ── Slide 5: Farewell ────────────────────────────────────────
    print("Slide 5: farewell")
    with fb.batch():
        fb.win_clear(win, 0x050A0F)
        fb.title_bar(win, "Multicast Demo Complete", Border.DOUBLE,
                     Color.MAGENTA, Color.BLACK, Color.MAGENTA)
        fb.colors(win, Color.WHITE, 0x050A0F)
        fb.print_align(win, rows // 2 - 1, Align.CENTER,
                       "Sent to all displays simultaneously")
        fb.colors(win, Color.GRAY, 0x050A0F)
        fb.print_align(win, rows // 2 + 1, Align.CENTER,
                       "One packet, many screens.")
        fb.win_refresh(win)
        fb.flush()
    time.sleep(2.0)


def main():
    print(f"fbcurses Multicast Demo")
    print(f"Group : {GROUP}")
    print(f"Port  : {PORT}")
    print()

    # Try to connect to a local server first to get screen dimensions
    # and verify at least one server is reachable
    print("Probing local server for screen dimensions...")
    cols, rows, win = None, None, None
    for host in ["127.0.0.1", "localhost"]:
        cols, rows, win = setup_unicast(host, PORT)
        if cols: break

    if not cols:
        # Fall back to common defaults if no local server responds
        print("No local server found — assuming 1920x1080 / 240x67")
        cols, rows, win = 240, 67, 1

    print(f"\nStarting multicast broadcast to {GROUP}:{PORT}")
    print("Press Ctrl-C to stop.\n")

    # Open multicast sender
    # loopback=True so the local server also receives our own packets
    with FbNetMulticast(GROUP, PORT, ttl=1, loopback=True) as fb:
        run_slideshow(fb, cols, rows, win)

    print("Done.")


if __name__ == "__main__":
    main()
