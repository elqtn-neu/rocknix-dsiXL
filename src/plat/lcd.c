// lcd.c: a DS pixel drawn the way a DSi's LCD shows it, when the panel has 3 or more of its own pixels for each.
// On a DSi each pixel is three upright red, green and blue stripes with a thin dark gap around them, and the gap
// between rows shows more than the one between columns. Scaled up as plain squares on a sharp 1024x768 panel the
// pixels run together, and every soft edge (antialiased text, shadows) reads as blur. Here each k x k block gets
// the DSi's structure back: the last row and column are the gaps, the rest are the stripes, each a touch brighter
// in its own colour. Cheap: a few multiplies per DS pixel (all three channels at once), reused along a run of one
// colour.
// Mode 2 first gives each colour the DS screen's own: duller, a little warmer (the profile of SuperDrastic's and
// ROCKNIX's "NDS color" shaders, the same polynomials, so the menu and a game with dsi-lcd-color.frag match).
#include "plat.h"
#include <string.h>
#include <math.h>

int plat_lcd = 1;
void plat_set_lcd(int mode) { plat_lcd = mode < 0 ? 0 : mode > 2 ? 2 : mode; }

/* the DS screen's colours: to linear light, the DS's mix of the three, back (ds-*-color.frag's ndsColor) */
static float lin_lut[256];
static uint32_t nds_color_calc(uint32_t c) {
    if (!lin_lut[255]) for (int i = 0; i < 256; i++) {
        float x = i / 255.0f, l = (((0.0450218219f * x - 0.15107093f) * x + 0.980162582f) * x + 0.0161565261f) * x - 0.000192440297f;
        lin_lut[i] = l < 0 ? 0 : l > 1 ? 1 : l;
    }
    float r = lin_lut[c >> 16 & 255], g = lin_lut[c >> 8 & 255], b = lin_lut[c & 255];
    float o[3] = { 0.87f * r + 0.255f * g - 0.125f * b, 0.10f * r + 0.645f * g + 0.255f * b, 0.10f * r + 0.17f * g + 0.73f * b };
    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        float t = sqrtf(o[i] < 0 ? 0 : o[i] > 1 ? 1 : o[i]);
        float v = (((((0.397060658f * t - 1.36815007f) * t + 1.87310197f) * t - 1.321717f) * t + 0.564332005f) * t + 0.855903541f) * t - 0.000458519754f;
        int q = (int)(v * 255 + 0.5f); q = q < 0 ? 0 : q > 255 ? 255 : q;
        out |= (uint32_t)q << (16 - 8 * i);
    }
    return out;
}
/* (a menu frame has a few hundred colours, most the same every frame: each worked out once) */
static uint32_t nc_key[4096], nc_val[4096];
static inline uint32_t nds_color(uint32_t c) {
    uint32_t h = (c * 2654435761u) >> 20;
    if (nc_key[h] != (c | 0x80000000u)) { nc_key[h] = c | 0x80000000u; nc_val[h] = nds_color_calc(c); }
    return nc_val[h];
}

#define GAP_ROW 200     /* x/256: the dark line between rows */
#define GAP_COL 230     /*        between columns */
#define STRIPE  246     /*        the other two colours in a stripe */

/* all three channels of c times f/256 (f <= 256), two multiplies */
static inline uint32_t scale(uint32_t c, uint32_t f) {
    return (((c & 0xff00ff) * f >> 8) & 0xff00ff) | (((c & 0x00ff00) * f >> 8) & 0x00ff00);
}

/* one DS pixel's k columns: k-1 stripes (red, green, blue across them) and the gap column */
static inline void cols(uint32_t c, int k, uint32_t *row, uint32_t *gap) {
    uint32_t s = scale(c, STRIPE);
    for (int j = 0; j < k - 1; j++) {
        int ch = j * 3 / (k - 1);
        uint32_t keep = 0xff0000u >> (ch * 8);
        row[j] = (c & keep) | (s & ~keep & 0xffffff);
        gap[j] = scale(row[j], GAP_ROW);
    }
    row[k - 1] = scale(c, GAP_COL); gap[k - 1] = scale(row[k - 1], GAP_ROW);
}

int lcd_rows(const uint32_t *src, int w, int k, uint32_t *row, uint32_t *gap) { return lcd_rows_mode(plat_lcd, src, w, k, row, gap); }
int lcd_rows_mode(int mode, const uint32_t *src, int w, int k, uint32_t *row, uint32_t *gap) {
    if (!mode || k < 3 || k > 16) return 0;
    uint32_t last = ~src[0], rr[16], gg[16];
    if (k == 4) {                                     /* the RG DS Plus: written out, the common case */
        for (int x = 0; x < w; x++, row += 4, gap += 4) {
            uint32_t c = src[x] & 0xffffff;
            if (c != last) { last = c; cols(mode == 2 ? nds_color(c) : c, 4, rr, gg); }
            row[0] = rr[0]; row[1] = rr[1]; row[2] = rr[2]; row[3] = rr[3];
            gap[0] = gg[0]; gap[1] = gg[1]; gap[2] = gg[2]; gap[3] = gg[3];
        }
        return 1;
    }
    for (int x = 0; x < w; x++, row += k, gap += k) {
        uint32_t c = src[x] & 0xffffff;
        if (c != last) { last = c; cols(mode == 2 ? nds_color(c) : c, k, rr, gg); }
        memcpy(row, rr, (size_t)k * 4); memcpy(gap, gg, (size_t)k * 4);
    }
    return 1;
}

uint32_t lcd_nds_color(uint32_t c) { return nds_color(c & 0xffffff); }
