/*
 * font8x16.h — Built-in 8×16 bitmap font (VGA ROM / Terminus derived)
 *
 * Each entry is FB_FONT_H (16) bytes.  Each byte is one pixel row,
 * MSB = leftmost column.  Glyphs 0x00–0x1F and 0x80–0xFF are blank
 * (rendered as spaces); 0x20–0x7E are printable ASCII.
 */

#ifndef FONT8X16_H
#define FONT8X16_H

#include <stdint.h>
#include <stdint.h>

/* Forward declaration only — definition in font8x16.c */
extern const uint8_t _fbFont[256][16];

#endif /* FONT8X16_H */
