/*
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

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

	// Multicast
	fbNetClient *mc = fbncOpenMcast(FB_NET_MCAST_ALL, port);
	fbncClear(mc, 0x000000);
	fbncRefreshAllFlush(mc);
	fbncClose(mc);

	return 0;
}
