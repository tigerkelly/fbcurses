/*
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

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
