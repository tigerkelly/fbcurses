/*
 * font_demo.c — fbcurses font showcase + widget demonstrations.
 *
 * Build together with the rest of the library:
 *   make
 *   sudo ./font_demo
 *
 * Shows every built-in font, then demonstrates the new widgets.
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>

static void sleepMs(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ─── Shared footer ──────────────────────────────────────────────── */
static void footer(fbWindow *win, const char *msg)
{
    int h = fbWindowRows(win);
    int w = fbWindowCols(win);
    fbSetColors(win, FB_GRAY, FB_BLACK);
    fbSetAttr(win, FB_ATTR_NONE);
    /* Clear footer row with spaces */
    fbMoveCursor(win, 1, h - 2);
    for (int c = 1; c < w - 1; c++) fbAddChar(win, ' ');
    fbPrintAligned(win, h - 2, FB_ALIGN_CENTER, msg);
}

/* ═══════════════════════════════════════════════════════════════════
 *  PAGE 1: Font Showcase — every built-in font at its true pixel size
 *
 *  Each font sample is drawn with fbDrawTextPx() at exact pixel coords,
 *  bypassing the VGA cell grid so glyphs render at their real dimensions.
 * ═══════════════════════════════════════════════════════════════════ */
static void pageFonts(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);

    /* Use VGA window only for the chrome (title bar, labels, footer) */
    fbWindow *outer = fbNewWindow(scr, 0, 0, W, H);
    if (!outer) return;

    fbClearWindow(outer, FB_BLACK);
    fbDrawTitleBar(outer, "Built-in Fonts  (rendered at true pixel dimensions)",
                   FB_BORDER_DOUBLE, FB_CYAN, FB_BLACK, FB_CYAN);
    footer(outer, "Press any key for next pageâ¦");

    /* Column header labels (in VGA) */
    fbSetColors(outer, FB_GRAY, FB_BLACK);
    fbSetAttr(outer, FB_ATTR_NONE);
    fbPrintAt(outer, 2,  2, "Font");
    fbPrintAt(outer, 16, 2, "Size");
    fbPrintAt(outer, 23, 2, "Sample text");
    fbPrintAt(outer, 55, 2, "Extended");

    /* Separator line */
    fbSetColors(outer, FB_GRAY, FB_BLACK);
    fbMoveCursor(outer, 1, 3);
    for (int c = 1; c < W - 1; c++) fbAddWchar(outer, L'─');

    fbRefresh(outer);   /* draw chrome into back-buffer first */

    static const struct {
        const fbFont *font;
        fbColor       accent;
        const char   *sample;
        const char   *extended;
    } demos[] = {
        { &fbVga,       FB_WHITE,         "AaBbCcDd 0123456789 !@#$", "The quick brown fox" },
        { &fbBold8,     FB_BRIGHT_YELLOW, "AaBbCcDd 0123456789 !@#$", "The quick brown fox" },
        { &fbThin5,     FB_CYAN,          "AaBbCcDd 0123456789 !@#$", "The quick brown fox" },
        { &fbNarrow6,   FB_BRIGHT_GREEN,  "AaBbCcDd 0123456789 !@#$", "The quick brown fox" },
        { &fbBlock8,    FB_BRIGHT_RED,    "AaBbCcDd 0123456789 !@#$", "The quick brown fox" },
        { &fbLcd7,      FB_BRIGHT_CYAN,   "0123456789 HELLO",          "ABCDEFGHIJKLMNOPQRSTU"},
        { &fbCga8,      FB_GRAY,          "AaBbCcDd 0123456789",       "compact 8x8"         },
        { &fbTall8x14,  FB_BRIGHT_GREEN,  "AaBbCcDd 0123456789",       "tall 8x14"           },
        { &fbWide,      FB_BRIGHT_YELLOW, "AaBbCcDd 0123",             "wide thick"          },
        /* Large fonts — shown only if they fit on screen */
        { &fbFont12x24, FB_BRIGHT_CYAN,   "AaBbCc 01234",             "12x24 smooth"        },
        { &fbFont16x32, FB_BRIGHT_MAGENTA,"AaBb 012",                  "16x32 smooth"        },
        { &fbFont24x48, FB_BRIGHT_RED,    "Aa 012",                    "24x48"               },
    };
    int nDemos = (int)(sizeof demos / sizeof demos[0]);

    /* Row spacing: each entry gets exactly its font height + 6px gap,
       so the table is tight but the large fonts still fit.
       First row starts at y=56 (3-row chrome = 48px + 8px gap).     */
    int baseY = 58;   /* pixel y of first sample row                  */
    int labelX = 16;  /* pixel x of the font-name label column        */
    int sizeX  = 128; /* pixel x of the "NxM" column                  */
    int sampleX = 184;/* pixel x where sample text begins             */
    int extX   = 440; /* pixel x of extended sample column            */

    int py = baseY;
    for (int i = 0; i < nDemos; i++) {
        const fbFont *font   = demos[i].font;
        fbColor       accent = demos[i].accent;
        int           pxH   = font->h / ((font->w + 7) / 8);  /* true pixel height */

        if (py + pxH > fbHeight(scr) - 24) break;

        /* Font name label — always in VGA */
        fbDrawTextPx(scr, labelX, py, font->name,
                     FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);

        /* Size label: actual pixel dimensions */
        char sizeBuf[16];
        snprintf(sizeBuf, sizeof(sizeBuf), "%dx%d", font->w, pxH);
        fbDrawTextPx(scr, sizeX, py, sizeBuf,
                     FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);

        /* Sample text at the font's true size */
        fbDrawTextPx(scr, sampleX, py, demos[i].sample,
                     accent, FB_BLACK, FB_ATTR_NONE, font);

        /* Extended sample — dimmed, only if there is room */
        if (extX + (int)strlen(demos[i].extended) * font->w < fbWidth(scr) - 8)
            fbDrawTextPx(scr, extX, py, demos[i].extended,
                         fbLerp(accent, FB_BLACK, 0.35f), FB_BLACK,
                         FB_ATTR_NONE, font);

        py += pxH + 6;   /* advance by this font's pixel height + gap */
    }

    /* Attribute showcase — drawn after the last font row */
    int attrY = py + 10;
    if (attrY + 20 < fbHeight(scr)) {
        fbDrawTextPx(scr, labelX, attrY,
                     "Attributes: ", FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);
        int ax = labelX + 13 * fbVga.w;
        ax = fbDrawTextPx(scr, ax, attrY, "Normal ",
                          FB_WHITE, FB_BLACK, FB_ATTR_NONE, &fbVga);
        ax = fbDrawTextPx(scr, ax, attrY, "Bold ",
                          FB_WHITE, FB_BLACK, FB_ATTR_BOLD, &fbVga);
        ax = fbDrawTextPx(scr, ax, attrY, "Dim ",
                          FB_WHITE, FB_BLACK, FB_ATTR_DIM, &fbVga);
        ax = fbDrawTextPx(scr, ax, attrY, "Underline ",
                          FB_CYAN, FB_BLACK, FB_ATTR_UNDERLINE, &fbVga);
        fbDrawTextPx(scr, ax, attrY, "Reverse",
                     FB_BLACK, FB_CYAN, FB_ATTR_REVERSE, &fbVga);
    }

    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(outer);
}


/* ═══════════════════════════════════════════════════════════════════
 *  PAGE: Large Fonts — 12x24, 16x32, 24x48 close-up showcase
 *
 *  Each large font gets its own horizontal band.  We show:
 *    - The font name and pixel dimensions
 *    - A full alphabet sample at true size
 *    - The digits 0-9
 *    - A pixel ruler so the size is visually obvious
 * ═══════════════════════════════════════════════════════════════════ */
static void pageLargeFonts(fbScreen *scr)
{
    int PW = fbWidth(scr), PH = fbHeight(scr);

    /* ── Safe drawing area ──────────────────────────────────────────
       Title bar  = 3 VGA rows  = 48px from top
       Footer     = 2 VGA rows  = 32px from bottom
       Left/right margin        = 16px each side                      */
    const int MARGIN   = 16;
    const int TOP_PX   = 48;                    /* below title bar    */
    const int BOT_PX   = PH - 32;              /* above footer       */
    const int LEFT_PX  = MARGIN;
    const int RIGHT_PX = PW - MARGIN;

    /* Clear the entire back buffer first so previous page's
       direct fbDrawTextPx pixels don't bleed through.               */
    fbClear(scr, FB_BLACK);

    /* Chrome window: title bar + footer only (VGA cell grid).
       Draw AFTER fbClear so it overwrites the cleared buffer.        */
    fbWindow *chrome = fbNewWindow(scr, 0, 0, fbCols(scr), fbRows(scr));
    if (!chrome) return;
    fbClearWindow(chrome, FB_BLACK);
    fbDrawTitleBar(chrome, "Large Fonts  \xe2\x80\x94  12x24 / 16x32 / 24x48",
                   FB_BORDER_DOUBLE, FB_BRIGHT_YELLOW, FB_BLACK, FB_BRIGHT_YELLOW);
    footer(chrome, "Press any key for next page\xe2\x80\xa6");
    fbRefresh(chrome);
    /* From here: draw bands to the back buffer INSIDE the safe area.
       Do not touch x < LEFT_PX, x >= RIGHT_PX, y < TOP_PX, y >= BOT_PX. */

    static const struct {
        const fbFont *font;
        fbColor       accent;
        const char   *alpha;
        const char   *digits;
    } large[] = {
        { &fbFont12x24, FB_BRIGHT_CYAN,    "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789" },
        { &fbFont16x32, FB_BRIGHT_MAGENTA, "ABCDEFGHIJKLMNOPQRSTU",      "0123456789" },
        { &fbFont24x48, FB_BRIGHT_RED,     "ABCDEFGHIJK",                "0123456789" },
    };
    int nLarge = (int)(sizeof large / sizeof large[0]);

    /* ── Compute per-band layout ─────────────────────────────────────
       Each band:
         LABEL_H = 20px  (VGA label + 4px pad)
         TEXT_H  = pxH   (the glyphs at true size)
         RULER_H = 22px  (4px gap + 1px baseline + 14px number + 3px pad)
         GAP     = 4px   (between bands)
       Total bandH = LABEL_H + TEXT_H + RULER_H + GAP               */
    const int LABEL_H = 20;
    const int RULER_H = 22;
    const int GAP     = 4;

    int y = TOP_PX;

    for (int i = 0; i < nLarge; i++) {
        const fbFont *font  = large[i].font;
        fbColor       accent = large[i].accent;
        int bpr  = (font->w + 7) / 8;
        int pxH  = font->h / bpr;
        int bandH = LABEL_H + pxH + RULER_H + GAP;

        /* Skip this band if it won't fit above the footer */
        if (y + bandH > BOT_PX) break;

        /* ── Subtle band background (stays inside safe area) ──────── */
        int bgR = (FB_COLOR_R(accent) * 10) / 255;
        int bgG = (FB_COLOR_G(accent) * 10) / 255;
        int bgB = (FB_COLOR_B(accent) * 10) / 255;
        fbColor bg = FB_RGB(bgR, bgG, bgB);
        for (int row = y; row < y + bandH; row++)
            for (int col = LEFT_PX; col < RIGHT_PX; col++)
                fbDrawPixel(scr, col, row, bg);

        /* ── Font name label at top of band ───────────────────────── */
        char labelBuf[32];
        snprintf(labelBuf, sizeof(labelBuf), "%s  (%dx%d px)",
                 font->name, font->w, pxH);
        fbDrawTextPx(scr, LEFT_PX + 8, y + 2, labelBuf,
                     accent, bg, FB_ATTR_BOLD, &fbVga);

        /* ── Height marker bar on left edge ───────────────────────── */
        int textY = y + LABEL_H;
        fbColor markerCol = fbLerp(accent, FB_WHITE, 0.6f);
        for (int ry = textY; ry < textY + pxH; ry++) {
            fbDrawPixel(scr, LEFT_PX,     ry, markerCol);
            fbDrawPixel(scr, LEFT_PX + 1, ry, markerCol);
        }
        for (int rx = LEFT_PX; rx <= LEFT_PX + 5; rx++) {
            fbDrawPixel(scr, rx, textY,           markerCol);
            fbDrawPixel(scr, rx, textY + pxH - 1, markerCol);
        }
        char hBuf[16]; snprintf(hBuf, sizeof(hBuf), "%dpx", pxH);
        fbDrawTextPx(scr, LEFT_PX + 4, textY + pxH / 2 - 4, hBuf,
                     markerCol, bg, FB_ATTR_NONE, &fbThin5);

        /* ── Sample alphabet — clip to safe width ─────────────────── */
        int textX = LEFT_PX + 20;    /* leave room for the height marker */
        int maxAlphaW = RIGHT_PX - textX - 8;
        int maxChars = maxAlphaW / font->w;
        if (maxChars < 1) maxChars = 1;

        char alphaBuf[32];
        int srcLen = (int)strlen(large[i].alpha);
        int nChars = srcLen < maxChars ? srcLen : maxChars;
        memcpy(alphaBuf, large[i].alpha, (size_t)nChars);
        alphaBuf[nChars] = '\0';

        int alphaEndX = fbDrawTextPx(scr, textX, textY, alphaBuf,
                                     accent, bg, FB_ATTR_NONE, font);

        /* ── Digits right-aligned, only if they don't overlap alpha ── */
        int digW = (int)strlen(large[i].digits) * font->w;
        int digX = RIGHT_PX - digW - 8;
        if (digX >= alphaEndX + font->w) {
            fbDrawTextPx(scr, digX, textY, large[i].digits,
                         fbLerp(accent, FB_WHITE, 0.3f), bg,
                         FB_ATTR_NONE, font);
        }

        /* ── Pixel ruler — entirely within band, above separator ──── */
        int rulerY   = textY + pxH + 4;   /* 4px gap below glyphs */
        int rulerTop = rulerY - 12;        /* numbers sit 12px above line */

        /* Clamp rulerTop so numbers don't overwrite the text */
        if (rulerTop < textY + pxH + 1) rulerTop = textY + pxH + 1;

        fbColor tickCol = fbLerp(accent, FB_WHITE, 0.25f);
        /* Baseline */
        for (int rx = LEFT_PX + 20; rx < RIGHT_PX - 8; rx++)
            fbDrawPixel(scr, rx, rulerY, tickCol);
        /* Ticks */
        for (int rx = 0; rx < RIGHT_PX - LEFT_PX - 28; rx++) {
            int sx = LEFT_PX + 20 + rx;
            if (rx % 50 == 0) {
                for (int ry = rulerY - 4; ry <= rulerY; ry++)
                    fbDrawPixel(scr, sx, ry, accent);
                char num[12]; snprintf(num, sizeof(num), "%d", rx);
                int nx = sx - (int)strlen(num) * fbThin5.w / 2;
                if (nx >= LEFT_PX + 20 && nx + (int)strlen(num) * fbThin5.w < RIGHT_PX)
                    fbDrawTextPx(scr, nx, rulerTop,
                                 num, accent, bg, FB_ATTR_NONE, &fbThin5);
            } else if (rx % 10 == 0) {
                for (int ry = rulerY - 2; ry <= rulerY; ry++)
                    fbDrawPixel(scr, sx, ry, tickCol);
            }
        }

        /* ── Band separator line ───────────────────────────────────── */
        int sepY = y + bandH - 1;
        if (sepY < BOT_PX)
            for (int sx = LEFT_PX; sx < RIGHT_PX; sx++)
                fbDrawPixel(scr, sx, sepY, fbLerp(accent, FB_BLACK, 0.6f));

        y += bandH;
    }

    /* ── Comparison strip: same text in VGA + each large font ──────── */
    int cmpY = y + GAP;
    if (cmpY + 20 < BOT_PX) {
        fbDrawTextPx(scr, LEFT_PX + 8, cmpY,
                     "Compare:", FB_GRAY, FB_BLACK, FB_ATTR_NONE, &fbVga);
        cmpY += 20;

        static const struct { const fbFont *f; fbColor c; } cmp[] = {
            { &fbVga,       FB_WHITE          },
            { &fbFont12x24, FB_BRIGHT_CYAN    },
            { &fbFont16x32, FB_BRIGHT_MAGENTA },
            { &fbFont24x48, FB_BRIGHT_RED     },
        };
        int nCmp = (int)(sizeof cmp / sizeof cmp[0]);
        int cx = LEFT_PX + 8;

        for (int i = 0; i < nCmp; i++) {
            const fbFont *f  = cmp[i].f;
            int fpxH = f->h / ((f->w + 7) / 8);
            if (cmpY + fpxH > BOT_PX) break;
            if (cx + f->w * 5 > RIGHT_PX) break;
            cx = fbDrawTextPx(scr, cx, cmpY, "Hello",
                              cmp[i].c, FB_BLACK, FB_ATTR_NONE, f);
            cx += 12;
        }
    }

    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(chrome);
}


/* ═══════════════════════════════════════════════════════════════════
 *  PAGE 2: Box-drawing characters in every border style
 * ═══════════════════════════════════════════════════════════════════ */
static void pageBoxDraw(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    if (!win) return;

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Box-Drawing Characters",
                   FB_BORDER_THICK, FB_YELLOW, FB_BLACK, FB_YELLOW);
    footer(win, "Press any key…");

    /* All border styles in a row */
    static const struct { fbBorderStyle style; const char *name; fbColor col; } bs[] = {
        { FB_BORDER_SINGLE,  "Single",  FB_WHITE   },
        { FB_BORDER_DOUBLE,  "Double",  FB_CYAN    },
        { FB_BORDER_ROUNDED, "Rounded", FB_GREEN   },
        { FB_BORDER_THICK,   "Thick",   FB_RED     },
        { FB_BORDER_DASHED,  "Dashed",  FB_MAGENTA },
    };
    int nbs = (int)(sizeof bs / sizeof bs[0]);

    for (int i = 0; i < nbs; i++) {
        int c = 2 + i * 16;
        fbDrawBox(win, c, 3, 14, 7, bs[i].style, bs[i].col);
        fbSetColors(win, bs[i].col, FB_BLACK);
        fbSetAttr(win, FB_ATTR_BOLD);
        int nl = (int)strlen(bs[i].name);
        fbPrintAt(win, c + (14 - nl) / 2, 6, "%s", bs[i].name);
    }

    /* Block elements showcase */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 12, "Block elements:");
    fbSetAttr(win, FB_ATTR_NONE);

    static const wchar_t blocks[] = {
        L'█', L'▓', L'▒', L'░',
        L'▀', L'▄', L'▌', L'▐',
        L'▁', L'▂', L'▃', L'▄', L'▅', L'▆', L'▇', L'█',
        L'▖', L'▗', L'▘', L'▝', L'▙', L'▚', L'▛', L'▜', L'▞', L'▟',
        0
    };
    static const fbColor bcolors[] = {
        FB_WHITE, FB_GRAY, FB_GRAY, FB_GRAY,
        FB_CYAN, FB_CYAN, FB_GREEN, FB_GREEN,
        FB_BLUE, FB_BLUE, FB_BLUE, FB_BLUE, FB_YELLOW, FB_YELLOW, FB_YELLOW, FB_YELLOW,
        FB_MAGENTA, FB_MAGENTA, FB_MAGENTA, FB_MAGENTA,
        FB_RED, FB_RED, FB_RED, FB_RED, FB_RED, FB_RED,
    };
    int col = 18;
    for (int i = 0; blocks[i]; i++) {
        if (col + 2 >= fbWindowCols(win)) col = 18;
        fbSetColors(win, bcolors[i], FB_BLACK);
        fbMoveCursor(win, col, 12);
        fbAddWchar(win, blocks[i]);
        col += 2;
    }

    /* Diagonal and rounded */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 15, "Diagonals & rounded:");
    fbSetAttr(win, FB_ATTR_NONE);

    static const wchar_t diags[] = { L'╱', L'╲', L'╳', L'╭', L'╮', L'╰', L'╯', 0 };
    static const fbColor dcols[] = { FB_RED, FB_GREEN, FB_YELLOW, FB_CYAN, FB_CYAN, FB_CYAN, FB_CYAN };
    col = 24;
    for (int i = 0; diags[i]; i++) {
        fbSetColors(win, dcols[i], FB_BLACK);
        fbMoveCursor(win, col, 15);
        fbAddWchar(win, diags[i]);
        col += 3;
    }

    /* Braille spinner demo */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 18, "Braille spinner: ");
    fbSetAttr(win, FB_ATTR_NONE);

    static const wchar_t spinFrames[] = L"⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
    int nFrames = 10;
    for (int frame = 0; frame < nFrames * 4; frame++) {
        fbSetColors(win, FB_CYAN, FB_BLACK);
        fbMoveCursor(win, 19, 18);
        fbAddWchar(win, spinFrames[frame % nFrames]);
        fbRefresh(win);
        fbFlush(scr);
        int k = fbGetKeyTimeout(scr, 80);
        if (k != FB_KEY_NONE) goto done_boxdraw;
    }

done_boxdraw:
    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* ═══════════════════════════════════════════════════════════════════
 *  PAGE 3: Widget showcase — gauges, sparkline, table, text input
 * ═══════════════════════════════════════════════════════════════════ */
static void pageWidgets(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    if (!win) return;

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Widgets",
                   FB_BORDER_DOUBLE, FB_MAGENTA, FB_BLACK, FB_MAGENTA);
    footer(win, "Press any key…");

    /* ── Gauges ── */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 3, "Vertical Gauges:");
    fbSetAttr(win, FB_ATTR_NONE);

    static const int gaugeVals[]   = { 20, 45, 70, 90, 55, 30, 80, 15 };
    static const fbColor gaugeFgs[]= {
        FB_BLUE, FB_CYAN, FB_GREEN, FB_BRIGHT_GREEN,
        FB_YELLOW, FB_BRIGHT_YELLOW, FB_RED, FB_MAGENTA
    };
    int nGauges = 8;
    for (int g = 0; g < nGauges; g++) {
        fbDrawGauge(win, 4 + g * 4, 4, 8, gaugeVals[g], 100,
                    gaugeFgs[g], FB_RGB(20,20,30));
        fbSetColors(win, FB_GRAY, FB_BLACK);
        fbPrintAt(win, 4 + g * 4, 13, "%2d%%", gaugeVals[g]);
    }

    /* ── Sparklines ── */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 16, "Sparklines:");
    fbSetAttr(win, FB_ATTR_NONE);

    /* Generate sine wave + noise */
    float spark1[40], spark2[40], spark3[40];
    for (int i = 0; i < 40; i++) {
        spark1[i] = (float)(0.5 + 0.45 * sin(i * 0.4));
        spark2[i] = (float)(0.5 + 0.45 * sin(i * 0.7 + 1.0));
        float noise = (float)(rand() % 100) / 200.0f - 0.25f;
        spark3[i] = spark1[i] * 0.6f + noise;
        if (spark3[i] < 0) spark3[i] = 0;
        if (spark3[i] > 1) spark3[i] = 1;
    }

    fbSetColors(win, FB_CYAN, FB_BLACK);
    fbPrintAt(win, 2, 17, "CPU: ");
    fbDrawSparkline(win, 7, 17, spark1, 40, 30, FB_CYAN, FB_BLACK);

    fbSetColors(win, FB_GREEN, FB_BLACK);
    fbPrintAt(win, 2, 18, "MEM: ");
    fbDrawSparkline(win, 7, 18, spark2, 40, 30, FB_GREEN, FB_BLACK);

    fbSetColors(win, FB_YELLOW, FB_BLACK);
    fbPrintAt(win, 2, 19, "NET: ");
    fbDrawSparkline(win, 7, 19, spark3, 40, 30, FB_YELLOW, FB_BLACK);

    /* ── Table ── */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 42, 3, "Data Table:");
    fbSetAttr(win, FB_ATTR_NONE);

    static const fbTableCol tcols[] = {
        { "Process",   12, FB_ALIGN_LEFT  },
        { "PID",        5, FB_ALIGN_RIGHT },
        { "CPU%",       5, FB_ALIGN_RIGHT },
        { "MEM%",       5, FB_ALIGN_RIGHT },
        { NULL, 0, 0 }
    };
    static const char *tdata[][4] = {
        { "fbcurses",  "1042", "0.2", "1.1" },
        { "Xorg",      "987",  "3.4", "8.2" },
        { "pulseaudio","1123", "0.1", "0.6" },
        { "bash",      "2001", "0.0", "0.2" },
        { "vim",       "2314", "0.3", "0.9" },
        { "htop",      "2400", "1.1", "0.4" },
    };
    int nDataRows = 6;
    /* Build pointer array */
    const char * const *rowPtrs[6];
    for (int r = 0; r < nDataRows; r++) rowPtrs[r] = tdata[r];

    fbDrawTable(win, 42, 4, tcols,
                (const char * const * const *)rowPtrs, nDataRows,
                2,  /* selRow */
                FB_BRIGHT_CYAN, FB_RGB(10,30,40),
                FB_WHITE,       FB_RGB(10,15,25),
                FB_BLACK,       FB_BRIGHT_GREEN);

    /* ── Text input widget ── */
    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_BOLD);
    fbPrintAt(win, 2, 22, "Text input (type, then Enter):");
    fbSetAttr(win, FB_ATTR_NONE);

    /* Draw input box */
    fbDrawBox(win, 2, 23, 38, 3, FB_BORDER_SINGLE, FB_CYAN);
    fbTextInput *ti = fbTextInputNew(win, 4, 24, 34, 64, "");
    if (ti) {
        fbRefresh(win);
        fbFlush(scr);

        for (;;) {
            fbTextInputDraw(ti, FB_WHITE, FB_RGB(10,20,30), FB_WHITE);
            fbRefresh(win);
            fbFlush(scr);
            int key = fbGetKey(scr);
            if (key == FB_KEY_ENTER || key == '\r' || key == FB_KEY_ESC)
                break;
            fbTextInputKey(ti, key);
        }

        /* Show what was typed */
        const char *typed = fbTextInputGet(ti);
        if (typed[0]) {
            fbSetColors(win, FB_BRIGHT_GREEN, FB_BLACK);
            fbPrintAt(win, 4, 27, "You typed: \"%s\"", typed);
        }
        fbTextInputFree(ti);
    }

    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* ═══════════════════════════════════════════════════════════════════
 *  PAGE 4: Menu widget demo
 * ═══════════════════════════════════════════════════════════════════ */
static void pageMenu(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    if (!win) return;

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Menu Widget",
                   FB_BORDER_ROUNDED, FB_GREEN, FB_BLACK, FB_GREEN);

    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbSetAttr(win, FB_ATTR_NONE);
    fbPrintAt(win, 4, 4, "A pop-up menu will appear at the centre of the screen.");
    fbPrintAt(win, 4, 5, "Use arrow keys to navigate, Enter to select, Esc to cancel.");

    footer(win, "Press any key to open the menu…");
    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);

    static const fbMenuItem menuItems[] = {
        { "New File",        1, false },
        { "Open File…",      2, false },
        { "Save",            3, false },
        { "",                0, false },   /* separator */
        { "Preferences…",   10, false },
        { "About fbcurses",  11, false },
        { "",                0, false },
        { "Quit",           99, false },
        { NULL,              0, false },
    };

    int col = W/2 - 10;
    int row = H/2 - 5;
    int result = fbMenu(scr, col, row, menuItems,
                        FB_WHITE,   FB_RGB(20,20,35),
                        FB_BLACK,   FB_CYAN,
                        FB_BORDER_ROUNDED);

    /* Show result */
    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Menu Widget",
                   FB_BORDER_ROUNDED, FB_GREEN, FB_BLACK, FB_GREEN);

    fbSetColors(win, FB_WHITE, FB_BLACK);
    if (result == -1) {
        fbToast(scr, FB_TOAST_WARNING, "Menu cancelled (Esc pressed)", 1500);
        fbPrintAt(win, 4, 4, "Menu cancelled.");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Selected item ID: %d", result);
        fbToast(scr, FB_TOAST_SUCCESS, msg, 1500);
        fbSetColors(win, FB_BRIGHT_GREEN, FB_BLACK);
        fbPrintAt(win, 4, 4, "Selected item ID: %d", result);
    }

    footer(win, "Press any key to continue…");
    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* ═══════════════════════════════════════════════════════════════════
 *  PAGE 5: Toast notifications
 * ═══════════════════════════════════════════════════════════════════ */
static void pageToasts(fbScreen *scr)
{
    int W = fbCols(scr), H = fbRows(scr);
    fbWindow *win = fbNewWindow(scr, 0, 0, W, H);
    if (!win) return;

    fbClearWindow(win, FB_BLACK);
    fbDrawTitleBar(win, "Toast Notifications",
                   FB_BORDER_DOUBLE, FB_BRIGHT_YELLOW, FB_BLACK, FB_BRIGHT_YELLOW);

    fbSetColors(win, FB_WHITE, FB_BLACK);
    fbPrintAt(win, 4, 4, "Showing all four toast variants…");
    footer(win, "Press any key to skip each.");
    fbRefresh(win);
    fbFlush(scr);
    sleepMs(400);

    fbToast(scr, FB_TOAST_INFO,    "fbcurses v1.0 loaded successfully",  1800);
    fbToast(scr, FB_TOAST_SUCCESS, "File saved: /tmp/demo.txt",          1800);
    fbToast(scr, FB_TOAST_WARNING, "Disk usage above 80%",               1800);
    fbToast(scr, FB_TOAST_ERROR,   "Connection refused: /dev/ttyUSB0",   1800);

    footer(win, "Press any key to continue…");
    fbRefresh(win);
    fbFlush(scr);
    fbGetKey(scr);
    fbDelWindow(win);
}

/* ═══════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════ */
/* ── Ctrl-C / kill handling ──────────────────────────────────────── */
static volatile sig_atomic_t _running = 1;
static void _sigStop(int sig) { (void)sig; _running = 0; }
static void _installSigHandlers(void) {
    struct sigaction sa;
    sa.sa_handler = _sigStop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}


int main(void)
{
    srand((unsigned)time(NULL));

    fbScreen *scr = fbInit(NULL);
    _installSigHandlers();
    if (!scr) {
        fprintf(stderr, "fbInit failed: %s\n", fbGetError());
        return 1;
    }

    pageFonts     (scr);
    pageLargeFonts(scr);
    pageBoxDraw   (scr);
    pageWidgets(scr);
    pageMenu   (scr);
    pageToasts (scr);

    fbShutdown(scr);
    return 0;
}
