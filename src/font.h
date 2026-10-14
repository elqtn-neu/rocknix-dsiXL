// font.h: the menu's text, from the fonts baked by tools/gen_font.py (2-bit antialiased, like the DSi's).
#pragma once
#include "gfx.h"

enum { FONT_SM, FONT_MD, FONT_BD, FONT_LG, FONT_XL, FONT_HS, FONT_COUNT };

typedef struct { uint16_t cp; uint32_t off; uint8_t w, h; int8_t ox, oy; int8_t adv; } Glyph;
typedef struct { int ascent, descent, count; const Glyph *glyphs; const uint8_t *bits; } FontData;
extern const FontData font_data[FONT_COUNT];

int text_w(int font, const char *utf8);
int text_h(int font);                                    /* the line height */
/* draws at (x, y) = the top of the line; returns the end x. alpha 0..255 */
int text(Surf *s, int font, int x, int y, uint32_t c, int alpha, const char *utf8);
void text_center(Surf *s, int font, int cx, int y, uint32_t c, int alpha, const char *utf8);
int text_right(Surf *s, int font, int rx, int y, uint32_t c, int alpha, const char *utf8);
/* word-wrapped and centred lines within width w; returns the height used */
int text_wrap_center(Surf *s, int font, int cx, int y, int w, int line_h, uint32_t c, int alpha, const char *utf8);
/* the text shortened with an ellipsis to fit max_w (into out) */
void text_fit(int font, const char *utf8, int max_w, char *out, int out_sz);
/* text into an image (logos made from a title): white on transparent, to be tinted */
int text_to_img(int font, const char *utf8, Img *out, uint32_t c);
uint32_t utf8_next(const char **s);
