// dsi.h: the DSi menu's furniture, at the panels' own resolution: the striped backgrounds with their faint diamonds,
// the tabs hanging from the top edge, the blue frame round the selected game ("START"), the scroll bar, the windows
// with their coloured title bars, the glossy buttons, the emblem pressed into the plastic, a game's name as a logo,
// the status bar, stars and sparkles. Measured from the DSi at 1:1 (256 x 192) and drawn four times finer.
#pragma once
#include "gfx.h"

typedef struct { uint32_t base, dark, mid, light, pale; } Accent;
Accent dsi_accent(void);                 /* from the user's favourite colour */
Accent dsi_accent_of(uint32_t base);

#define D_TEXT      0x414141
#define D_TEXT_SOFT 0x8a8a8a
#define D_GOLD      0xd9a21c
#define D_STAR      0xf6b81c
#define D_BLUE      0x1b8ef2

/* backgrounds (copied from a picture made once per colour): the top screen's grey stripes, the bottom's lighter ones
 * tinted toward the selected game's colour (amt 0..1), the favourites' cream ones with stars; par: the pattern's slide
 * (it moves slower than the cartridges while the shelf moves) */
void dsi_bg_top(Surf *s, uint32_t tint, float amt);
void dsi_bg_bottom(Surf *s, uint32_t tint, float amt, float par);
void dsi_bg_favs(Surf *s, uint32_t tint, float amt, float par);
void dsi_bg_dark(Surf *s, uint32_t tint, float amt, float par);   /* the night's */

/* a tab hanging from the top edge: glyph "L" / "R" / NULL, its label (or a star when label is "*") */
void dsi_tab(Surf *s, int x, int y, int w, const char *glyph, int glyph_right, const char *label, int count, int active, float press, int alpha);
/* the selected game's frame: the accent, glossy, its label on the bottom bar ("START"); shine 0..1 a light passing */
void dsi_frame(Surf *s, float cx, float cy, float w, float h, const char *label, float shine, float press, int alpha);
/* the scroll bar: count games, pos the shelf, marks[i] a favourite (gold), press -1 / 1 an arrow held */
void dsi_scroll_bar(Surf *s, int y, int count, float pos, int press, const uint8_t *marks, int alpha);
int dsi_scroll_hit(int x, int count, int *zone);   /* the game a touch at x points at (zone -1 / 1: the arrows) */
#define DSB_H 64
#define DSB_BTN 76
/* a window: a white panel, its title bar in the accent */
void dsi_window(Surf *s, int x, int y, int w, int h, const char *title, int alpha);
/* a button: white, glossy, its border the accent when focused; key: the button's glyph ("A") or NULL */
void dsi_button(Surf *s, int x, int y, int w, int h, const char *label, const char *key, int focus, float press, int alpha);
/* the accent button (the one that starts things) */
void dsi_button_main(Surf *s, int x, int y, int w, int h, const char *label, const char *key, float press, int alpha);
/* the faces alone (a button with things of its own drawn on it): white, or the accent */
void dsi_face(Surf *s, int x, int y, int w, int h, int r, int focus, float press, int alpha);
void dsi_face_main(Surf *s, int x, int y, int w, int h, int r, float press, int alpha);
/* a section's heading: small grey capitals, a thin rule after them */
void dsi_section(Surf *s, int x, int y, int w, const char *text, int alpha);
/* a round icon button (the sun, the gear) */
void dsi_round(Surf *s, float cx, float cy, float r, float press, int alpha);
/* the console's emblem pressed into the plastic, with thin rules either side: sys 0 DS, 1 GBA, 2 favourites, 3 recent */
void dsi_emblem(Surf *s, int sys, int cy, uint32_t tint, int alpha);
/* a game's name as the DSi shows it on the top screen: bold, outlined, a light gradient, a soft shadow (made once) */
int dsi_logo(const char *name, int max_w, Img *out);
void dsi_status(Surf *s, int y, int alpha);
void dsi_star(Surf *s, float cx, float cy, float r, uint32_t c, int alpha);
void dsi_sparkle(Surf *s, float x, float y, float size, uint32_t c, int alpha);
/* a light-coloured speech balloon pointing down at (cx, tip_y) */
void dsi_balloon(Surf *s, float cx, float tip_y, const char *txt, int alpha);
/* the button glyph (A, B, L...) in its little rounded square */
void dsi_glyph(Surf *s, int x, int y, const char *letter, uint32_t bg, int alpha);
