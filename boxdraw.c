/*
 * boxdraw.c — Pixel-perfect renderer for Unicode box-drawing / block /
 *             braille characters, scaling to any font cell size.
 *
 * Coordinate convention
 *   px0, py0  — top-left pixel of the cell
 *   fw, fh    — font cell width/height in pixels (from the active font)
 *   cx = px0 + fw/2   horizontal centre
 *   cy = py0 + fh/2   vertical centre
 *
 * Line weights
 *   thin  : 1 px
 *   thick : 2 px
 *   double: two parallel 1-px strokes offset ±1 from centre
 */

#include "fbcurses_internal.h"

/* ── Low-level pixel helpers ────────────────────────────────────── */

static inline void px_(fbScreen *s, int x, int y, fbColor c)
{
    _fbPutPixelBack(s, x, y, c);
}
static void hrun(fbScreen *s, int x0, int x1, int y, fbColor c)
{
    for (int x = x0; x <= x1; x++) px_(s, x, y, c);
}
static void vrun(fbScreen *s, int x, int y0, int y1, fbColor c)
{
    for (int y = y0; y <= y1; y++) px_(s, x, y, c);
}
static void fillrect(fbScreen *s, int x0, int y0, int x1, int y1, fbColor c)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            px_(s, x, y, c);
}

/* ── Thin segment helpers (fw/fh explicit) ──────────────────────── */
static void segHL  (fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){ hrun(s,p0,p0+fw/2,q0+fh/2,c); }
static void segHR  (fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){ hrun(s,p0+fw/2,p0+fw-1,q0+fh/2,c); }
static void segVT  (fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){ vrun(s,p0+fw/2,q0,q0+fh/2,c); }
static void segVB  (fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){ vrun(s,p0+fw/2,q0+fh/2,q0+fh-1,c); }

/* ── Thick segment helpers (2-px wide/tall) ─────────────────────── */
static void segHL_K(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cy=q0+fh/2; int cx=p0+fw/2;
    hrun(s,p0,cx,cy,c); hrun(s,p0,cx,cy+1,c);
}
static void segHR_K(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cy=q0+fh/2; int cx=p0+fw/2;
    hrun(s,cx,p0+fw-1,cy,c); hrun(s,cx,p0+fw-1,cy+1,c);
}
static void segVT_K(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    vrun(s,cx,q0,cy,c); vrun(s,cx+1,q0,cy,c);
}
static void segVB_K(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    vrun(s,cx,cy,q0+fh-1,c); vrun(s,cx+1,cy,q0+fh-1,c);
}

/* ── Double-line helpers (two 1-px rails ±1 around centre) ─────── */
static void segDHL(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    hrun(s,p0,cx,cy-1,c); hrun(s,p0,cx,cy+1,c);
}
static void segDHR(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    hrun(s,cx,p0+fw-1,cy-1,c); hrun(s,cx,p0+fw-1,cy+1,c);
}
static void segDVT(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    vrun(s,cx-1,q0,cy,c); vrun(s,cx+1,q0,cy,c);
}
static void segDVB(fbScreen *s,int p0,int q0,int fw,int fh,fbColor c){
    int cx=p0+fw/2; int cy=q0+fh/2;
    vrun(s,cx-1,cy,q0+fh-1,c); vrun(s,cx+1,cy,q0+fh-1,c);
}

/* ════════════════════════════════════════════════════════════════════
 *  Main dispatch
 * ════════════════════════════════════════════════════════════════════ */
bool _fbDrawBoxChar(fbScreen *scr, int px0, int py0,
                    wchar_t ch, fbColor fg, fbColor bg,
                    int fw, int fh)
{
    int cx = px0 + fw/2;
    int cy = py0 + fh/2;

    fillrect(scr, px0, py0, px0+fw-1, py0+fh-1, bg);

/* Compact shorthands — all reference scr/px0/py0/fw/fh/fg in scope */
#define HL    segHL  (scr,px0,py0,fw,fh,fg)
#define HR    segHR  (scr,px0,py0,fw,fh,fg)
#define VT    segVT  (scr,px0,py0,fw,fh,fg)
#define VB    segVB  (scr,px0,py0,fw,fh,fg)
#define HL_K  segHL_K(scr,px0,py0,fw,fh,fg)
#define HR_K  segHR_K(scr,px0,py0,fw,fh,fg)
#define VT_K  segVT_K(scr,px0,py0,fw,fh,fg)
#define VB_K  segVB_K(scr,px0,py0,fw,fh,fg)
#define DHL   segDHL (scr,px0,py0,fw,fh,fg)
#define DHR   segDHR (scr,px0,py0,fw,fh,fg)
#define DVT   segDVT (scr,px0,py0,fw,fh,fg)
#define DVB   segDVB (scr,px0,py0,fw,fh,fg)

    /* ── Box Drawing Block U+2500–U+257F ────────────────────────── */
    if (ch >= 0x2500 && ch <= 0x257F) {
        switch (ch) {
        /* Thin straights */
        case 0x2500: HL; HR; break;                    /* ─ */
        case 0x2502: VT; VB; break;                    /* │ */
        /* Thick straights */
        case 0x2501: HL_K; HR_K; break;                /* ━ */
        case 0x2503: VT_K; VB_K; break;                /* ┃ */
        /* Dashed thin horizontal */
        case 0x2504:
            hrun(scr,px0,px0+fw/4-1,cy,fg);
            hrun(scr,px0+fw/2,px0+3*fw/4-1,cy,fg);
            hrun(scr,px0+fw*3/4,px0+fw-1,cy,fg); break;
        case 0x254C:
            hrun(scr,px0,px0+fw/3-1,cy,fg);
            hrun(scr,px0+fw*2/3,px0+fw-1,cy,fg); break;
        case 0x2508:
            for(int i=0;i<4;i++) px_(scr,px0+i*fw/4+fw/8,cy,fg);
            break;
        /* Dashed thin vertical */
        case 0x2506:
            vrun(scr,cx,py0,py0+fh/4-1,fg);
            vrun(scr,cx,py0+fh/2,py0+3*fh/4-1,fg);
            vrun(scr,cx,py0+3*fh/4,py0+fh-1,fg); break;
        case 0x254E:
            vrun(scr,cx,py0,py0+fh/3-1,fg);
            vrun(scr,cx,py0+fh*2/3,py0+fh-1,fg); break;
        /* Thin corners */
        case 0x250C: HR; VB; break;                    /* ┌ */
        case 0x2510: HL; VB; break;                    /* ┐ */
        case 0x2514: HR; VT; break;                    /* └ */
        case 0x2518: HL; VT; break;                    /* ┘ */
        /* Thin T-junctions */
        case 0x251C: HR; VT; VB; break;                /* ├ */
        case 0x2524: HL; VT; VB; break;                /* ┤ */
        case 0x252C: HL; HR; VB; break;                /* ┬ */
        case 0x2534: HL; HR; VT; break;                /* ┴ */
        case 0x253C: HL; HR; VT; VB; break;            /* ┼ */
        /* Thick corners */
        case 0x250F: HR_K; VB_K; break;                /* ┏ */
        case 0x2513: HL_K; VB_K; break;                /* ┓ */
        case 0x2517: HR_K; VT_K; break;                /* ┗ */
        case 0x251B: HL_K; VT_K; break;                /* ┛ */
        /* Thick T-junctions */
        case 0x2523: HR_K; VT_K; VB_K; break;          /* ┣ */
        case 0x252B: HL_K; VT_K; VB_K; break;          /* ┫ */
        case 0x2533: HL_K; HR_K; VB_K; break;          /* ┳ */
        case 0x253B: HL_K; HR_K; VT_K; break;          /* ┻ */
        case 0x254B: HL_K; HR_K; VT_K; VB_K; break;   /* ╋ */
        /* Mixed thin/thick */
        case 0x251D: HR_K; VT;  VB;  break;            /* ┝ */
        case 0x2525: HL_K; VT;  VB;  break;            /* ┥ */
        case 0x252F: HL;   HR;  VB_K; break;            /* ┯ */
        case 0x2537: HL;   HR;  VT_K; break;            /* ┷ */
        case 0x253F: HL;   HR;  VT_K; VB_K; break;    /* ┿ */
        case 0x2542: HL_K; HR_K;VT;  VB;  break;      /* ╂ */
        /* Double straights */
        case 0x2550: DHL; DHR; break;                  /* ═ */
        case 0x2551: DVT; DVB; break;                  /* ║ */
        /* Double corners */
        case 0x2554:  /* ╔ */
            hrun(scr,cx,px0+fw-1,cy-1,fg); hrun(scr,cx,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,cy,py0+fh-1,fg); vrun(scr,cx+1,cy,py0+fh-1,fg); break;
        case 0x2557:  /* ╗ */
            hrun(scr,px0,cx,cy-1,fg); hrun(scr,px0,cx,cy+1,fg);
            vrun(scr,cx-1,cy,py0+fh-1,fg); vrun(scr,cx+1,cy,py0+fh-1,fg); break;
        case 0x255A:  /* ╚ */
            hrun(scr,cx,px0+fw-1,cy-1,fg); hrun(scr,cx,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,py0,cy,fg); vrun(scr,cx+1,py0,cy,fg); break;
        case 0x255D:  /* ╝ */
            hrun(scr,px0,cx,cy-1,fg); hrun(scr,px0,cx,cy+1,fg);
            vrun(scr,cx-1,py0,cy,fg); vrun(scr,cx+1,py0,cy,fg); break;
        /* Double T-junctions */
        case 0x2560: /* ╠ */
            hrun(scr,cx,px0+fw-1,cy-1,fg); hrun(scr,cx,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,py0,py0+fh-1,fg); vrun(scr,cx+1,py0,py0+fh-1,fg); break;
        case 0x2563: /* ╣ */
            hrun(scr,px0,cx,cy-1,fg); hrun(scr,px0,cx,cy+1,fg);
            vrun(scr,cx-1,py0,py0+fh-1,fg); vrun(scr,cx+1,py0,py0+fh-1,fg); break;
        case 0x2566: /* ╦ */
            hrun(scr,px0,px0+fw-1,cy-1,fg); hrun(scr,px0,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,cy,py0+fh-1,fg); vrun(scr,cx+1,cy,py0+fh-1,fg); break;
        case 0x2569: /* ╩ */
            hrun(scr,px0,px0+fw-1,cy-1,fg); hrun(scr,px0,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,py0,cy,fg); vrun(scr,cx+1,py0,cy,fg); break;
        case 0x256C: /* ╬ */
            hrun(scr,px0,px0+fw-1,cy-1,fg); hrun(scr,px0,px0+fw-1,cy+1,fg);
            vrun(scr,cx-1,py0,py0+fh-1,fg); vrun(scr,cx+1,py0,py0+fh-1,fg);
            px_(scr,cx,cy-1,bg); px_(scr,cx,cy+1,bg);
            px_(scr,cx-1,cy,bg); px_(scr,cx+1,cy,bg); break;
        /* Rounded corners */
        case 0x256D: /* ╭ */
            hrun(scr,cx+1,px0+fw-1,cy,fg);
            vrun(scr,cx,cy+1,py0+fh-1,fg);
            px_(scr,cx+1,cy,fg); break;
        case 0x256E: /* ╮ */
            hrun(scr,px0,cx-1,cy,fg);
            vrun(scr,cx,cy+1,py0+fh-1,fg);
            px_(scr,cx-1,cy,fg); break;
        case 0x2570: /* ╰ */
            hrun(scr,cx+1,px0+fw-1,cy,fg);
            vrun(scr,cx,py0,cy-1,fg);
            px_(scr,cx+1,cy,fg); break;
        case 0x256F: /* ╯ */
            hrun(scr,px0,cx-1,cy,fg);
            vrun(scr,cx,py0,cy-1,fg);
            px_(scr,cx-1,cy,fg); break;
        /* Diagonal lines */
        case 0x2571: /* ╱ */
            for(int i=0;i<fh;i++)
                px_(scr,px0+fw-1-(int)((float)i*(fw-1)/(fh>1?fh-1:1)),py0+i,fg);
            break;
        case 0x2572: /* ╲ */
            for(int i=0;i<fh;i++)
                px_(scr,px0+(int)((float)i*(fw-1)/(fh>1?fh-1:1)),py0+i,fg);
            break;
        case 0x2573: /* ╳ */
            for(int i=0;i<fh;i++){
                px_(scr,px0+fw-1-(int)((float)i*(fw-1)/(fh>1?fh-1:1)),py0+i,fg);
                px_(scr,px0+    (int)((float)i*(fw-1)/(fh>1?fh-1:1)),py0+i,fg);
            }
            break;
        default:
#undef HL
#undef HR
#undef VT
#undef VB
#undef HL_K
#undef HR_K
#undef VT_K
#undef VB_K
#undef DHL
#undef DHR
#undef DVT
#undef DVB
            return false;
        }

#undef HL
#undef HR
#undef VT
#undef VB
#undef HL_K
#undef HR_K
#undef VT_K
#undef VB_K
#undef DHL
#undef DHR
#undef DVT
#undef DVB
        return true;
    }

    /* ── Block Elements U+2580–U+259F ───────────────────────────── */
    if (ch >= 0x2580 && ch <= 0x259F) {
        int hw = fw/2, hh = fh/2;
        switch (ch) {
        case 0x2580: fillrect(scr,px0,py0,px0+fw-1,py0+hh-1,fg); break;         /* ▀ upper half */
        case 0x2581: fillrect(scr,px0,py0+7*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▁ 1/8 */
        case 0x2582: fillrect(scr,px0,py0+6*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▂ 2/8 */
        case 0x2583: fillrect(scr,px0,py0+5*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▃ 3/8 */
        case 0x2584: fillrect(scr,px0,py0+hh,    px0+fw-1,py0+fh-1,fg); break;  /* ▄ lower half */
        case 0x2585: fillrect(scr,px0,py0+3*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▅ 5/8 */
        case 0x2586: fillrect(scr,px0,py0+2*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▆ 6/8 */
        case 0x2587: fillrect(scr,px0,py0+1*fh/8,px0+fw-1,py0+fh-1,fg); break;  /* ▇ 7/8 */
        case 0x2588: fillrect(scr,px0,py0,px0+fw-1,py0+fh-1,fg); break;         /* █ full */
        case 0x2589: fillrect(scr,px0,py0,px0+7*fw/8,py0+fh-1,fg); break;       /* ▉ 7/8 wide */
        case 0x258A: fillrect(scr,px0,py0,px0+6*fw/8,py0+fh-1,fg); break;       /* ▊ 6/8 */
        case 0x258B: fillrect(scr,px0,py0,px0+5*fw/8,py0+fh-1,fg); break;       /* ▋ 5/8 */
        case 0x258C: fillrect(scr,px0,py0,px0+hw-1,  py0+fh-1,fg); break;       /* ▌ left half */
        case 0x258D: fillrect(scr,px0,py0,px0+3*fw/8,py0+fh-1,fg); break;       /* ▍ 3/8 */
        case 0x258E: fillrect(scr,px0,py0,px0+2*fw/8,py0+fh-1,fg); break;       /* ▎ 2/8 */
        case 0x258F: fillrect(scr,px0,py0,px0+1*fw/8,py0+fh-1,fg); break;       /* ▏ 1/8 */
        case 0x2590: fillrect(scr,px0+hw,py0,px0+fw-1,py0+fh-1,fg); break;      /* ▐ right half */
        case 0x2591: /* ░ light */
            for(int y=py0;y<py0+fh;y++)
                for(int x=px0;x<px0+fw;x++)
                    if((x+y)%2==0) px_(scr,x,y,fg);
            break;
        case 0x2592: /* ▒ medium */
            for(int y=py0;y<py0+fh;y++)
                for(int x=px0;x<px0+fw;x++)
                    if((x+y)%2==0||(x%2==0&&y%2==0)) px_(scr,x,y,fg);
            break;
        case 0x2593: /* ▓ dark */
            for(int y=py0;y<py0+fh;y++)
                for(int x=px0;x<px0+fw;x++)
                    if((x+y)%4!=3) px_(scr,x,y,fg);
            break;
        /* Quadrants */
        case 0x2596: fillrect(scr,px0,py0+hh,px0+hw-1,py0+fh-1,fg); break;      /* ▖ LL */
        case 0x2597: fillrect(scr,px0+hw,py0+hh,px0+fw-1,py0+fh-1,fg); break;   /* ▗ LR */
        case 0x2598: fillrect(scr,px0,py0,px0+hw-1,py0+hh-1,fg); break;         /* ▘ UL */
        case 0x259D: fillrect(scr,px0+hw,py0,px0+fw-1,py0+hh-1,fg); break;      /* ▝ UR */
        case 0x2599: /* ▙ all but UR */
            fillrect(scr,px0,py0,px0+hw-1,py0+fh-1,fg);
            fillrect(scr,px0+hw,py0+hh,px0+fw-1,py0+fh-1,fg); break;
        case 0x259A: /* ▚ UL + LR */
            fillrect(scr,px0,py0,px0+hw-1,py0+hh-1,fg);
            fillrect(scr,px0+hw,py0+hh,px0+fw-1,py0+fh-1,fg); break;
        case 0x259B: /* ▛ all but LR */
            fillrect(scr,px0,py0,px0+fw-1,py0+hh-1,fg);
            fillrect(scr,px0,py0+hh,px0+hw-1,py0+fh-1,fg); break;
        case 0x259C: /* ▜ all but LL */
            fillrect(scr,px0,py0,px0+fw-1,py0+hh-1,fg);
            fillrect(scr,px0+hw,py0+hh,px0+fw-1,py0+fh-1,fg); break;
        case 0x259E: /* ▞ UR + LL */
            fillrect(scr,px0+hw,py0,px0+fw-1,py0+hh-1,fg);
            fillrect(scr,px0,py0+hh,px0+hw-1,py0+fh-1,fg); break;
        case 0x259F: /* ▟ all but UL */
            fillrect(scr,px0+hw,py0,px0+fw-1,py0+hh-1,fg);
            fillrect(scr,px0,py0+hh,px0+fw-1,py0+fh-1,fg); break;
        default: return false;
        }
        return true;
    }

    /* ── Braille Patterns U+2800–U+28FF ─────────────────────────── */
    if (ch >= 0x2800 && ch <= 0x28FF) {
        /*
         * ISO 11548-1 dot layout (8-dot):
         *   col-0  col-1
         *   dot1   dot4    row 0
         *   dot2   dot5    row 1
         *   dot3   dot6    row 2
         *   dot7   dot8    row 3
         *
         * Unicode bit → dot: bit(N-1) = dotN
         */
        uint8_t bits = (uint8_t)(ch - 0x2800);

        int dw  = fw / 2;          /* column width  */
        int dh  = fh / 4;          /* row height    */
        int dr  = (dw > 2) ? 1 : 0; /* dot radius  */

        /* dot centre x: col 0 = px0+dw/2,  col 1 = px0+dw+dw/2 */
        /* dot centre y: row r = py0+r*dh+dh/2                    */
        static const int dc[8] = {0,0,0,1,1,1,0,1}; /* col index */
        static const int dr_[8]= {0,1,2,0,1,2,3,3}; /* row index */

        for (int d = 0; d < 8; d++) {
            if (!(bits & (uint8_t)(1u << d))) continue;
            int bx = px0 + dc[d]*dw + dw/2;
            int by = py0 + dr_[d]*dh + dh/2;
            for (int dy2 = -dr; dy2 <= dr; dy2++)
                for (int dx2 = -dr; dx2 <= dr; dx2++)
                    px_(scr, bx+dx2, by+dy2, fg);
        }
        return true;
    }

    return false;
}
