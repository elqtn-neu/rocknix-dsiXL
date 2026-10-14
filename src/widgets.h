// widgets.h: the DSi menu's furniture, drawn at the DS's own pixels: the striped backgrounds, the status bar, the
// L/R tabs, the selection frame, the scroll bar, the windows and buttons of the options.
#pragma once
#include "gfx.h"

/* the palette, sampled from the DSi menu */
#define C_STRIPE_A  0xebebeb
#define C_STRIPE_B  0xf3f3f3
#define C_TEXT      0x414141
#define C_TEXT_SOFT 0x8a8a8a
#define C_WATERMARK 0xd6d6d6

typedef struct { uint32_t base, dark, mid, light, pale; } Accent;
Accent accent(void);                     /* from the user's favourite colour */
Accent accent_of(uint32_t base);

void bg_top(Surf *s, double t);
/* white, the stripes and a slow pattern; tint: the selected game's colour mixed in by amt (0..256); par_x: how far the
 * pattern has slid (parallax, pixels) */
void bg_bottom(Surf *s, double t, uint32_t tint, int amt, float par_x);
/* the system's emblem at the top of the bottom screen: its little console and its name, pressed into the plastic */
void emblem(Surf *s, int sys, float cx, int y, uint32_t tint, int alpha);
void status_bar(Surf *s, float y_off, int alpha, double t);
/* a DSi tab: the dark L/R button glyph and a label; side 0 left (hangs from the top-left), 1 right */
void tab(Surf *s, int side, int x, int y, int w, const char *label, int active, float press, int alpha);
void btn_glyph(Surf *s, int x, int y, const char *letter, uint32_t bg, int alpha);
/* the blue (accent) frame around the selected item, its label underneath ("START") */
void sel_frame(Surf *s, float cx, float cy, float w, float h, const char *label, uint32_t base, int alpha);
/* the scroll bar along the bottom: count items, sel, view = items visible; press -1 left, 1 right */
void scroll_bar(Surf *s, int y, int count, float pos, float view, int press, int alpha);
int scroll_bar_hit(int x, int y0, int count, float view, int *zone);   /* the item a touch at x points at */
/* a window and a button in the DSi's style */
void window(Surf *s, float x, float y, float w, float h, const char *title, int alpha);
void button(Surf *s, float x, float y, float w, float h, const char *label, int focus, float press, int alpha);
/* a blinking 4-point sparkle */
void sparkle(Surf *s, float x, float y, float size, uint32_t c, int alpha);
/* a glossy light band swept over an image's opaque pixels */
void blit_glint(Surf *s, const Img *im, float dx, float dy, float dw, float dh, float phase, float strength);
