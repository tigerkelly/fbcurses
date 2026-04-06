/*
 * fonts.c — Font registry: wraps the VGA bitmap data in an fbFont
 *           descriptor, and provides the global fbFontList[] array.
 */

#include "fonts.h"
#include <stddef.h>  /* NULL */
#include "font8x16.h"         /* _fbFont[256][16] — the raw VGA data  */

/* ─── VGA descriptor (wraps the existing font8x16 data) ─────────── */
const fbFont fbVga = {
    .name = "VGA8x16",
    .w    = 8,
    .h    = 16,
    .data = _fbFont,
};

/* ─── Font list ──────────────────────────────────────────────────── */
/* Defined in their respective font_*.c files */
extern const fbFont fbBold8;
extern const fbFont fbThin5;
extern const fbFont fbNarrow6;
extern const fbFont fbBlock8;
extern const fbFont fbLcd7;

const fbFont * const fbFontList[] = {
    &fbVga,
    &fbBold8,
    &fbThin5,
    &fbNarrow6,
    &fbBlock8,
    &fbLcd7,
    NULL,
};

const int fbFontCount = 6;
