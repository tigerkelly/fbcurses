/*
 * dialogs.c — Modal dialog widgets for fbcurses.
 *
 *   fbFilePicker  — scrollable directory browser
 *   fbColorPicker — 24-row × 32-col colour swatch palette
 *   fbMsgBox      — configurable modal message box
 *
 * Copyright (c) 2026 Richard Kelly Wiles (rkwiles@twc.com)
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════════════════
 *  FILE PICKER
 * ══════════════════════════════════════════════════════════════════ */

/* Comparison: directories first, then alphabetical */
static int _entcmp(const void *a, const void *b)
{
    const struct dirent *da = *(const struct dirent **)a;
    const struct dirent *db = *(const struct dirent **)b;
    bool adir = (da->d_type == DT_DIR);
    bool bdir = (db->d_type == DT_DIR);
    if (adir != bdir) return adir ? -1 : 1;
    return strcmp(da->d_name, db->d_name);
}

bool fbFilePicker(fbScreen *scr, const char *startDir,
                  char *outPath, int outLen)
{
    char cwd[768];
    if (startDir) {
        snprintf(cwd, sizeof(cwd), "%s", startDir);
    } else {
        if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), "/");
    }

    int W = fbCols(scr), H = fbRows(scr);
    int wCols = W * 2 / 3;
    int wRows = H * 3 / 4;
    int wCol  = (W - wCols) / 2;
    int wRow  = (H - wRows) / 2;

    fbWindow *win = fbNewWindow(scr, wCol, wRow, wCols, wRows);
    if (!win) return false;

    int listRows = wRows - 4;   /* 2 border + 1 title + 1 path */
    int listCols = wCols - 4;
    int sel = 0, scroll = 0;

    struct dirent **entries = NULL;
    int nEntries = 0;

    bool result = false;

    for (;;) {
        /* ── Read directory ── */
        if (entries) {
            for (int i = 0; i < nEntries; i++) free(entries[i]);
            free(entries);
            entries = NULL; nEntries = 0;
        }

        DIR *d = opendir(cwd);
        if (d) {
            int cap = 64;
            entries = malloc((size_t)cap * sizeof(struct dirent *));
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0) continue;
                if (nEntries == cap) {
                    cap *= 2;
                    entries = realloc(entries, (size_t)cap * sizeof(struct dirent *));
                }
                entries[nEntries] = malloc(sizeof(struct dirent));
                *entries[nEntries] = *ent;
                nEntries++;
            }
            closedir(d);
            qsort(entries, (size_t)nEntries, sizeof(struct dirent *), _entcmp);
        }
        if (sel >= nEntries) sel = nEntries > 0 ? nEntries - 1 : 0;

        /* ── Draw ── */
        fbClearWindow(win, FB_RGB(10, 15, 25));
        fbDrawTitleBar(win, "File Picker",
                       FB_BORDER_DOUBLE, FB_CYAN, FB_BLACK, FB_CYAN);

        /* Current path */
        fbSetColors(win, FB_GRAY, FB_RGB(10,15,25));
        fbSetAttr(win, FB_ATTR_NONE);
        char pathBuf[256];
        snprintf(pathBuf, sizeof(pathBuf), " %.*s", listCols - 1, cwd);
        fbPrintAt(win, 1, 2, "%-*s", wCols - 2, pathBuf);

        /* Separator */
        for (int c = 1; c < wCols - 1; c++) {
            fbCell *cell = _fbGetCell(win, c, 3);
            cell->ch = L'─'; cell->fg = FB_CYAN;
            cell->bg = FB_RGB(10,15,25); cell->dirty = true;
        }

        /* File list */
        for (int i = 0; i < listRows && (scroll + i) < nEntries; i++) {
            int ei = scroll + i;
            struct dirent *e = entries[ei];
            bool isDir = (e->d_type == DT_DIR);
            bool isSel = (ei == sel);

            fbColor fg = isDir ? FB_BRIGHT_CYAN : FB_WHITE;
            fbColor bg = isSel ? FB_RGB(30, 60, 90) : FB_RGB(10, 15, 25);

            if (isSel) {
                for (int c = 1; c < wCols - 1; c++) {
                    fbCell *cell = _fbGetCell(win, c, 4 + i);
                    cell->bg = bg; cell->dirty = true;
                }
            }

            char lineBuf[512];
            snprintf(lineBuf, sizeof(lineBuf), "%s%s",
                     isDir ? "▸ " : "  ", e->d_name);

            fbSetColors(win, isSel ? FB_WHITE : fg, bg);
            fbSetAttr(win, isSel ? FB_ATTR_BOLD : FB_ATTR_NONE);
            fbPrintAt(win, 2, 4 + i, "%-*.*s", listCols, listCols, lineBuf);
        }

        /* Scrollbar hint */
        if (nEntries > listRows) {
            fbSetColors(win, FB_GRAY, FB_RGB(10,15,25));
            int barPos = sel * (listRows - 1) / (nEntries - 1);
            for (int i = 0; i < listRows; i++) {
                fbCell *cell = _fbGetCell(win, wCols - 2, 4 + i);
                cell->ch = (i == barPos) ? L'█' : L'│';
                cell->fg = (i == barPos) ? FB_CYAN : FB_GRAY;
                cell->bg = FB_RGB(10,15,25);
                cell->dirty = true;
            }
        }

        /* Footer */
        fbSetColors(win, FB_GRAY, FB_RGB(10,15,25));
        fbSetAttr(win, FB_ATTR_NONE);
        fbPrintAt(win, 1, wRows - 2,
                  "%-*s", wCols - 2,
                  " ↑↓ Navigate  Enter Select  ← Parent  Esc Cancel");

        fbRefresh(win);
        fbFlush(scr);

        /* ── Input ── */
        int key = fbGetKey(scr);

        if (key == FB_KEY_UP   || key == 'k') {
            if (sel > 0) { sel--; if (sel < scroll) scroll = sel; }
        } else if (key == FB_KEY_DOWN || key == 'j') {
            if (sel < nEntries - 1) {
                sel++;
                if (sel >= scroll + listRows) scroll = sel - listRows + 1;
            }
        } else if (key == FB_KEY_PAGE_UP) {
            sel   = sel > listRows ? sel - listRows : 0;
            scroll = scroll > listRows ? scroll - listRows : 0;
            if (sel < scroll) scroll = sel;
        } else if (key == FB_KEY_PAGE_DOWN) {
            sel    = sel + listRows < nEntries ? sel + listRows : nEntries - 1;
            scroll = scroll + listRows < nEntries - listRows
                   ? scroll + listRows : (nEntries > listRows ? nEntries - listRows : 0);
            if (sel >= scroll + listRows) scroll = sel - listRows + 1;
        } else if (key == FB_KEY_HOME || key == 'g') {
            sel = 0; scroll = 0;
        } else if (key == FB_KEY_END || key == 'G') {
            sel = nEntries > 0 ? nEntries - 1 : 0;
            scroll = sel >= listRows ? sel - listRows + 1 : 0;
        } else if (key == FB_KEY_ENTER || key == '\r') {
            if (nEntries > 0) {
                struct dirent *e = entries[sel];
                char newPath[768];
                snprintf(newPath, sizeof(newPath) - 1, "%.*s/%.*s", 510, cwd, 255, e->d_name);
                if (e->d_type == DT_DIR) {
                    /* Navigate into directory */
                    snprintf(cwd, sizeof(cwd), "%s", newPath);
                    sel = 0; scroll = 0;
                } else {
                    /* Select file */
                    snprintf(outPath, (size_t)outLen, "%s", newPath);
                    result = true;
                    goto done;
                }
            }
        } else if (key == FB_KEY_LEFT || key == FB_KEY_BACKSPACE || key == 127) {
            /* Go up one directory */
            char *slash = strrchr(cwd, '/');
            if (slash && slash != cwd) {
                *slash = '\0';
            } else if (strcmp(cwd, "/") != 0) {
                snprintf(cwd, sizeof(cwd), "/");
            }
            sel = 0; scroll = 0;
        } else if (key == FB_KEY_ESC || key == 'q') {
            result = false;
            goto done;
        }
    }

done:
    if (entries) {
        for (int i = 0; i < nEntries; i++) free(entries[i]);
        free(entries);
    }
    fbDelWindow(win);
    fbFlush(scr);
    return result;
}

/* ══════════════════════════════════════════════════════════════════
 *  COLOUR PICKER
 * ══════════════════════════════════════════════════════════════════ */

/* Build a visually pleasant 8×16 swatch palette:
   rows 0-5: rainbow at full / medium saturation
   row  6:   grayscale ramp
   row  7:   named fbcurses constants */
static fbColor _makePalette(int row, int col)
{
    if (row < 6) {
        /* Hue wheel: col 0-15 full sat, 16-31 desaturated */
        float hue = (col % 16) / 16.0f * 360.0f;
        float sat = (col < 16) ? 1.0f : 0.5f;
        float val = (row < 3)  ? 1.0f - row * 0.15f : 0.5f - (row - 3) * 0.1f;

        /* HSV → RGB */
        float h6 = hue / 60.0f;
        int   hi = (int)h6 % 6;
        float f  = h6 - (int)h6;
        float p  = val * (1.0f - sat);
        float q  = val * (1.0f - sat * f);
        float t  = val * (1.0f - sat * (1.0f - f));
        float r, g, b;
        switch (hi) {
            case 0: r=val; g=t;   b=p;   break;
            case 1: r=q;   g=val; b=p;   break;
            case 2: r=p;   g=val; b=t;   break;
            case 3: r=p;   g=q;   b=val; break;
            case 4: r=t;   g=p;   b=val; break;
            default:r=val; g=p;   b=q;   break;
        }
        return FB_RGB((uint8_t)(r*255),(uint8_t)(g*255),(uint8_t)(b*255));
    }
    if (row == 6) {
        /* Grayscale */
        uint8_t v = (uint8_t)(col * 255 / 31);
        return FB_RGB(v, v, v);
    }
    /* Row 7: named colours */
    static const fbColor named[] = {
        FB_BLACK, FB_WHITE, FB_RED, FB_GREEN, FB_BLUE, FB_YELLOW,
        FB_MAGENTA, FB_CYAN, FB_GRAY, FB_BRIGHT_RED, FB_BRIGHT_GREEN,
        FB_BRIGHT_BLUE, FB_BRIGHT_YELLOW, FB_BRIGHT_MAGENTA, FB_BRIGHT_CYAN,
        FB_RGB(255,128,0), FB_RGB(0,255,128), FB_RGB(128,0,255),
        FB_RGB(255,0,128), FB_RGB(0,128,255), FB_RGB(128,255,0),
        FB_RGB(64,0,0), FB_RGB(0,64,0), FB_RGB(0,0,64),
        FB_RGB(64,64,0), FB_RGB(0,64,64), FB_RGB(64,0,64),
        FB_RGB(200,150,100), FB_RGB(100,200,150), FB_RGB(150,100,200),
        FB_RGB(30,30,30), FB_RGB(240,240,240),
    };
    int idx = col < 32 ? col : 31;
    return named[idx];
}

fbColor fbColorPicker(fbScreen *scr, fbColor initial)
{
    int ROWS = 8, COLS = 32;
    int SWATCH = 2;             /* each swatch is 2 chars wide */
    int wCols  = COLS * SWATCH + 6;
    int wRows  = ROWS + 6;

    int W = fbCols(scr), H = fbRows(scr);
    int wCol = (W - wCols) / 2;
    int wRow = (H - wRows) / 2;
    if (wCol < 0) wCol = 0;
    if (wRow < 0) wRow = 0;

    fbWindow *win = fbNewWindow(scr, wCol, wRow, wCols, wRows);
    if (!win) return FB_TRANSPARENT;

    /* Build palette array */
    fbColor palette[8][32];
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            palette[r][c] = _makePalette(r, c);

    /* Find initial selection */
    int selR = 0, selC = 0;
    uint32_t bestDist = 0xFFFFFFFFu;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            fbColor p = palette[r][c];
            int dr = (int)FB_COLOR_R(p) - (int)FB_COLOR_R(initial);
            int dg = (int)FB_COLOR_G(p) - (int)FB_COLOR_G(initial);
            int db = (int)FB_COLOR_B(p) - (int)FB_COLOR_B(initial);
            uint32_t d = (uint32_t)(dr*dr + dg*dg + db*db);
            if (d < bestDist) { bestDist = d; selR = r; selC = c; }
        }
    }

    for (;;) {
        fbClearWindow(win, FB_RGB(10,10,20));
        fbDrawTitleBar(win, "Colour Picker",
                       FB_BORDER_ROUNDED, FB_YELLOW, FB_BLACK, FB_YELLOW);

        /* Draw swatches */
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                fbColor col = palette[r][c];
                bool isSel = (r == selR && c == selC);
                int dc = 2 + c * SWATCH;
                int dr = 3 + r;

                fbCell *cell1 = _fbGetCell(win, dc,   dr);
                fbCell *cell2 = _fbGetCell(win, dc+1, dr);

                wchar_t glyph = isSel ? L'◆' : L'█';
                fbColor fg    = isSel ? FB_WHITE : col;

                cell1->ch = glyph; cell1->fg = fg; cell1->bg = col; cell1->dirty = true;
                cell2->ch = glyph; cell2->fg = fg; cell2->bg = col; cell2->dirty = true;
            }
        }

        /* Preview box */
        fbColor cur = palette[selR][selC];
        fbSetColors(win, FB_WHITE, FB_RGB(10,10,20));
        fbSetAttr(win, FB_ATTR_NONE);
        fbPrintAt(win, 2, ROWS + 4,
                  "Selected: #%02X%02X%02X",
                  FB_COLOR_R(cur), FB_COLOR_G(cur), FB_COLOR_B(cur));

        /* Preview swatch */
        for (int i = 0; i < 8; i++) {
            fbCell *cell = _fbGetCell(win, 14 + i, ROWS + 4);
            cell->ch = L'█'; cell->fg = cur; cell->bg = cur; cell->dirty = true;
        }

        fbSetColors(win, FB_GRAY, FB_RGB(10,10,20));
        fbPrintAt(win, 2, ROWS + 5,
                  "%-*s", wCols - 4,
                  " Arrows: navigate   Enter: select   Esc: cancel");

        fbRefresh(win);
        fbFlush(scr);

        int key = fbGetKey(scr);
        if      (key == FB_KEY_UP)    { if (selR > 0) selR--; }
        else if (key == FB_KEY_DOWN)  { if (selR < ROWS-1) selR++; }
        else if (key == FB_KEY_LEFT)  { if (selC > 0) selC--; }
        else if (key == FB_KEY_RIGHT) { if (selC < COLS-1) selC++; }
        else if (key == FB_KEY_ENTER || key == '\r') {
            fbColor chosen = palette[selR][selC];
            fbDelWindow(win);
            fbFlush(scr);
            return chosen;
        } else if (key == FB_KEY_ESC || key == 'q') {
            fbDelWindow(win);
            fbFlush(scr);
            return FB_TRANSPARENT;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  MESSAGE BOX
 * ══════════════════════════════════════════════════════════════════ */

int fbMsgBox(fbScreen *scr, const char *title, const char *msg,
             fbMsgBoxButtons buttons, fbToastKind kind)
{
    /* Style from toast kind */
    static const fbColor kindBorder[] = {
        [FB_TOAST_INFO]    = FB_CYAN,
        [FB_TOAST_SUCCESS] = FB_BRIGHT_GREEN,
        [FB_TOAST_WARNING] = FB_BRIGHT_YELLOW,
        [FB_TOAST_ERROR]   = FB_BRIGHT_RED,
    };
    static const fbColor kindBg[] = {
        [FB_TOAST_INFO]    = FB_RGB(12, 25, 40),
        [FB_TOAST_SUCCESS] = FB_RGB(10, 35, 18),
        [FB_TOAST_WARNING] = FB_RGB(40, 32,  8),
        [FB_TOAST_ERROR]   = FB_RGB(45, 12, 12),
    };
    if (kind < 0 || kind > FB_TOAST_ERROR) kind = FB_TOAST_INFO;

    fbColor borderCol = kindBorder[kind];
    fbColor bgCol     = kindBg[kind];

    /* Break msg into lines */
    char lines[16][128];
    int  nLines = 0;
    {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s", msg);
        char *p = tmp, *nl;
        while (*p && nLines < 16) {
            nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            snprintf(lines[nLines++], 128, "%.*s", 127, p);
            if (!nl) break;
            p = nl + 1;
        }
    }

    /* Button labels */
    typedef struct { const char *label; int result; int key; } BtnDef;
    BtnDef btns[4];
    int nBtns = 0;
    if (buttons & FB_MSGBOX_RESULT_YES)    btns[nBtns++] = (BtnDef){"  Yes  ", FB_MSGBOX_RESULT_YES,    'y'};
    if (buttons & FB_MSGBOX_RESULT_NO)     btns[nBtns++] = (BtnDef){"  No   ", FB_MSGBOX_RESULT_NO,     'n'};
    if (buttons & FB_MSGBOX_RESULT_OK)     btns[nBtns++] = (BtnDef){"  OK   ", FB_MSGBOX_RESULT_OK,     '\r'};
    if (buttons & FB_MSGBOX_RESULT_CANCEL) btns[nBtns++] = (BtnDef){" Cancel", FB_MSGBOX_RESULT_CANCEL, FB_KEY_ESC};
    if (nBtns == 0) { btns[nBtns++] = (BtnDef){"  OK   ", FB_MSGBOX_RESULT_OK, '\r'}; }

    /* Compute size */
    int maxW = title ? (int)strlen(title) + 4 : 10;
    for (int i = 0; i < nLines; i++) {
        int l = (int)strlen(lines[i]) + 4;
        if (l > maxW) maxW = l;
    }
    int btnRowW = nBtns * 9 + 2;
    if (btnRowW > maxW) maxW = btnRowW;
    if (maxW > fbCols(scr) - 4) maxW = fbCols(scr) - 4;

    int wRows = nLines + 6;   /* border×2 + title + blank + text + blank + buttons */
    int wCols = maxW;
    int wCol  = (fbCols(scr) - wCols) / 2;
    int wRow  = (fbRows(scr) - wRows) / 2;

    fbWindow *win = fbNewWindow(scr, wCol, wRow, wCols, wRows);
    if (!win) return FB_MSGBOX_RESULT_CANCEL;

    int selBtn = 0;

    for (;;) {
        fbClearWindow(win, bgCol);
        fbDrawTitleBar(win, title ? title : "", FB_BORDER_ROUNDED,
                       borderCol, FB_BLACK, borderCol);

        /* Message lines */
        for (int i = 0; i < nLines; i++) {
            fbSetColors(win, FB_WHITE, bgCol);
            fbSetAttr(win, FB_ATTR_NONE);
            fbPrintAt(win, 2, 2 + i, "%.*s", wCols - 4, lines[i]);
        }

        /* Button row */
        int btnY = wRows - 2;
        int totalBtnW = nBtns * 9;
        int btnX = (wCols - totalBtnW) / 2;

        for (int b = 0; b < nBtns; b++) {
            bool isSel = (b == selBtn);
            fbColor bfg = isSel ? FB_BLACK    : FB_WHITE;
            fbColor bbg = isSel ? borderCol   : FB_RGB(30,30,50);
            fbSetColors(win, bfg, bbg);
            fbSetAttr(win, isSel ? FB_ATTR_BOLD : FB_ATTR_NONE);

            /* Draw button with brackets */
            fbPrintAt(win, btnX + b * 9, btnY, "[%s]", btns[b].label);
        }

        fbRefresh(win);
        fbFlush(scr);

        int key = fbGetKey(scr);

        /* Check shortcut keys */
        for (int b = 0; b < nBtns; b++) {
            if (key == btns[b].key ||
                (key >= 'A' && key <= 'Z' && key == btns[b].key - 32)) {
                int res = btns[b].result;
                fbDelWindow(win);
                fbFlush(scr);
                return res;
            }
        }

        if (key == FB_KEY_LEFT  || key == FB_KEY_UP)   { if (selBtn > 0) selBtn--; }
        if (key == FB_KEY_RIGHT || key == FB_KEY_DOWN)  { if (selBtn < nBtns-1) selBtn++; }
        if (key == FB_KEY_ENTER || key == '\r') {
            int res = btns[selBtn].result;
            fbDelWindow(win);
            fbFlush(scr);
            return res;
        }
        if (key == FB_KEY_ESC) {
            fbDelWindow(win);
            fbFlush(scr);
            return FB_MSGBOX_RESULT_CANCEL;
        }
    }
}
