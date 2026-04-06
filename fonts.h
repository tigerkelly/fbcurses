#ifndef FBCURSES_FONTS_H
#define FBCURSES_FONTS_H

/*
 * fonts.h — Font descriptor and built-in font declarations for fbcurses.
 *
 * Each font is described by an fbFont struct that carries its name,
 * pixel dimensions, and a pointer to its 256-glyph bitmap table.
 *
 * Glyph data layout:
 *   data[c][row]  — one byte per row, MSB = leftmost pixel.
 *   Width  w: only the top w bits of each byte are used.
 *   Height h: h rows per glyph.
 *
 * The active font for a window is set with fbSetFont().
 * Character-cell dimensions (cols, rows) of a window are recomputed
 * automatically whenever the font changes.
 */

#include <stdint.h>

typedef struct {
    const char     *name;           /* human-readable label             */
    int             w;              /* glyph width  in pixels           */
    int             h;              /* glyph height in pixels           */
    const void     *data;            /* data[256][h] — cast to uint8_t (*)[h] in renderer */
} fbFont;

/* ─── Built-in fonts ─────────────────────────────────────────────── */

/** fbVga — classic 8×16 VGA/PC BIOS bitmap font (the original default) */
extern const fbFont fbVga;

/** fbBold8 — 8×16 bold variant: stroke width doubled by bit-smear    */
extern const fbFont fbBold8;

/** fbThin5 — compact 5×8 font: fits ~192 cols on a 1080p screen      */
extern const fbFont fbThin5;

/** fbNarrow6 — 6×12 narrow font: good for log/info side-panels       */
extern const fbFont fbNarrow6;

/** fbBlock8 — 8×16 chunky pixel-art / retro block letterforms        */
extern const fbFont fbBlock8;

/** fbLcd7 — 8×16 seven-segment LCD style: great for clocks/counters  */
extern const fbFont fbLcd7;


/** fbCga8     — 8×8 CGA-style compact font (fits ~135 cols on 1080p)    */
extern const fbFont fbCga8;

/** fbThin6x12 — 6×12 thin condensed font                                */
extern const fbFont fbThin6x12;

/** fbTall8x14 — 8×14 tall font with mild serif hints                    */
extern const fbFont fbTall8x14;

/** fbWide     — 8×16 wide display/title font with thick strokes         */
extern const fbFont fbWide;


/* ── Large scaled fonts ─────────────────────────────────────────── */

/** fbFont12x24 — 12×24 scaled VGA with diagonal smoothing           */
extern const fbFont fbFont12x24;

/** fbFont16x32 — 16×32 scaled VGA with diagonal smoothing           */
extern const fbFont fbFont16x32;

/** fbFont24x48 — 24×48 scaled VGA (3× nearest-neighbour)           */
extern const fbFont fbFont24x48;

/* ── Pixel-height helper ─────────────────────────────────────────── */
/**
 * FB_FONT_PX_H(font) — actual pixel height of a font.
 *
 * For fonts with w ≤ 8 this equals font->h directly.
 * For wide fonts (w > 8) font->h stores bytes-per-glyph, so divide
 * by ceil(w/8) to get the true pixel row count.
 */
#define FB_FONT_BPR(f)   (((f)->w + 7) / 8)
#define FB_FONT_PX_H(f)  ((f)->h / FB_FONT_BPR(f))

/* ─── Font list (NULL-terminated, for enumeration) ───────────────── */
extern const fbFont * const fbFontList[];
extern const int             fbFontCount;

#endif /* FBCURSES_FONTS_H */
