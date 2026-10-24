// ui_int.h: what the parts of the menu share (ui.c the core: state, theme, input, motion, the frame and its big
// moments; ui_home.c the shelves; ui_game.c a game's page; ui_set.c Settings).
#pragma once
#include "ui.h"
#include "config.h"
#include "library.h"
#include "hfont.h"
#include "hgfx.h"
#include "art.h"
#include "audio.h"
#include <math.h>

#define SW 1024
#define SH 768

/* ---- motion: springs (stiffness k, damping c), stepped in small fixed steps ---- */
typedef struct { float x, v, to; } Spr;
void spr_step(Spr *s, float k, float c, float dt);
int spr_moving(const Spr *s, float eps);
static inline void spr_set(Spr *s, float x) { s->x = s->to = x; s->v = 0; }
/* the house springs: snappy (UI), bouncy (things that land), soft (big moves) */
#define K_SNAP 520.f
#define C_SNAP 42.f
#define K_BOUNCE 420.f
#define C_BOUNCE 24.f
#define K_SOFT 180.f
#define C_SOFT 26.f

extern double T;                        /* the menu's time, seconds */

/* ---- the look ---- */
typedef struct {
    uint32_t bg, bg2, card, text, sub, faint, chip, track, line, shadow_a;
    int dark;
} Pal;
extern Pal pal;
extern uint32_t accent, accent_ink;     /* the user's colour (the DSi's sixteen), and one dark enough for white on it */
void theme_update(void);                /* by the clock, or as set; true when it changed */
int theme_changed(void);

/* ---- the shelves ---- */
enum { SH_FAV, SH_DS, SH_GBA, SH_RECENT, SH_COUNT };
#define MAXG 1024
extern Game *shelf[SH_COUNT][MAXG];
extern int shn[SH_COUNT], tab, sel[SH_COUNT];
void shelves_build(void);
Game *cur_game(void);

/* ---- pages, and the big moments ---- */
enum { P_HOME, P_GAME, P_SETTINGS };
enum { Q_NONE, Q_BOOT, Q_LAUNCH, Q_RETURN, Q_SLEEP, Q_ASLEEP, Q_WAKE };
extern int page, page_from, seq;
extern Spr page_k;                      /* 0 at the old page .. 1 at the new one */
extern double seq_t;
void go_page(int p);
void start_launch(int how);             /* how: 0 play (resume when there is one), 1 from the start, 2 + n slot n */

/* ---- input ---- */
extern Input in, pin;
int pressed(int b);
int released(int b);
int repeat(int b);                      /* pressed, then again and again while held, faster */
typedef struct { int down, x, y, x0, y0, moved, id; double t0, lt; float vx; int lx; } Touch;
extern Touch tch;                        /* the bottom screen's touch */
int tapped(int x, int y, int w, int h);  /* a touch that went down and came up inside, without dragging */
int touching(int x, int y, int w, int h);/* held down inside (for the pressed look) */

/* ---- pressed buttons: a press sinks them; the release springs them back ---- */
float press_of(int id);                 /* 0..1 */
void press_hit(int id);                 /* this button was activated (by a key): it sinks for a moment */

/* ---- drawing helpers shared by the pages ---- */
void dirty(int screen);                 /* redraw that screen (0 top, 1 bottom) this frame */
void card(Surf *s, int x, int y, int w, int h, int r, int lift);   /* a raised card in the theme */
void pill_button(Surf *s, int x, int y, int w, int h, const char *label, const char *key, int primary, float press, int font);
void key_cap(Surf *s, int cx, int cy, int r, const char *k, uint32_t bg, uint32_t fg);   /* an A / B / Y button's mark */
void icon(Surf *s, int which, float cx, float cy, float size, uint32_t c, int a);
enum { IC_GEAR, IC_SUN, IC_MOON, IC_STAR, IC_STAR_O, IC_BACK, IC_PLAY, IC_RESTART, IC_POWER, IC_EXIT, IC_WIFI, IC_LEFT, IC_RIGHT, IC_SLEEP, IC_IMAGE, IC_CHECK };
void focus_ring(Surf *s);               /* the d-pad's focus, gliding between things */
void focus_to(int x, int y, int w, int h, int r);
void focus_hide(void);
void status_bar(Surf *s, int y, int alpha);
void game_cover(Surf *s, Game *g, int x, int y, int w, int h, int r, int a);   /* its art, or a made-up cover */
void dss_screens(Surf *s, const Img *pic, Game *g, int x, int y, int w, int a);  /* both DS screens, small (w each) */
void ago(long long when, char *out, int n);          /* "2 hours ago" */
void played_text(const Game *g, char *out, int n);   /* "41 h", "20 min" */
/* where a game's DraStic states are */
int ds_resume_path(const Game *g, char *out, int n); /* the quick resume state; 1 when it's there */
int ds_slot_path(const Game *g, int slot, char *out, int n);
int resume_available(const Game *g, long long *when);

/* ---- the pages ---- */
void home_enter(void);
void home_update(float dt);
void home_draw_top(Surf *s);
void home_draw_bot(Surf *s, float yoff, int alpha);
void home_selection_changed(void);
void game_enter(void);
void game_prefetch(Game *g);           /* its page made ready ahead (while the home is idle on it) */
void game_update(float dt);
void game_draw_top(Surf *s);
void game_draw_bot(Surf *s, float yoff, int alpha);
void set_enter(void);
void set_update(float dt);
void set_draw_top(Surf *s);
void set_draw_bot(Surf *s, float yoff, int alpha);

/* the system menu (START) */
extern int sysmenu;
void sysmenu_open(void);
void notice(const char *msg);

/* BananaOS's settings, where its own Settings keeps them */
void bset_get(const char *file, const char *key, char *out, int n, const char *def);
void bset_set(const char *file, const char *key, const char *val);
const char *bpath(const char *p);
int clock24(void);
void brightness_set(int lv);
extern int bri_lv;

/* ---- more shared pieces ---- */
void press_touch(int id, int x, int y, int w, int h);   /* a button under the finger sinks while it's held */
extern int tch_up, tch_up_moved;                          /* the finger came up this frame (and had dragged) */
/* the cartridge: where the selected one rests on the deck, and one drawn at any rect (its sprite, scaled) */
void home_cart_rect(Game *g, float *x, float *y, float *w, float *h);
void cart_draw(Surf *s, Game *g, float x, float y, float w, float h, int a);
extern int cover_fast;
void cover_prepare(Game *g, int w, int h, int r);   /* game_cover's picture at that size made now (nothing drawn) */                  /* game_cover: things in motion (scaled straight, nothing kept) */
void img_round(Img *im, int r);         /* an image's corners rounded off, into its alpha */
uint32_t accent_text(void);             /* the user's colour for words on the page */
