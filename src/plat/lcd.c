// lcd.c: a DS pixel drawn the way a DSi's LCD shows it, when the panel has 3 or more of its own pixels for each.
// On a DSi each pixel is three upright red, green and blue stripes with a thin dark gap around them, and the gap
// between rows shows more than the one between columns. Scaled up as plain squares on a sharp 1024x768 panel the
// pixels run together, and every soft edge (antialiased text, shadows) reads as blur. Here each k x k block gets
// the DSi's structure back: the last row and column are the gaps, the rest are the stripes, each a touch brighter
// in its own colour. Cheap: a few multiplies per DS pixel (all three channels at once), reused along a run of one
// colour.
#include "plat.h"
#include <string.h>

int plat_lcd = 1;
void plat_set_lcd(int on) { plat_lcd = !!on; }

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

int lcd_rows(const uint32_t *src, int w, int k, uint32_t *row, uint32_t *gap) {
    if (!plat_lcd || k < 3 || k > 16) return 0;
    uint32_t last = ~src[0], rr[16], gg[16];
    if (k == 4) {                                     /* the RG DS Plus: written out, the common case */
        for (int x = 0; x < w; x++, row += 4, gap += 4) {
            uint32_t c = src[x] & 0xffffff;
            if (c != last) { last = c; cols(c, 4, rr, gg); }
            row[0] = rr[0]; row[1] = rr[1]; row[2] = rr[2]; row[3] = rr[3];
            gap[0] = gg[0]; gap[1] = gg[1]; gap[2] = gg[2]; gap[3] = gg[3];
        }
        return 1;
    }
    for (int x = 0; x < w; x++, row += k, gap += k) {
        uint32_t c = src[x] & 0xffffff;
        if (c != last) { last = c; cols(c, k, rr, gg); }
        memcpy(row, rr, (size_t)k * 4); memcpy(gap, gg, (size_t)k * 4);
    }
    return 1;
}
