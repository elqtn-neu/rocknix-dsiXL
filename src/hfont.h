// hfont.h: the menu's text at the panels' own resolution: M PLUS 1p, baked by tools/gen_hdfont.py at each size
// the menu uses (4 bits of coverage a pixel).
#pragma once
#include "gfx.h"

enum { HF_CAPS, HF_SMALL, HF_BODY, HF_BODYB, HF_LABEL, HF_BTN, HF_TITLE, HF_HERO, HF_HUGE, HF_CLOCK, HF_MD, HF_BD, HF_LG, HF_LOGO, HF_XS, HF_COUNT };

typedef struct { uint16_t cp; uint32_t off; uint16_t w, h; int16_t ox, oy; int16_t adv; } HGlyph;
typedef struct { int ascent, descent, count; const HGlyph *glyphs; const uint8_t *bits; } HFontData;
extern const HFontData hfont_data[HF_COUNT];

int ht_h(int f);                                                    /* the line's height */
int ht_w(int f, const char *utf8, int track);                       /* track: extra pixels between letters */
/* draws with the line's top at y; returns the end x */
int ht(Surf *s, int f, int x, int y, uint32_t c, int alpha, int track, const char *utf8);
void ht_center(Surf *s, int f, int cx, int y, uint32_t c, int alpha, int track, const char *utf8);
int ht_right(Surf *s, int f, int rx, int y, uint32_t c, int alpha, int track, const char *utf8);
/* the text cut to max_w with an ellipsis (into out) */
void ht_fit(int f, const char *utf8, int max_w, char *out, int out_sz);
/* word-wrapped into width w, at most max_lines (the last one ellipsized); returns the lines drawn */
int ht_wrap(Surf *s, int f, int x, int y, int w, int line_h, int max_lines, uint32_t c, int alpha, const char *utf8);
int ht_wrap_lines(int f, int w, int max_lines, const char *utf8);
