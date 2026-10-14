// ui.c: the menu. Everything moves on springs and eases, at 60 frames a second, drawn at the DS's 256x192:
//   boot     white screens, the logo and its chime on the top screen, the health and safety screen on the bottom,
//            "Touch the Touch Screen to continue."; then everything slides in, as the DSi menu does
//   menu     top: the status bar and the selected game's logo, floating. bottom: the cartridges in a row, the
//            selected one popped out in its frame and in colour, the others greyer; NDS / GBA tabs (L / R); the
//            DSi's scroll bar along the bottom
//   launch   the cartridge jumps off the bottom screen, comes up into the top one, hangs, and slams down into the
//            slot at the bottom of the top screen (where a DSi's card slot is); then white, and the game
//   return   back from a game: the cartridge comes out of the slot and drops back into its place
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
#include <stdio.h>
#include <stdlib.h>

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

typedef struct { Spring pos; int sel; } Row;
static Row row[SYS_COUNT];
static int sys;                         /* the system shown */
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

/* launch */
static Game *lg; static int l_sys; static int launched_file_written;
/* the return from a game */
static Game *rg; static double ret_t = -1;

/* particles: dust from the slam, sparkles, confetti */
enum { P_DUST, P_SPARK, P_CONFETTI };
typedef struct { float x, y, vx, vy, life, max, size; uint32_t c; int scr, kind; float ph; } Part;
static Part parts[192];

/* per cartridge: its jelly wobble and its hop (indexed like lib[s].g) */
typedef struct { Spring wob; } Fx;
static Fx *fx[SYS_COUNT]; static int fx_n[SYS_COUNT];
static double last_input = 0;           /* idle moments start 15 s after this */
static float tint_rgb[3]; static int tint_init;   /* the bottom screen's colour, eased toward the selected game's */
static int confetti_done;
typedef struct { float x, y, v, ph, sz; } Flake;
static Flake flakes[30];
static double shake_t = -10; static float shake_amp;

static Img boot_logo;

/* ---- helpers ---- */

static Game *cur_game(int s) { return lib[s].n ? &lib[s].g[CLAMP(row[s].sel, 0, lib[s].n - 1)] : 0; }

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

static void fx_alloc(void) {
    for (int s = 0; s < SYS_COUNT; s++) {
        free(fx[s]); fx[s] = calloc((size_t)MAX(1, lib[s].n), sizeof(Fx)); fx_n[s] = fx[s] ? lib[s].n : 0;
    }
}
static Fx *fx_of(int s, int i) { static Fx none = { { 0, 0 } }; return (i >= 0 && i < fx_n[s]) ? &fx[s][i] : &none; }

static void select_game(int s, int i, int with_sound) {
    if (!lib[s].n) return;
    int n = CLAMP(i, 0, lib[s].n - 1);
    if (n == row[s].sel) return;
    row[s].sel = n;
    sel_t = T;
    notch_pending = 1;
    a_hold = 0;
    /* the tick rises along the list and falls back, as the DSi's does; passing ones (a glide, a flick) are quieter */
    float pitch = lib[s].n > 1 ? 0.86f + 0.34f * n / (lib[s].n - 1) : 1.0f;
    if (with_sound == 1) sfx_pitch(SFX_CURSOR, pitch);
    else if (with_sound == 2) sfx_ex(SFX_CURSOR, pitch * 1.06f, 0.45f);
    snprintf(cfg.last[s], sizeof cfg.last[s], "%s", lib[s].g[n].file);
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
    if (!lib[sys].n) { sfx(SFX_BUMP); return; }
    int n = r->sel + d;
    if (n < 0 || n >= lib[sys].n) { end_bump(d, 1.0f); return; }
    select_game(sys, n, 1);
}

static void switch_sys(int to) {
    if (to == sys || (sw_t >= 0 && T - sw_t < 0.25)) return;
    sw_from = sys; sw_dir = to > sys ? 1 : -1; sys = to; sw_t = T;
    cfg.sys = sys;
    sfx(SFX_TAB);
    rumble(15, 0.4f);
    glide_dir = 0; coast = 0; a_hold = 0;
    frame_x.x = 128; frame_x.v = 0;
    sel_t = T;
}

/* art is kept only for the games near the selection (the handheld has 1 GB) */
static void manage_art(void) {
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) {
        Game *g = &lib[s].g[i];
        int near = s == sys && abs(i - row[s].sel) <= 6;
        if (s != sys && abs(i - row[s].sel) <= 1) near = 1;
        if (g == lg || g == rg || g == logo_cur || g == logo_prev) near = 1;
        if (!near && (g->cart || g->label_state || g->logo_state)) { cart_free(g); game_free_art(g); }
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

typedef struct { float spacing, sel_w, oth_w, cy; } RowGeo;
static RowGeo geo(int s) {
    return s == SYS_NDS ? (RowGeo){ 54, 60, 36, 106 } : (RowGeo){ 70, 82, 48, 106 };
}

typedef struct { float x, y, w, f, d; CartPose pose; } Item;

static void layout(int s, int i, float pos, float dx, Item *it) {
    RowGeo G = geo(s);
    float cw, ch, cd; cart_size(s, &cw, &ch, &cd);
    float d = i - pos, ad = fabsf(d);
    float f = 1 - smoothstep(0, 1, ad);                   /* 1 when it is the one in the middle */
    it->d = d; it->f = f;
    it->x = 128 + d * G.spacing + (ad < 1 ? d : SGN(d)) * (G.sel_w - G.oth_w) * 0.5f + dx;
    float pop = 1 + 0.06f * clampf(frame_pop.x, -1, 1) * f;
    it->w = lerpf(G.oth_w, G.sel_w, f) * pop;
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
    /* idle moments: the selected one turns all the way round, or a wave runs along the row */
    double idle = T - last_input - 15;
    if (state == ST_MENU && !opt_open && idle > 0) {
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
static Img *game_photo(Game *g) {
    if (!g) return 0;
    if (!g->shot_state) game_load_art(g);
    if (g->shot_state < 1) return 0;
    if (g->photo.px) return &g->photo;
    /* a title screen alone (it is the logo) is shown bigger than a screenshot with a logo over it */
    int pw = g->shot_state == 2 ? 196 : 160, ph = MAX(1, g->shot.h * pw / g->shot.w), B = 5;
    Img small; img_resize_area(&g->shot, &small, pw, ph);
    Img framed;
    if (!small.px || img_alloc(&framed, pw + 2 * B, ph + 2 * B)) { img_free(&small); return 0; }
    Surf fs = { framed.w, framed.h, framed.w, framed.px, 0, 0, framed.w, framed.h };
    rect_vgrad(&fs, 0, 0, framed.w, framed.h, 0xffffff, 0xf2f2f2, 255);
    rectf(&fs, 0, 0, framed.w, 1, 0xdedede); rectf(&fs, 0, framed.h - 1, framed.w, 1, 0xc8c8c8);
    rectf(&fs, 0, 0, 1, framed.h, 0xd6d6d6); rectf(&fs, framed.w - 1, 0, 1, framed.h, 0xd0d0d0);
    blit(&fs, &small, B, B, 255);
    rect_blend(&fs, B, B, pw, 1, 0x000000, 40); rect_blend(&fs, B, B, 1, ph, 0x000000, 30);   /* the print's edge */
    for (int i = 0; i < framed.w * framed.h; i++) framed.px[i] |= 0xff000000u;
    uint32_t h = hash_str(g->file);
    float ang = ((h & 1) ? 1 : -1) * (1.6f + (h >> 1) % 10 * 0.12f) * PI_F / 180;
    img_rotate(&framed, ang, &g->photo);
    img_free(&small); img_free(&framed);
    return g->photo.px ? &g->photo : 0;
}

static void draw_logo(Surf *s, Game *g, float alpha, float scale, float dy, int view) {
    if (!g || alpha <= 0.01f) return;
    Img *ph = view ? game_photo(g) : 0;     /* the photo only in the photo view (X) */
    Img *lo = ph && g->shot_state == 2 ? &g->shot : game_logo(g);
    if (!lo) return;
    float bob = sinf((float)T * 1.6f) * 3.0f;
    float cx = 128, cy = 96 + dy + bob;
    if (ph) {
        /* the photo, floating gently on its own beat; the logo over its lower edge, floating on another */
        float pb = sinf((float)T * 1.15f + 1.0f) * 1.5f;
        float pw = ph->w * scale, phh = ph->h * scale;
        int alone = g->shot_state == 2;               /* the title screen: the logo is in it */
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
    Library *L = &lib[sy];
    Row *r = &row[sy];
    if (!L->n) {
        int a = (int)(alpha * 255);
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
    for (int i = (int)floorf(pos) - 4; i <= (int)ceilf(pos) + 4 && n < 16; i++) if (i >= 0 && i < L->n) idx[n++] = i;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (fabsf(idx[a] - pos) < fabsf(idx[b] - pos)) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    Item it[16];
    float rdx = sy == sys ? clampf(rubber.x, -24, 24) : 0;
    for (int k = 0; k < n; k++) layout(sy, idx[k], pos, dx - rdx, &it[k]);
    /* the floor shadows */
    for (int k = 0; k < n; k++) {
        float lift = (it[k].y - (geo(sy).cy - 8)) ;
        ellipse_soft(s, it[k].x, 153, it[k].w * 0.46f, 3.5f, 0x000000, (int)(alpha * (50 + lift * 1.5f)));
    }
    /* the frame behind the one in the middle */
    RowGeo G = geo(sy);
    float cw, ch, cd; cart_size(sy, &cw, &ch, &cd);
    float fw = G.sel_w + 16, fh = G.sel_w * ch / cw + 34;
    float near = 1 - MIN(1.0f, fabsf(pos - roundf(pos)) * 2.2f);
    float fp = 1 + 0.05f * clampf(frame_pop.x, -1, 1);
    float fa = hide ? alpha * alpha * alpha : alpha;            /* launching: the frame goes first */
    /* the frame: where it has travelled to, stretched along its way while it moves */
    float fx = sy == sys ? frame_x.x : 128, fv = sy == sys ? fabsf(frame_x.v) : 0;
    float stretch = MIN(18.0f, fv * 0.045f);
    float shine = fmodf((float)T + 2.0f, 4.5f) / 0.9f;           /* a light passes over it every 4.5 s */
    (void)near;
    sel_frame(s, fx + dx, G.cy + 9, fw * fp + stretch, fh * fp - stretch * 0.25f, "START", tint_now(), shine, (int)(fa * 255));
    for (int k = 0; k < n; k++) {
        Game *g = &L->g[idx[k]];
        if ((hide && g == lg) || (rg && g == rg)) continue;   /* it is flying: drawn by the animation */
        it[k].pose.alpha = alpha * (1 - smoothstep(2.6f, 3.6f, fabsf(it[k].d)));
        if (sy == sys && idx[k] != r->sel) it[k].pose.alpha *= smoothstep(0, 0.3f, (float)(T - reorder_t));
        cart_draw(s, g, &it[k].pose);
        /* a favourite: a gold star on its top right corner (popping on, or shrinking away when it stops being one) */
        float pk = g == fav_g ? (float)(T - fav_t) / 0.45f : 2;
        if (g->fav || (pk < 1 && !fav_on)) {
            float cw2, ch2, cd2; cart_size(sy, &cw2, &ch2, &cd2);
            float h = it[k].w * ch2 / cw2, r0 = 3.2f + 2.3f * it[k].f;
            float rr = r0;
            if (pk < 1) rr = fav_on ? r0 * (0.2f + 0.8f * ease_out_elastic(pk)) : r0 * (1 - ease_in_cubic(pk));
            star(s, roundf(it[k].x + it[k].w * 0.44f), roundf(it[k].y - h * 0.5f + 1), rr, C_STAR, (int)(it[k].pose.alpha * 255));
        }
    }
}

static const uint8_t *fav_marks(int s) {
    if (lib[s].n > marks_n) { uint8_t *m = realloc(marks, (size_t)lib[s].n); if (!m) return 0; marks = m; marks_n = lib[s].n; }
    for (int i = 0; i < lib[s].n; i++) marks[i] = (uint8_t)lib[s].g[i].fav;
    return marks;
}

static float parallax(void) { return lib[sys].n ? -row[sys].pos.x * geo(sys).spacing * 0.35f : 0; }

static void draw_bottom(Surf *s, float intro) {
    bg_bottom(s, T, tint_now(), 72, parallax());
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
        char t[160]; text_fit(FONT_MD, g->title, g->fav ? 222 : 236, t, sizeof t);
        int tw = text_w(FONT_MD, t), tx = 128 - (tw + (g->fav ? 13 : 0)) / 2, ty = 44 + (int)((1 - k) * 4);
        if (g->fav) { star(s, tx + 5, ty + 11, 4.2f, C_STAR, (int)(255 * k * intro)); tx += 13; }
        text(s, FONT_MD, tx, ty, 0x505050, (int)(255 * k * intro), t);
    }

    /* the cartridges: switching systems slides one row out and the other in */
    float dx_in = (1 - ease_out_cubic(intro)) * 220;
    if (sw < 1) {
        float k = ease_in_out(sw);
        draw_row(s, sw_from, -sw_dir * k * 280, 1 - k * 0.5f, 0);
        draw_row(s, sys, sw_dir * (1 - k) * 280, 0.5f + 0.5f * k, 0);
    } else draw_row(s, sys, dx_in, intro, state == ST_LAUNCH);

    /* the tabs */
    float pl = ((tch.down && tch.zone == 1 && tch.x0 < 128) || (in.held & BIT(B_L1))) ? 1 : 0;
    float pr = ((tch.down && tch.zone == 1 && tch.x0 >= 128) || (in.held & BIT(B_R1))) ? 1 : 0;
    float ty = (1 - ease_out_cubic(intro)) * -22;
    tab(s, 0, -6, (int)ty, 82, "NDS", sys == SYS_NDS, pl, 255);
    tab(s, 1, 256 - 76, (int)ty, 82, "GBA", sys == SYS_GBA, pr, 255);

    /* the scroll bar */
    float sby = 176 + (1 - ease_out_cubic(intro)) * 20;
    scroll_bar(s, (int)sby, lib[sys].n, lib[sys].n ? row[sys].pos.x : 0, 5, (T - sb_press_t < 0.12) ? sb_press : 0, fav_marks(sys), 255);
}

/* ---- the options window ---- */

enum { OPT_COLOR, OPT_CLOCK, OPT_SFX, OPT_MUSIC, OPT_RUMBLE, OPT_RESCAN, OPT_ES, OPT_POWER, OPT_COUNT };

static void opt_label(int i, char *b, int n) {
    switch (i) {
    case OPT_COLOR: snprintf(b, (size_t)n, "Color: %s", fav_color_name(cfg.color)); break;
    case OPT_CLOCK: snprintf(b, (size_t)n, "Clock: %s", cfg.clock24 ? "24-hour" : "12-hour"); break;
    case OPT_SFX: snprintf(b, (size_t)n, "Sound effects: %s", cfg.sfx ? "On" : "Off"); break;
    case OPT_MUSIC: snprintf(b, (size_t)n, "Music: %s", cfg.music ? "On" : "Off"); break;
    case OPT_RUMBLE: snprintf(b, (size_t)n, "Vibration: %s", cfg.rumble ? "On" : "Off"); break;
    case OPT_RESCAN: snprintf(b, (size_t)n, "Look for new games"); break;
    case OPT_ES: snprintf(b, (size_t)n, "Exit to EmulationStation"); break;
    case OPT_POWER: snprintf(b, (size_t)n, "Power off"); break;
    }
}

static void opt_act(int i, int dir) {
    switch (i) {
    case OPT_COLOR: cfg.color = (cfg.color + (dir ? dir : 1) + 16) % 16; sfx(SFX_CURSOR); config_save(); break;
    case OPT_CLOCK: cfg.clock24 = !cfg.clock24; sfx(SFX_SELECT); config_save(); break;
    case OPT_SFX: cfg.sfx = !cfg.sfx; sfx(SFX_SELECT); config_save(); break;
    case OPT_MUSIC: cfg.music = !cfg.music; if (cfg.music) music_play(); else music_stop(); sfx(SFX_SELECT); config_save(); break;
    case OPT_RUMBLE: cfg.rumble = !cfg.rumble; sfx(SFX_SELECT); config_save(); rumble(30, 0.6f); break;
    case OPT_RESCAN:
        sfx(SFX_SELECT);
        for (int s = 0; s < SYS_COUNT; s++) for (int k = 0; k < lib[s].n; k++) cart_free(&lib[s].g[k]);
        logo_cur = logo_prev = 0;
        library_scan();
        fx_alloc();
        for (int s = 0; s < SYS_COUNT; s++) { row[s].sel = CLAMP(row[s].sel, 0, MAX(0, lib[s].n - 1)); row[s].pos.x = row[s].sel; }
        opt_open = 0; opt_t = T;
        break;
    case OPT_ES: sfx(SFX_SELECT); exit_code = EXIT_TO_ES; break;
    case OPT_POWER: sfx(SFX_SELECT); exit_code = EXIT_POWEROFF; break;
    }
}

#define OPT_X 30
#define OPT_Y 6
#define OPT_W 196
#define OPT_ROW 18

static void draw_options(Surf *s, float k) {
    if (k <= 0.001f) return;
    rect_blend(s, 0, 0, 256, 192, 0x000000, (int)(90 * k));
    float y = OPT_Y + (1 - ease_out_back(k)) * 140;
    int a = (int)(255 * MIN(1.0f, k * 1.5f));
    window(s, OPT_X, y, OPT_W, 30 + OPT_COUNT * OPT_ROW + 6, "Options", a);
    for (int i = 0; i < OPT_COUNT; i++) {
        char b[64]; opt_label(i, b, sizeof b);
        float by = y + 26 + i * OPT_ROW;
        float press = (tch.down && tch.zone == 3 && opt_sel == i) ? 1 : 0;
        button(s, OPT_X + 8, by, OPT_W - 16, OPT_ROW - 3, b, i == opt_sel, press, a);
        if (i == OPT_COLOR) {
            rrect(s, OPT_X + OPT_W - 26, by + 3, 10, 10, 3, fav_color(cfg.color), a);
            rrect_line(s, OPT_X + OPT_W - 26, by + 3, 10, 10, 3, 1, 0x505050, a);
        }
    }
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

static void draw_boot(Surf *top, Surf *bot, float t) {
    /* from black to white, the logo and the chime, the text, then the wait for a touch */
    int white = (int)(255 * smoothstep(0, 0.45f, t));
    fill(top, 0x000000); fill(bot, 0x000000);
    rect_blend(top, 0, 0, 256, 192, 0xffffff, white);
    rect_blend(bot, 0, 0, 256, 192, 0xffffff, white);
    float la = smoothstep(0.55f, 1.05f, t);
    if (boot_logo.px) blit(top, &boot_logo, 128 - boot_logo.w / 2, 77 - boot_logo.h / 2, (int)(255 * la));
    else text_center(top, FONT_XL, 128, 62, 0x000000, (int)(255 * la), "NINTENDO DSi");
    float ta = smoothstep(0.9f, 1.4f, t);
    int a = (int)(255 * ta);
    /* the health and safety screen (the layout of the DSi's) */
    int hw = text_w(FONT_HS, "WARNING \xe2\x80\x93 HEALTH AND SAFETY") + 17;
    warn_icon(bot, 128 - hw / 2.0f, 16, a);
    text(bot, FONT_HS, 128 - hw / 2 + 17, 12, 0x000000, a, "WARNING \xe2\x80\x93 HEALTH AND SAFETY");
    static const char *body[] = { "BEFORE PLAYING, READ THE HEALTH", "AND SAFETY PRECAUTIONS BOOKLET", "FOR IMPORTANT INFORMATION", "ABOUT YOUR HEALTH AND SAFETY." };
    for (int i = 0; i < 4; i++) squeezed(bot, FONT_MD, 128, 39 + i * 19, 214, 0x101010, a, body[i]);
    squeezed(bot, FONT_SM, 128, 117, 236, 0x101010, a, "TO GET AN EXTRA COPY FOR YOUR REGION, GO ONLINE AT");
    text_center(bot, FONT_MD, 128, 131, 0x2b9fd0, a, "www.nintendo.com/healthsafety/");
    float pulse = 0.55f + 0.45f * cosf((float)(t - 1.6f) * 3.3f);
    if (t > 1.6f) text_center(bot, FONT_MD, 128, 155, 0x505050, (int)(a * pulse), "Touch the Touch Screen to continue.");
}

/* ---- init ---- */

void ui_init(const UiStart *st) {
    char p[400];
    snprintf(p, sizeof p, "%s/assets/boot_logo.png", cfg.home);
    img_load(p, &boot_logo);
    sys = cfg.sys;
    for (int s = 0; s < SYS_COUNT; s++) {
        row[s].sel = 0;
        for (int i = 0; i < lib[s].n; i++) if (!strcmp(lib[s].g[i].file, cfg.last[s])) row[s].sel = i;
        row[s].pos.x = row[s].sel;
    }
    if (!lib[sys].n && lib[!sys].n) sys = !sys;
    state = (st->no_boot || !cfg.boot_splash || st->ret_file[0]) ? ST_MENU : ST_BOOT;
    st_t = 0;
    if (state == ST_MENU) intro_t = 0;
    if (st->ret_file[0]) {
        int s = CLAMP(st->ret_sys, 0, SYS_COUNT - 1);
        for (int i = 0; i < lib[s].n; i++) if (!strcmp(lib[s].g[i].file, st->ret_file)) { sys = s; row[s].sel = i; row[s].pos.x = i; rg = &lib[s].g[i]; }
        if (rg) { ret_t = 0; intro_t = 0.25; }
    }
    if (state == ST_MENU && !rg) music_play();
    fx_alloc();
    last_input = 0;
    /* the snow of December: flakes spread down the top screen to begin with */
    for (int i = 0; i < ARRAY_LEN(flakes); i++) {
        uint32_t h = hash3(i, 5, 9);
        flakes[i] = (Flake){ (float)(h % 256), (float)((h >> 8) % 192), 7 + (float)((h >> 16) % 9), (float)((h >> 4) % 628) / 100, (h >> 20) % 3 == 0 ? 2.0f : 1.0f };
    }
    /* a birthday: confetti as the menu appears (not when coming back from a game) */
    if (state == ST_MENU && !rg && is_birthday()) { confetti(); confetti_done = 1; }
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
    lg = g; l_sys = sys;
    state = ST_LAUNCH; st_t = 0;
    sfx(SFX_SELECT);
    rumble(25, 0.6f);
    music_fade(0, 1.2f);
    snprintf(cfg.last[sys], sizeof cfg.last[sys], "%s", g->file);
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
    if (down && !tch.down) {
        last_input = T;
        tap_burst(1, (float)x, (float)y);
        tch.down = 1; tch.moved = 0; tch.x0 = tch.x = x; tch.y0 = tch.y = y; tch.t0 = T;
        tch.pos0 = row[sys].pos.x; tch.vel = 0; tch.lastx = (float)x; tch.lastt = T;
        if (opt_open) tch.zone = 3;
        else if (y < 18) tch.zone = 1;
        else if (y >= 176) {
            tch.zone = 2;
            int z, i = scroll_bar_hit(x, 176, lib[sys].n, 5, &z);
            if (z) { sb_press = z; sb_press_t = T; move(z); }
            else if (i >= 0) { select_game(sys, i, 1); sfx(SFX_SCROLL); }
        } else {
            tch.zone = 0;
            coast = 0; glide_dir = 0;
            RowGeo G = geo(sys);
            if (lib[sys].n && abs(x - 128) < G.sel_w * 0.6f + 6 && y > 60 && y < 160) tch.zone = 5;   /* on the selected one */
        }
        if (tch.zone == 3) {
            int i = (int)((y - (OPT_Y + 26)) / OPT_ROW);
            if (x >= OPT_X && x < OPT_X + OPT_W && y >= OPT_Y + 26 && i >= 0 && i < OPT_COUNT) { if (i != opt_sel) sfx(SFX_CURSOR); opt_sel = i; }
            else tch.zone = 4;          /* outside the window: closes it */
        }
        return;
    }
    if (down && tch.down) {
        if (abs(x - tch.x0) > 4 || abs(y - tch.y0) > 4) tch.moved = 1;
        if (tch.zone == 5 && tch.moved) tch.zone = 0;   /* it was a drag after all */
        if (tch.zone == 0 && tch.moved && lib[sys].n) {
            RowGeo G = geo(sys);
            float p = tch.pos0 - (x - tch.x0) / G.spacing;
            /* past an end it gives, but less and less */
            if (p < 0) p = -0.5f * (1 - expf(p * 1.5f));
            else if (p > lib[sys].n - 1) p = lib[sys].n - 1 + 0.5f * (1 - expf(-(p - (lib[sys].n - 1)) * 1.5f));
            row[sys].pos.x = p;
            row[sys].pos.v = tch.vel;            /* (not stepped while held: what the tilt reads) */
            last_input = T;
            if (((int)(T * 60)) % 3 == 0) spawn_k(P_SPARK, 1, (float)x, (float)y, 0, -8, 0.3f, -1.5f, fav_color(cfg.color));
            double dtt = T - tch.lastt;
            if (dtt > 0.001) { tch.vel = lerpf(tch.vel, (float)(-(x - tch.lastx) / G.spacing / dtt), 0.5f); tch.lastx = (float)x; tch.lastt = T; }
            int n = CLAMP((int)roundf(row[sys].pos.x), 0, lib[sys].n - 1);
            if (n != row[sys].sel) { select_game(sys, n, 1); rumble(7, 0.25f); }   /* a ratchet under the finger */
        }
        if (tch.zone == 2 && lib[sys].n) {
            int z, i = scroll_bar_hit(x, 176, lib[sys].n, 5, &z);
            if (!z && i >= 0 && i != row[sys].sel) { select_game(sys, i, 0); sfx(SFX_SCROLL); }
        }
        tch.x = x; tch.y = y;
        return;
    }
    if (!down && tch.down) {
        tch.down = 0;
        if (tch.zone == 5 && lib[sys].n) { fx_of(sys, row[sys].sel)->wob.v += 9; start_launch(); }
        else if (tch.zone == 0 && lib[sys].n) {
            if (tch.moved) {
                /* a flick carries on by itself (coasting), a slow release settles where it is */
                float v = clampf(tch.vel, -28, 28);
                if (fabsf(v) > 2.5f && T - tch.lastt < 0.08) { coast = 1; row[sys].pos.v = v; }
                else { row[sys].pos.v = 0; select_game(sys, CLAMP((int)roundf(row[sys].pos.x), 0, lib[sys].n - 1), 0); notch_pending = 1; }
            } else {
                /* a tap: on the selected cartridge starts it, on another selects it */
                RowGeo G = geo(sys);
                if (abs(tch.x - 128) < G.sel_w * 0.6f + 6 && tch.y > 60 && tch.y < 160) { fx_of(sys, row[sys].sel)->wob.v += 9; start_launch(); }
                else {
                    float best = 1e9f; int bi = -1;
                    for (int i = MAX(0, row[sys].sel - 4); i <= MIN(lib[sys].n - 1, row[sys].sel + 4); i++) {
                        Item it; layout(sys, i, row[sys].pos.x, 0, &it);
                        float d = fabsf(it.x - tch.x);
                        if (d < best && d < it.w * 0.6f + 4) { best = d; bi = i; }
                    }
                    if (bi >= 0) { fx_of(sys, bi)->wob.v += 11; select_game(sys, bi, 1); }
                }
            }
        } else if (tch.zone == 1 && !tch.moved) switch_sys(tch.x0 < 128 ? SYS_NDS : SYS_GBA);
        else if (tch.zone == 3 && !tch.moved) {
            int i = (int)((tch.y - (OPT_Y + 26)) / OPT_ROW);
            if (i == opt_sel) opt_act(i, 1);
        } else if (tch.zone == 4) { opt_open = 0; opt_t = T; sfx(SFX_BACK); }
    }
}

/* Y: a favourite, or not. The star pops on (with a burst of gold), the list sorts the favourites to the front, and the
 * row keeps this game where it is while its neighbours change around it */
static void toggle_fav(void) {
    Row *r = &row[sys];
    int old = r->sel, on = !lib[sys].g[old].fav;
    int n = library_set_fav(sys, old, on);
    Game *g = &lib[sys].g[n];
    r->sel = n; r->pos.x += (float)(n - old);
    snprintf(cfg.last[sys], sizeof cfg.last[sys], "%s", g->file);
    logo_cur = g; logo_prev = 0;                       /* (the games moved in memory) */
    for (int i = 0; i < fx_n[sys]; i++) fx[sys][i].wob = (Spring){ 0, 0 };
    fx_of(sys, n)->wob.v += on ? -10 : 8;
    fav_g = g; fav_on = on; fav_t = T;
    if (n != old) reorder_t = T;
    glide_dir = 0; coast = 0; a_hold = 0;
    if (on) {
        sfx_pitch(SFX_SELECT, 1.25f); rumble(20, 0.5f);
        RowGeo G = geo(sys);
        for (int i = 0; i < 10; i++) {
            float a = i * (2 * PI_F / 10), sp = 50 + (hash3(i, (int)(T * 60), 5) % 40);
            spawn_k(P_SPARK, 1, 128 + G.sel_w * 0.42f, G.cy - 26, cosf(a) * sp, sinf(a) * sp, 0.5f, -(1.5f + (i & 1)), C_STAR);
        }
    } else { sfx_pitch(SFX_BACK, 1.1f); rumble(10, 0.3f); }
}

static void menu_update(float dt) {
    if (opt_open) {
        if (repeat(B_UP)) { opt_sel = (opt_sel + OPT_COUNT - 1) % OPT_COUNT; sfx(SFX_CURSOR); }
        if (repeat(B_DOWN)) { opt_sel = (opt_sel + 1) % OPT_COUNT; sfx(SFX_CURSOR); }
        if (repeat(B_LEFT) && opt_sel == OPT_COLOR) opt_act(OPT_COLOR, -1);
        if (repeat(B_RIGHT) && opt_sel == OPT_COLOR) opt_act(OPT_COLOR, 1);
        if (pressed(btn_a())) opt_act(opt_sel, 1);
        if (pressed(btn_b()) || pressed(B_START)) { opt_open = 0; opt_t = T; sfx(SFX_BACK); }
    } else if (intro_t < 0 || T - intro_t > 0.25) {
        /* left / right: a step at once; held, a glide that picks up speed; let go, it settles on the next one */
        for (int k = 0; k < 2; k++) {
            int b = k ? B_RIGHT : B_LEFT, dir = k ? 1 : -1;
            if (pressed(b)) { coast = 0; glide_dir = 0; held_since[k] = T; move(dir); }
            else if ((in.held & BIT(b)) && !glide_dir && T - held_since[k] > 0.32 && lib[sys].n) { glide_dir = dir; glide_speed = 7; }
        }
        if (glide_dir && lib[sys].n) {
            int b = glide_dir > 0 ? B_RIGHT : B_LEFT;
            Row *r = &row[sys];
            if (!(in.held & BIT(b))) {
                int n = glide_dir > 0 ? (int)ceilf(r->pos.x - 0.05f) : (int)floorf(r->pos.x + 0.05f);
                select_game(sys, CLAMP(n, 0, lib[sys].n - 1), 0);
                glide_dir = 0;
            } else {
                glide_speed = MIN(22.0f, glide_speed + 18 * dt);
                float np = r->pos.x + glide_dir * glide_speed * dt;
                if (np < 0 || np > lib[sys].n - 1) {
                    if ((glide_dir < 0 && r->pos.x > 0) || (glide_dir > 0 && r->pos.x < lib[sys].n - 1)) end_bump(glide_dir, 0.6f);
                    np = clampf(np, 0, (float)(lib[sys].n - 1));
                    spring_step(&rubber, glide_dir * 8.0f, 300, 22, dt);      /* held against the end: stays stretched */
                }
                r->pos.x = np; r->pos.v = glide_dir * glide_speed;
                int n = CLAMP((int)roundf(np), 0, lib[sys].n - 1);
                if (n != r->sel) select_game(sys, n, 2);
            }
        }
        if (repeat(B_UP)) move(row[sys].sel > 0 ? -MIN(5, row[sys].sel) : -1);           /* five at a time */
        if (repeat(B_DOWN)) { int n = lib[sys].n - 1 - row[sys].sel; move(n > 0 ? MIN(5, n) : 1); }
        if (pressed(B_L1)) { if (sys != SYS_NDS) switch_sys(SYS_NDS); else { bump_dir = -1; bump_t = T; sfx(SFX_BUMP); } }
        if (pressed(B_R1)) { if (sys != SYS_GBA) switch_sys(SYS_GBA); else { bump_dir = 1; bump_t = T; sfx(SFX_BUMP); } }
        if (pressed(B_SELECT)) switch_sys(!sys);
        /* A: the cartridge sinks in while it's held; letting go starts it */
        if (pressed(btn_a()) && lib[sys].n) { a_hold = 1; rumble(10, 0.3f); }
        if (a_hold && !(in.held & BIT(btn_a()))) { a_hold = 0; start_launch(); }
        if (pressed(B_START)) { opt_open = 1; opt_t = T; opt_sel = 0; sfx(SFX_OPEN); }
        if (pressed(B_X)) {           /* the top screen: the logo, or the photo */
            cfg.top_view = !cfg.top_view; view_t = T; state_save();
            sfx_pitch(SFX_TAB, cfg.top_view ? 1.12f : 0.92f); rumble(12, 0.3f);
        }
        if (pressed(B_Y) && lib[sys].n) toggle_fav();
    }
    touch_update();
    /* a flick coasts on, slowing, ticking past each cartridge, then settles on one */
    if (coast && lib[sys].n) {
        Row *r = &row[sys];
        r->pos.x += r->pos.v * dt;
        r->pos.v *= expf(-3.0f * dt);
        if (r->pos.x < 0 || r->pos.x > lib[sys].n - 1) {
            int d = r->pos.x < 0 ? -1 : 1;
            r->pos.x = clampf(r->pos.x, 0, (float)(lib[sys].n - 1));
            end_bump(d, clampf(fabsf(r->pos.v) / 12, 0.4f, 1.2f));
            r->pos.v = 0; coast = 0;
        } else if (fabsf(r->pos.v) < 2.0f) coast = 0;
        int n = CLAMP((int)roundf(r->pos.x), 0, lib[sys].n - 1);
        if (n != r->sel) { select_game(sys, n, 2); rumble(6, 0.2f); }
    }
    /* the row follows the selection on a quick spring: there in about a fifth of a second, a hint of overshoot */
    int dragging = tch.down && tch.zone == 0 && tch.moved;
    for (int s = 0; s < SYS_COUNT; s++)
        if (!((dragging || glide_dir || coast) && s == sys)) spring_step(&row[s].pos, (float)row[s].sel, 430, 34, dt);
    if (!glide_dir) spring_step(&rubber, 0, 320, 17, dt);
    spring_step(&frame_pop, 0, 300, 12, dt);
    /* the frame goes after the selected cartridge (which the row is bringing to the middle): it lunges toward it and
     * they settle together */
    if (lib[sys].n) {
        Item it; layout(sys, row[sys].sel, row[sys].pos.x, 0, &it);
        spring_step(&frame_x, it.x - clampf(rubber.x, -24, 24) * 0.25f, 520, 30, dt);
    }
    spring_step(&sink, (a_hold || (tch.down && tch.zone == 5)) ? 1.0f : 0.0f, 700, 34, dt);
    /* the detent: the selection has locked into the frame */
    if (notch_pending && !glide_dir && !coast && !dragging && lib[sys].n && fabsf(row[sys].pos.x - row[sys].sel) < 0.06f) {
        notch_pending = 0;
        frame_pop.v -= 3.2f;
        sfx_ex(SFX_NOTCH, 1.0f, 0.8f);
        rumble(12, 0.35f);
    }
    /* the jelly settles; the selection's hop when the row arrives */
    for (int s = 0; s < SYS_COUNT; s++) for (int i = MAX(0, row[s].sel - 5); i <= MIN(fx_n[s] - 1, row[s].sel + 5); i++) spring_step(&fx[s][i].wob, 0, 430, 7.5f, dt);
    opt_anim = clampf(opt_anim + (opt_open ? dt : -dt) / 0.22f, 0, 1);
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
#define L_WHITE  1.45f
#define L_END    1.85f

static void launch_draw(Surf *top, Surf *bot, float t) {
    float cw, ch, cd; cart_size(l_sys, &cw, &ch, &cd);
    RowGeo G = geo(l_sys);
    /* bottom: the cartridge squashes, then jumps off the top edge; the others drop away */
    Item base; layout(l_sys, row[l_sys].sel, row[l_sys].pos.x, 0, &base);
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

int ui_frame(const Input *input, double dt, Surf *top, Surf *bot) {
    pin = in; in = *input;
    T += dt; st_t += dt;
    if (in.quit) return EXIT_QUIT;
    float fdt = (float)MIN(dt, 0.05);
    rumble_tick();
    parts_step(fdt);

    if (state == ST_BOOT) {
        draw_boot(top, bot, (float)st_t);
        static double cont_t = -1;
        if (st_t > 0.6 && st_t - dt <= 0.6) sfx(SFX_BOOT);
        if (cont_t < 0 && st_t > 1.0 && (any_pressed() || (cfg.boot_wait > 0 && st_t > cfg.boot_wait + 1.0))) { cont_t = st_t; sfx(SFX_CONTINUE); }
        if (cont_t >= 0) {
            float k = (float)(st_t - cont_t);
            rect_blend(bot, 0, 0, 256, 192, 0xffffff, (int)(255 * smoothstep(0, 0.3f, k)));
            rect_blend(top, 0, 0, 256, 192, 0xffffff, (int)(255 * smoothstep(0.1f, 0.4f, k)));
            if (k > 0.5f) {
                state = ST_MENU; st_t = 0; intro_t = 0; music_play(); sfx(SFX_WHOOSH); last_input = T;
                if (is_birthday() && !confetti_done) { confetti(); confetti_done = 1; }
            }
        }
        pin = in;
        return exit_code;
    }

    if (in.held != pin.held || in.touch[0] || in.touch[1]) last_input = T;
    if (in.touch[0] && !pin.touch[0]) tap_burst(0, (float)in.tx[0], (float)in.ty[0]);   /* the top screen is a touchscreen too */
    if (state == ST_MENU) menu_update(fdt);
    tint_step(fdt);
    float intro = intro_t >= 0 ? ease_out_cubic((float)((st_t - intro_t) / 0.55)) : 1;
    if (state == ST_LAUNCH) intro = 1;
    manage_art();

    /* the logo follows the selection */
    Game *g = cur_game(sys);
    if (g != logo_cur) { logo_prev = logo_cur; logo_cur = g; logo_t = T; }

    /* ---- top ---- */
    bg_top(top, T);
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
        } else draw_logo(top, logo_cur, ka * intro, sc, (1 - intro) * 20, cfg.top_view);
    }
    snow(top, fdt);

    /* ---- bottom ---- */
    if (state == ST_LAUNCH) {
        /* the menu stays, the row fades and drops; the jumping cartridge is drawn over it */
        Surf tmp = *bot;
        bg_bottom(bot, T, tint_now(), 72, parallax());
        float t = (float)st_t, k = smoothstep(0.08f, 0.5f, t);
        emblem(bot, l_sys, 128, 22, tint_now(), 255);
        draw_row(bot, l_sys, 0, 1 - k * 0.85f, 1);
        tab(bot, 0, -6, 0, 82, "NDS", l_sys == SYS_NDS, 0, 255);
        tab(bot, 1, 256 - 76, 0, 82, "GBA", l_sys == SYS_GBA, 0, 255);
        scroll_bar(bot, 176, lib[l_sys].n, row[l_sys].pos.x, 5, 0, fav_marks(l_sys), 255);
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
                Item it; layout(rg->sys, row[rg->sys].sel, row[rg->sys].pos.x, 0, &it);
                float k = ease_out_back(clampf((t - 0.8f) / 0.42f, 0, 1));
                CartPose q = it.pose; q.sy = lerpf(-40, it.y, k);
                q.scale *= 1 + (1 - k) * 0.5f; q.yaw += (1 - k) * PI_F * 0.5f;
                if (t - dt < 1.1f && t >= 1.1f) sfx(SFX_SELECT);
                cart_draw(bot, rg, &q);
            }
            if (t >= 1.25f) { rg = 0; ret_t = -1; music_play(); }
        }
        draw_options(bot, opt_anim);
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
    if (rg && ret_t >= 0 && ret_t < 0.35) {
        int a = (int)(255 * (1 - ret_t / 0.35));
        rect_blend(top, 0, 0, 256, 192, 0xffffff, a); rect_blend(bot, 0, 0, 256, 192, 0xffffff, a);
    }
    if (state == ST_LAUNCH) {
        float t = (float)st_t;
        if (t > L_WHITE) {
            int a = (int)(255 * smoothstep(L_WHITE, L_END - 0.1f, t));
            rect_blend(top, 0, 0, 256, 192, 0xffffff, a); rect_blend(bot, 0, 0, 256, 192, 0xffffff, a);
        }
        if (t >= L_END) { write_launch(); return EXIT_LAUNCH; }
    }
    if (exit_code >= 0 && exit_code != EXIT_LAUNCH) { state_save(); return exit_code; }
    return -1;
}
