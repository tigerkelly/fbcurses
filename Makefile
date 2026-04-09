# fbcurses -- Linux Framebuffer TUI Library

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
          -DFBIMAGE_PNG -DFBIMAGE_JPEG \
          $(shell pkg-config --cflags libavcodec libavformat libswscale libavutil libpng libjpeg 2>/dev/null)
LDFLAGS = -lm \
          $(shell pkg-config --libs libavcodec libavformat libswscale libavutil libpng libjpeg 2>/dev/null)
AR      = ar

LIB_SRC = fbcurses.c strqtok_r.c qparse.c fbimage.c fbvideo.c font8x16.c font_12x24.c font_16x32.c font_24x48.c boxdraw.c fonts.c font_bold8.c font_thin5.c font_narrow6.c font_block8.c font_lcd7.c font_block8x8.c font_tall8x14.c font_thin6x12.c font_wide16x16.c widgets.c dialogs.c fbnet.c fbnet_client.c
LIB_OBJ = $(LIB_SRC:.c=.o)
LIB     = libfbcurses.a

DEMOS   = demo font_demo net_demo

.PHONY: all clean install

all: $(LIB) $(DEMOS)

# Library
$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^
	ranlib $@

# Demo programs
demo: demo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -lfbcurses $(LDFLAGS)

font_demo: font_demo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -lfbcurses $(LDFLAGS)

net_demo: net_demo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -lfbcurses $(LDFLAGS)

unicast: unicast.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -lfbcurses $(LDFLAGS)

quickstart: quickstart.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -lfbcurses $(LDFLAGS)

# Compile rule
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Install
PREFIX ?= /usr/local

install: $(LIB)
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 644 fbcurses.h      $(DESTDIR)$(PREFIX)/include/
	install -m 644 fonts.h         $(DESTDIR)$(PREFIX)/include/
	install -m 644 fbnet.h         $(DESTDIR)$(PREFIX)/include/
	install -m 644 fbnet_client.h  $(DESTDIR)$(PREFIX)/include/
	install -m 644 fbimage.h        $(DESTDIR)$(PREFIX)/include/
	install -m 644 fbvideo.h        $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(LIB)          $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -f $(LIB_OBJ) $(LIB) $(DEMOS)
