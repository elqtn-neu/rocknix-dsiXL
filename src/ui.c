// ui.c: the menu. Everything moves on springs and eases, at 60 frames a second, drawn at the DS's 256x192:
//   boot     the DSi XL's start: out of black, the logo and its chime on the top screen, the health and safety screen
//            on the bottom, "Touch the Touch Screen to continue." (a touch, as on the DSi; A held through it goes
//            straight into the game played last); then white, and everything slides in, as the DSi menu does
//   menu     top: the status bar and the selected game's logo, floating. bottom: the cartridges in a row, the
//            selected one popped out in its frame and in colour, the others greyer; NDS / GBA tabs (L / R); the
//            DSi's scroll bar along the bottom
//   launch   the cartridge jumps off the bottom screen, comes up into the top one, hangs, and slams down into the
//            slot at the bottom of the top screen (where a DSi's card slot is); then both screens fade to black,
//            and stay black until the game draws
//   return   back from a game: out of black, the cartridge comes out of the slot and drops back into its place
#define _GNU_SOURCE
#include "ui.h"
#include "config.h"
#include "library.h"
#include "cart.h"
#include "font.h"
#include "img.h"
#include "widgets.h"
#include "audio.h"
#include "rumble.h"
#include "gamecfg.h"
#include "power.h"
#include "job.h"
#include "panels.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <limits.h>

enum { ST_BOOT, ST_MENU, ST_LAUNCH };

static double T;                        /* seconds since start */
static int state; static double st_t;  /* the state and the time in it */
static Input in, pin;                   /* this frame's input and the last one's */
static int exit_code = -1;

/* ---- input ---- */

static int btn_a(void) { return cfg.swap_ab ? B_B : B_A; }
static int btn_b(void) { return cfg.swap_ab ? B_A : B_B; }
static int pressed(int b) { return (in.held & BIT(b)) && !(pin.held & BIT(b)); }
static double rep_at[B_COUNT]; static int rep_n[B_COUNT];
/* a held direction repeats: after 0.3 s, then faster the longer it's held (like the DSi's scrolling) */
static int repeat(int b) {
    if (pressed(b)) { rep_at[b] = T + 0.30; rep_n[b] = 0; return 1; }
    if ((in.held & BIT(b)) && T >= rep_at[b]) { rep_n[b]++; rep_at[b] = T + (rep_n[b] > 6 ? 0.055 : 0.095); return 1; }
    return 0;
}
static int any_pressed(void) { return (in.held & ~pin.held) != 0 || (in.touch[1] && !pin.touch[1]) || (in.touch[0] && !pin.touch[0]); }

/* ---- the menu's state ---- */

/* the tabs: the two systems, and the favourites (a mix of both, the game played last first: "continue") */
enum { V_NDS = SYS_NDS, V_GBA = SYS_GBA, V_FAV, V_COUNT };
static int tab_pos(int v) { return v == V_NDS ? 0 : v == V_FAV ? 1 : 2; }     /* left to right: NDS, star, GBA */
static const int TAB_AT[3] = { V_NDS, V_FAV, V_GBA };
static Game **favv; static int favv_n; static int has_cont;
/* the NDS and GBA rows: the games in the order chosen (by name, recently played, most played), hidden ones left out */
static int *ord[SYS_COUNT], ordn[SYS_COUNT];
static int vn(int v) { return v < V_FAV ? ordn[v] : favv_n; }
static Game *vg(int v, int i) { return v < V_FAV ? &lib[v].g[ord[v][i]] : favv[i]; }
static int is_cont(int v, int i) { return v == V_FAV && has_cont && i == 0; }

typedef struct { Spring pos; int sel; } Row;
static Row row[V_COUNT];
static int sys;                         /* the tab shown (V_*) */
static double sw_t = -1; static int sw_from, sw_dir;   /* switching systems: since when, from which, which way */
static Spring frame_pop;                /* the selection frame's bounce */
static double sel_t;                    /* when the selection last changed */
static double intro_t = -1;             /* the menu sliding in (after boot, or back from a game) */
static int bump_dir; static double bump_t = -10;     /* the end of the list: a nudge */
/* the feel of moving: a held direction glides (faster and faster), a flick coasts, the ends stretch like rubber, and
 * the selection locking into the frame is a detent (a click, a squeeze of the frame, a buzz) */
static int glide_dir; static float glide_speed; static double held_since[2];
static int coast;
static Spring rubber;                   /* the row pulled past an end, in pixels */
static int notch_pending;
static int a_hold; static Spring sink;
static Spring frame_x = { 128, 0 };
static double view_t = -10;             /* X: the top screen switched between the logo and the photo */
static double fav_t = -10; static Game *fav_g; static int fav_on;   /* Y: the star popping on (or off) */
static double reorder_t = -10;          /* the list re-sorted (a favourite moved): the neighbours fade in */
static uint8_t *marks; static int marks_n;     /* the selection frame travels with the cartridges, a little behind them */  /* A (or a finger) held on the selected cartridge: it sinks in */

/* the top screen's logo: the current one and the one leaving */
static Game *logo_cur, *logo_prev; static double logo_t = -10;

/* touch on the bottom screen */
static struct {
    int down, moved, zone;      /* zone: 0 cartridges, 1 tabs, 2 scroll bar */
    int x0, y0, x, y; double t0;
    float pos0, vel, lastx; double lastt;
} tch;
static int sb_press; static double sb_press_t;

/* options */
static int opt_open; static double opt_t; static int opt_sel; static float opt_anim;
static int optv[32], optn;              /* the options shown (ids), built when the window opens */
static Spring opt_scroll; static float opt_drag0;   /* the list's first row (scrolling), where a drag started */
static void opt_open_now(void);
/* the emulators' settings (SELECT, the gear, Options): a page for the DS, a page for the GBA (L/R), each its options,
 * then Recommended and Done. They're for every game of the system. */
static int gs_open; static float gs_anim; static int gs_sys, gs_sel, gs_n;
static int gs_choice[SYS_COUNT][GOPT_MAX]; static double gs_val_t = -10; static int gs_val_dir; static double gs_open_t = -10;
static double gs_page_t = -10; static int gs_page_dir;
static void start_launch(void);
static void gs_open_here(void);

/* launch */
static Game *lg; static int l_sys, l_view; static int launched_file_written;
/* the return from a game */
static Game *rg; static int r_view; static double ret_t = -1;

/* particles: dust from the slam, sparkles, confetti */
enum { P_DUST, P_SPARK, P_CONFETTI };
typedef struct { float x, y, vx, vy, life, max, size; uint32_t c; int scr, kind; float ph; } Part;
static Part parts[192];

/* per cartridge: its jelly wobble and its hop (indexed like lib[s].g) */
typedef struct { Spring wob; } Fx;
static Fx *fx[V_COUNT]; static int fx_n[V_COUNT];
static Spring frame_w, frame_h;         /* the frame's size: a DS card's or a Game Pak's (the favourites mix them) */
static double last_input = 0;           /* idle moments start 15 s after this */
static float tint_rgb[3]; static int tint_init;   /* the bottom screen's colour, eased toward the selected game's */
static int confetti_done;
typedef struct { float x, y, v, ph, sz; } Flake;
static Flake flakes[30];
static double shake_t = -10; static float shake_amp;

static Img boot_logo;
static double auto_go_t = -1;
static int help_wait;                   /* the controls window was just closed before a first launch: launch now */
/* back from a game that didn't start: what went wrong (launch.sh's /tmp/dsixl-error), in a window until OK */
static int err_pending, err_open; static float err_anim; static char err_title[160], err_msg[400];
/* power: the screens dim and go off when nothing is touched; the input that wakes them does nothing else */
static int rescued;
static int pw_mode; static int swallow; static double wake_t = -10; static int wake_black; static uint32_t raw_held; static int raw_touch;           /* A held through the start: "continue" starts by itself at this time */

/* brightness: the DSi's five levels (device/brightness.sh), changed with the Anbernic button + volume (ROCKNIX's
 * input service runs the script, the menu sees its file change) or the sun button; a bar shows on the top screen */
static int bri_lv = 4; static double bri_t = -10, bri_poll = -10; static struct timespec bri_mt; static int bri_seen;

/* ---- helpers ---- */

static Game *cur_game(int v) { int n = vn(v); return n ? vg(v, CLAMP(row[v].sel, 0, n - 1)) : 0; }

/* the favourites tab's list: the game played last ("continue"), then the favourites in the order they were added */
static void fx_size(int v) {
    free(fx[v]); fx[v] = calloc((size_t)MAX(1, vn(v)), sizeof(Fx)); fx_n[v] = fx[v] ? vn(v) : 0;
}
static void fav_rebuild(void) {
    Game *keep = cur_game(V_FAV);
    int cap = 1;
    for (int s = 0; s < SYS_COUNT; s++) cap += lib[s].n;
    free(favv); favv = calloc((size_t)cap, sizeof(Game *)); favv_n = 0; has_cont = 0;
    if (!favv) return;
    Game *recent = 0;
    if (cfg.recent[0]) for (int i = 0; i < lib[cfg.recent_sys].n; i++) if (!strcmp(lib[cfg.recent_sys].g[i].file, cfg.recent)) recent = &lib[cfg.recent_sys].g[i];
    if (recent) { favv[favv_n++] = recent; has_cont = 1; }
    int max = 0;
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) max = MAX(max, lib[s].g[i].fav);
    for (int o = 1; o <= max; o++)
        for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++)
            if (lib[s].g[i].fav == o && &lib[s].g[i] != recent && (!lib[s].g[i].hidden || cfg.show_hidden)) favv[favv_n++] = &lib[s].g[i];
    fx_size(V_FAV);
    /* the same game stays selected if it's still there */
    int sel = 0;
    for (int i = 0; i < favv_n; i++) if (favv[i] == keep) sel = i;
    row[V_FAV].sel = CLAMP(sel, 0, MAX(0, favv_n - 1));
}

static int ord_sys;
static int ord_cmp(const void *a, const void *b) {
    int i = *(const int *)a, j = *(const int *)b;
    const Game *x = &lib[ord_sys].g[i], *y = &lib[ord_sys].g[j];
    if (cfg.sort == 1 && x->last_played != y->last_played) return x->last_played > y->last_played ? -1 : 1;
    if (cfg.sort == 2 && x->secs != y->secs) return x->secs > y->secs ? -1 : 1;
    return i - j;                       /* (the library is in name order) */
}
static void order_rebuild(void) {
    for (int s = 0; s < SYS_COUNT; s++) {
        free(ord[s]); ord[s] = malloc(sizeof(int) * (size_t)MAX(1, lib[s].n)); ordn[s] = 0;
        if (!ord[s]) continue;
        for (int i = 0; i < lib[s].n; i++) if (!lib[s].g[i].hidden || cfg.show_hidden) ord[s][ordn[s]++] = i;
        ord_sys = s; if (ordn[s] > 1) qsort(ord[s], (size_t)ordn[s], sizeof(int), ord_cmp);
        /* the same game stays selected */
        int sel = 0;
        for (int i = 0; i < ordn[s]; i++) if (!strcmp(vg(s, i)->file, cfg.last[s])) sel = i;
        row[s].sel = CLAMP(sel, 0, MAX(0, ordn[s] - 1)); row[s].pos.x = (float)row[s].sel; row[s].pos.v = 0;
        free(fx[s]); fx[s] = calloc((size_t)MAX(1, vn(s)), sizeof(Fx)); fx_n[s] = fx[s] ? vn(s) : 0;
    }
}

static Part *spawn_k(int kind, int scr, float x, float y, float vx, float vy, float life, float size, uint32_t c) {
    for (int i = 0; i < ARRAY_LEN(parts); i++) if (parts[i].life <= 0) {
        parts[i] = (Part){ x, y, vx, vy, life, life, size, c, scr, kind, (float)(hash3(i, (int)(T * 1000), 3) % 628) / 100 };
        return &parts[i];
    }
    return 0;
}
static void spawn(int scr, float x, float y, float vx, float vy, float life, float size, uint32_t c) {
    spawn_k(size < 0 ? P_SPARK : P_DUST, scr, x, y, vx, vy, life, size, c);
}
static void parts_step(float dt) {
    for (int i = 0; i < ARRAY_LEN(parts); i++) {
        Part *p = &parts[i]; if (p->life <= 0) continue;
        p->life -= dt; p->x += p->vx * dt; p->y += p->vy * dt;
        switch (p->kind) {
        case P_DUST: p->vx *= 1 - 3.5f * dt; p->vy = p->vy * (1 - 3.5f * dt) + 25 * dt; break;
        case P_SPARK: p->vx *= 1 - 5.0f * dt; p->vy *= 1 - 5.0f * dt; p->vy -= 6 * dt; break;
        case P_CONFETTI:     /* tumbling paper: falls to a slow drift, swaying */
            p->ph += dt * 7; p->vy += (38 - p->vy) * 2.2f * dt; p->vx += (sinf(p->ph * 0.7f) * 22 - p->vx) * 2.0f * dt;
            if (p->y > 200) p->life = 0;
            break;
        }
    }
}
static void parts_draw(Surf *s, int scr) {
    for (int i = 0; i < ARRAY_LEN(parts); i++) {
        Part *p = &parts[i]; if (p->life <= 0 || p->scr != scr) continue;
        float k = p->life / p->max;
        if (p->kind == P_SPARK) sparkle(s, p->x, p->y, -p->size * (0.4f + 0.6f * k), p->c, (int)(255 * MIN(1.0f, k * 1.6f)));
        else if (p->kind == P_CONFETTI) {
            /* a little rectangle seen turning: its width goes with the turn */
            float w = fabsf(cosf(p->ph)) * 3 + 0.6f;
            uint32_t c = cosf(p->ph) > 0 ? p->c : col_mix(p->c, 0x000000, 60);
            rect_blend(s, (int)(p->x - w / 2), (int)p->y, (int)ceilf(w), 3, c, (int)(255 * MIN(1.0f, k * 3)));
        } else ellipse_soft(s, p->x, p->y, p->size * (1.6f - k * 0.6f), p->size * (1.2f - k * 0.4f), p->c, (int)(150 * k));
    }
}

/* a stylus tap: a burst of stars in the user's colour */
static void tap_burst(int scr, float x, float y) {
    uint32_t c = fav_color(cfg.color);
    for (int i = 0; i < 7; i++) {
        float a = i * (2 * PI_F / 7) + (float)(hash3(i, (int)(T * 60), 1) % 100) / 100, sp = 55 + (hash3(i, (int)(T * 60), 2) % 35);
        spawn_k(P_SPARK, scr, x, y, cosf(a) * sp, sinf(a) * sp, 0.38f, -(1.5f + (i & 1)), c);
    }
    spawn_k(P_SPARK, scr, x, y, 0, 0, 0.25f, -4, col_mix(c, 0xffffff, 120));
}
static void confetti(void) {
    static const uint32_t pal[] = { 0xf2545b, 0xf7b32b, 0x3fb950, 0x2f8fe8, 0xb35de8, 0xff7ab8, 0x2ec4b6 };
    for (int s = 0; s < 2; s++) for (int i = 0; i < 45; i++) {
        uint32_t h = hash3(i, s, 77);
        spawn_k(P_CONFETTI, s, (float)(h % 256), -5.0f - (float)((h >> 8) % 120), 0, 20 + (float)((h >> 16) % 30), 6, 1, pal[(h >> 4) % ARRAY_LEN(pal)]);
    }
}

static void fx_alloc(void) { for (int v = 0; v < V_COUNT; v++) fx_size(v); }
static Fx *fx_of(int s, int i) { static Fx none = { { 0, 0 } }; return (i >= 0 && i < fx_n[s]) ? &fx[s][i] : &none; }

static void select_game(int s, int i, int with_sound) {
    if (!vn(s)) return;
    int n = CLAMP(i, 0, vn(s) - 1);
    if (n == row[s].sel) return;
    row[s].sel = n;
    sel_t = T;
    notch_pending = 1;
    a_hold = 0;
    /* the tick rises along the list and falls back, as the DSi's does; passing ones (a glide, a flick) are quieter */
    float pitch = vn(s) > 1 ? 0.86f + 0.34f * n / (vn(s) - 1) : 1.0f;
    if (with_sound == 1) sfx_pitch(SFX_CURSOR, pitch);
    else if (with_sound == 2) sfx_ex(SFX_CURSOR, pitch * 1.06f, 0.45f);
    if (s < V_FAV) snprintf(cfg.last[s], sizeof cfg.last[s], "%s", vg(s, n)->file);
    else snprintf(cfg.last_fav, sizeof cfg.last_fav, "%s", vg(s, n)->file);
}

/* the end of the list: the row stretches past it and snaps back, the frame jiggles, a double buzz */
static void end_bump(int d, float k) {
    bump_dir = d; bump_t = T;
    rubber.v += d * 190 * k;
    frame_pop.v += 2.5f * k;
    sfx(SFX_BUMP);
    rumble_double(12, 0.45f);
}

static void move(int d) {
    Row *r = &row[sys];
    if (!vn(sys)) { sfx(SFX_BUMP); return; }
    int n = r->sel + d;
    if (n < 0 || n >= vn(sys)) { end_bump(d, 1.0f); return; }
    select_game(sys, n, 1);
}

/* up / down: to the next letter's first game (the previous one's), when the row is in name order */
static double letter_t = -10;
static char initial(const Game *g) {
    for (const char *c = g->title; *c; c++) {
        if (isalpha((unsigned char)*c)) return (char)toupper((unsigned char)*c);
        if (isdigit((unsigned char)*c)) return '#';
    }
    return '#';
}
static void jump(int d) {
    int n = vn(sys), i = row[sys].sel;
    if (!n) { sfx(SFX_BUMP); return; }
    if (sys == V_FAV || cfg.sort != 0) {
        if (d < 0) move(i > 0 ? -MIN(5, i) : -1); else move(n - 1 - i > 0 ? MIN(5, n - 1 - i) : 1);
        return;
    }
    char c = initial(vg(sys, i));
    if (d > 0) {
        while (i < n && initial(vg(sys, i)) == c) i++;
        if (i >= n) { end_bump(1, 1.0f); return; }
    } else {
        int j = i; while (j > 0 && initial(vg(sys, j - 1)) == c) j--;
        if (j == i) {
            if (i == 0) { end_bump(-1, 1.0f); return; }
            c = initial(vg(sys, i - 1)); j = i - 1;
            while (j > 0 && initial(vg(sys, j - 1)) == c) j--;
        }
        i = j;
    }
    select_game(sys, i, 1);
    letter_t = T;
    rumble(10, 0.3f);
}

static void switch_sys(int to) {
    if (to == sys || (sw_t >= 0 && T - sw_t < 0.25)) return;
    sw_from = sys; sw_dir = tab_pos(to) > tab_pos(sys) ? 1 : -1; sys = to; sw_t = T;
    cfg.sys = sys;
    sfx(SFX_TAB);
    rumble(15, 0.4f);
    glide_dir = 0; coast = 0; a_hold = 0;
    frame_x.x = 128; frame_x.v = 0;
    frame_w.v = frame_h.v = 0; frame_w.x = 0;          /* (sized at the next step) */
    sel_t = T;
}

/* art is kept only for the games near the selection (the handheld has 1 GB) */
static void manage_art(void) {
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) lib[s].g[i].keep = 0;
    for (int v = 0; v < V_COUNT; v++) {
        int r = v == sys ? 6 : 1;
        for (int i = MAX(0, row[v].sel - r); i <= MIN(vn(v) - 1, row[v].sel + r); i++) vg(v, i)->keep = 1;
    }
    if (lg) lg->keep = 1;
    if (rg) rg->keep = 1;
    if (logo_cur) logo_cur->keep = 1;
    if (logo_prev) logo_prev->keep = 1;
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) {
        Game *g = &lib[s].g[i];
        if (!g->keep && (g->cart || g->label_state || g->logo_state)) { cart_free(g); game_free_art(g); }
    }
}

/* ---- the fallback logo: the title, set like a logo (bold, white rim, a soft shadow) ---- */

static void dilate_into(Img *dst, const Img *mask, int ox, int oy, int r, uint32_t c) {
    for (int y = 0; y < dst->h; y++) for (int x = 0; x < dst->w; x++) {
        int best = 0;
        for (int j = -r; j <= r && best < 255; j++) for (int i = -r; i <= r; i++) {
            if (i * i + j * j > r * r + r) continue;
            int mx = x - ox + i, my = y - oy + j;
            if (mx < 0 || my < 0 || mx >= mask->w || my >= mask->h) continue;
            best = MAX(best, (int)CA(mask->px[(size_t)my * mask->w + mx]));
        }
        if (best) {
            uint32_t *d = &dst->px[(size_t)y * dst->w + x];
            int da = (int)CA(*d);
            if (best > da) *d = (c & 0xffffff) | ((uint32_t)best << 24);
        }
    }
}

static void make_text_logo(Game *g) {
    /* the title in lines of at most 200 px, in the biggest font that keeps it to three lines */
    char lines[4][128]; int nl = 0, font = FONT_XL;
    for (int attempt = 0; attempt < 2; attempt++) {
        nl = 0; lines[0][0] = 0;
        char buf[128]; snprintf(buf, sizeof buf, "%s", g->title);
        for (char *w = strtok(buf, " "); w; w = strtok(0, " ")) {
            char t[256]; snprintf(t, sizeof t, "%s%s%s", lines[nl], lines[nl][0] ? " " : "", w);
            if (lines[nl][0] && text_w(font, t) > 200) { if (nl == 3) break; nl++; snprintf(lines[nl], sizeof lines[nl], "%s", w); }
            else snprintf(lines[nl], sizeof lines[nl], "%s", t);
        }
        nl++;
        if (nl <= 3) break;
        font = FONT_LG;
    }
    int lh = font == FONT_XL ? 27 : 19, w = 0;
    for (int i = 0; i < nl; i++) w = MAX(w, text_w(font, lines[i]));
    Img mask; if (img_alloc(&mask, w + 4, nl * lh + 12)) return;
    for (int i = 0; i < nl; i++) {
        Img li; if (text_to_img(font, lines[i], &li, 0xffffff)) continue;
        Surf ms = { mask.w, mask.h, mask.w, mask.px, 0, 0, mask.w, mask.h };
        (void)ms;
        int ox = (mask.w - li.w) / 2, oy = i * lh;
        for (int y = 0; y < li.h; y++) for (int x = 0; x < li.w; x++) {
            int mx = ox + x, my = oy + y; if (mx < 0 || my < 0 || mx >= mask.w || my >= mask.h) continue;
            uint32_t a = CA(li.px[(size_t)y * li.w + x]); if (a > CA(mask.px[(size_t)my * mask.w + mx])) mask.px[(size_t)my * mask.w + mx] = a << 24 | 0xffffff;
        }
        img_free(&li);
    }
    game_tint(g);
    Accent ac = g->tint_state == 1 ? accent_of(g->tint) : accent();
    Img out; if (img_alloc(&out, mask.w + 8, mask.h + 8)) { img_free(&mask); return; }
    dilate_into(&out, &mask, 4, 5, 3, 0x2a2a2a);          /* the outer rim, a little low: depth */
    dilate_into(&out, &mask, 4, 4, 2, 0xffffff);          /* the white rim */
    /* the letters: the accent, light at the top, deep at the bottom */
    for (int y = 0; y < mask.h; y++) for (int x = 0; x < mask.w; x++) {
        int a = (int)CA(mask.px[(size_t)y * mask.w + x]); if (!a) continue;
        int line_y = y % lh;
        uint32_t c = col_mix(col_mix(ac.base, 0xffffff, 110), col_mix(ac.base, 0x000000, 70), line_y * 256 / lh);
        uint32_t *d = &out.px[(size_t)(y + 4) * out.w + x + 4];
        *d = (col_mix(*d | 0xff000000u, c | 0xff000000u, a) & 0xffffff) | 0xff000000u;
    }
    img_free(&mask);
    img_free(&g->logo);
    g->logo = out; g->logo_state = 3;
}

static Img *game_logo(Game *g) {
    if (!g) return 0;
    if (!g->logo_state) game_load_art(g);
    if (g->logo_state < 0) make_text_logo(g);
    return g->logo.px ? &g->logo : 0;
}

/* ---- the carousel's layout ---- */

typedef struct { float spacing, cy; } RowGeo;
static RowGeo geo(int v) { return v == V_NDS ? (RowGeo){ 54, 106 } : v == V_GBA ? (RowGeo){ 70, 106 } : (RowGeo){ 70, 103 }; }
/* a cartridge's width in the middle and elsewhere: a DS card or a Game Pak (the favourites mix them) */
static float sel_w_of(int csys) { return csys == SYS_NDS ? 60 : 82; }
static float oth_w_of(int csys) { return csys == SYS_NDS ? 36 : 48; }
#define CONT_GAP 18                     /* the favourites: the space after "continue" */
static float sel_w_now(int v) { Game *g = cur_game(v); return sel_w_of(g ? g->sys : (v == V_GBA ? SYS_GBA : SYS_NDS)); }

typedef struct { float x, y, w, f, d; CartPose pose; } Item;

static void layout(int s, int i, float pos, float dx, Item *it) {
    RowGeo G = geo(s);
    int cs = vg(s, i)->sys;
    float cw, ch, cd; cart_size(cs, &cw, &ch, &cd);
    float sw = sel_w_of(cs), ow = oth_w_of(cs);
    float d = i - pos, ad = fabsf(d);
    float f = 1 - smoothstep(0, 1, ad);                   /* 1 when it is the one in the middle */
    it->d = d; it->f = f;
    it->x = 128 + d * G.spacing + (ad < 1 ? d : SGN(d)) * (sw - ow) * 0.5f + dx;
    if (s == V_FAV && has_cont) it->x += (i >= 1 ? CONT_GAP : 0) - CONT_GAP * clampf(pos, 0, 1);
    float pop = 1 + 0.06f * clampf(frame_pop.x, -1, 1) * f;
    it->w = lerpf(ow, sw, f) * pop;
    float bob = sinf((float)T * 2.1f + i * 0.9f) * (1.2f + 1.3f * f);
    it->y = G.cy + 8 * (1 - f) - 2 * f + bob;
    CartPose *p = &it->pose;
    memset(p, 0, sizeof *p);
    p->sx = it->x; p->sy = it->y;
    p->scale = it->w / cw;
    /* nearly face-on: the others turned a little toward the middle, the selected one a touch to show its edge */
    p->yaw = clampf(-d * 0.42f, -0.38f, 0.38f) + f * 0.10f;
    p->pitch = -0.06f;
    p->roll = 0;
    /* held (A, or a finger on it): it sinks in a little */
    float sk = clampf(sink.x, -0.5f, 1.2f) * f;
    p->scale *= 1 - 0.06f * sk; it->y += 2 * sk; p->sy = it->y;
    /* the others lean a little toward the selected one */
    p->roll += 0.07f * SGN(d) * smoothstep(0.2f, 1.2f, ad);
    /* a flick tilts them all the way they're going, their tops trailing */
    float v = clampf(row[s].pos.v, -14, 14);
    if (fabsf(v) > 0.05f) { p->roll -= clampf(v * 0.02f, -0.11f, 0.11f); p->yaw += clampf(v * 0.03f, -0.16f, 0.16f); }
    /* jelly: squash one way, stretch the other */
    Fx *e = fx_of(s, i);
    float w = clampf(e->wob.x, -1.2f, 1.2f);
    p->stretch_x = 1 + 0.13f * w; p->stretch_y = 1 - 0.13f * w;
    float gk = (float)(T - gs_open_t) / 0.5f;
    if (gk >= 0 && gk < 1 && f > 0.5f && s == sys) p->yaw += 2 * PI_F * ease_in_out(gk);
    /* idle moments: the selected one turns all the way round, or a wave runs along the row */
    double idle = T - last_input - 15;
    if (state == ST_MENU && !opt_open && !gs_open && idle > 0) {
        double ph = fmod(idle, 7.0); int kind = ((int)(idle / 7.0)) & 1;
        if (kind == 0 && f > 0.5f && ph < 1.6) p->yaw += 2 * PI_F * ease_in_out((float)(ph / 1.6));
        if (kind == 1) {
            float q = (float)((ph - (it->x / 256.0) * 0.55) / 0.5);
            if (q > 0 && q < 1) { it->y -= sinf(q * PI_F) * 6; p->sy = it->y; p->roll += sinf(q * PI_F * 2) * 0.06f; }
        }
    }
    p->alpha = 1;
    p->desat = (1 - f) * 0.82f;
    p->bright = 0.80f + 0.25f * f;
    /* every 5 s a soft glossy streak sweeps across the selected cartridge */
    float gp = fmodf((float)T, 5.0f);
    p->gloss = f * 0.4f; p->gloss_pos = -0.5f + gp * 1.6f;
}

/* ---- drawing: the top screen ---- */

/* the photo: the game's title screen, printed with a white border like the DSi's top-screen photos, a little
 * crooked (each game its own way), made once per game */
static void frame_photo(const Img *src, int pw, uint32_t h, Img *out) {
    memset(out, 0, sizeof *out);
    int ph = MAX(1, src->h * pw / src->w), B = 5;
    Img small; img_resize_area(src, &small, pw, ph);
    Img framed;
    if (!small.px || img_alloc(&framed, pw + 2 * B, ph + 2 * B)) { img_free(&small); return; }
    Surf fs = { framed.w, framed.h, framed.w, framed.px, 0, 0, framed.w, framed.h };
    rect_vgrad(&fs, 0, 0, framed.w, framed.h, 0xffffff, 0xf2f2f2, 255);
    rectf(&fs, 0, 0, framed.w, 1, 0xdedede); rectf(&fs, 0, framed.h - 1, framed.w, 1, 0xc8c8c8);
    rectf(&fs, 0, 0, 1, framed.h, 0xd6d6d6); rectf(&fs, framed.w - 1, 0, 1, framed.h, 0xd0d0d0);
    blit(&fs, &small, B, B, 255);
    rect_blend(&fs, B, B, pw, 1, 0x000000, 40); rect_blend(&fs, B, B, 1, ph, 0x000000, 30);   /* the print's edge */
    for (int i = 0; i < framed.w * framed.h; i++) framed.px[i] |= 0xff000000u;
    float ang = ((h & 1) ? 1 : -1) * (1.6f + (h >> 1) % 10 * 0.12f) * PI_F / 180;
    img_rotate(&framed, ang, out);
    img_free(&small); img_free(&framed);
}
static Img *photo_for(Game *g);
static int photo_is_title;
static Img *game_photo(Game *g) {
    if (!g) return 0;
    if (!g->shot_state) game_load_art(g);
    if (g->shot_state < 1) return 0;
    if (g->photo.px) return &g->photo;
    /* a title screen alone (it is the logo) is shown bigger than a screenshot with a logo over it */
    frame_photo(&g->shot, g->shot_state == 2 ? 196 : 160, hash_str(g->file), &g->photo);
    return g->photo.px ? &g->photo : 0;
}

static void draw_logo(Surf *s, Game *g, float alpha, float scale, float dy, int view) {
    if (!g || alpha <= 0.01f) return;
    Img *ph = view ? photo_for(g) : 0;      /* the photo only in the photo view (X) */
    Img *lo = ph && photo_is_title ? &g->shot : game_logo(g);
    if (!lo) return;
    float bob = sinf((float)T * 1.6f) * 3.0f;
    float cx = 128, cy = 96 + dy + bob;
    if (ph) {
        /* the photo, floating gently on its own beat; the logo over its lower edge, floating on another */
        float pb = sinf((float)T * 1.15f + 1.0f) * 1.5f;
        float pw = ph->w * scale, phh = ph->h * scale;
        int alone = photo_is_title;                   /* the title screen: the logo is in it */
        float px = 128 - pw / 2, py = (alone ? 100 : 80) + dy * 0.6f + pb - phh / 2;
        int pst = fabsf(scale - 1) < 0.01f;
        if (pst) { px = roundf(px); py = roundf(py); }
        blit_tint(s, ph, px + 2, py + 3, pw, phh, 0x000000, (int)(alpha * 55));
        blit_scaled(s, ph, px, py, pw, phh, (int)(alpha * 255), !pst);
        if (alone) return;
        float k = MIN(1.0f, MIN(200.0f / lo->w, 74.0f / lo->h));
        scale *= k;
        cy = 146 + dy + bob;
    }
    float w = lo->w * scale, h = lo->h * scale;
    int a = (int)(alpha * 255);
    /* the glow behind (drawn once per size) and the shadow on the floor (smaller and fainter while it rises) */
    static Img glow; static int gw, gh;
    int ew = (int)(w * 0.62f + 14), eh = (int)(h * 0.7f + 12);
    if (ew != gw || eh != gh || !glow.px) {
        img_free(&glow); gw = ew; gh = eh;
        if (!img_alloc(&glow, ew * 2, eh * 2)) {
            Surf gs = { glow.w, glow.h, glow.w, glow.px, 0, 0, glow.w, glow.h };
            for (int y = 0; y < glow.h; y++) for (int x = 0; x < glow.w; x++) {
                float dx = (x + 0.5f - ew) / ew, dy = (y + 0.5f - eh) / eh, d = dx * dx + dy * dy;
                float f = d < 1 ? (1 - d) * (1 - d) : 0;
                glow.px[(size_t)y * glow.w + x] = ARGB((int)(f * 200), 255, 255, 255);
            }
            (void)gs;
        }
    }
    if (!ph) blit(s, &glow, (int)roundf(cx) - gw, (int)roundf(cy) - gh, a);   /* (over a photo it would wash it out) */
    float rise = (bob + 3) / 6;
    if (!ph) ellipse_soft(s, cx, 170, (w * 0.40f) * (1.05f - rise * 0.12f), 4.5f, 0x000000, (int)(a * (0.22f - rise * 0.06f)));
    float x = cx - w / 2, y = cy - h / 2;
    int still = fabsf(scale - 1) < 0.01f;
    if (still) { x = roundf(x); y = roundf(y); }
    if (ph) still = 0;               /* a logo fitted under the photo is drawn smoothly scaled */
    blit_tint(s, lo, x + 2, y + 3, w, h, 0x000000, a * 38 / 255);
    blit_scaled(s, lo, x, y, w, h, a, !still);
    /* a glint every 4 s */
    float gp = fmodf((float)T + 1.3f, 4.0f);
    if (gp < 0.8f) blit_glint(s, lo, x, y, w, h, -20 + gp / 0.8f * (w + h * 0.6f + 40), 0.55f * alpha);
    /* sparkles that come and go around it */
    for (int k = 0; k < 3; k++) {
        int cyc = (int)((T + k * 0.7) / 2.1);
        float ph = fmodf((float)(T + k * 0.7), 2.1f) / 2.1f;
        uint32_t h0 = hash3(cyc, k, (int)(intptr_t)g & 0xffff);
        float sx = x + (h0 % 1000) / 1000.0f * w, sy = y + ((h0 >> 10) % 1000) / 1000.0f * h;
        float tw = sinf(ph * PI_F);
        if (CA(lo->px[(size_t)CLAMP((int)((sy - y) / scale), 0, lo->h - 1) * lo->w + CLAMP((int)((sx - x) / scale), 0, lo->w - 1)]) > 128)
            sparkle(s, sx, sy, 2 + 2 * tw, 0xffffff, (int)(alpha * 230 * tw));
    }
}

/* the cartridge slot: a thin strip of the console that rises from the bottom edge, seen a little from above: its top
 * face with the slit in it, and its front face with the power lamp. A cartridge is drawn between the two, cut off at
 * the slit, so it goes into the slit and not behind the strip. */
#define SLOT_H 14
static float slot_top(float rise) { return 192 - rise * SLOT_H; }
static float slot_open_y(float rise) { return slot_top(rise) + 3; }   /* the slit: below it, the cartridge is inside */
static void draw_slot_back(Surf *s, float rise, float slit_w) {
    if (rise <= 0.001f) return;
    int y = (int)roundf(slot_top(rise));
    rect_vgrad(s, 0, y, 256, 5, 0xa3a7ae, 0x868a92, 255);           /* the top face */
    hline(s, 0, 255, y, 0xc4c8ce, 255);
    float sw = slit_w;
    rrect(s, 128 - sw / 2, y + 1, sw, 3.5f, 1.5f, 0x0c0d0f, 255);    /* the slit */
    hline(s, (int)(128 - sw / 2) + 2, (int)(128 + sw / 2) - 3, y + 1, 0x2a2c30, 255);
}
static void draw_slot_front(Surf *s, float rise, float slit_w, int led) {
    if (rise <= 0.001f) return;
    int y = (int)roundf(slot_top(rise));
    /* the slit's near lip, over the cartridge: it is in the slot */
    hline(s, (int)(128 - slit_w / 2), (int)(128 + slit_w / 2), y + 4, 0x8e929a, 255);
    rect_vgrad(s, 0, y + 5, 256, 192 - (y + 5) + 8, 0x5e6168, 0x3a3c41, 255);   /* the front face */
    hline(s, 0, 255, y + 5, 0x70747b, 255);
    uint32_t lc = led == 2 ? 0x46f04a : led == 1 ? 0x2fae32 : 0x2a3a2a;
    disc(s, 232, y + 9.5f, 1.8f, lc, 255);
    if (led == 2) ellipse_soft(s, 232, y + 9.5f, 6, 5, 0x46f04a, 90);
    text(s, FONT_SM, 14, y + 1, 0x9da1a8, 255, "DSi XL");
}

/* ---- drawing: the bottom screen ---- */

static uint32_t tint_now(void) { return RGB((int)tint_rgb[0], (int)tint_rgb[1], (int)tint_rgb[2]); }
/* the colour the bottom screen eases toward: the selected game's own, else the user's */
static void tint_step(float dt) {
    Game *g = cur_game(sys);
    uint32_t want = fav_color(cfg.color);
    if (g) { game_tint(g); if (g->tint_state == 1) want = g->tint; }
    float k = tint_init ? 1 - expf(-dt * 7) : 1;
    tint_init = 1;
    float w[3] = { (float)CR(want), (float)CG(want), (float)CB(want) };
    for (int i = 0; i < 3; i++) tint_rgb[i] += (w[i] - tint_rgb[i]) * k;
}

static void draw_row(Surf *s, int sy, float dx, float alpha, int hide) {
    Row *r = &row[sy];
    int N = vn(sy);
    if (!N) {
        int a = (int)(alpha * 255);
        if (sy == V_FAV) {
            /* nothing yet: a big soft star and how to get one here */
            star(s, 128 + dx, 92 + sinf((float)T * 1.6f) * 2, 15, 0xf3cf6a, a * 200 / 255);
            text_center(s, FONT_BD, (int)(128 + dx), 112, 0x7a6532, a, "No favorites yet.");
            text_center(s, FONT_MD, (int)(128 + dx), 130, C_TEXT_SOFT, a, "Press Y on a game to add it here.");
            return;
        }
        window(s, 28 + dx, 66, 200, 76, 0, a);
        char m[300]; snprintf(m, sizeof m, "No %s games yet.", sys_short(sy));
        text_center(s, FONT_BD, (int)(128 + dx), 74, C_TEXT, a, m);
        const char *dir = sy == SYS_NDS ? cfg.dir_nds : cfg.dir_gba;
        char d[300]; text_fit(FONT_SM, dir, 180, d, sizeof d);
        text_center(s, FONT_MD, (int)(128 + dx), 94, C_TEXT_SOFT, a, "Put them in");
        text_center(s, FONT_SM, (int)(128 + dx), 112, accent().dark, a, d);
        return;
    }
    float pos = r->pos.x;
    /* which items, far ones first */
    int idx[16], n = 0;
    for (int i = (int)floorf(pos) - 4; i <= (int)ceilf(pos) + 4 && n < 16; i++) if (i >= 0 && i < N) idx[n++] = i;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (fabsf(idx[a] - pos) < fabsf(idx[b] - pos)) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    Item it[16];
    float rdx = sy == sys ? clampf(rubber.x, -24, 24) : 0;
    for (int k = 0; k < n; k++) layout(sy, idx[k], pos, dx - rdx, &it[k]);
    RowGeo G = geo(sy);
    int favs = sy == V_FAV;
    if (favs) {
        /* the favourites stand on a shelf; "continue" on its own, a little apart */
        float x0 = 1e9f;
        for (int k = 0; k < n; k++) x0 = MIN(x0, it[k].x - it[k].w / 2 - 10);
        if (has_cont) {
            Item c0, c1; layout(sy, 0, pos, dx - rdx, &c0);
            if (N > 1) layout(sy, 1, pos, dx - rdx, &c1);
            float split = N > 1 ? (c0.x + c0.w / 2 + c1.x - c1.w / 2) / 2 : c0.x + c0.w / 2 + 14;
            /* "continue" has a little shelf of its own, the favourites a long one */
            shelf(s, MAX(-8.0f, x0), split - 4, 148, (int)(alpha * 255));
            if (N > 1) shelf(s, split + 4, 264, 148, (int)(alpha * 255));
        } else shelf(s, -8, 264, 148, (int)(alpha * 255));
    }
    /* the floor shadows (on the shelf: closer, firmer) */
    for (int k = 0; k < n; k++) {
        float lift = (it[k].y - (G.cy - 8));
        ellipse_soft(s, it[k].x, favs ? 149 : 153, it[k].w * 0.46f, favs ? 2.5f : 3.5f, 0x000000, (int)(alpha * (50 + lift * 1.5f)));
    }
    /* the frame: where it has travelled to, stretched along its way while it moves, sized for the selected cartridge */
    float fp = 1 + 0.05f * clampf(frame_pop.x, -1, 1);
    float fa = hide ? alpha * alpha * alpha : alpha;            /* launching: the frame goes first */
    float fx = sy == sys ? frame_x.x : 128, fv = sy == sys ? fabsf(frame_x.v) : 0;
    float stretch = MIN(18.0f, fv * 0.045f);
    float shine = fmodf((float)T + 2.0f, 4.5f) / 0.9f;           /* a light passes over it every 4.5 s */
    float fw = frame_w.x, fh = frame_h.x;
    if (sy != sys || fw < 1) {
        Game *cg = cur_game(sy); int cs = cg ? cg->sys : SYS_NDS;
        float cw, ch, cd; cart_size(cs, &cw, &ch, &cd);
        fw = sel_w_of(cs) + 16; fh = sel_w_of(cs) * ch / cw + 34;
    }
    const char *label = is_cont(sy, r->sel) ? "CONTINUE" : "START";
    fw = MAX(fw, (float)text_w(FONT_BD, label) + 16);        /* (wide enough for its word) */
    sel_frame(s, fx + dx, G.cy + 9, fw * fp + stretch, fh * fp - stretch * 0.25f, label, favs ? C_GOLD : tint_now(), shine, (int)(fa * 255));
    for (int k = 0; k < n; k++) {
        Game *g = vg(sy, idx[k]);
        if ((hide && g == lg) || (rg && g == rg)) continue;   /* it is flying: drawn by the animation */
        it[k].pose.alpha = alpha * (1 - smoothstep(2.6f, 3.6f, fabsf(it[k].d)));
        if (sy == sys && idx[k] != r->sel) it[k].pose.alpha *= smoothstep(0, 0.3f, (float)(T - reorder_t));
        if (g->hidden) it[k].pose.alpha *= 0.45f;         /* (hidden, but shown: faded) */
        cart_draw(s, g, &it[k].pose);
        if (g->resume) {
            /* a resume point: a little green play mark on its bottom right corner */
            float cw0, ch0, cd0; cart_size(g->sys, &cw0, &ch0, &cd0);
            float hh = it[k].w * ch0 / cw0, rx = it[k].x + it[k].w * 0.5f - 2, ry = it[k].y + hh * 0.5f - 3;
            int ra = (int)(it[k].pose.alpha * 255), r = it[k].f > 0.5f ? 6 : 4;
            disc(s, rx + 0.5f, ry + 1, (float)r, 0x000000, ra * 50 / 255);
            disc(s, rx, ry, (float)r, 0x2f9e44, ra);
            disc(s, rx, ry, r - 1.0f, 0x46c25a, ra);
            tri_fill(s, rx - r * 0.3f, ry - r * 0.45f, rx - r * 0.3f, ry + r * 0.45f, rx + r * 0.5f, ry, 0xffffff, ra);
        }
        if (g->is_new) {
            /* a new game: a red "NEW" tag on its top left corner, swinging a little */
            float cw0, ch0, cd0; cart_size(g->sys, &cw0, &ch0, &cd0);
            float hh = it[k].w * ch0 / cw0, nx = it[k].x - it[k].w * 0.5f - 3, ny = it[k].y - hh * 0.5f - 4 + sinf((float)T * 3 + idx[k]) * 0.8f;
            int na = (int)(it[k].pose.alpha * 255);
            rrect(s, nx + 1, ny + 1, 23, 10, 4, 0x000000, na * 40 / 255);
            rrect(s, nx, ny, 23, 10, 4, 0xd8452e, na);
            rrect_vgrad(s, nx + 1, ny + 1, 21, 8, 3, 0xf26a4f, 0xd03a24, na);
            text_center(s, FONT_SM, (int)(nx + 12), (int)ny - 4, 0xffffff, na, "NEW");
        }
        float cw2, ch2, cd2; cart_size(g->sys, &cw2, &ch2, &cd2);
        float h = it[k].w * ch2 / cw2;
        /* a favourite: a gold star on its top right corner (popping on, or shrinking away when it stops being one) */
        float pk = g == fav_g ? (float)(T - fav_t) / 0.45f : 2;
        if (g->fav || (pk < 1 && !fav_on)) {
            float r0 = 3.2f + 2.3f * it[k].f, rr = r0;
            if (pk < 1) rr = fav_on ? r0 * (0.2f + 0.8f * ease_out_elastic(pk)) : r0 * (1 - ease_in_cubic(pk));
            star(s, roundf(it[k].x + it[k].w * 0.44f), roundf(it[k].y - h * 0.5f + 1), rr, C_STAR, (int)(it[k].pose.alpha * 255));
        }
        /* "continue": its balloon over it */
        /* (selected, the title line says it instead) */
        if (is_cont(sy, idx[k]) && !hide && it[k].f < 0.95f)
            balloon(s, it[k].x, it[k].y - h * 0.5f - 3, "Continue", C_GOLD, (int)(it[k].pose.alpha * 255 * (1 - it[k].f)));
    }
}

static const uint8_t *fav_marks(int s) {
    if (s >= V_FAV) return 0;
    if (vn(s) > marks_n) { uint8_t *m = realloc(marks, (size_t)vn(s)); if (!m) return 0; marks = m; marks_n = vn(s); }
    for (int i = 0; i < vn(s); i++) marks[i] = (uint8_t)(vg(s, i)->fav != 0);
    return marks;
}

static float parallax(void) { return vn(sys) ? -row[sys].pos.x * geo(sys).spacing * 0.35f : 0; }

static void draw_tabs(Surf *s, int cur, int y, int pressable) {
    int touch_at = (tch.down && tch.zone == 1) ? (tch.x0 < 90 ? V_NDS : tch.x0 < 166 ? V_FAV : V_GBA) : -1;
    float pl = pressable && (touch_at == V_NDS || (in.held & BIT(B_L1))) ? 1 : 0;
    float pr = pressable && (touch_at == V_GBA || (in.held & BIT(B_R1))) ? 1 : 0;
    float pm = pressable && touch_at == V_FAV ? 1 : 0;
    tab(s, 0, -6, y, 74, "NDS", cur == V_NDS, pl, 255);
    tab(s, 1, 256 - 68, y, 74, "GBA", cur == V_GBA, pr, 255);
    tab_star(s, 128 - 19, y, 38, cur == V_FAV, pm, 255);
}

static void draw_bottom(Surf *s, float intro) {
    float sw0 = sw_t >= 0 ? (float)((T - sw_t) / 0.42) : 2;
    int fav_bg = sys == V_FAV;
    if (sw0 < 1 && (sw_from == V_FAV) != (sys == V_FAV)) {
        /* into or out of the favourites: the paper changes halfway across */
        fav_bg = ease_in_out(sw0) > 0.5f ? sys == V_FAV : sw_from == V_FAV;
    }
    if (fav_bg) bg_favs(s, T, tint_now(), 40, parallax()); else bg_bottom(s, T, tint_now(), 72, parallax());
    float sw = sw_t >= 0 ? (float)((T - sw_t) / 0.42) : 2;
    /* the system's emblem */
    if (sw < 1) {
        float k = ease_in_out(sw);
        emblem(s, sw_from, 128 - sw_dir * k * 40, 22, tint_now(), (int)(255 * (1 - k)));
        emblem(s, sys, 128 + sw_dir * (1 - k) * 40, 22, tint_now(), (int)(255 * k));
    } else emblem(s, sys, 128, 22, tint_now(), (int)(255 * intro));

    /* the title of the selected game */
    Game *g = cur_game(sys);
    if (g && state != ST_LAUNCH) {
        float k = ease_out_cubic((float)(T - sel_t) / 0.25f);
        int cont = is_cont(sys, row[sys].sel), a = (int)(255 * k * intro), ty = 44 + (int)((1 - k) * 4);
        const char *pre = cont ? "Continue playing: " : "";
        int pw = cont ? text_w(FONT_MD, pre) + 9 : 0;
        char t[160]; text_fit(FONT_MD, g->title, 236 - pw - (g->fav ? 13 : 0), t, sizeof t);
        int tw = text_w(FONT_MD, t), tx = 128 - (tw + pw + (g->fav ? 13 : 0)) / 2;
        if (cont) {
            /* the game played last: a play mark and the words in gold-brown before its title */
            tri_fill(s, tx, ty + 7, tx, ty + 15, tx + 6, ty + 11, C_GOLD, a);
            tx = text(s, FONT_MD, tx + 9, ty, 0x8a6418, a, pre);
        }
        if (g->fav) { star(s, tx + 5, ty + 11, 4.2f, C_STAR, a); tx += 13; }
        text(s, FONT_MD, tx, ty, 0x505050, a, t);
    }

    /* the cartridges: switching systems slides one row out and the other in */
    float dx_in = (1 - ease_out_cubic(intro)) * 220;
    if (sw < 1) {
        float k = ease_in_out(sw);
        draw_row(s, sw_from, -sw_dir * k * 280, 1 - k * 0.5f, 0);
        draw_row(s, sys, sw_dir * (1 - k) * 280, 0.5f + 0.5f * k, 0);
    } else draw_row(s, sys, dx_in, intro, state == ST_LAUNCH);

    /* the tabs */
    draw_tabs(s, sys, (int)((1 - ease_out_cubic(intro)) * -22), 1);

    /* the scroll bar */
    float sby = 176 + (1 - ease_out_cubic(intro)) * 20;
    scroll_bar(s, (int)sby, vn(sys), vn(sys) ? row[sys].pos.x : 0, 5, (T - sb_press_t < 0.12) ? sb_press : 0, fav_marks(sys), 255);
    /* the letter, in a bubble over the scroll bar, while jumping or dragging along it */
    float lk = (float)(T - letter_t);
    if (lk < 0.8f && vn(sys) && sys != V_FAV) {
        Game *lg0 = cur_game(sys);
        float bx = 26 + 204 * (vn(sys) > 1 ? row[sys].pos.x / (vn(sys) - 1) : 0.5f), byy = 150;
        bx = clampf(bx, 46, 210);                      /* (clear of the sun and gear buttons) */
        float pk = ease_out_back(sat(lk / 0.15f)) * (1 - smoothstep(0.6f, 0.8f, lk));
        int la = (int)(255 * pk);
        Accent ac = accent();
        rrect(s, bx - 13 + 1, byy - 13 + 2, 26, 26, 8, 0x000000, la * 40 / 255);
        rrect(s, bx - 13, byy - 13, 26, 26, 8, ac.base, la);
        rrect_vgrad(s, bx - 12, byy - 12, 24, 24, 7, 0xffffff, ac.pale, la);
        tri_fill(s, bx - 4, byy + 12, bx + 4, byy + 12, bx, byy + 17, ac.base, la);
        char l[2] = { lg0 ? initial(lg0) : '#', 0 };
        text_center(s, FONT_LG, (int)bx, (int)byy - 13, col_mix(ac.base, 0x000000, 120), la, l);
    }
}

/* ---- brightness ---- */

static const int BRI_PCT[5] = { 10, 25, 45, 70, 100 };
static int bri_from_system(void) {
    /* the level nearest ROCKNIX's setting (system.cfg's display.brightness), as the script reckons it */
    const char *e = getenv("DSIXL_SYSTEM_CFG");
    FILE *f = fopen(e && *e ? e : "/storage/.config/system/configs/system.cfg", "r");
    int pct = 70; char line[256];
    if (f) { while (fgets(line, sizeof line, f)) if (!strncmp(line, "display.brightness=", 19)) pct = atoi(line + 19); fclose(f); }
    int best = 0;
    for (int i = 1; i < 5; i++) if (abs(BRI_PCT[i] - pct) < abs(BRI_PCT[best] - pct)) best = i;
    return best + 1;
}
static void bri_show(int lv) {
    if (lv != bri_lv || T - bri_t > 1.4) sfx_ex(SFX_CURSOR, 0.80f + 0.09f * lv, 0.8f);
    bri_lv = lv; bri_t = T;
    rumble(8, 0.25f);
}
/* the sun button: round the five levels, as the DSi's does */
static void bri_set(int lv) {
    bri_show(lv);
    char sh[400]; snprintf(sh, sizeof sh, "%s/brightness.sh", cfg.home);
    if (access(sh, X_OK)) return;                   /* (a desktop: only the bar) */
    char cmd[520]; snprintf(cmd, sizeof cmd, "'%s' set %d >/dev/null 2>&1 &", sh, lv);
    if (system(cmd)) {}
}
/* the script's file: changed by the hotkey (or by bri_set) */
static void bri_poll_step(void) {
    if (T - bri_poll < 0.1) return;
    bri_poll = T;
    struct stat st;
    if (stat("/tmp/dsixl-brightness", &st)) return;
    if (bri_seen && st.st_mtim.tv_sec == bri_mt.tv_sec && st.st_mtim.tv_nsec == bri_mt.tv_nsec) return;
    bri_mt = st.st_mtim;
    FILE *f = fopen("/tmp/dsixl-brightness", "r"); int lv = 0;
    if (f) { if (fscanf(f, "%d", &lv) != 1) lv = 0; fclose(f); }
    if (lv < 1 || lv > 5) return;
    if (bri_seen) bri_show(lv); else bri_lv = lv;   /* (what it was before the menu started: no bar) */
    bri_seen = 1;
}

static void sun_icon(Surf *s, float cx, float cy, float r, uint32_t c, int a) {
    for (int i = 0; i < 8; i++) {
        float an = i * PI_F / 4 + PI_F / 8;
        disc(s, cx + cosf(an) * (r + 2.4f), cy + sinf(an) * (r + 2.4f), r * 0.32f + 0.35f, c, a);
    }
    disc(s, cx, cy, r, c, a);
}

/* the sun in the bottom left corner (the gear's partner) */
#define SUN_X 18
#define SUN_Y 166
static void draw_sun_button(Surf *s, int alpha) {
    int pr = tch.down && tch.zone == 12;
    float y = SUN_Y + (pr ? 1 : 0);
    rrect(s, SUN_X - 13 + 1, y - 8 + 2, 26, 16, 8, 0x000000, alpha * 30 / 255);
    rrect(s, SUN_X - 13, y - 8, 26, 16, 8, 0xb8b8b8, alpha);
    rrect_vgrad(s, SUN_X - 12, y - 7, 24, 14, 7, 0xffffff, 0xe6e6e6, alpha);
    sun_icon(s, SUN_X, y, 2.6f + 0.25f * bri_lv, 0xe0a010, alpha);
}

/* the bar on the top screen: the sun and five steps, dropping in under the status bar and leaving after a moment */
static void draw_bri_bar(Surf *s) {
    float k = (float)(T - bri_t);
    if (k < 0 || k > 1.7f) return;
    float in_k = ease_out_back(sat(k / 0.24f)), out = 1 - smoothstep(1.3f, 1.7f, k);
    int a = (int)(255 * out * sat(k / 0.1f));
    float y = 22 + (1 - in_k) * -26, x = 128 - 46;
    rrect(s, x + 1, y + 2, 92, 26, 9, 0x000000, a * 40 / 255);
    rrect(s, x, y, 92, 26, 9, 0xb4b4b4, a);
    rrect_vgrad(s, x + 1, y + 1, 90, 24, 8, 0xffffff, 0xe8e8e8, a);
    sun_icon(s, x + 15, y + 13, 3.0f + 0.3f * bri_lv, 0xe0a010, a);
    Accent ac = accent();
    for (int i = 0; i < 5; i++) {
        int h = 5 + i * 3, bx = (int)x + 32 + i * 11, by = (int)y + 20 - h;
        int on = i < bri_lv;
        float pop = (i == bri_lv - 1) ? 1 + 0.35f * (1 - ease_out_cubic(k / 0.25f)) : 1;
        int ph = (int)roundf(h * pop);
        rrect(s, bx, by - (ph - h), 8, ph, 2, on ? col_mix(ac.base, 0x000000, 40) : 0xc4c4c4, a);
        rrect_vgrad(s, bx + 1, by - (ph - h) + 1, 6, ph - 2, 1.5f, on ? col_mix(ac.base, 0xffffff, 90) : 0xe2e2e2, on ? ac.base : 0xd2d2d2, a);
    }
}

/* ---- a game that couldn't start ---- */

#define ERR_W 216
static int err_h(void) { return 30 + text_wrap_center(0, FONT_SM, 0, 0, ERR_W - 24, 13, 0, 0, err_msg) + 32; }
static int err_y(void) { return 96 - err_h() / 2; }
static void draw_error(Surf *s, float k) {
    if (k <= 0.001f) return;
    rect_blend(s, 0, 0, 256, 192, 0x000000, (int)(90 * k));
    int h = err_h();
    float y = err_y() + (1 - ease_out_back(k)) * 120, x = 128 - ERR_W / 2;
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    window(s, x, y, ERR_W, h, 0, a);
    text_center_fit(s, FONT_BD, 128, (int)y + 4, ERR_W - 20, 0x303030, a, err_title);
    int used = text_wrap_center(s, FONT_SM, 128, (int)y + 26, ERR_W - 24, 13, C_TEXT_SOFT, a, err_msg);
    float press = (tch.down && tch.zone == 14) ? 1 : 0;
    button(s, 128 - 40, y + 30 + used + 4, 80, 18, "OK", 1, press, a);
}
static void err_close(void) { err_open = 0; sfx(SFX_BACK); last_input = T; }

/* ---- the battery: a notice on the top screen when it runs low, and when the charger goes in ---- */

static double note_t = -10; static char note_txt[64]; static int note_kind;   /* 0 battery low, 1 charging, 2 done (a tick), 3 Wi-Fi */
static void notice(int kind, const char *txt) { note_kind = kind; note_t = T; snprintf(note_txt, sizeof note_txt, "%s", txt); }
static void battery_watch(void) {
    static double at = -10; static int prev = -1, prev_chg = -1;
    if (T - at < 1) return;
    at = T;
    int chg, lv = battery_level(&chg);
    if (lv < 0) return;
    if (prev >= 0 && !chg) {
        if (prev > 5 && lv <= 5) { notice(0, "Battery very low: charge now."); sfx(SFX_ERROR); rumble_double(30, 0.6f); }
        else if (prev > 15 && lv <= 15) { notice(0, "Battery low: charge soon."); sfx(SFX_ERROR); rumble(30, 0.5f); }
    }
    if (prev_chg == 0 && chg) { notice(1, "Charging"); sfx_pitch(SFX_SELECT, 1.3f); rumble(20, 0.4f); }
    prev = lv; prev_chg = chg;
}
static void draw_notice(Surf *s) {
    float k = (float)(T - note_t);
    if (k < 0 || k > 4) return;
    float in_k = ease_out_back(sat(k / 0.3f)), out = 1 - smoothstep(3.5f, 4, k);
    int a = (int)(255 * out * sat(k / 0.1f));
    int font = text_w(FONT_MD, note_txt) > 200 ? FONT_SM : FONT_MD;
    int tw = text_w(font, note_txt), w = tw + 40, x = 128 - w / 2;
    float y = 160 + (1 - in_k) * 40;
    rrect(s, x + 1, y + 2, w, 22, 9, 0x000000, a * 40 / 255);
    rrect(s, x, y, w, 22, 9, note_kind == 0 ? 0xd8452e : note_kind == 3 ? accent().base : 0x6aa84f, a);
    rrect_vgrad(s, x + 1, y + 1, w - 2, 20, 8, 0xffffff, 0xeeeeee, a);
    int bx = x + 9, by = (int)y + 7;
    if (note_kind <= 1) {
        /* a battery: red and blinking when low, filling up when charging */
        rect_blend(s, bx, by + 2, 2, 4, 0x505050, a); rect_blend(s, bx + 2, by, 14, 9, 0x505050, a); rect_blend(s, bx + 3, by + 1, 12, 7, 0xffffff, a);
        if (note_kind) { int f = ((int)(k * 3)) % 5; for (int i = 0; i < f; i++) rect_blend(s, bx + 12 - i * 3, by + 2, 2, 5, 0x6aa84f, a); }
        else if (((int)(k * 3)) & 1) rect_blend(s, bx + 12, by + 2, 2, 5, 0xd8452e, a);
    } else if (note_kind == 2) {
        for (int i = 0; i < 4; i++) { rect_blend(s, bx + 2 + i, by + 3 + i, 2, 2, 0x3a9a3a, a); }
        for (int i = 0; i < 7; i++) { rect_blend(s, bx + 5 + i, by + 6 - i, 2, 2, 0x3a9a3a, a); }
    } else {
        /* Wi-Fi: three arcs and a dot */
        for (int r = 0; r < 3; r++) for (int i = -3 - r; i <= 3 + r; i++) {
            float yy = by + 9 - sqrtf((float)((4 + r * 3) * (4 + r * 3) - i * i));
            pblend(s, bx + 7 + i, (int)yy, accent().base, a);
        }
        rect_blend(s, bx + 6, by + 8, 2, 2, accent().base, a);
    }
    text(s, font, x + 30, (int)y + (font == FONT_SM ? 3 : 1), C_TEXT, a, note_txt);
}

/* ---- the options window ---- */

enum { OPT_EMU, OPT_COLOR, OPT_CLOCK, OPT_SFX, OPT_MUSIC, OPT_RUMBLE, OPT_RESCAN, OPT_ES, OPT_POWER,
       OPT_WIFI, OPT_TZ, OPT_ART, OPT_UPDATE, OPT_SORT, OPT_HIDE, OPT_SHOWHIDDEN, OPT_CONTROLS, OPT_ALBUM, OPT_COUNT };
static void flows_on_options_open(void);
static void opt_label_more(int i, char *b, int n);
static void opt_act_more(int i);
static int update_found(void);

static void opt_build(void) {
    static const int order[] = { OPT_EMU, OPT_SORT, OPT_HIDE, OPT_SHOWHIDDEN, OPT_ALBUM, OPT_ART, OPT_WIFI, OPT_TZ, OPT_COLOR,
                                 OPT_CLOCK, OPT_SFX, OPT_MUSIC, OPT_RUMBLE, OPT_CONTROLS, OPT_RESCAN, OPT_ES, OPT_POWER };
    optn = 0;
    if (update_found()) optv[optn++] = OPT_UPDATE;      /* a newer release on the card: first */
    for (int i = 0; i < ARRAY_LEN(order); i++) {
        if (order[i] == OPT_HIDE && !cur_game(sys)) continue;
        if (order[i] == OPT_SHOWHIDDEN && !library_hidden_count()) continue;
        optv[optn++] = order[i];
    }
}
static void opt_open_now(void) {
    flows_on_options_open();
    opt_build();
    opt_open = 1; opt_t = T; opt_sel = 0; opt_scroll.x = 0; opt_scroll.v = 0;
    sfx(SFX_OPEN);
}
static int opt_id(int i) { return i >= 0 && i < optn ? optv[i] : -1; }

static void opt_label(int i, char *b, int n) {
    switch (i) {
    case OPT_EMU: snprintf(b, (size_t)n, "Emulator settings"); break;
    case OPT_COLOR: snprintf(b, (size_t)n, "Color: %s", fav_color_name(cfg.color)); break;
    case OPT_CLOCK: snprintf(b, (size_t)n, "Clock: %s", cfg.clock24 ? "24-hour" : "12-hour"); break;
    case OPT_SFX: snprintf(b, (size_t)n, "Sound effects: %s", cfg.sfx ? "On" : "Off"); break;
    case OPT_MUSIC: snprintf(b, (size_t)n, "Music: %s", cfg.music ? "On" : "Off"); break;
    case OPT_RUMBLE: snprintf(b, (size_t)n, "Vibration: %s", cfg.rumble ? "On" : "Off"); break;
    case OPT_RESCAN: snprintf(b, (size_t)n, "Look for new games"); break;
    case OPT_ES: snprintf(b, (size_t)n, "Exit to EmulationStation"); break;
    case OPT_POWER: snprintf(b, (size_t)n, "Power off"); break;
    default: opt_label_more(i, b, n); break;
    }
}

/* the folders read again; returns how many games are new */
static void warm_start(void);
static void resume_scan(void);
static int rescan(void) {
    int before = 0, after = 0;
    for (int s = 0; s < SYS_COUNT; s++) for (int k = 0; k < lib[s].n; k++) { before += lib[s].g[k].is_new; cart_free(&lib[s].g[k]); }
    logo_cur = logo_prev = 0; fav_g = 0;
    library_scan();
    for (int s = 0; s < SYS_COUNT; s++) for (int k = 0; k < lib[s].n; k++) after += lib[s].g[k].is_new;
    order_rebuild();
    fav_rebuild();
    row[V_FAV].sel = CLAMP(row[V_FAV].sel, 0, MAX(0, vn(V_FAV) - 1)); row[V_FAV].pos.x = (float)row[V_FAV].sel;
    resume_scan();
    warm_start();
    return after - before;
}

static void opt_act(int i, int dir) {
    switch (i) {
    case OPT_EMU: opt_open = 0; opt_t = T; gs_open_here(); break;
    case OPT_COLOR: cfg.color = (cfg.color + (dir ? dir : 1) + 16) % 16; sfx(SFX_CURSOR); config_save(); break;
    case OPT_CLOCK: cfg.clock24 = !cfg.clock24; sfx(SFX_SELECT); config_save(); break;
    case OPT_SFX: cfg.sfx = !cfg.sfx; sfx(SFX_SELECT); config_save(); break;
    case OPT_MUSIC: cfg.music = !cfg.music; if (cfg.music) music_play(); else music_stop(); sfx(SFX_SELECT); config_save(); break;
    case OPT_RUMBLE: cfg.rumble = !cfg.rumble; sfx(SFX_SELECT); config_save(); rumble(30, 0.6f); break;
    case OPT_RESCAN: {
        sfx(SFX_SELECT);
        int n = rescan();
        if (n > 0) { char m[48]; snprintf(m, sizeof m, n == 1 ? "1 new game" : "%d new games", n); notice(2, m); }
        else notice(2, "No new games");
        opt_open = 0; opt_t = T;
        break;
    }
    case OPT_ES: sfx(SFX_SELECT); exit_code = EXIT_TO_ES; break;
    case OPT_POWER: sfx(SFX_SELECT); exit_code = EXIT_POWEROFF; break;
    default: opt_act_more(i); break;
    }
}

#define OPT_X 22
#define OPT_Y 4
#define OPT_W 212
#define OPT_ROW 16
#define OPT_VIS 8                       /* rows seen at once: the list scrolls */
#define OPT_LIST_Y (OPT_Y + 24)

/* the window's last line: the battery (and what it's drawing), the games card's free space */
static void opt_info(char *b, int n) {
    int chg, lv = battery_level(&chg);
    float w, h, fg, tg;
    char bat[64] = "", disk[64] = "";
    if (lv >= 0) {
        if (chg) snprintf(bat, sizeof bat, "%d%%, charging", lv);
        else if (!power_draw(&w, &h) && h > 0) snprintf(bat, sizeof bat, "%d%%, %dh %02dm left", lv, (int)h, (int)(fmodf(h, 1) * 60));
        else snprintf(bat, sizeof bat, "%d%%", lv);
    }
    if (!disk_free_gb(cfg.dir_nds, &fg, &tg) && tg > 0.05f) snprintf(disk, sizeof disk, "%.1f GB free", fg);
    snprintf(b, (size_t)n, "%s%s%s", bat, bat[0] && disk[0] ? "  \xc2\xb7  " : "", disk);
}

static void draw_options(Surf *s, float k) {
    if (k <= 0.001f) return;
    rect_blend(s, 0, 0, 256, 192, 0x000000, (int)(90 * k));
    float y = OPT_Y + (1 - ease_out_back(k)) * 140;
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    int vis = MIN(optn, OPT_VIS);
    window(s, OPT_X, y, OPT_W, 28 + vis * OPT_ROW + 17, "Options", a);
    Surf list = *s; surf_clip(&list, OPT_X + 2, (int)y + 25, OPT_W - 4, vis * OPT_ROW);
    float sc = opt_scroll.x;
    for (int i = MAX(0, (int)sc - 1); i < MIN(optn, (int)sc + vis + 2); i++) {
        int id = optv[i];
        char b[64]; opt_label(id, b, sizeof b);
        float by = y + 26 + (i - sc) * OPT_ROW;
        float press = (tch.down && tch.zone == 3 && opt_sel == i && !tch.moved) ? 1 : 0;
        button(&list, OPT_X + 8, by, OPT_W - 22, OPT_ROW - 2, b, i == opt_sel, press, a);
        if (id == OPT_COLOR) {
            rrect(&list, OPT_X + OPT_W - 32, by + 3, 10, 10, 3, fav_color(cfg.color), a);
            rrect_line(&list, OPT_X + OPT_W - 32, by + 3, 10, 10, 3, 1, 0x505050, a);
        }
    }
    /* where in the list: a thin bar on the right */
    if (optn > vis) {
        float th = vis * OPT_ROW - 2, bh = MAX(10.0f, th * vis / optn), by = y + 26 + (th - bh) * sc / (optn - vis);
        rrect(s, OPT_X + OPT_W - 10, y + 26, 4, th, 2, 0xdedede, a);
        rrect(s, OPT_X + OPT_W - 10, by, 4, bh, 2, accent().base, a);
    }
    static double info_t = -10; static char info[128];
    if (T - info_t > 2) { info_t = T; opt_info(info, sizeof info); }
    hline(s, OPT_X + 8, OPT_X + OPT_W - 9, (int)(y + 28 + vis * OPT_ROW), 0xe0e0e0, a);
    /* a little battery before it */
    int iw = text_w(FONT_SM, info), ix = 128 - (iw + 14) / 2, iy = (int)(y + 33 + vis * OPT_ROW);
    if (info[0] && battery_level(0) >= 0) {
        rect_blend(s, ix, iy + 1, 1, 3, 0x8a8a8a, a); rect_blend(s, ix + 1, iy, 9, 5, 0x8a8a8a, a);
        rect_blend(s, ix + 2, iy + 1, 7, 3, 0xffffff, a);
        rect_blend(s, ix + 9 - MAX(1, battery_level(0) * 7 / 100), iy + 1, MAX(1, battery_level(0) * 7 / 100), 3, 0x6aa84f, a);
        ix += 14;
    }
    text(s, FONT_SM, ix, iy - 5, 0x8a8a8a, a, info);
}

/* the selection stays in sight */
static void opt_follow(float dt) {
    int vis = MIN(optn, OPT_VIS);
    static float want;
    if (!(tch.down && tch.zone == 3 && tch.moved)) {
        if (opt_sel < want) want = (float)opt_sel;
        if (opt_sel > want + vis - 1) want = (float)(opt_sel - vis + 1);
        want = clampf(want, 0, (float)MAX(0, optn - vis));
        spring_step(&opt_scroll, want, 500, 40, dt);
    } else want = clampf(roundf(opt_scroll.x), 0, (float)MAX(0, optn - vis));
}

/* ---- the emulators' settings ---- */

#define GS_X 8
#define GS_W 240
#define GS_Y 4
#define GS_ROW 18
#define GS_TABS 19                      /* the DS / GBA page tabs under the title */
static int gs_row_y0(void) { return GS_Y + 22 + GS_TABS + 2; }
static int gs_btn_y(void) { return gs_row_y0() + gs_n * GS_ROW + 3; }
static int gs_rows_of(int sys) { const GameOpt *o; return gamecfg_opts(sys, &o); }

static void gs_open_on(int sys_) {
    gs_open = 1; gs_sys = sys_; gs_sel = 0; gs_open_t = T;
    for (int s2 = 0; s2 < SYS_COUNT; s2++) gamecfg_load(s2, gs_choice[s2]);
    gs_n = gs_rows_of(gs_sys);
    sfx(SFX_OPEN); rumble(12, 0.3f);
    a_hold = 0; glide_dir = 0; coast = 0;
}
static void gs_close(void) {
    if (!gs_open) return;
    for (int s2 = 0; s2 < SYS_COUNT; s2++) if (gamecfg_save(s2, gs_choice[s2])) plat_log("settings: could not write system.cfg");
    gs_open = 0;
}
static void gs_page(int sys_) {
    if (sys_ == gs_sys) return;
    gs_page_dir = sys_ > gs_sys ? 1 : -1; gs_page_t = T;
    gs_sys = sys_; gs_n = gs_rows_of(gs_sys); gs_sel = MIN(gs_sel, gs_n + 1);
    sfx(SFX_TAB); rumble(10, 0.3f);
}
static void gs_change(int row, int dir) {
    const GameOpt *o; gamecfg_opts(gs_sys, &o);
    gs_choice[gs_sys][row] = (gs_choice[gs_sys][row] + dir + o[row].n) % o[row].n;
    gs_val_t = T; gs_val_dir = dir;
    sfx_pitch(SFX_CURSOR, dir > 0 ? 1.12f : 0.94f); rumble(6, 0.2f);
}
static void gs_activate(void) {
    if (gs_sel < gs_n) gs_change(gs_sel, 1);
    else if (gs_sel == gs_n) {          /* Recommended: this page's options back to their recommended choices */
        const GameOpt *o; gamecfg_opts(gs_sys, &o);
        for (int i = 0; i < gs_n; i++) gs_choice[gs_sys][i] = o[i].rec;
        gs_val_t = T; gs_val_dir = 1;
        sfx_pitch(SFX_SELECT, 1.15f); rumble(15, 0.4f);
    } else { gs_close(); sfx(SFX_BACK); }
}

static void gear_icon(Surf *s, float cx, float cy, uint32_t c, int a) {
    for (int i = 0; i < 8; i++) {
        float an = i * PI_F / 4, x = cx + cosf(an) * 4.6f, y = cy + sinf(an) * 4.6f;
        disc(s, x, y, 1.3f, c, a);
    }
    disc(s, cx, cy, 4.0f, c, a);
    disc(s, cx, cy, 1.6f, 0xffffff, a);
}

/* the gear in the bottom right corner: tapped (or SELECT), the emulators' settings */
#define GEAR_X 238
#define GEAR_Y 166
static void draw_gear_button(Surf *s, int alpha) {
    int pr = tch.down && tch.zone == 6;
    float y = GEAR_Y + (pr ? 1 : 0);
    rrect(s, GEAR_X - 13 + 1, y - 8 + 2, 26, 16, 8, 0x000000, alpha * 30 / 255);
    rrect(s, GEAR_X - 13, y - 8, 26, 16, 8, 0xb8b8b8, alpha);
    rrect_vgrad(s, GEAR_X - 12, y - 7, 24, 14, 7, 0xffffff, 0xe6e6e6, alpha);
    gear_icon(s, GEAR_X, y, 0x5a5a5a, alpha);
}

static void draw_game_settings(Surf *bot, Surf *top, float k) {
    if (k <= 0.001f) return;
    const GameOpt *o; int n = gamecfg_opts(gs_sys, &o);
    int *ch = gs_choice[gs_sys];
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    /* the bottom screen: the window, sliding up */
    rect_blend(bot, 0, 0, 256, 192, 0x000000, (int)(80 * k));
    float y = GS_Y + (1 - ease_out_back(k)) * 180;
    int bh = 22 + GS_TABS + 2 + n * GS_ROW + 3 + 20;
    window(bot, GS_X, y, GS_W, bh, "Emulator settings", a);
    Accent ac = accent();
    /* the page tabs: L Nintendo DS | Game Boy Advance R */
    for (int p = 0; p < 2; p++) {
        float tx = GS_X + 4 + p * 117, tw = 115, ty = y + 22;
        int act = gs_sys == p;
        rrect(bot, tx, ty, tw, GS_TABS - 3, 5, act ? ac.base : 0xc8c8c8, a);
        rrect_vgrad(bot, tx + 1, ty + 1, tw - 2, GS_TABS - 5, 4, act ? 0xffffff : 0xf0f0f0, act ? ac.pale : 0xe2e2e2, a);
        btn_glyph(bot, p ? (int)(tx + tw - 15) : (int)tx + 4, (int)ty + 2, p ? "R" : "L", act ? 0x3c3c3c : 0xa0a0a0, a);
        /* the words centred in the room the glyph leaves */
        text_center_fit(bot, FONT_SM, (int)(p ? tx + (tw - 16) / 2 : tx + 16 + (tw - 16) / 2), (int)ty - 1, (int)tw - 15, act ? col_mix(ac.base, 0x000000, 130) : 0x8a8a8a, a, p ? "Game Boy Advance" : "Nintendo DS");
    }
    /* the rows, sliding in from the side when the page changes */
    float pk = ease_out_cubic((float)(T - gs_page_t) / 0.22f);
    int pdx = (int)((1 - pk) * gs_page_dir * 40);
    Surf rows = *bot; surf_clip(&rows, GS_X + 2, (int)y + 22 + GS_TABS, GS_W - 4, n * GS_ROW + 3);
    for (int i = 0; i < n; i++) {
        float ry = y + 22 + GS_TABS + 2 + i * GS_ROW;
        int sel = gs_sel == i, rx = pdx;
        if (sel) {
            rrect(&rows, GS_X + 4 + rx, ry, GS_W - 8, GS_ROW - 2, 5, ac.base, a);
            rrect_vgrad(&rows, GS_X + 5 + rx, ry + 1, GS_W - 10, GS_ROW - 4, 4, 0xffffff, ac.pale, a);
        } else if (i) hline(&rows, GS_X + 10 + rx, GS_X + GS_W - 11 + rx, (int)ry - 1, 0xe2e2e2, a);
        text(&rows, FONT_MD, GS_X + 10 + rx, (int)ry - 2, sel ? col_mix(ac.base, 0x000000, 130) : C_TEXT, a, o[i].label);
        /* the value between its arrows; the recommended choice wears a little star */
        int vx0 = GS_X + 100 + rx, vx1 = GS_X + GS_W - 10 + rx, vc = (vx0 + vx1) / 2;
        uint32_t arrow = sel ? ac.base : 0xb0b0b0;
        float pl = (tch.down && tch.zone == 7 && gs_sel == i && tch.x0 < vc) ? 1 : 0, pr = (tch.down && tch.zone == 7 && gs_sel == i && tch.x0 >= vc) ? 1 : 0;
        tri_fill(&rows, vx0 + 6 - pl, ry + 3.5f, vx0 + 6 - pl, ry + 12.5f, vx0 + 1 - pl, ry + 8, arrow, a);
        tri_fill(&rows, vx1 - 6 + pr, ry + 3.5f, vx1 - 6 + pr, ry + 12.5f, vx1 - 1 + pr, ry + 8, arrow, a);
        const char *name = o[i].names[ch[i]];
        int rec = ch[i] == o[i].rec;
        int tw = text_w(FONT_MD, name) + (rec ? 10 : 0);
        float slide = sel ? (1 - ease_out_cubic((float)(T - gs_val_t) / 0.18f)) * gs_val_dir * 10 : 0;
        int tx = vc - tw / 2 + (int)slide;
        Surf clip = rows; surf_clip(&clip, MAX(rows.cx0, vx0 + 8), (int)ry, vx1 - vx0 - 16, GS_ROW);
        int ex = text(&clip, FONT_MD, tx, (int)ry - 2, sel ? 0x2a2a2a : 0x505050, a, name);
        if (rec) star(&clip, ex + 5, ry + 8, 3.0f, C_STAR, a);
    }
    /* Recommended and Done */
    float by = y + 22 + GS_TABS + 2 + n * GS_ROW + 3;
    float pr0 = (tch.down && tch.zone == 8 && tch.x0 < 128) ? 1 : 0, pr1 = (tch.down && tch.zone == 8 && tch.x0 >= 128) ? 1 : 0;
    button(bot, GS_X + 8, by, 108, 16, "Recommended", gs_sel == n, pr0, a);
    button(bot, GS_X + GS_W - 116, by, 108, 16, "Done", gs_sel == n + 1, pr1, a);
    /* the top screen: what the highlighted option does, and what the choice does */
    if (!top) return;
    rect_blend(top, 0, 14, 256, 178, 0xf6f6f6, (int)(180 * k));
    float ty = 104 + (1 - ease_out_cubic(k)) * 90;
    window(top, 14, ty, 228, 80, 0, a);
    const char *who = gs_sys == SYS_NDS ? "every DS game" : "every GBA game";
    if (gs_sel < n) {
        text(top, FONT_BD, 24, (int)ty + 1, col_mix(ac.base, 0x000000, 110), a, o[gs_sel].label);
        text_right(top, FONT_SM, 232, (int)ty + 3, 0x9a9a9a, a, ch[gs_sel] == o[gs_sel].rec ? "recommended" : "");
        text_wrap_center(top, FONT_SM, 128, (int)ty + 20, 206, 13, C_TEXT_SOFT, a, o[gs_sel].help);
        char line[200]; snprintf(line, sizeof line, "%s: %s", o[gs_sel].names[ch[gs_sel]], o[gs_sel].notes[ch[gs_sel]]);
        text_wrap_center(top, FONT_SM, 128, (int)ty + 46, 206, 13, 0x303030, a, line);
    } else if (gs_sel == n) {
        text(top, FONT_BD, 24, (int)ty + 1, col_mix(ac.base, 0x000000, 110), a, "Recommended");
        char line[200]; snprintf(line, sizeof line, "This page back to the best balance of looks, speed and battery for this handheld (the choices with a star), for %s.", who);
        text_wrap_center(top, FONT_SM, 128, (int)ty + 24, 206, 13, 0x303030, a, line);
    } else {
        text(top, FONT_BD, 24, (int)ty + 1, col_mix(ac.base, 0x000000, 110), a, "Done");
        text_wrap_center(top, FONT_SM, 128, (int)ty + 24, 206, 13, 0x303030, a, "Keep these settings. They're used for every game (EmulationStation's system settings show them too).");
    }
}

/* open on the page of what's in front of you: the selected game's system, else the tab's */
static void gs_open_here(void) {
    Game *g = cur_game(sys);
    gs_open_on(g ? g->sys : (sys == V_GBA ? SYS_GBA : SYS_NDS));
}

static void gs_update(void) {
    int n = gs_n;
    if (repeat(B_UP)) { gs_sel = (gs_sel + n + 1) % (n + 2); sfx(SFX_CURSOR); }
    if (repeat(B_DOWN)) { gs_sel = (gs_sel + 1) % (n + 2); sfx(SFX_CURSOR); }
    if (repeat(B_LEFT)) { if (gs_sel < n) gs_change(gs_sel, -1); else if (gs_sel == n + 1) { gs_sel = n; sfx(SFX_CURSOR); } }
    if (repeat(B_RIGHT)) { if (gs_sel < n) gs_change(gs_sel, 1); else if (gs_sel == n) { gs_sel = n + 1; sfx(SFX_CURSOR); } }
    if (pressed(B_L1)) gs_page(SYS_NDS);
    if (pressed(B_R1)) gs_page(SYS_GBA);
    if (pressed(btn_a())) gs_activate();
    if (pressed(btn_b()) || pressed(B_SELECT) || pressed(B_START)) { gs_close(); sfx(SFX_BACK); }
}

/* ---- the boot screens ---- */

static void warn_icon(Surf *s, float x, float y, int a) {
    tri_fill(s, x + 7, y, x + 14.5f, y + 13, x - 0.5f, y + 13, 0x000000, a);
    tri_fill(s, x + 7, y + 2.2f, x + 12.6f, y + 11.8f, x + 1.4f, y + 11.8f, 0xf8d81c, a);
    rect_blend(s, (int)x + 6, (int)y + 4, 2, 5, 0x000000, a);
    rect_blend(s, (int)x + 6, (int)y + 10, 2, 1, 0x000000, a);
}

static void squeezed(Surf *s, int font, int cx, int y, int max_w, uint32_t c, int a, const char *str) {
    Img im; if (text_to_img(font, str, &im, c)) return;
    float w = MIN((float)im.w, (float)max_w);
    blit_scaled(s, &im, roundf(cx - w / 2), (float)y, w, (float)im.h, a, w < im.w);
    img_free(&im);
}

/* the DSi XL's start, as it goes: both screens come up out of black together, the logo on the top one and the health
 * and safety screen on the bottom one, while the start-up chime plays; a moment later "Touch the Touch Screen to
 * continue." starts to pulse. A touch (only a touch, as on the DSi): a tap sound, the bottom screen goes white, then
 * the top, and the menu slides in out of the white. */
#define BOOT_IN     0.55f               /* out of black */
#define BOOT_CHIME  0.08f
#define BOOT_PROMPT 1.9f                /* "Touch the Touch Screen to continue." */
static void draw_boot(Surf *top, Surf *bot, float t) {
    fill(top, 0xffffff); fill(bot, 0xffffff);
    if (boot_logo.px) blit(top, &boot_logo, 128 - boot_logo.w / 2, 77 - boot_logo.h / 2, 255);
    else text_center(top, FONT_XL, 128, 62, 0x000000, 255, "NINTENDO DSi");
    int a = 255;
    /* the health and safety screen (the layout of the DSi's) */
    int hw = text_w(FONT_HS, "WARNING \xe2\x80\x93 HEALTH AND SAFETY") + 17;
    warn_icon(bot, 128 - hw / 2.0f, 16, a);
    text(bot, FONT_HS, 128 - hw / 2 + 17, 12, 0x000000, a, "WARNING \xe2\x80\x93 HEALTH AND SAFETY");
    static const char *body[] = { "BEFORE PLAYING, READ THE HEALTH", "AND SAFETY PRECAUTIONS BOOKLET", "FOR IMPORTANT INFORMATION", "ABOUT YOUR HEALTH AND SAFETY." };
    for (int i = 0; i < 4; i++) squeezed(bot, FONT_MD, 128, 39 + i * 19, 214, 0x101010, a, body[i]);
    squeezed(bot, FONT_SM, 128, 117, 236, 0x101010, a, "TO GET AN EXTRA COPY FOR YOUR REGION, GO ONLINE AT");
    text_center(bot, FONT_MD, 128, 131, 0x2b9fd0, a, "www.nintendo.com/healthsafety/");
    /* the prompt fades in, then breathes */
    float pk = smoothstep(BOOT_PROMPT, BOOT_PROMPT + 0.35f, t);
    float pulse = t < BOOT_PROMPT + 0.35f ? 1 : 0.62f + 0.38f * cosf((float)(t - BOOT_PROMPT - 0.35f) * 3.1f);
    if (t > BOOT_PROMPT) text_center(bot, FONT_MD, 128, 155, 0x505050, (int)(a * pk * pulse), "Touch the Touch Screen to continue.");
    /* out of black: the whole screen together */
    int black = (int)(255 * (1 - smoothstep(0, BOOT_IN, t)));
    if (black) { rect_blend(top, 0, 0, 256, 192, 0x000000, black); rect_blend(bot, 0, 0, 256, 192, 0x000000, black); }
}

/* ---- Wi-Fi, the time zone, getting art, updating: what runs in the background, and the windows that ask ---- */

enum { F_NONE, F_BUSY, F_PICK, F_KBD };
enum { K_WIFI_ON, K_WIFI_SCAN, K_WIFI_LIST, K_WIFI_PASS, K_WIFI_JOIN, K_WIFI_OFF, K_TZ_REGION, K_TZ_CITY, K_TZ_SET, K_ART, K_UPDATE };
static int flow, flow_kind;
static Job fjob, netjob, artjob;
static Picker pk; static Kbd kb;
static float busy_anim, busy_prog = -1; static char busy1[80], busy2[120];
static int wifi_state; static char wifi_ssid[64];     /* 0 not known yet, 1 connected, 2 not connected, 3 off, 4 no Wi-Fi */
static char join_ssid[72];
static char tz_region[40];
static char upd_zip[400], upd_ver[32]; static double upd_checked = -100;
static int art_shown, art_found; static long art_off; static char art_line[160]; static double art_poll_t;

static void script_path(char *out, int n, const char *name) { snprintf(out, (size_t)n, "%s/%s", cfg.home, name); }
static int have_script(const char *name) { char p[400]; script_path(p, sizeof p, name); return !access(p, X_OK); }
/* a word for the shell: 'it''s' */
static void shq(char *out, int n, const char *str) {
    int o = 0;
    if (o < n - 1) out[o++] = '\'';
    for (; *str && o < n - 6; str++) {
        if (*str == '\'') { memcpy(out + o, "'\\''", 4); o += 4; } else out[o++] = *str;
    }
    if (o < n - 1) out[o++] = '\'';
    out[o] = 0;
}
static void run_script(Job *j, const char *tag, const char *name, const char *a1, const char *a2, const char *a3) {
    char p[400], q[3][300] = { "", "", "" }, cmd[1400];
    script_path(p, sizeof p, name);
    const char *a[3] = { a1, a2, a3 };
    for (int i = 0; i < 3; i++) if (a[i]) shq(q[i], sizeof q[i], a[i]);
    char sp[420]; shq(sp, sizeof sp, p);
    snprintf(cmd, sizeof cmd, "%s %s %s %s", sp, q[0], q[1], q[2]);
    job_start(j, tag, cmd);
}
static void busy(int kind, const char *l1, const char *l2) {
    flow = F_BUSY; flow_kind = kind; busy_prog = -1;
    snprintf(busy1, sizeof busy1, "%s", l1); snprintf(busy2, sizeof busy2, "%s", l2 ? l2 : "");
}
static void flow_end(void) { flow = F_NONE; last_input = T; }
static void say_error(const char *title, const char *msg) {
    snprintf(err_title, sizeof err_title, "%s", title); snprintf(err_msg, sizeof err_msg, "%s", msg);
    err_open = 1; sfx(SFX_ERROR); rumble_double(20, 0.5f);
}
/* the last line a job printed (for an error) */
static void job_last_line(const Job *j, char *out, int n) {
    char buf[1024]; job_output(j, buf, sizeof buf);
    int l = (int)strlen(buf);
    while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == ' ')) buf[--l] = 0;
    char *nl = strrchr(buf, '\n');
    snprintf(out, (size_t)n, "%s", nl ? nl + 1 : buf);
}

/* ---- Wi-Fi ---- */

static void wifi_parse_status(const char *o) {
    char st[32] = "", ss[64] = "";
    sscanf(o, "%31[^\t\n]\t%63[^\t\n]", st, ss);
    wifi_state = !strcmp(st, "connected") ? 1 : !strcmp(st, "disconnected") ? 2 : !strcmp(st, "off") ? 3 : !strcmp(st, "none") ? 4 : 0;
    snprintf(wifi_ssid, sizeof wifi_ssid, "%s", wifi_state == 1 ? ss : "");
}
static void wifi_status_poll(void) {
    if (netjob.pid && !job_running(&netjob)) { char o[256]; job_output(&netjob, o, sizeof o); wifi_parse_status(o); }
}
static void wifi_scan(void) {
    busy(K_WIFI_SCAN, "Looking for networks\xe2\x80\xa6", "");
    run_script(&fjob, "net", "net.sh", "scan", 0, 0);
}
static void wifi_list(const char *out) {
    picker_open(&pk, "Wi-Fi", "Choose your network. Those with a padlock need a password; it's remembered for next time.");
    flow = F_PICK; flow_kind = K_WIFI_LIST;
    char buf[8192]; snprintf(buf, sizeof buf, "%s", out);
    for (char *ln = strtok(buf, "\n"); ln; ln = strtok(0, "\n")) {
        int sig, sec, use; char ss[72];
        if (sscanf(ln, "%d\t%d\t%d\t%71[^\n]", &sig, &sec, &use, ss) != 4) continue;
        PickItem *it = picker_add(&pk, ss);
        it->sig = CLAMP((sig + 12) / 25, 0, 4); it->lock = sec; it->check = use;
        if (use) pk.sel = pk.n - 1;
    }
    snprintf(picker_add(&pk, "Look again")->note, sizeof pk.it[0].note, "%s", "");
    if (wifi_state != 3) picker_add(&pk, "Turn Wi-Fi off");
}
static void wifi_join(const char *ssid_in, const char *psk) {
    char ssid[72]; snprintf(ssid, sizeof ssid, "%s", ssid_in);      /* (it may be join_ssid itself) */
    snprintf(join_ssid, sizeof join_ssid, "%s", ssid);
    char l2[120]; snprintf(l2, sizeof l2, "%s", ssid);
    busy(K_WIFI_JOIN, "Connecting\xe2\x80\xa6", l2);
    run_script(&fjob, "net", "net.sh", "connect", ssid, psk ? psk : "");
}
static void wifi_open(void) {
    opt_open = 0; opt_t = T;
    if (!have_script("net.sh")) { say_error("Wi-Fi isn't available.", "This only works on the handheld (net.sh is missing)."); return; }
    if (wifi_state == 4) { say_error("No Wi-Fi here.", "The handheld's Wi-Fi didn't answer. Try again after a restart."); return; }
    if (wifi_state == 3) { busy(K_WIFI_ON, "Turning Wi-Fi on\xe2\x80\xa6", ""); run_script(&fjob, "net", "net.sh", "on", 0, 0); return; }
    wifi_scan();
}

/* ---- the time zone ---- */

static void tz_now(char *out, int n) {
    const char *e = getenv("TZ");
    if (e && *e == ':') { snprintf(out, (size_t)n, "%s", e + 1); return; }
    char rp[PATH_MAX];
    const char *z = realpath("/etc/localtime", rp) ? strstr(rp, "zoneinfo/") : 0;
    snprintf(out, (size_t)n, "%s", z ? z + 9 : "UTC");
}
static void tz_list(const char *region) {
    char p[400]; snprintf(p, sizeof p, "%s/assets/timezones.txt", cfg.home);
    FILE *f = fopen(p, "r");
    char cur[96]; tz_now(cur, sizeof cur);
    if (!region) {
        picker_open(&pk, "Time zone", "Where you are: first the part of the world, then the city whose time you keep.");
        flow = F_PICK; flow_kind = K_TZ_REGION;
    } else {
        picker_open(&pk, region, "The city whose time you keep (or the nearest one in your time zone).");
        flow = F_PICK; flow_kind = K_TZ_CITY;
    }
    char line[96], last[40] = "";
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char reg[40]; snprintf(reg, sizeof reg, "%s", line); char *sl = strchr(reg, '/'); if (sl) *sl = 0;
        if (!region) {
            if (strcmp(reg, last)) {
                snprintf(last, sizeof last, "%s", reg);
                PickItem *it = picker_add(&pk, reg);
                if (!strncmp(cur, reg, strlen(reg))) { it->check = 1; pk.sel = pk.n - 1; }
            }
        } else if (!strcmp(reg, region) && sl) {
            char lab[72]; snprintf(lab, sizeof lab, "%s", line + strlen(reg) + 1);
            for (char *c = lab; *c; c++) { if (*c == '_') *c = ' '; }
            PickItem *it = picker_add(&pk, lab);
            if (!strcmp(cur, line)) { it->check = 1; pk.sel = pk.n - 1; }
        }
    }
    if (f) fclose(f);
}
static void tz_set(const char *zone) {
    char z[96]; snprintf(z, sizeof z, ":%s", zone);
    if (have_script("net.sh")) { busy(K_TZ_SET, "Setting the time zone\xe2\x80\xa6", zone); run_script(&fjob, "net", "net.sh", "tz", zone, 0); }
    setenv("TZ", z, 1); tzset();
    if (!have_script("net.sh")) { char m[96]; snprintf(m, sizeof m, "Time zone: %s", strchr(zone, '/') ? strrchr(zone, '/') + 1 : zone); notice(2, m); }
}

/* ---- getting art ---- */

static void art_start(void) {
    opt_open = 0; opt_t = T;
    if (job_running(&artjob)) { art_shown = 1; flow = F_BUSY; flow_kind = K_ART; return; }
    if (wifi_state == 2 || wifi_state == 3 || wifi_state == 4) {
        say_error("Not connected to the internet.", "Art comes from the internet: connect to Wi-Fi first (Options > Wi-Fi).");
        return;
    }
    char sp[400]; script_path(sp, sizeof sp, "dsixl-scrape.py");
    if (access(sp, R_OK)) { snprintf(sp, sizeof sp, "%s/tools/dsixl-scrape.py", cfg.home); }
    char q[420], h[300], cmd[900]; shq(q, sizeof q, sp); shq(h, sizeof h, cfg.home);
    snprintf(cmd, sizeof cmd, "DSIXL_HOME=%s exec python3 -u %s --progress", h, q);
    if (job_start(&artjob, "art", cmd)) { say_error("Couldn't get art.", "The art script didn't start."); return; }
    art_off = 0; art_found = 0; art_shown = 1;
    busy(K_ART, "Getting art\xe2\x80\xa6", "Starting");
}
/* a game's art arrived: it's loaded again, and its cartridge gives a little jump */
static void art_arrived(const char *sysname, const char *base) {
    int s = !strcmp(sysname, "gba") ? SYS_GBA : SYS_NDS;
    for (int i = 0; i < lib[s].n; i++) {
        Game *g = &lib[s].g[i];
        if (strcmp(g->base, base)) continue;
        cart_free(g); game_free_art(g); g->tint_state = 0;
        for (int v = 0; v < V_COUNT; v++) for (int k = 0; k < vn(v); k++) if (vg(v, k) == g) fx_of(v, k)->wob.v += 10;
        if (g == cur_game(sys)) { logo_prev = 0; logo_t = T; }
    }
}
static void art_poll(void) {
    if (!artjob.pid && !art_off) return;
    int running = job_running(&artjob);
    if (running && T - art_poll_t < 0.2) return;
    art_poll_t = T;
    FILE *f = fopen(artjob.out, "rb");
    if (f) {
        fseek(f, art_off, SEEK_SET);
        char line[600];
        while (fgets(line, sizeof line, f)) {
            if (!strchr(line, '\n') && running) break;              /* a line still being written */
            art_off = ftell(f);
            line[strcspn(line, "\n")] = 0;
            if (line[0] != '@') { if (line[0]) snprintf(art_line, sizeof art_line, "%s", line + strspn(line, " ")); continue; }
            char *fld[8]; int nf = 0;
            for (char *t = strtok(line, "\t"); t && nf < 8; t = strtok(0, "\t")) fld[nf++] = t;
            if (nf >= 4 && !strcmp(fld[1], "download")) {
                snprintf(busy1, sizeof busy1, "Downloading the games database");
                snprintf(busy2, sizeof busy2, "%s of %s MB (once a month)", fld[2], fld[3]);
                busy_prog = atof(fld[3]) > 0 ? (float)(atof(fld[2]) / atof(fld[3])) : -1;
            } else if (nf >= 2 && !strcmp(fld[1], "reading")) {
                snprintf(busy1, sizeof busy1, "Reading the games database");
                snprintf(busy2, sizeof busy2, "A minute or two");
                busy_prog = -1;
            } else if (nf >= 7 && !strcmp(fld[1], "game")) {
                snprintf(busy1, sizeof busy1, "Getting art: %s of %s", fld[3], fld[4]);
                snprintf(busy2, sizeof busy2, "%s", fld[5]);
                busy_prog = atof(fld[4]) > 0 ? (float)(atof(fld[3]) / atof(fld[4])) : -1;
                if (atoi(fld[6]) > 0) { art_found++; art_arrived(fld[2], fld[5]); }
            }
        }
        fclose(f);
    }
    if (!running) {
        art_off = 0;
        if (flow == F_BUSY && flow_kind == K_ART) flow_end();
        art_shown = 0;
        if (artjob.rc == 0) {
            char m[80]; snprintf(m, sizeof m, art_found == 1 ? "New art for 1 game" : "New art for %d games", art_found);
            notice(2, art_found ? m : "No new art found");
            sfx_pitch(SFX_SELECT, 1.2f);
        } else {
            char m[300]; snprintf(m, sizeof m, "The art script stopped (error %d): %s", artjob.rc, art_line);
            say_error("Couldn't get all the art.", m);
        }
    }
}

/* ---- updating: a newer release's zip on the games card ---- */

static int ver_cmp(const char *a, const char *b) {
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) break;
    }
    return 0;
}
static int update_found(void) {
    if (T - upd_checked < 30) return upd_zip[0] != 0;
    upd_checked = T; upd_zip[0] = 0;
    const char *dirs[] = { getenv("DSIXL_UPDATE_DIR"), "/storage/roms/ports", "/storage/roms", "/storage/games-external", "/storage/games-external/roms/ports" };
    for (int d = 0; d < ARRAY_LEN(dirs); d++) {
        if (!dirs[d] || !*dirs[d]) continue;
        char pat[400]; snprintf(pat, sizeof pat, "%s/dsixl-*.zip", dirs[d]);
        glob_t g;
        if (glob(pat, 0, 0, &g)) continue;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            const char *v = strrchr(g.gl_pathv[i], '/') + 7;
            char ver[32]; snprintf(ver, sizeof ver, "%.*s", (int)(strlen(v) - 4), v);
            if (ver_cmp(ver, DSIXL_VERSION) > 0 && (!upd_zip[0] || ver_cmp(ver, upd_ver) > 0)) {
                snprintf(upd_zip, sizeof upd_zip, "%s", g.gl_pathv[i]); snprintf(upd_ver, sizeof upd_ver, "%s", ver);
            }
        }
        globfree(&g);
    }
    return upd_zip[0] != 0;
}
static void update_start(void) {
    opt_open = 0; opt_t = T;
    if (!have_script("update.sh")) { say_error("Can't update from here.", "This only works on the handheld (update.sh is missing)."); return; }
    char l1[80]; snprintf(l1, sizeof l1, "Updating to %s\xe2\x80\xa6", upd_ver);
    busy(K_UPDATE, l1, "Don't turn the handheld off.");
    run_script(&fjob, "update", "update.sh", upd_zip, 0, 0);
}

/* ---- the options' part ---- */

static void flows_on_options_open(void) {
    if (have_script("net.sh") && !job_running(&netjob)) run_script(&netjob, "netstatus", "net.sh", "status", 0, 0);
}
static void opt_label_more(int i, char *b, int n) {
    switch (i) {
    case OPT_WIFI:
        if (wifi_state == 1) snprintf(b, (size_t)n, "Wi-Fi: %s", wifi_ssid);
        else snprintf(b, (size_t)n, "Wi-Fi: %s", wifi_state == 2 ? "Not connected" : wifi_state == 3 ? "Off" : wifi_state == 4 ? "None" : "\xe2\x80\xa6");
        break;
    case OPT_TZ: {
        char z[96]; tz_now(z, sizeof z);
        const char *c = strrchr(z, '/'); char city[64]; snprintf(city, sizeof city, "%s", c ? c + 1 : z);
        for (char *p = city; *p; p++) if (*p == '_') *p = ' ';
        snprintf(b, (size_t)n, "Time zone: %s", city);
        break;
    }
    case OPT_ART:
        if (job_running(&artjob)) snprintf(b, (size_t)n, "Getting art\xe2\x80\xa6 (see how it goes)");
        else snprintf(b, (size_t)n, "Get art from the internet");
        break;
    case OPT_UPDATE: snprintf(b, (size_t)n, "Update to %s", upd_ver); break;
    case OPT_SORT: snprintf(b, (size_t)n, "Order: %s", cfg.sort == 1 ? "Recently played" : cfg.sort == 2 ? "Most played" : "By name"); break;
    case OPT_HIDE: { Game *g = cur_game(sys); snprintf(b, (size_t)n, "%s this game", g && g->hidden ? "Unhide" : "Hide"); break; }
    case OPT_SHOWHIDDEN: snprintf(b, (size_t)n, "Show hidden (%d): %s", library_hidden_count(), cfg.show_hidden ? "On" : "Off"); break;
    case OPT_CONTROLS: snprintf(b, (size_t)n, "Controls in games"); break;
    case OPT_ALBUM: snprintf(b, (size_t)n, "Album"); break;
    }
}
static void help_open(int sys_, int then_launch);
static void album_open(void);
static void opt_act_more(int i) {
    switch (i) {
    case OPT_WIFI: sfx(SFX_SELECT); wifi_open(); break;
    case OPT_TZ: sfx(SFX_SELECT); opt_open = 0; opt_t = T; tz_list(0); break;
    case OPT_ART: sfx(SFX_SELECT); art_start(); break;
    case OPT_UPDATE: sfx(SFX_SELECT); update_start(); break;
    case OPT_SORT:
        cfg.sort = (cfg.sort + 1) % 3; state_save(); order_rebuild(); sel_t = T; reorder_t = T;
        sfx_pitch(SFX_TAB, 0.95f + 0.08f * cfg.sort); rumble(10, 0.3f);
        break;
    case OPT_HIDE: {
        Game *g = cur_game(sys); if (!g) break;
        int on = !g->hidden;
        library_set_hidden(g, on);
        opt_open = 0; opt_t = T;
        if (sys < V_FAV) {
            /* the next one along stays selected, or the one before at the end */
            int i = row[sys].sel;
            if (on && !cfg.show_hidden) snprintf(cfg.last[sys], sizeof cfg.last[sys], "%s", i + 1 < vn(sys) ? vg(sys, i + 1)->file : i > 0 ? vg(sys, i - 1)->file : "");
        }
        order_rebuild(); fav_rebuild(); reorder_t = T; sel_t = T; logo_cur = cur_game(sys); logo_prev = 0;
        for (int k = 0; k < 10; k++) {
            float an = k * (2 * PI_F / 10), sp = 40 + (hash3(k, (int)(T * 60), 6) % 30);
            spawn_k(P_SPARK, 1, 128, geo(sys).cy, cosf(an) * sp, sinf(an) * sp, 0.4f, -1.5f, 0xc8c0b0);
        }
        notice(2, on ? (cfg.show_hidden ? "Hidden (shown faded)" : "Hidden: see Options to show") : "Back in the row");
        sfx(on ? SFX_BACK : SFX_SELECT);
        break;
    }
    case OPT_SHOWHIDDEN: cfg.show_hidden = !cfg.show_hidden; state_save(); order_rebuild(); fav_rebuild(); reorder_t = T; sfx(SFX_SELECT); break;
    case OPT_CONTROLS: { Game *g = cur_game(sys); opt_open = 0; opt_t = T; help_open(g ? g->sys : SYS_NDS, 0); break; }
    case OPT_ALBUM: opt_open = 0; opt_t = T; album_open(); break;
    }
}

/* ---- each frame ---- */

static PanelIn panel_in(void) {
    static int was, x0, y0, moved;
    PanelIn p; memset(&p, 0, sizeof p);
    p.t = T; p.a = btn_a(); p.b = btn_b();
    for (int b = 0; b < B_COUNT; b++) { if (pressed(b)) p.pressed |= BIT(b); if (repeat(b)) p.repeat |= BIT(b); }
    int d = in.touch[1];
    if (d && !was) { p.began = 1; x0 = in.tx[1]; y0 = in.ty[1]; moved = 0; tap_burst(1, (float)x0, (float)y0); }
    if (d && (abs(in.tx[1] - x0) > 4 || abs(in.ty[1] - y0) > 4)) moved = 1;
    if (!d && was) p.ended = 1;
    p.down = d; p.moved = moved; p.x = d ? in.tx[1] : tch.x; p.y = d ? in.ty[1] : tch.y; p.x0 = x0; p.y0 = y0;
    if (d) { tch.x = in.tx[1]; tch.y = in.ty[1]; }
    was = d;
    return p;
}

static void flow_job_done(void) {
    char o[8192]; job_output(&fjob, o, sizeof o);
    int rc = fjob.rc;
    switch (flow_kind) {
    case K_WIFI_ON: wifi_parse_status(o); wifi_scan(); break;
    case K_WIFI_SCAN: wifi_list(o); break;
    case K_WIFI_JOIN:
        flow_end();
        if (rc == 0 && strstr(o, "ok")) {
            wifi_state = 1; snprintf(wifi_ssid, sizeof wifi_ssid, "%s", join_ssid);
            char m[96]; snprintf(m, sizeof m, "Connected to %s", join_ssid); notice(3, m); sfx_pitch(SFX_SELECT, 1.2f);
        } else {
            char m[200]; snprintf(m, sizeof m, "Check the password for \xe2\x80\x9c%s\xe2\x80\x9d, and that the network is in reach.", join_ssid);
            say_error("Couldn't connect.", m);
        }
        run_script(&netjob, "netstatus", "net.sh", "status", 0, 0);
        break;
    case K_WIFI_OFF: flow_end(); wifi_parse_status(o); notice(3, "Wi-Fi off"); break;
    case K_TZ_SET: {
        flow_end();
        char z[96]; tz_now(z, sizeof z);
        if (rc == 0) { char m[96]; snprintf(m, sizeof m, "Time zone: %s", strchr(z, '/') ? strrchr(z, '/') + 1 : z); notice(2, m); }
        else { char m[200]; job_last_line(&fjob, m, sizeof m); say_error("Couldn't set the time zone.", m); }
        break;
    }
    case K_UPDATE:
        if (rc == 0) { exit_code = EXIT_QUIT; flow_end(); }     /* the session starts the new menu */
        else { flow_end(); char m[300]; job_last_line(&fjob, m, sizeof m); say_error("The update didn't work.", m); }
        break;
    default: flow_end(); break;
    }
}

static void flow_pick(int r) {
    if (r == -2) { flow_end(); if (flow_kind == K_TZ_CITY) tz_list(0); return; }
    PickItem *it = &pk.it[r];
    switch (flow_kind) {
    case K_WIFI_LIST:
        if (!strcmp(it->label, "Look again") && it->sig < 0) wifi_scan();
        else if (!strcmp(it->label, "Turn Wi-Fi off") && it->sig < 0) { busy(K_WIFI_OFF, "Turning Wi-Fi off\xe2\x80\xa6", ""); run_script(&fjob, "net", "net.sh", "off", 0, 0); }
        else if (it->check) { flow_end(); char m[96]; snprintf(m, sizeof m, "Connected to %s", it->label); notice(3, m); }
        else if (it->lock) {
            snprintf(join_ssid, sizeof join_ssid, "%s", it->label);
            char pr[120]; snprintf(pr, sizeof pr, "The password for \xe2\x80\x9c%s\xe2\x80\x9d", it->label);
            kbd_open(&kb, "Wi-Fi password", pr, "");
            flow = F_KBD; flow_kind = K_WIFI_PASS;
        } else wifi_join(it->label, 0);
        break;
    case K_TZ_REGION: snprintf(tz_region, sizeof tz_region, "%s", it->label); if (!strcmp(it->label, "UTC")) { flow_end(); tz_set("UTC"); } else tz_list(tz_region); break;
    case K_TZ_CITY: {
        char z[96]; snprintf(z, sizeof z, "%s/%s", tz_region, it->label);
        for (char *c = z; *c; c++) if (*c == ' ') *c = '_';
        flow_end(); tz_set(z);
        break;
    }
    default: flow_end(); break;
    }
}

static void flow_update(float dt) {
    PanelIn pi = panel_in();
    switch (flow) {
    case F_BUSY:
        if (flow_kind == K_ART) {
            /* getting art goes on by itself: B puts the window away (Options > Get art brings it back) */
            if (pressed(btn_b()) || (pi.ended && !pi.moved)) { art_shown = 0; flow_end(); sfx(SFX_BACK); }
        } else if (!job_running(&fjob)) flow_job_done();
        break;
    case F_PICK: { int r = picker_update(&pk, &pi, dt); if (r != -1) flow_pick(r); break; }
    case F_KBD: {
        int r = kbd_update(&kb, &pi, dt);
        if (r == 1) wifi_join(join_ssid, kb.text);
        else if (r == 0) { flow_end(); wifi_scan(); }
        break;
    }
    }
}

/* every frame, whatever else is going on: the windows animate, the jobs are watched */
static void flows_step(float dt) {
    static const PanelIn none;
    PanelIn idle = none; idle.t = T;
    if (flow != F_PICK) picker_update(&pk, &idle, dt);
    if (flow != F_KBD) kbd_update(&kb, &idle, dt);
    busy_anim = clampf(busy_anim + (flow == F_BUSY ? dt : -dt) / 0.2f, 0, 1);
    wifi_status_poll();
    art_poll();
}
static void flows_draw(Surf *bot, Surf *top) {
    picker_draw(&pk, bot, top);
    kbd_draw(&kb, bot, top);
    busy_draw(bot, busy1, busy2, T, busy_anim, busy_prog);
    if (flow == F_BUSY && flow_kind == K_ART && busy_anim > 0.5f)
        text_center(bot, FONT_SM, 128, 176, 0xffffff, (int)(255 * busy_anim), "B: carry on in the background");
}
/* art coming in the background: a small turning mark in the top screen's corner */
static void flows_draw_top(Surf *top) {
    if (!job_running(&artjob) || flow == F_BUSY) return;
    Accent ac = accent();
    for (int i = 0; i < 8; i++) {
        float an = i * PI_F / 4; int lead = ((int)(T * 10)) % 8, age = (lead - i + 8) % 8;
        disc(top, 28 + cosf(an) * 4, 8 + sinf(an) * 4, 1.2f, col_mix(ac.base, 0xffffff, MIN(220, age * 32)), 255);
    }
}

/* ---- the controls in games: shown before the first game of each system, and from Options ---- */

static int help_on, help_sys, help_then_launch; static float help_anim;
static void help_open(int sys_, int then_launch) {
    help_on = 1; help_sys = sys_; help_then_launch = then_launch;
    sfx(SFX_OPEN); rumble(12, 0.3f); a_hold = 0;
}
static void help_close(void) {
    help_on = 0;
    if (help_then_launch) {
        cfg.seen_help[help_sys] = 1; state_save();
        help_wait = 1; start_launch(); help_wait = 0;
    } else sfx(SFX_BACK);
}
/* a button's name in a little dark chip; returns the end x */
static int chip(Surf *s, int x, int y, const char *label, int a) {
    int w = text_w(FONT_SM, label) + 8;
    rrect(s, x, y, w, 12, 4, 0x3c3c3c, a);
    text(s, FONT_SM, x + 4, y - 3, 0xffffff, a, label);
    return x + w;
}
static int plus(Surf *s, int x, int y, int a) { return text(s, FONT_MD, x + 2, y - 4, C_TEXT_SOFT, a, "+") + 2; }
/* what, then its buttons: after it on the line when they fit, else on their own line under it (returns the height) */
static int help_line(Surf *s, int y, const char *what, const char *const *keys, int nk, int a) {
    int lx = text(s, FONT_SM, 22, y - 2, C_TEXT, a, what) + 8;
    int w = 0;
    for (int i = 0; i < nk; i++) w += text_w(FONT_SM, keys[i]) + 8 + (i ? text_w(FONT_MD, "+") + 4 : 0);
    int x = MAX(lx, 104), h = 0;
    if (x + w > 234) { x = 128 - w / 2; y += 15; h = 15; }
    for (int i = 0; i < nk; i++) { if (i) x = plus(s, x, y, a); x = chip(s, x, y, keys[i], a); }
    return h;
}
static void help_draw(Surf *bot, Surf *top) {
    float k = help_anim;
    if (k <= 0.001f) return;
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    rect_blend(bot, 0, 0, 256, 192, 0x000000, (int)(90 * k));
    int ds = help_sys == SYS_NDS;
    float y = (ds ? 12 : 4) + (1 - ease_out_back(k)) * 160;
    int h = ds ? 166 : 182;
    window(bot, 12, y, 232, h, help_then_launch ? "Before you play" : "Controls in games", a);
    static const char *quit[] = { "L", "SELECT", "START" };
    int ly = (int)y + 30;
    ly += help_line(bot, ly, "Leave the game", quit, 3, a) + 18;
    if (ds) {
        text_wrap_center(bot, FONT_SM, 128, ly - 2, 206, 12, C_TEXT_SOFT, a,
                         "Hold all three together. With Resume on (Emulator settings), that saves exactly where you are first: the next start carries on from there.");
        ly += 52;
        text_wrap_center(bot, FONT_SM, 128, ly, 206, 12, C_TEXT_SOFT, a, "The touch screen is the DS's own. Close the lid to sleep.");
    } else {
        static const char *sv[] = { "SELECT", "R" }, *ld[] = { "SELECT", "L" }, *mn[] = { "SELECT", "X" };
        ly += help_line(bot, ly, "Save a state", sv, 2, a) + 16;
        ly += help_line(bot, ly, "Load it", ld, 2, a) + 16;
        ly += help_line(bot, ly, "RetroArch's menu", mn, 2, a) + 20;
        text_wrap_center(bot, FONT_SM, 128, ly - 2, 206, 12, C_TEXT_SOFT, a,
                         "(ROCKNIX's usual buttons.) With Resume on, leaving saves where you are. Close the lid to sleep.");
    }
    float press = (tch.down && tch.zone == 15) ? 1 : 0;
    button(bot, 128 - 44, y + h - 24, 88, 18, help_then_launch ? "Play" : "OK", 1, press, a);
    if (top) {
        rect_blend(top, 0, 14, 256, 178, 0xf6f6f6, (int)(150 * k));
        float ty = 70 + (1 - ease_out_cubic(k)) * 90;
        window(top, 28, ty, 200, 52, 0, a);
        text_center_fit(top, FONT_BD, 128, (int)ty + 4, 186, C_TEXT, a, ds ? "Nintendo DS" : "Game Boy Advance");
        text_center_fit(top, FONT_SM, 128, (int)ty + 26, 186, C_TEXT_SOFT, a, help_then_launch ? "Shown once. Options > Controls in games." : "The same for every game.");
    }
}
static void help_update(void) {
    if (pressed(btn_a()) || pressed(btn_b()) || pressed(B_START)) { help_close(); return; }
    if (in.touch[1] && !tch.down) { tch.down = 1; tch.zone = 15; tch.moved = 0; }
    else if (!in.touch[1] && tch.down && tch.zone == 15) { tch.down = 0; help_close(); }
}

/* ---- the album: the screenshots taken in games ---- */

#define ALB_MAX 400
typedef struct { char path[400]; long long mt; char low[160]; } AlbPic;
static AlbPic *alb; static int albn, alb_scanned;
static int alb_open, alb_sel, alb_show; static float alb_anim; static Spring alb_scroll;
static double alb_show_t, alb_sel_t;
static Img *alb_th; static signed char *alb_th_st;
static Img alb_big, alb_old; static int alb_big_i = -1; static double alb_big_t = -10;

static int alb_cmp(const void *a, const void *b) { const AlbPic *x = a, *y = b; return x->mt < y->mt ? 1 : x->mt > y->mt ? -1 : 0; }
static void album_scan(void) {
    alb_scanned = 1;
    for (int i = 0; i < albn; i++) if (alb_th) img_free(&alb_th[i]);
    free(alb); free(alb_th); free(alb_th_st); alb = 0; alb_th = 0; alb_th_st = 0; albn = 0;
    alb = calloc(ALB_MAX, sizeof *alb);
    if (!alb) return;
    const char *dirs[] = { getenv("DSIXL_ALBUM"), "/storage/roms/screenshots", "/storage/.config/drastic/screenshots", "/storage/roms/nds/screenshots" };
    for (int d = 0; d < ARRAY_LEN(dirs); d++) {
        if (!dirs[d] || !*dirs[d]) continue;
        static const char *pats[] = { "%s/*.png", "%s/*.jpg", "%s/*.PNG", "%s/*.bmp" };
        for (int q = 0; q < ARRAY_LEN(pats); q++) {
            char pat[420]; snprintf(pat, sizeof pat, pats[q], dirs[d]);
            glob_t g;
            if (glob(pat, 0, 0, &g)) continue;
            for (size_t i = 0; i < g.gl_pathc && albn < ALB_MAX; i++) {
                struct stat st; if (stat(g.gl_pathv[i], &st)) continue;
                AlbPic *p = &alb[albn++];
                snprintf(p->path, sizeof p->path, "%s", g.gl_pathv[i]);
                p->mt = st.st_mtime;
                const char *b = strrchr(p->path, '/') + 1; int n = 0;
                for (; *b && n < (int)sizeof p->low - 1; b++) p->low[n++] = (char)tolower((unsigned char)*b);
                p->low[n] = 0;
            }
            globfree(&g);
        }
    }
    qsort(alb, (size_t)albn, sizeof *alb, alb_cmp);
    alb_th = calloc((size_t)MAX(1, albn), sizeof(Img)); alb_th_st = calloc((size_t)MAX(1, albn), 1);
}
static void album_open(void) {
    album_scan();
    alb_open = 1; alb_sel = 0; alb_show = 0; alb_scroll.x = alb_scroll.v = 0; alb_big_i = -1; alb_sel_t = T;
    sfx(SFX_OPEN); rumble(12, 0.3f);
}
static void album_close(void) {
    alb_open = 0; sfx(SFX_BACK);
    img_free(&alb_big); img_free(&alb_old); alb_big_i = -1;
    for (int i = 0; i < albn; i++) { img_free(&alb_th[i]); alb_th_st[i] = 0; }
}
#define AL_COLS 4
#define AL_CW 58
#define AL_CH 44
#define AL_X 12
#define AL_Y 28
#define AL_ROWS 3
static void album_pick(int i) {
    if (!albn) return;
    i = CLAMP(i, 0, albn - 1);
    if (i == alb_sel) return;
    alb_sel = i; alb_sel_t = T; sfx(SFX_CURSOR);
}
static void album_update(float dt) {
    if (pressed(btn_b()) || pressed(B_START)) { album_close(); return; }
    if (albn) {
        if (repeat(B_LEFT)) album_pick(alb_sel - 1);
        if (repeat(B_RIGHT)) album_pick(alb_sel + 1);
        if (repeat(B_UP)) album_pick(alb_sel - AL_COLS);
        if (repeat(B_DOWN)) album_pick(alb_sel + AL_COLS);
        if (repeat(B_L1)) album_pick(alb_sel - AL_COLS * AL_ROWS);
        if (repeat(B_R1)) album_pick(alb_sel + AL_COLS * AL_ROWS);
        if (pressed(btn_a())) { alb_show = !alb_show; alb_show_t = T; sfx_pitch(SFX_SELECT, alb_show ? 1.2f : 0.9f); }
        if (alb_show && T - alb_show_t > 4) { alb_show_t = T; alb_sel = (alb_sel + 1) % albn; alb_sel_t = T; }
    }
    /* touch: a picture is chosen; the chosen one again starts (or stops) the slideshow */
    int d = in.touch[1];
    if (d && !tch.down) {
        tch.down = 1; tch.zone = 16; tch.moved = 0; tch.x0 = in.tx[1]; tch.y0 = in.ty[1];
        int cx = (in.tx[1] - AL_X) / (AL_CW + 2), cy = (int)((in.ty[1] - AL_Y) / (float)(AL_CH + 2) + alb_scroll.x);
        if (in.tx[1] >= AL_X && cx < AL_COLS && in.ty[1] >= AL_Y && in.ty[1] < AL_Y + AL_ROWS * (AL_CH + 2)) {
            int i = cy * AL_COLS + cx;
            if (i >= 0 && i < albn) { if (i == alb_sel) { alb_show = !alb_show; alb_show_t = T; sfx(SFX_SELECT); } else album_pick(i); }
        } else if (in.ty[1] >= 172) album_close();
    } else if (!d && tch.down && tch.zone == 16) tch.down = 0;
    /* the selected row in sight */
    int row_ = alb_sel / AL_COLS;
    float want = alb_scroll.x;
    if (row_ < want) want = (float)row_;
    if (row_ > want + AL_ROWS - 1) want = (float)(row_ - AL_ROWS + 1);
    spring_step(&alb_scroll, want, 400, 36, dt);
    /* the pictures: the big one when it changes, two thumbnails a frame */
    if (albn && alb_big_i != alb_sel) {
        Img src;
        img_free(&alb_old); alb_old = alb_big; memset(&alb_big, 0, sizeof alb_big);
        if (!img_load_fit(alb[alb_sel].path, 256, 192, FIT_CONTAIN, cfg.cache, &src)) {
            for (int q = 0; q < src.w * src.h; q++) src.px[q] |= 0xff000000u;
            frame_photo(&src, src.w * 150 / src.h > 210 ? 210 : src.w * 150 / src.h, hash_str(alb[alb_sel].path), &alb_big);
            img_free(&src);
        }
        alb_big_i = alb_sel; alb_big_t = T;
    }
    int budget = 2;
    for (int r = (int)alb_scroll.x; r <= (int)alb_scroll.x + AL_ROWS && budget; r++)
        for (int c = 0; c < AL_COLS && budget; c++) {
            int i = r * AL_COLS + c;
            if (i < 0 || i >= albn || alb_th_st[i]) continue;
            alb_th_st[i] = img_load_fit(alb[i].path, AL_CW - 4, AL_CH - 4, FIT_COVER, cfg.cache, &alb_th[i]) ? -1 : 1;
            if (alb_th_st[i] > 0) for (int q = 0; q < alb_th[i].w * alb_th[i].h; q++) alb_th[i].px[q] |= 0xff000000u;
            budget--;
        }
}
static void album_draw(Surf *bot, Surf *top) {
    float k = alb_anim;
    if (k <= 0.001f) return;
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    Accent ac = accent();
    /* the bottom screen: the pictures in a grid on the DSi Camera's dark paper */
    float dy = (1 - ease_out_cubic(k)) * 192;
    rect_vgrad(bot, 0, (int)dy, 256, 192, 0x3a3d44, 0x24262b, a);
    for (int y = (int)dy; y < 192; y += 4) hline(bot, 0, 255, y, 0xffffff, a * 6 / 255);
    char t[64]; snprintf(t, sizeof t, albn ? "Album  %d / %d" : "Album", alb_sel + 1, albn);
    text_center(bot, FONT_BD, 128, (int)dy + 3, 0xffffff, a, t);
    Surf grid = *bot; surf_clip(&grid, 0, (int)dy + AL_Y - 2, 256, AL_ROWS * (AL_CH + 2) + 2);
    for (int i = 0; i < albn; i++) {
        int r = i / AL_COLS, c = i % AL_COLS;
        float ry = r - alb_scroll.x;
        if (ry < -1 || ry > AL_ROWS) continue;
        float x = AL_X + c * (AL_CW + 2) + 0, y = dy + AL_Y + ry * (AL_CH + 2);
        int sel = i == alb_sel;
        float pop = sel ? 1 + 0.06f * (1 - ease_out_cubic((float)(T - alb_sel_t) / 0.2f)) : 1;
        float w = AL_CW * pop, h = AL_CH * pop; x -= (w - AL_CW) / 2; y -= (h - AL_CH) / 2;
        rrect(&grid, x, y, w, h, 3, sel ? ac.base : 0x5a5e66, a);
        rrect(&grid, x + 2, y + 2, w - 4, h - 4, 2, 0x16171a, a);
        if (alb_th_st[i] > 0) blit(&grid, &alb_th[i], (int)(x + (w - alb_th[i].w) / 2), (int)(y + (h - alb_th[i].h) / 2), a);
        if (!sel) rect_blend(&grid, (int)x + 2, (int)y + 2, (int)w - 4, (int)h - 4, 0x000000, a * 50 / 255);
    }
    if (!albn) text_wrap_center(bot, FONT_SM, 128, (int)dy + 60, 220, 13, 0xd0d0d0, a,
                                "No pictures yet. The screenshots you take in games (roms/screenshots) show up here.");
    text_center(bot, FONT_SM, 128, (int)dy + 176, 0xa8acb4, a, alb_show ? "A: stop the slideshow   B: back" : "A: slideshow   L/R: a page   B: back");
    /* the top screen: the picture, printed and a little crooked, the last one fading under it */
    rect_vgrad(top, 0, 0, 256, 192, 0x2c2e33, 0x1c1d21, a);
    float fk = clampf((float)(T - alb_big_t) / 0.3f, 0, 1);
    if (alb_old.px && fk < 1) blit(top, &alb_old, 128 - alb_old.w / 2, 96 - alb_old.h / 2, (int)(a * (1 - fk)));
    if (alb_big.px) {
        float sc = 0.94f + 0.06f * ease_out_back(fk);
        float w = alb_big.w * sc, h = alb_big.h * sc;
        blit_tint(top, &alb_big, 128 - w / 2 + 3, 96 - h / 2 + 4, w, h, 0x000000, a * 90 / 255);
        blit_scaled(top, &alb_big, 128 - w / 2, 96 - h / 2, w, h, (int)(a * fk), fk < 1);
    }
    if (alb_show) { /* the slideshow's progress: a thin line along the bottom */
        float sp = (float)fmod(T - alb_show_t, 4.0) / 4.0f;
        rect_blend(top, 0, 190, (int)(256 * sp), 2, ac.base, a);
    }
}

/* a game's pictures from the album (its screenshots: named after the game), for the photo view */
static int alb_of(const Game *g, int *idx, int max) {
    if (!alb_scanned) album_scan();
    char lb[256]; int n = 0, m = 0;
    for (const char *c = g->base; *c && n < 255; c++) lb[n++] = (char)tolower((unsigned char)*c);
    lb[n] = 0;
    for (int i = 0; i < albn && m < max; i++) if (n > 2 && !strncmp(alb[i].low, lb, (size_t)n)) idx[m++] = i;
    return m;
}

/* ---- resume points ---- */

static int resume_png(const Game *g, char *out, int n) {
    const char *root = getenv("DSIXL_SAVESTATES");
    char p[700];
    if (g->sys == SYS_NDS) {
        snprintf(p, sizeof p, "%s/nds/%s.resume.dss", root ? root : "/storage/roms/savestates", g->base);
        if (!file_exists(p)) return -1;
        snprintf(out, (size_t)n, "%s.png", p);          /* (a picture, when something left one) */
        return 0;
    }
    const char *dirs[] = { root ? root : "/storage/roms/savestates", "/storage/.config/retroarch/states" };
    for (int d = 0; d < 2; d++) for (int sub = 0; sub < 2; sub++) {
        snprintf(p, sizeof p, "%s%s/%s.state.auto", dirs[d], sub ? "" : "/gba", g->base);
        if (file_exists(p)) { snprintf(out, (size_t)n, "%s.png", p); return 0; }
    }
    return -1;
}
static void resume_scan(void) {
    char p[720];
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) lib[s].g[i].resume = !resume_png(&lib[s].g[i], p, sizeof p);
}
/* (photo_is_title: declared above) */
/* the photo for the top screen: the game's own, its album pictures in turn (every 5 s), and for "continue" the
 * picture of where it was left, when there is one */
static Img *photo_for(Game *g) {
    static Img alt; static int alt_key = -1; static const Game *alt_g;
    Img *base = game_photo(g);
    photo_is_title = g->shot_state == 2;
    int key = -1; char path[720] = "";
    if (is_cont(sys, row[sys].sel) && g == cur_game(sys) && g->resume && !resume_png(g, path, sizeof path) && file_exists(path)) key = 100000;
    else {
        int idx[32], m = alb_of(g, idx, 32);
        if (!m) return base;
        int k = ((int)(T / 5)) % (m + (base ? 1 : 0));
        if (base && k == 0) return base;
        key = idx[base ? k - 1 : k];
        snprintf(path, sizeof path, "%s", alb[key].path);
    }
    if (alt_key != key || alt_g != g) {
        img_free(&alt); alt_key = key; alt_g = g;
        Img src;
        if (!img_load_fit(path, 256, 192, FIT_CONTAIN, cfg.cache, &src)) {
            for (int q = 0; q < src.w * src.h; q++) src.px[q] |= 0xff000000u;
            frame_photo(&src, 160, hash_str(path), &alt);
            img_free(&src);
        }
    }
    if (!alt.px) return base;
    photo_is_title = 0;
    return &alt;
}

/* ---- the volume keys: ROCKNIX changes the volume, the menu shows it (the DSi's bar, with a speaker) ---- */

static double vol_t = -10, vol_until = -10, vol_read_t; static int vol_pct = -1;
static int volume_now(void) {
    const char *e = getenv("DSIXL_SYSTEM_CFG");
    FILE *f = fopen(e && *e ? e : "/storage/.config/system/configs/system.cfg", "r");
    int v = -1; char line[256];
    if (f) { while (fgets(line, sizeof line, f)) if (!strncmp(line, "audio.volume=", 13)) v = atoi(line + 13); fclose(f); }
    return v;
}
static void volume_step(void) {
    if ((pressed(B_VOLUP) || pressed(B_VOLDN)) && !(in.held & BIT(B_MENU))) {
        vol_t = T; vol_until = T + 0.8;
        if (vol_pct < 0) vol_pct = 50;
        vol_pct = CLAMP(vol_pct + (pressed(B_VOLUP) ? 5 : -5), 0, 100);     /* (straight away; the real level follows) */
    }
    if ((in.held & (BIT(B_VOLUP) | BIT(B_VOLDN))) && !(in.held & BIT(B_MENU))) { vol_t = T; vol_until = T + 0.8; }
    if (T < vol_until && T - vol_read_t > 0.1) { vol_read_t = T; int v = volume_now(); if (v >= 0) vol_pct = v; }
}
static void draw_vol_bar(Surf *s) {
    float k = (float)(T - vol_t);
    if (k < 0 || k > 1.7f || vol_pct < 0) return;
    float in_k = ease_out_back(sat(k / 0.24f)), out = 1 - smoothstep(1.3f, 1.7f, k);
    int a = (int)(255 * out * sat(k / 0.1f));
    float y = 22 + (1 - in_k) * -26, x = 128 - 56;
    rrect(s, x + 1, y + 2, 112, 26, 9, 0x000000, a * 40 / 255);
    rrect(s, x, y, 112, 26, 9, 0xb4b4b4, a);
    rrect_vgrad(s, x + 1, y + 1, 110, 24, 8, 0xffffff, 0xe8e8e8, a);
    /* a speaker, its waves for how loud */
    int sx = (int)x + 10, sy = (int)y + 8;
    rect_blend(s, sx, sy + 3, 3, 5, 0x505050, a);
    tri_fill(s, sx + 3, sy + 3, sx + 8, sy - 1, sx + 8, sy + 12, 0x505050, a);
    tri_fill(s, sx + 3, sy + 8, sx + 8, sy - 1, sx + 8, sy + 12, 0x505050, a);
    for (int w = 0; w < 2; w++) if (vol_pct > w * 50) for (int i = -2 - w * 2; i <= 2 + w * 2; i++)
        pblend(s, sx + 11 + w * 3 + (int)(2 - fabsf((float)i) * 0.5f), sy + 5 + i, 0x505050, a);
    Accent ac = accent();
    int lit = (vol_pct * 8 + 50) / 100;
    for (int i = 0; i < 8; i++) {
        int bx = (int)x + 32 + i * 9, by = (int)y + 7;
        int on = i < lit;
        rrect(s, bx, by, 7, 12, 2, on ? col_mix(ac.base, 0x000000, 40) : 0xc4c4c4, a);
        rrect_vgrad(s, bx + 1, by + 1, 5, 10, 1.5f, on ? col_mix(ac.base, 0xffffff, 90) : 0xe2e2e2, on ? ac.base : 0xd2d2d2, a);
    }
}

/* ---- the time of day on the top screen: a warm morning, a golden late afternoon, a violet dusk, a blue night ---- */

static void draw_mood(Surf *s) {
    time_t now = time(0); struct tm lt; localtime_r(&now, &lt);
    float h = lt.tm_hour + lt.tm_min / 60.0f;
    const char *f = getenv("DSIXL_FAKE_HOUR"); if (f) h = (float)atof(f);
    /* (hour, colour, strength): eased between them */
    static const struct { float h; uint32_t c; float a; } K[] = {
        { 0, 0x24366e, 34 }, { 5, 0x24366e, 30 }, { 7, 0xffc39a, 22 }, { 9, 0xffffff, 0 }, { 15.5f, 0xffffff, 0 },
        { 17.5f, 0xffae5c, 26 }, { 19.5f, 0x8f72c8, 26 }, { 21, 0x24366e, 34 }, { 24, 0x24366e, 34 },
    };
    int i = 0; while (i < ARRAY_LEN(K) - 2 && h >= K[i + 1].h) i++;
    float t = smoothstep(K[i].h, K[i + 1].h, h);
    float a = lerpf(K[i].a, K[i + 1].a, t);
    uint32_t c = col_mix(K[i].c, K[i + 1].c, (int)(t * 256));
    if (K[i].a == 0) c = K[i + 1].c; else if (K[i + 1].a == 0) c = K[i].c;
    if (a > 0.5f) rect_blend(s, 0, 0, 256, 192, c, (int)a);
}

/* ---- play time ---- */

static void played_text(const Game *g, char *b, int n) {
    if (g->is_new && !g->plays) { snprintf(b, (size_t)n, "New: not played yet"); return; }
    if (!g->plays) { snprintf(b, (size_t)n, "Not played yet"); return; }
    char when[40] = "";
    time_t now = time(0);
    struct tm a, c; time_t lp = (time_t)g->last_played; localtime_r(&lp, &a); localtime_r(&now, &c);
    long days = (long)((now - lp) / 86400);
    if (a.tm_yday == c.tm_yday && a.tm_year == c.tm_year) snprintf(when, sizeof when, "today");
    else if (days < 2) snprintf(when, sizeof when, "yesterday");
    else if (days < 14) snprintf(when, sizeof when, "%ld days ago", days);
    else strftime(when, sizeof when, "%b %d", &a);
    long m = (g->secs + 30) / 60;
    if (m < 60) snprintf(b, (size_t)n, "Played %ld min  \xc2\xb7  last %s", MAX(1L, m), when);
    else snprintf(b, (size_t)n, "Played %ldh %02ldm  \xc2\xb7  last %s", m / 60, m % 60, when);
}

/* ---- the art cache, warmed in the background ---- */

typedef struct { int sys; char base[256]; } WarmItem;
static void *warm_main(void *arg) {
    WarmItem *w = arg;
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 15);
    Game *g = calloc(1, sizeof(Game));
    for (int i = 0; g && w[i].base[0]; i++) {
        g->sys = w[i].sys; snprintf(g->base, sizeof g->base, "%s", w[i].base);
        game_warm_art(g);
    }
    free(g); free(w);
    return 0;
}
static void warm_start(void) {
    int n = 0;
    for (int s = 0; s < SYS_COUNT; s++) n += lib[s].n;
    WarmItem *w = calloc((size_t)n + 1, sizeof *w);
    if (!w) return;
    /* the games near the selection first */
    int k = 0;
    for (int d = 0; k < n && d < 100000; d++) {
        int any = 0;
        for (int s = 0; s < SYS_COUNT; s++) for (int sg = -1; sg <= 1; sg += 2) {
            if (d == 0 && sg > 0) continue;
            int i = row[s].sel + sg * d;
            if (i < 0 || i >= vn(s)) continue;
            any = 1;
            Game *g = vg(s, i); w[k].sys = s; snprintf(w[k].base, sizeof w[k].base, "%s", g->base); k++;
        }
        if (!any) break;
    }
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, warm_main, w)) free(w);
    pthread_attr_destroy(&at);
}

/* ---- the games card watched: games put on it (over the network) show up by themselves ---- */

static void watch_card(void) {
    static double at;
    if (T - at < 5 || state != ST_MENU || rg || flow != F_NONE || gs_open || opt_open || help_on || alb_open || tch.down) return;
    at = T;
    int changed = 0;
    for (int s = 0; s < SYS_COUNT; s++) if (library_count_files(s) != lib[s].n) changed = 1;
    if (!changed) return;
    int n = rescan();
    if (n > 0) { char m[48]; snprintf(m, sizeof m, n == 1 ? "1 new game" : "%d new games", n); notice(2, m); sfx_pitch(SFX_SELECT, 1.2f); }
}

/* ---- init ---- */

void ui_init(const UiStart *st) {
    char p[400];
    snprintf(p, sizeof p, "%s/assets/boot_logo.png", cfg.home);
    img_load(p, &boot_logo);
    bri_lv = bri_from_system();
    /* just updated (update.sh leaves the version) */
    FILE *uf = fopen("/tmp/dsixl-updated", "r");
    if (uf) { char v[32] = ""; if (fgets(v, sizeof v, uf)) { v[strcspn(v, "\n")] = 0; char m[64]; snprintf(m, sizeof m, "Updated to %s", v); notice(2, m); } fclose(uf); remove("/tmp/dsixl-updated"); }
    order_rebuild();
    fav_rebuild();
    for (int i = 0; i < favv_n; i++) if (!strcmp(favv[i]->file, cfg.last_fav)) row[V_FAV].sel = i;
    if (has_cont) row[V_FAV].sel = 0;                  /* the favourites open on "continue" */
    for (int v = 0; v < V_COUNT; v++) row[v].pos.x = row[v].sel;
    /* the favourites are the start; back from a game, the tab it was started from */
    sys = st->ret_file[0] ? CLAMP(cfg.sys, 0, V_COUNT - 1) : V_FAV;
    state = (st->no_boot || !cfg.boot_splash || st->ret_file[0]) ? ST_MENU : ST_BOOT;
    st_t = 0;
    if (state == ST_MENU) intro_t = 0;
    if (st->ret_file[0]) {
        int s = CLAMP(st->ret_sys, 0, SYS_COUNT - 1);
        for (int i = 0; i < vn(sys); i++) {
            Game *g = vg(sys, i);
            if (g->sys == s && !strcmp(g->file, st->ret_file)) { row[sys].sel = i; row[sys].pos.x = i; rg = g; r_view = sys; break; }
        }
        if (rg) { ret_t = 0; intro_t = 0.25; }
        /* how the game went: launch.sh leaves word when it couldn't start one, or saved one for the battery */
        FILE *f = fopen("/tmp/dsixl-error", "r");
        if (f) {
            if (fgets(err_title, sizeof err_title, f) && fgets(err_msg, sizeof err_msg, f)) err_pending = 1;
            err_title[strcspn(err_title, "\n")] = 0; err_msg[strcspn(err_msg, "\n")] = 0;
            fclose(f); remove("/tmp/dsixl-error");
        }
        if (!access("/tmp/dsixl-rescued", F_OK)) { remove("/tmp/dsixl-rescued"); rescued = 1; }
    }
    if (state == ST_MENU && !rg) music_play();
    fx_alloc();
    { Game *cg = cur_game(sys); int cs = cg ? cg->sys : SYS_NDS; float cw, ch, cd; cart_size(cs, &cw, &ch, &cd);
      frame_w.x = sel_w_of(cs) + 16; frame_h.x = sel_w_of(cs) * ch / cw + 34; }
    last_input = 0;
    /* the snow of December: flakes spread down the top screen to begin with */
    for (int i = 0; i < ARRAY_LEN(flakes); i++) {
        uint32_t h = hash3(i, 5, 9);
        flakes[i] = (Flake){ (float)(h % 256), (float)((h >> 8) % 192), 7 + (float)((h >> 16) % 9), (float)((h >> 4) % 628) / 100, (h >> 20) % 3 == 0 ? 2.0f : 1.0f };
    }
    /* a birthday: confetti as the menu appears (not when coming back from a game) */
    if (state == ST_MENU && !rg && is_birthday()) { confetti(); confetti_done = 1; }
    resume_scan();
    warm_start();
}

static void snow(Surf *s, float dt) {
    if (!is_snow_season()) return;
    for (int i = 0; i < ARRAY_LEN(flakes); i++) {
        Flake *f = &flakes[i];
        f->y += f->v * f->sz * 0.7f * dt; f->ph += dt * 1.3f;
        float x = f->x + sinf(f->ph) * 4;
        if (f->y > 194) { f->y = -3; f->x = (float)(hash3(i, (int)(T * 10), 4) % 256); }
        /* white on the near-white stripes: each flake gets a cool shadow under it so it reads */
        int ix = (int)x, iy = (int)f->y;
        if (f->sz > 1.5f) {
            rect_blend(s, ix, iy + 1, 3, 2, 0x8fa4ba, 110); rect_blend(s, ix - 1, iy, 3, 2, 0xffffff, 255);
            pblend(s, ix, iy - 1, 0xffffff, 140); pblend(s, ix - 2, iy, 0xffffff, 140);
        } else { pblend(s, ix + 1, iy + 1, 0x8fa4ba, 130); pblend(s, ix, iy, 0xffffff, 255); }
    }
}

/* ---- update ---- */

static void start_launch(void) {
    Game *g = cur_game(sys);
    if (!g) { sfx(SFX_ERROR); return; }
    /* the first game of a system: how its controls go, first */
    if (!cfg.seen_help[g->sys] && !help_wait) { help_open(g->sys, 1); return; }
    library_mark_played(g);
    lg = g; l_sys = g->sys; l_view = sys;
    state = ST_LAUNCH; st_t = 0;
    sfx(SFX_SELECT);
    rumble(25, 0.6f);
    music_fade(0, 1.2f);
    if (sys < V_FAV) snprintf(cfg.last[sys], sizeof cfg.last[sys], "%s", g->file);
    else snprintf(cfg.last_fav, sizeof cfg.last_fav, "%s", g->file);
    cfg.recent_sys = g->sys; snprintf(cfg.recent, sizeof cfg.recent, "%s", g->file);
    cfg.sys = sys;
    state_save();
}

static void write_launch(void) {
    if (launched_file_written) return;
    launched_file_written = 1;
    const char *p = getenv("DSIXL_LAUNCH_FILE"); if (!p) p = "/tmp/dsixl-launch";
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s\n%s\n%s\n", sys_short(l_sys), lg->path, lg->file); fclose(f); }
}

static void touch_update(void) {
    int down = in.touch[1], x = in.tx[1], y = in.ty[1];
    if (err_open || (tch.down && tch.zone == 14)) {
        /* the error window: a tap anywhere closes it (the OK button looks pressed while held) */
        if (down && !tch.down) { tch.down = 1; tch.zone = 14; tch.moved = 0; tch.x0 = x; tch.y0 = y; }
        else if (!down && tch.down) { tch.down = 0; if (err_open) err_close(); }
        return;
    }
    if (down && !tch.down) {
        last_input = T;
        tap_burst(1, (float)x, (float)y);
        tch.down = 1; tch.moved = 0; tch.x0 = tch.x = x; tch.y0 = tch.y = y; tch.t0 = T;
        tch.pos0 = row[sys].pos.x; tch.vel = 0; tch.lastx = (float)x; tch.lastt = T;
        if (gs_open) {
            int n = gs_n, by = gs_btn_y(), r0 = gs_row_y0();
            if (y >= by - 2 && y < by + 20) tch.zone = 8;
            else if (y >= GS_Y + 22 && y < GS_Y + 22 + GS_TABS) tch.zone = 11;          /* the page tabs */
            else if (y >= r0 && y < r0 + n * GS_ROW && x >= GS_X && x < GS_X + GS_W) {
                int i = (y - r0) / GS_ROW;
                if (i != gs_sel) { gs_sel = i; sfx(SFX_CURSOR); }
                tch.zone = x >= GS_X + 100 ? 7 : 9;
            } else tch.zone = 10;
            return;
        }
        if (opt_open) tch.zone = 3;
        else if (x >= GEAR_X - 15 && x < GEAR_X + 15 && y >= GEAR_Y - 10 && y < GEAR_Y + 10) tch.zone = 6;
        else if (x >= SUN_X - 15 && x < SUN_X + 15 && y >= SUN_Y - 10 && y < SUN_Y + 10) tch.zone = 12;
        else if (y < 18) tch.zone = 1;
        else if (y >= 176) {
            tch.zone = 2;
            int z, i = scroll_bar_hit(x, 176, vn(sys), 5, &z);
            if (z) { sb_press = z; sb_press_t = T; move(z); }
            else if (i >= 0) { select_game(sys, i, 1); sfx(SFX_SCROLL); }
        } else {
            tch.zone = 0;
            coast = 0; glide_dir = 0;
            if (vn(sys) && abs(x - 128) < sel_w_now(sys) * 0.6f + 6 && y > 60 && y < 160) tch.zone = 5;   /* on the selected one */
        }
        if (tch.zone == 3) {
            int vis = MIN(optn, OPT_VIS), i = (int)floorf((y - (OPT_Y + 26)) / (float)OPT_ROW + opt_scroll.x);
            opt_drag0 = opt_scroll.x;
            if (x >= OPT_X && x < OPT_X + OPT_W && y >= OPT_Y + 26 && y < OPT_Y + 26 + vis * OPT_ROW && i >= 0 && i < optn) { if (i != opt_sel) sfx(SFX_CURSOR); opt_sel = i; }
            else if (x >= OPT_X && x < OPT_X + OPT_W && y >= OPT_Y && y < OPT_Y + 28 + vis * OPT_ROW + 17) tch.zone = 13;   /* the window's edge: nothing */
            else tch.zone = 4;          /* outside the window: closes it */
        }
        return;
    }
    if (down && tch.down) {
        if (abs(x - tch.x0) > 4 || abs(y - tch.y0) > 4) tch.moved = 1;
        if (tch.zone == 5 && tch.moved) tch.zone = 0;   /* it was a drag after all */
        if (tch.zone == 0 && tch.moved && vn(sys)) {
            RowGeo G = geo(sys);
            float p = tch.pos0 - (x - tch.x0) / G.spacing;
            /* past an end it gives, but less and less */
            if (p < 0) p = -0.5f * (1 - expf(p * 1.5f));
            else if (p > vn(sys) - 1) p = vn(sys) - 1 + 0.5f * (1 - expf(-(p - (vn(sys) - 1)) * 1.5f));
            row[sys].pos.x = p;
            row[sys].pos.v = tch.vel;            /* (not stepped while held: what the tilt reads) */
            last_input = T;
            if (((int)(T * 60)) % 3 == 0) spawn_k(P_SPARK, 1, (float)x, (float)y, 0, -8, 0.3f, -1.5f, fav_color(cfg.color));
            double dtt = T - tch.lastt;
            if (dtt > 0.001) { tch.vel = lerpf(tch.vel, (float)(-(x - tch.lastx) / G.spacing / dtt), 0.5f); tch.lastx = (float)x; tch.lastt = T; }
            int n = CLAMP((int)roundf(row[sys].pos.x), 0, vn(sys) - 1);
            if (n != row[sys].sel) { select_game(sys, n, 1); rumble(7, 0.25f); }   /* a ratchet under the finger */
        }
        if (tch.zone == 3 && tch.moved) {
            /* dragging the list */
            int vis = MIN(optn, OPT_VIS);
            opt_scroll.x = clampf(opt_drag0 - (y - tch.y0) / (float)OPT_ROW, 0, (float)MAX(0, optn - vis)); opt_scroll.v = 0;
        }
        if (tch.zone == 2 && vn(sys)) {
            int z, i = scroll_bar_hit(x, 176, vn(sys), 5, &z);
            if (!z && i >= 0 && i != row[sys].sel) { select_game(sys, i, 0); sfx(SFX_SCROLL); }
            if (!z) letter_t = T;
        }
        tch.x = x; tch.y = y;
        return;
    }
    if (!down && tch.down) {
        tch.down = 0;
        if (tch.zone == 5 && vn(sys)) { fx_of(sys, row[sys].sel)->wob.v += 9; start_launch(); }
        else if (tch.zone == 0 && vn(sys)) {
            if (tch.moved) {
                /* a flick carries on by itself (coasting), a slow release settles where it is */
                float v = clampf(tch.vel, -28, 28);
                if (fabsf(v) > 2.5f && T - tch.lastt < 0.08) { coast = 1; row[sys].pos.v = v; }
                else { row[sys].pos.v = 0; select_game(sys, CLAMP((int)roundf(row[sys].pos.x), 0, vn(sys) - 1), 0); notch_pending = 1; }
            } else {
                /* a tap: on the selected cartridge starts it, on another selects it */
                if (abs(tch.x - 128) < sel_w_now(sys) * 0.6f + 6 && tch.y > 60 && tch.y < 160) { fx_of(sys, row[sys].sel)->wob.v += 9; start_launch(); }
                else {
                    float best = 1e9f; int bi = -1;
                    for (int i = MAX(0, row[sys].sel - 4); i <= MIN(vn(sys) - 1, row[sys].sel + 4); i++) {
                        Item it; layout(sys, i, row[sys].pos.x, 0, &it);
                        float d = fabsf(it.x - tch.x);
                        if (d < best && d < it.w * 0.6f + 4) { best = d; bi = i; }
                    }
                    if (bi >= 0) { fx_of(sys, bi)->wob.v += 11; select_game(sys, bi, 1); }
                }
            }
        } else if (tch.zone == 1 && !tch.moved) switch_sys(tch.x0 < 90 ? V_NDS : tch.x0 < 166 ? V_FAV : V_GBA);
        else if (tch.zone == 3 && !tch.moved) {
            int i = (int)floorf((tch.y - (OPT_Y + 26)) / (float)OPT_ROW + opt_scroll.x);
            if (i == opt_sel) opt_act(opt_id(i), 1);
        } else if (tch.zone == 4) { opt_open = 0; opt_t = T; sfx(SFX_BACK); }
        else if (tch.zone == 6 && !tch.moved) gs_open_here();
        else if (tch.zone == 12 && !tch.moved) bri_set(bri_lv % 5 + 1);
        else if (tch.zone == 7 && !tch.moved) gs_change(gs_sel, tch.x0 < (GS_X + 100 + GS_X + GS_W - 10) / 2 ? -1 : 1);
        else if (tch.zone == 9 && !tch.moved) gs_change(gs_sel, 1);
        else if (tch.zone == 8 && !tch.moved) { gs_sel = gs_n + (tch.x0 >= 128); gs_activate(); }
        else if (tch.zone == 10 && !tch.moved) { gs_close(); sfx(SFX_BACK); }
        else if (tch.zone == 11 && !tch.moved) gs_page(tch.x0 < 128 ? SYS_NDS : SYS_GBA);
    }
}

/* Y: a favourite, or not. The star pops on with a burst of gold (or shrinks away). In the favourites tab, taking a
 * game out of the favourites takes it off the shelf: its neighbours close up. */
static void toggle_fav(void) {
    Row *r = &row[sys];
    Game *g = cur_game(sys);
    if (!g) return;
    int on = !g->fav;
    library_set_fav(g, on);
    fav_g = g; fav_on = on; fav_t = T;
    glide_dir = 0; coast = 0; a_hold = 0;
    int was_cont = is_cont(sys, r->sel);
    if (sys == V_FAV && !on && !was_cont) {
        /* off the shelf: a puff where it stood, and the list closes up */
        for (int i = 0; i < 10; i++) {
            float a = i * (2 * PI_F / 10), sp = 40 + (hash3(i, (int)(T * 60), 6) % 30);
            spawn_k(P_SPARK, 1, 128, geo(sys).cy, cosf(a) * sp, sinf(a) * sp, 0.4f, -1.5f, 0xc8c0b0);
        }
        int keep = r->sel;
        fav_rebuild();
        r->sel = CLAMP(keep < vn(sys) ? keep : keep - 1, 0, MAX(0, vn(sys) - 1));
        r->pos.x = (float)r->sel; reorder_t = T; notch_pending = 1; sel_t = T;
    } else fav_rebuild();
    if (sys == V_FAV) { logo_cur = cur_game(sys); logo_prev = 0; }
    fx_of(sys, r->sel)->wob.v += on ? -10 : 8;
    if (on) {
        sfx_pitch(SFX_SELECT, 1.25f); rumble(20, 0.5f);
        RowGeo G = geo(sys);
        for (int i = 0; i < 10; i++) {
            float a = i * (2 * PI_F / 10), sp = 50 + (hash3(i, (int)(T * 60), 5) % 40);
            spawn_k(P_SPARK, 1, 128 + sel_w_now(sys) * 0.42f, G.cy - 26, cosf(a) * sp, sinf(a) * sp, 0.5f, -(1.5f + (i & 1)), C_STAR);
        }
    } else { sfx_pitch(SFX_BACK, 1.1f); rumble(10, 0.3f); }
}

static void menu_update(float dt) {
    if (help_on) help_update();
    else if (alb_open) album_update(dt);
    else if (flow != F_NONE && !err_open) flow_update(dt);
    else if (err_open) { if (pressed(btn_a()) || pressed(btn_b()) || pressed(B_START)) err_close(); }
    else if (gs_open) gs_update();
    else if (opt_open) {
        if (repeat(B_UP)) { opt_sel = (opt_sel + optn - 1) % optn; sfx(SFX_CURSOR); }
        if (repeat(B_DOWN)) { opt_sel = (opt_sel + 1) % optn; sfx(SFX_CURSOR); }
        if (repeat(B_LEFT) && opt_id(opt_sel) == OPT_COLOR) opt_act(OPT_COLOR, -1);
        if (repeat(B_RIGHT) && opt_id(opt_sel) == OPT_COLOR) opt_act(OPT_COLOR, 1);
        if (pressed(btn_a())) opt_act(opt_id(opt_sel), 1);
        if (pressed(btn_b()) || pressed(B_START)) { opt_open = 0; opt_t = T; sfx(SFX_BACK); }
    } else if (intro_t < 0 || T - intro_t > 0.25) {
        /* left / right: a step at once; held, a glide that picks up speed; let go, it settles on the next one */
        for (int k = 0; k < 2; k++) {
            int b = k ? B_RIGHT : B_LEFT, dir = k ? 1 : -1;
            if (pressed(b)) { coast = 0; glide_dir = 0; held_since[k] = T; move(dir); }
            else if ((in.held & BIT(b)) && !glide_dir && T - held_since[k] > 0.32 && vn(sys)) { glide_dir = dir; glide_speed = 7; }
        }
        if (glide_dir && vn(sys)) {
            int b = glide_dir > 0 ? B_RIGHT : B_LEFT;
            Row *r = &row[sys];
            if (!(in.held & BIT(b))) {
                int n = glide_dir > 0 ? (int)ceilf(r->pos.x - 0.05f) : (int)floorf(r->pos.x + 0.05f);
                select_game(sys, CLAMP(n, 0, vn(sys) - 1), 0);
                glide_dir = 0;
            } else {
                glide_speed = MIN(22.0f, glide_speed + 18 * dt);
                float np = r->pos.x + glide_dir * glide_speed * dt;
                if (np < 0 || np > vn(sys) - 1) {
                    if ((glide_dir < 0 && r->pos.x > 0) || (glide_dir > 0 && r->pos.x < vn(sys) - 1)) end_bump(glide_dir, 0.6f);
                    np = clampf(np, 0, (float)(vn(sys) - 1));
                    spring_step(&rubber, glide_dir * 8.0f, 300, 22, dt);      /* held against the end: stays stretched */
                }
                r->pos.x = np; r->pos.v = glide_dir * glide_speed;
                int n = CLAMP((int)roundf(np), 0, vn(sys) - 1);
                if (n != r->sel) select_game(sys, n, 2);
            }
        }
        if (repeat(B_UP)) jump(-1);              /* by letter (in name order), else five at a time */
        if (repeat(B_DOWN)) jump(1);
        /* L / R: along the tabs, NDS - favourites - GBA; SELECT goes round them */
        if (pressed(B_L1)) { int p = tab_pos(sys); if (p > 0) switch_sys(TAB_AT[p - 1]); else { bump_dir = -1; bump_t = T; sfx(SFX_BUMP); } }
        if (pressed(B_R1)) { int p = tab_pos(sys); if (p < 2) switch_sys(TAB_AT[p + 1]); else { bump_dir = 1; bump_t = T; sfx(SFX_BUMP); } }
        if (pressed(B_SELECT)) gs_open_here();
        /* A: the cartridge sinks in while it's held; letting go starts it */
        if (pressed(btn_a()) && vn(sys)) { a_hold = 1; rumble(10, 0.3f); }
        if (a_hold && !(in.held & BIT(btn_a()))) { a_hold = 0; start_launch(); }
        if (pressed(B_START)) opt_open_now();
        if (pressed(B_X)) {           /* the top screen: the logo, or the photo */
            cfg.top_view = !cfg.top_view; view_t = T; state_save();
            sfx_pitch(SFX_TAB, cfg.top_view ? 1.12f : 0.92f); rumble(12, 0.3f);
        }
        if (pressed(B_Y) && vn(sys)) toggle_fav();
    }
    if (flow == F_NONE && !help_on && !alb_open) touch_update();
    help_anim = clampf(help_anim + (help_on ? dt : -dt) / 0.22f, 0, 1);
    alb_anim = clampf(alb_anim + (alb_open ? dt : -dt) / 0.25f, 0, 1);
    /* a flick coasts on, slowing, ticking past each cartridge, then settles on one */
    if (coast && vn(sys)) {
        Row *r = &row[sys];
        r->pos.x += r->pos.v * dt;
        r->pos.v *= expf(-3.0f * dt);
        if (r->pos.x < 0 || r->pos.x > vn(sys) - 1) {
            int d = r->pos.x < 0 ? -1 : 1;
            r->pos.x = clampf(r->pos.x, 0, (float)(vn(sys) - 1));
            end_bump(d, clampf(fabsf(r->pos.v) / 12, 0.4f, 1.2f));
            r->pos.v = 0; coast = 0;
        } else if (fabsf(r->pos.v) < 2.0f) coast = 0;
        int n = CLAMP((int)roundf(r->pos.x), 0, vn(sys) - 1);
        if (n != r->sel) { select_game(sys, n, 2); rumble(6, 0.2f); }
    }
    /* the row follows the selection on a quick spring: there in about a fifth of a second, a hint of overshoot */
    int dragging = tch.down && tch.zone == 0 && tch.moved;
    for (int s = 0; s < V_COUNT; s++)
        if (!((dragging || glide_dir || coast) && s == sys)) spring_step(&row[s].pos, (float)row[s].sel, 430, 34, dt);
    if (!glide_dir) spring_step(&rubber, 0, 320, 17, dt);
    spring_step(&frame_pop, 0, 300, 12, dt);
    {
        Game *cg = cur_game(sys); int cs = cg ? cg->sys : SYS_NDS;
        float cw, ch, cd; cart_size(cs, &cw, &ch, &cd);
        float tw = sel_w_of(cs) + 16, th = sel_w_of(cs) * ch / cw + 34;
        if (frame_w.x < 1) { frame_w.x = tw; frame_h.x = th; }
        spring_step(&frame_w, tw, 420, 30, dt); spring_step(&frame_h, th, 420, 30, dt);
    }
    /* the frame goes after the selected cartridge (which the row is bringing to the middle): it lunges toward it and
     * they settle together */
    if (vn(sys)) {
        Item it; layout(sys, row[sys].sel, row[sys].pos.x, 0, &it);
        spring_step(&frame_x, it.x - clampf(rubber.x, -24, 24) * 0.25f, 520, 30, dt);
    }
    spring_step(&sink, (a_hold || (tch.down && tch.zone == 5)) ? 1.0f : 0.0f, 700, 34, dt);
    /* the detent: the selection has locked into the frame */
    if (notch_pending && !glide_dir && !coast && !dragging && vn(sys) && fabsf(row[sys].pos.x - row[sys].sel) < 0.06f) {
        notch_pending = 0;
        frame_pop.v -= 3.2f;
        sfx_ex(SFX_NOTCH, 1.0f, 0.8f);
        rumble(12, 0.35f);
    }
    /* the jelly settles; the selection's hop when the row arrives */
    for (int s = 0; s < V_COUNT; s++) for (int i = MAX(0, row[s].sel - 5); i <= MIN(fx_n[s] - 1, row[s].sel + 5); i++) spring_step(&fx[s][i].wob, 0, 430, 7.5f, dt);
    opt_anim = clampf(opt_anim + (opt_open ? dt : -dt) / 0.22f, 0, 1);
    err_anim = clampf(err_anim + (err_open ? dt : -dt) / 0.22f, 0, 1);
    if (opt_open) opt_follow(dt);
    gs_anim = clampf(gs_anim + (gs_open ? dt : -dt) / 0.24f, 0, 1);
}

static void sound_parts_slam(void) {
    sfx(SFX_SLAM);
    rumble(90, 1.0f);
    shake_t = T; shake_amp = 5;
    for (int i = 0; i < 26; i++) {
        float side = (i & 1) ? 1 : -1, sp = 30 + (hash3(i, 1, 7) % 70);
        spawn(0, 128 + side * (20 + (hash3(i, 2, 7) % 40)), slot_open_y(1) - 1, side * sp, -(15 + (float)(hash3(i, 3, 7) % 40)), 0.5f + (hash3(i, 4, 7) % 40) / 100.0f, 3 + (hash3(i, 5, 7) % 4), 0x8a8478);
    }
    for (int i = 0; i < 8; i++) spawn(0, 128 + ((int)(hash3(i, 9, 3) % 120) - 60), slot_open_y(1) - 6 - (hash3(i, 8, 3) % 30), 0, -10, 0.5f, -3, 0xffffff);
}

/* ---- the launch: timeline (seconds) ---- */
#define L_SQUASH 0.10f
#define L_UP     0.36f      /* off the top of the bottom screen */
#define L_APEX   0.66f      /* risen to the top of the top screen */
#define L_HANG   0.80f
#define L_SLAM   0.93f      /* home */
#define L_FADE   1.40f      /* then both screens to black */
#define L_END    1.90f

static void launch_draw(Surf *top, Surf *bot, float t) {
    float cw, ch, cd; cart_size(l_sys, &cw, &ch, &cd);
    RowGeo G = geo(l_view);
    /* bottom: the cartridge squashes, then jumps off the top edge; the others drop away */
    Item base; layout(l_view, row[l_view].sel, row[l_view].pos.x, 0, &base);
    if (t < L_UP) {
        CartPose p = base.pose;
        if (t < L_SQUASH) { float k = sinf(t / L_SQUASH * PI_F); p.sy += 3 * k; p.scale *= 1 - 0.06f * k; }
        else {
            float k = ease_in_cubic((t - L_SQUASH) / (L_UP - L_SQUASH));
            p.sy = lerpf(base.y, -ch * p.scale - 20, k);
            p.scale *= 1 + 0.35f * k;
            p.yaw *= 1 - k; p.pitch = lerpf(p.pitch, 0.25f, k); p.roll *= 1 - k;
            /* a trail of fading copies: the speed */
            for (int j = 3; j >= 1; j--) {
                CartPose q = p; float kk = ease_in_cubic(MAX(0.0f, (t - j * 0.018f - L_SQUASH) / (L_UP - L_SQUASH)));
                q.sy = lerpf(base.y, -ch * p.scale - 20, kk); q.alpha = 0.18f * (4 - j) / 3.0f; q.gloss = 0;
                cart_draw(bot, lg, &q);
            }
        }
        p.gloss = 0;
        cart_draw(bot, lg, &p);
    }
    /* top: the logo goes, the slot rises, the cartridge comes up from below and slams down into it */
    float rise = ease_out_back(clampf((t - 0.15f) / 0.3f, 0, 1));
    float sc = (l_sys == SYS_NDS ? 92.0f : 128.0f) / cw;
    (void)G;
    float apex_y = 66;
    if (t >= L_UP - 0.05f) {
        CartPose p; memset(&p, 0, sizeof p);
        p.sx = 128; p.alpha = 1; p.bright = 1.05f; p.scale = sc;
        float home = slot_open_y(1) + ch * sc * 0.42f - ch * sc * 0.5f;   /* its lower part (42%) in the slot */
        if (t < L_APEX) {
            float k = ease_out_cubic((t - (L_UP - 0.05f)) / (L_APEX - L_UP + 0.05f));
            p.sy = lerpf(192 + ch * sc * 0.6f, apex_y, k);
            p.yaw = (1 - k) * 2 * PI_F;                          /* a full turn on the way up */
            p.pitch = (1 - k) * 0.5f; p.roll = sinf(k * PI_F) * 0.08f;
        } else if (t < L_HANG) {
            float k = (t - L_APEX) / (L_HANG - L_APEX);
            p.sy = apex_y - sinf(k * PI_F) * 3;
            p.roll = sinf(k * PI_F * 2) * 0.03f; p.pitch = -0.05f * sinf(k * PI_F);
        } else if (t < L_SLAM) {
            float k = ease_in_cubic((t - L_HANG) / (L_SLAM - L_HANG));
            p.sy = lerpf(apex_y - 3, home, k);
            p.scale = sc * (1 + 0.04f * k);
            /* motion lines on the way down */
            for (int j = 0; j < 6; j++) {
                float lx = 128 - cw * sc * 0.45f + j * cw * sc * 0.18f;
                vline(top, (int)lx, (int)(p.sy - ch * sc * 0.5f - 22 * k), (int)(p.sy - ch * sc * 0.5f - 4), 0x9a9a9a, (int)(120 * k));
            }
        } else {
            /* home: a small bounce, then still */
            float k = (t - L_SLAM) / 0.22f;
            p.sy = home - (k < 1 ? sinf(k * PI_F) * 3 * (1 - k) : 0);
            p.scale = sc * (k < 1 ? 1 + 0.03f * (1 - k) * cosf(k * PI_F * 3) : 1);
        }
        /* the slot's top face, the cartridge (cut at the slit: below it, it's inside), the slot's front */
        draw_slot_back(top, rise, cw * sc + 8);
        Surf clip = *top; surf_clip(&clip, 0, 0, 256, rise > 0.001f ? (int)roundf(slot_open_y(rise)) : 192);
        cart_draw(&clip, lg, &p);
    } else draw_slot_back(top, rise, cw * sc + 8);
    draw_slot_front(top, rise, cw * sc + 8, t >= L_SLAM ? ((int)((t - L_SLAM) * 8) % 2 ? 2 : 1) : 0);
}

static int raw_new_input(const Input *i) {
    int t = i->touch[0] || i->touch[1];
    int n = (i->held & ~raw_held) != 0 || (t && !raw_touch);
    raw_held = i->held; raw_touch = t;
    return n;
}
static void wake(int from_black) {
    if (pw_mode >= PW_DIM) { wake_t = T; wake_black = from_black; if (cfg.music && state == ST_MENU) music_fade(1, 0.6f); }
    swallow = 1; last_input = T; pw_mode = PW_FULL;
}

int ui_power(void) { return pw_mode; }
void ui_idle_input(const Input *i) { if (raw_new_input(i)) wake(1); }
void ui_resumed(void) {
    /* the lid opened: out of black, the open sound */
    pw_mode = PW_DIM; wake(1); swallow = 0;
    sfx(SFX_OPEN);
}
static void pw_step(void) {
    double idle = T - last_input;
    int m = PW_FULL;
    if (state == ST_LAUNCH || rg || auto_go_t > 0) m = PW_FULL;
    else if (cfg.screen_off_after > 0 && idle > cfg.screen_off_after) m = PW_OFF;
    else if (cfg.dim_after > 0 && idle > cfg.dim_after) m = PW_DIM;
    else if (idle > 30) m = PW_CALM;
    if (m == PW_OFF && pw_mode != PW_OFF) music_fade(0, 1.0f);
    pw_mode = m;
}

int ui_frame(const Input *input, double dt, Surf *top, Surf *bot) {
    pin = in; in = *input;
    /* the input that wakes dimmed screens is only that: nothing reacts to it until it's let go */
    if (raw_new_input(input) && pw_mode >= PW_DIM) wake(0);
    if (swallow) {
        if (!input->held && !input->touch[0] && !input->touch[1]) swallow = 0;
        in.held = 0; in.touch[0] = in.touch[1] = 0;
    }
    T += dt; st_t += dt;
    if (in.quit) { job_stop(&artjob); return EXIT_QUIT; }
    float fdt = (float)MIN(dt, 0.05);
    rumble_tick();
    parts_step(fdt);

    if (state == ST_BOOT) {
        if (in.held != pin.held || in.touch[1]) last_input = T;
        draw_boot(top, bot, (float)st_t);
        static double cont_t = -1;
        if (st_t > BOOT_CHIME && st_t - dt <= BOOT_CHIME) sfx(SFX_BOOT);
        /* a touch on the bottom screen once the screens are up; buttons only after a long while (a touchscreen that
         * doesn't answer must not lock you out), or after boot_wait */
        int touched = in.touch[1] && !pin.touch[1];
        if (cont_t < 0 && st_t > BOOT_IN && (touched || (st_t > 12 && any_pressed()) || (cfg.boot_wait > 0 && st_t > cfg.boot_wait + 1.0))) {
            cont_t = st_t; sfx(SFX_CONTINUE); rumble(15, 0.4f);
            if (touched) tap_burst(1, (float)in.tx[1], (float)in.ty[1]);
        }
        if (cont_t >= 0) {
            float k = (float)(st_t - cont_t);
            rect_blend(bot, 0, 0, 256, 192, 0xffffff, (int)(255 * smoothstep(0, 0.3f, k)));
            rect_blend(top, 0, 0, 256, 192, 0xffffff, (int)(255 * smoothstep(0.1f, 0.4f, k)));
            if (k > 0.5f) {
                state = ST_MENU; st_t = 0; intro_t = 0; music_play(); sfx(SFX_WHOOSH); last_input = T;
                if (is_birthday() && !confetti_done) { confetti(); confetti_done = 1; }
                /* A held through the start: straight on into the game played last */
                if ((in.held & BIT(btn_a())) && has_cont) {
                    sys = V_FAV; cfg.sys = sys; row[V_FAV].sel = 0; row[V_FAV].pos.x = 0; logo_cur = cur_game(sys);
                    auto_go_t = T + 0.7;
                }
            }
        }
        parts_draw(bot, 1);
        pw_step();
        return exit_code;
    }

    if (in.held != pin.held || in.touch[0] || in.touch[1]) last_input = T;
    if (in.touch[0] && !pin.touch[0]) tap_burst(0, (float)in.tx[0], (float)in.ty[0]);   /* the top screen is a touchscreen too */
    if (state == ST_MENU) {
        menu_update(fdt);
        bri_poll_step();
        battery_watch();
        flows_step(fdt);
        volume_step();
        watch_card();
        if (!rg && err_pending) { err_pending = 0; err_open = 1; sfx(SFX_ERROR); rumble_double(20, 0.5f); }
        if (!rg && rescued) { rescued = 0; notice(0, "Game saved: battery at 3%"); }
        { static int announced; if (!rg && !announced && st_t > 1.2 && T - note_t > 4) { announced = 1; if (update_found()) { char m[64]; snprintf(m, sizeof m, "Update %s ready: see Options", upd_ver); notice(2, m); } } }
        if (auto_go_t > 0 && T >= auto_go_t) { auto_go_t = -1; if (sys == V_FAV && has_cont && row[V_FAV].sel == 0 && !opt_open && !gs_open) start_launch(); }
    }
    tint_step(fdt);
    float intro = intro_t >= 0 ? ease_out_cubic((float)((st_t - intro_t) / 0.55)) : 1;
    if (state == ST_LAUNCH) intro = 1;
    manage_art();

    /* the logo follows the selection */
    Game *g = cur_game(sys);
    if (g != logo_cur) { logo_prev = logo_cur; logo_cur = g; logo_t = T; }

    /* ---- top ---- */
    bg_top(top, T);
    draw_mood(top);
    float sb_y = (1 - ease_out_cubic(intro)) * -16;
    status_bar(top, sb_y, 255, T);
    if (state == ST_LAUNCH) {
        float t = (float)st_t;
        float k = smoothstep(0.05f, 0.3f, t);
        draw_logo(top, lg, 1 - k, 1 - 0.25f * k, -10 * k, cfg.top_view);   /* (the cartridge and the slot come below) */
    } else {
        float lk = (float)(T - logo_t);
        if (logo_prev && lk < 0.14f) draw_logo(top, logo_prev, 1 - lk / 0.14f, 1 - 0.1f * lk / 0.14f, lk * 30, cfg.top_view);
        float ka = clampf((lk - 0.06f) / 0.3f, 0, 1);
        float sc = 0.8f + 0.2f * ease_out_back(ka);
        if (rg && ret_t >= 0 && ret_t < 0.9) ka = 0;            /* coming back: the logo after the cartridge */
        else if (rg && ret_t >= 0.9) ka = MIN(ka, ease_out_cubic((float)(ret_t - 0.9) / 0.3f));
        /* X: the view that's going shrinks away, the new one pops in */
        float vk = (float)(T - view_t) / 0.34f;
        if (vk < 1) {
            draw_logo(top, logo_cur, ka * intro * (1 - smoothstep(0, 0.45f, vk)), sc * (1 - 0.1f * vk), vk * 6, !cfg.top_view);
            float nk = smoothstep(0.3f, 1, vk);
            draw_logo(top, logo_cur, ka * intro * nk, sc * (0.88f + 0.12f * ease_out_back(nk)), (1 - intro) * 20, cfg.top_view);
        } else {
            /* "continue" with a picture of where it was left: shown whatever the view */
            int cv = cfg.top_view || (logo_cur && logo_cur->resume && is_cont(sys, row[sys].sel) && logo_cur == cur_game(sys) && state == ST_MENU);
            draw_logo(top, logo_cur, ka * intro, sc, (1 - intro) * 20, cv);
            if (state == ST_MENU && logo_cur && cv && ka * intro > 0.5f) {
                char pt[96];
                if (!cfg.top_view) snprintf(pt, sizeof pt, "Where you left off");
                else played_text(logo_cur, pt, sizeof pt);
                text_center(top, FONT_SM, 128, 176, C_TEXT_SOFT, (int)(220 * ka * intro), pt);
            }
        }
    }
    snow(top, fdt);
    if (state == ST_MENU) { draw_bri_bar(top); draw_vol_bar(top); draw_notice(top); flows_draw_top(top); }

    /* ---- bottom ---- */
    if (state == ST_LAUNCH) {
        /* the menu stays, the row fades and drops; the jumping cartridge is drawn over it */
        Surf tmp = *bot;
        if (l_view == V_FAV) bg_favs(bot, T, tint_now(), 40, parallax()); else bg_bottom(bot, T, tint_now(), 72, parallax());
        float t = (float)st_t, k = smoothstep(0.08f, 0.5f, t);
        emblem(bot, l_view, 128, 22, l_view == V_FAV ? C_GOLD : tint_now(), 255);
        draw_row(bot, l_view, 0, 1 - k * 0.85f, 1);
        draw_tabs(bot, l_view, 0, 0);
        scroll_bar(bot, 176, vn(l_view), row[l_view].pos.x, 5, 0, fav_marks(l_view), 255);
        (void)tmp;
        launch_draw(top, bot, t);
        if (t >= L_SLAM && t - dt < L_SLAM) sound_parts_slam();
        if (t >= L_UP - 0.2f && t - dt < L_UP - 0.2f) sfx(SFX_LAUNCH);
    } else {
        draw_bottom(bot, intro);
        /* back from a game: the cartridge comes up out of the slot, drops off the bottom of the top screen and lands
         * in its place in the row */
        if (rg && ret_t >= 0) {
            ret_t += dt;
            float t = (float)ret_t, cw, ch, cd; cart_size(rg->sys, &cw, &ch, &cd);
            float sc = (rg->sys == SYS_NDS ? 92.0f : 128.0f) / cw, home = slot_open_y(1) + ch * sc * 0.42f - ch * sc * 0.5f;
            float rise = 1 - ease_in_cubic(clampf((t - 0.55f) / 0.3f, 0, 1));
            CartPose p; memset(&p, 0, sizeof p); p.sx = 128; p.alpha = 1; p.bright = 1.05f; p.scale = sc;
            if (t < 0.25f) p.sy = home;
            else if (t < 0.55f) { float k = ease_out_cubic((t - 0.25f) / 0.3f); p.sy = lerpf(home, 70, k); if (t - dt < 0.25f) sfx(SFX_WHOOSH); }
            else { float k = ease_in_cubic((t - 0.55f) / 0.28f); p.sy = lerpf(70, 192 + ch * sc, k); p.yaw = k * PI_F * 0.5f; }
            if (t < 0.85f) {
                draw_slot_back(top, rise, cw * sc + 8);
                Surf clip = *top; surf_clip(&clip, 0, 0, 256, rise > 0.001f ? (int)roundf(slot_open_y(rise)) : 192);
                cart_draw(&clip, rg, &p);
                draw_slot_front(top, rise, cw * sc + 8, t < 0.25f ? 1 : 0);
            }
            if (t >= 0.8f && t < 1.25f) {
                /* into the bottom screen, landing where it belongs */
                Item it; layout(r_view, row[r_view].sel, row[r_view].pos.x, 0, &it);
                float k = ease_out_back(clampf((t - 0.8f) / 0.42f, 0, 1));
                CartPose q = it.pose; q.sy = lerpf(-40, it.y, k);
                q.scale *= 1 + (1 - k) * 0.5f; q.yaw += (1 - k) * PI_F * 0.5f;
                if (t - dt < 1.1f && t >= 1.1f) sfx(SFX_SELECT);
                cart_draw(bot, rg, &q);
            }
            if (t >= 1.25f) { rg = 0; ret_t = -1; music_play(); }
        }
        if (state == ST_MENU && !gs_open) { draw_gear_button(bot, (int)(255 * intro * (1 - gs_anim))); draw_sun_button(bot, (int)(255 * intro * (1 - gs_anim))); }
        draw_game_settings(bot, top, gs_anim);
        draw_options(bot, opt_anim);
        flows_draw(bot, top);
        help_draw(bot, top);
        album_draw(bot, top);
        draw_error(bot, err_anim);
    }

    /* the particles, the shake, the fades */
    parts_draw(top, 0); parts_draw(bot, 1);
    if (T - shake_t < 0.3) {
        float k = 1 - (float)(T - shake_t) / 0.3f, a = shake_amp * k * k;
        int dy = (int)roundf(sinf((float)(T - shake_t) * 70) * a), dx = (int)roundf(cosf((float)(T - shake_t) * 53) * a * 0.4f);
        surf_shift(top, dx, dy, 0x34363a);
        surf_shift(bot, 0, (int)roundf(dy * 0.4f), 0xf2f2f2);
        if (T - shake_t < 0.06) rect_blend(top, 0, 0, 256, 192, 0xffffff, 70);
    }
    if (state == ST_MENU && intro_t >= 0 && !rg) {
        /* in from white */
        float k = (float)((st_t - intro_t) / 0.35);
        if (k < 1) { rect_blend(top, 0, 0, 256, 192, 0xffffff, (int)(255 * (1 - k))); rect_blend(bot, 0, 0, 256, 192, 0xffffff, (int)(255 * (1 - k))); }
    }
    if (rg && ret_t >= 0 && ret_t < 0.4) {
        /* back from the game: out of black */
        int a = (int)(255 * (1 - ease_in_out((float)(ret_t / 0.4))));
        rect_blend(top, 0, 0, 256, 192, 0x000000, a); rect_blend(bot, 0, 0, 256, 192, 0x000000, a);
    }
    if (state == ST_LAUNCH) {
        float t = (float)st_t;
        if (t > L_FADE) {
            /* to black, held a moment: the game takes the screens from black, so there is no seam */
            int a = (int)(255 * ease_in_out((t - L_FADE) / (L_END - 0.12f - L_FADE)));
            rect_blend(top, 0, 0, 256, 192, 0x000000, a); rect_blend(bot, 0, 0, 256, 192, 0x000000, a);
        }
        if (t >= L_END) { write_launch(); job_stop(&artjob); return EXIT_LAUNCH; }
    }
    if (wake_black && T - wake_t < 0.35) {
        int a = (int)(255 * (1 - ease_out_cubic((float)((T - wake_t) / 0.35))));
        rect_blend(top, 0, 0, 256, 192, 0x000000, a); rect_blend(bot, 0, 0, 256, 192, 0x000000, a);
    }
    pw_step();
    if (exit_code >= 0 && exit_code != EXIT_LAUNCH) { state_save(); job_stop(&artjob); return exit_code; }
    return -1;
}
