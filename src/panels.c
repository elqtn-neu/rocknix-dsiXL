// panels.c: see panels.h.
#define _GNU_SOURCE
#include "panels.h"
#include "plat/plat.h"
#include "widgets.h"
#include "font.h"
#include "audio.h"
#include "rumble.h"
#include <stdio.h>

static int pr(const PanelIn *in, int b) { return (in->pressed >> b) & 1; }
static int rp(const PanelIn *in, int b) { return (in->repeat >> b) & 1; }

/* ======== the list ======== */

#define PK_X 12
#define PK_W 232
#define PK_Y 4
#define PK_ROW 17
#define PK_VIS 8
static int pk_vis(const Picker *p) { return MIN(p->n, PK_VIS); }
static int pk_list_y(void) { return PK_Y + 25; }

void picker_open(Picker *p, const char *title, const char *hint) {
    snprintf(p->title, sizeof p->title, "%s", title);
    snprintf(p->hint, sizeof p->hint, "%s", hint ? hint : "");
    p->n = 0; p->sel = 0; p->scroll.x = p->scroll.v = 0; p->want = 0;
    p->open = 1; p->touch_i = -1;
    sfx(SFX_OPEN);
}

PickItem *picker_add(Picker *p, const char *label) {
    if (p->n >= ARRAY_LEN(p->it)) return &p->it[p->n - 1];
    PickItem *it = &p->it[p->n++];
    memset(it, 0, sizeof *it);
    snprintf(it->label, sizeof it->label, "%s", label);
    it->sig = -1;
    return it;
}

int picker_update(Picker *p, const PanelIn *in, float dt) {
    p->anim = clampf(p->anim + (p->open ? dt : -dt) / 0.22f, 0, 1);
    if (!p->open) return -1;
    int vis = pk_vis(p), res = -1;
    if (p->n) {
        if (rp(in, B_UP)) { p->sel = (p->sel + p->n - 1) % p->n; sfx(SFX_CURSOR); }
        if (rp(in, B_DOWN)) { p->sel = (p->sel + 1) % p->n; sfx(SFX_CURSOR); }
        if (rp(in, B_LEFT) || rp(in, B_L1)) { p->sel = MAX(0, p->sel - vis); sfx(SFX_CURSOR); }      /* a page */
        if (rp(in, B_RIGHT) || rp(in, B_R1)) { p->sel = MIN(p->n - 1, p->sel + vis); sfx(SFX_CURSOR); }
        if (pr(in, in->a)) res = p->sel;
    }
    if (pr(in, in->b) || pr(in, B_START)) res = -2;
    /* touch: a row is chosen by tapping it (a drag scrolls); outside the window: back */
    int ly = pk_list_y();
    if (in->began) {
        p->drag0 = p->scroll.x; p->touch_i = -1;
        if (in->x >= PK_X && in->x < PK_X + PK_W && in->y >= ly && in->y < ly + vis * PK_ROW) {
            int i = (int)floorf((in->y - ly) / (float)PK_ROW + p->scroll.x);
            if (i >= 0 && i < p->n) { if (i != p->sel) sfx(SFX_CURSOR); p->sel = i; p->touch_i = i; }
        } else if (!(in->x >= PK_X && in->x < PK_X + PK_W && in->y < ly + vis * PK_ROW + 22)) p->touch_i = -3;
    }
    if (in->down && in->moved && p->touch_i != -3) {
        p->scroll.x = clampf(p->drag0 - (in->y - in->y0) / (float)PK_ROW, 0, (float)MAX(0, p->n - vis)); p->scroll.v = 0;
        p->want = roundf(p->scroll.x); p->touch_i = -1;
    }
    if (in->ended && !in->moved) {
        if (p->touch_i >= 0 && p->touch_i == p->sel) res = p->sel;
        else if (p->touch_i == -3) res = -2;
    }
    if (in->ended) p->touch_i = -1;
    /* the selection in sight */
    if (!(in->down && in->moved)) {
        if (p->sel < p->want) p->want = (float)p->sel;
        if (p->sel > p->want + vis - 1) p->want = (float)(p->sel - vis + 1);
        p->want = clampf(p->want, 0, (float)MAX(0, p->n - vis));
        spring_step(&p->scroll, p->want, 500, 40, dt);
    }
    if (res >= 0) { sfx(SFX_SELECT); rumble(12, 0.35f); p->open = 0; }
    else if (res == -2) { sfx(SFX_BACK); p->open = 0; }
    return res;
}

static void signal_bars(Surf *s, int x, int y, int sig, uint32_t on, int a) {
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 2;
        rect_blend(s, x + i * 3, y + 9 - h, 2, h, i < sig ? on : 0xd0d0d0, a);
    }
}
static void padlock(Surf *s, int x, int y, uint32_t c, int a) {
    rrect_line(s, x + 1, y, 5, 6, 2.5f, 1, c, a);
    rect_blend(s, x, y + 3, 7, 5, c, a);
    pblend(s, x + 3, y + 5, 0xffffff, a);
}
static void tick(Surf *s, int x, int y, uint32_t c, int a) {
    for (int i = 0; i < 3; i++) { pblend(s, x + i, y + 3 + i, c, a); pblend(s, x + i, y + 4 + i, c, a); }
    for (int i = 0; i < 5; i++) { pblend(s, x + 3 + i, y + 5 - i, c, a); pblend(s, x + 3 + i, y + 6 - i, c, a); }
}

void picker_draw(Picker *p, Surf *bot, Surf *top) {
    float k = p->anim;
    if (k <= 0.001f) return;
    int a = (int)(255 * MIN(1.0f, k * 1.5f)), vis = MAX(1, pk_vis(p));
    rect_blend(bot, 0, 0, 256, 192, 0x000000, (int)(90 * k));
    float y = PK_Y + (1 - ease_out_back(k)) * 150;
    window(bot, PK_X, y, PK_W, 25 + vis * PK_ROW + 6, p->title, a);
    Accent ac = accent();
    Surf list = *bot; surf_clip(&list, PK_X + 2, (int)y + 24, PK_W - 4, vis * PK_ROW + 2);
    float sc = p->scroll.x;
    for (int i = MAX(0, (int)sc - 1); i < MIN(p->n, (int)sc + vis + 2); i++) {
        PickItem *it = &p->it[i];
        float ry = y + 25 + (i - sc) * PK_ROW;
        int sel = i == p->sel;
        float press = (p->touch_i == i) ? 1 : 0;
        if (sel) {
            rrect(&list, PK_X + 4, ry + press, PK_W - 16, PK_ROW - 2, 5, ac.base, a);
            rrect_vgrad(&list, PK_X + 5, ry + 1 + press, PK_W - 18, PK_ROW - 4, 4, 0xffffff, ac.pale, a);
        } else if (i) hline(&list, PK_X + 10, PK_X + PK_W - 17, (int)ry - 1, 0xe4e4e4, a);
        int rx = PK_X + PK_W - 18;                 /* the right edge of the row's contents */
        if (it->sig >= 0) { signal_bars(&list, rx - 12, (int)ry + 3, it->sig, sel ? ac.base : 0x707070, a); rx -= 16; }
        if (it->lock) { padlock(&list, rx - 8, (int)ry + 4, 0x909090, a); rx -= 11; }
        if (it->note[0]) rx = text_right(&list, FONT_SM, rx - 2, (int)ry - 1, 0x9a9a9a, a, it->note) - 4;
        int lx = PK_X + 12;
        if (it->check) { tick(&list, lx - 2, (int)ry + 4, 0x3a9a3a, a); lx += 9; }
        char t[96]; text_fit(FONT_MD, it->label, rx - lx - 4, t, sizeof t);
        text(&list, FONT_MD, lx, (int)ry - 2 + (int)press, sel ? col_mix(ac.base, 0x000000, 130) : C_TEXT, a, t);
    }
    if (!p->n) text_center(bot, FONT_MD, 128, (int)y + 30, C_TEXT_SOFT, a, "Nothing here.");
    if (p->n > vis) {
        float th = vis * PK_ROW - 2, bh = MAX(10.0f, th * vis / p->n), by = y + 25 + (th - bh) * sc / (p->n - vis);
        rrect(bot, PK_X + PK_W - 9, y + 25, 4, th, 2, 0xdedede, a);
        rrect(bot, PK_X + PK_W - 9, by, 4, bh, 2, ac.base, a);
    }
    if (top && p->hint[0]) {
        float ty = 120 + (1 - ease_out_cubic(k)) * 80;
        window(top, 20, ty, 216, 52, 0, a);
        text_wrap_center(top, FONT_SM, 128, (int)ty + 6, 196, 13, C_TEXT_SOFT, a, p->hint);
    }
}

/* ======== the keyboard ======== */

/* four rows of ten, then Shift, the symbols, the space bar, back, OK */
static const char *KROWS[2][4] = {
    { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-" },
    { "!@#$%^&*()", "_=+[]{}<>|", "~`;:\"\\/?'#", "<>(){}^$%&" },
};
#define KB_X 8
#define KB_Y 44
#define KB_KW 24
#define KB_KH 26
enum { K_SHIFT = 100, K_SYM, K_SPACE, K_BACK, K_OK };
/* the bottom row: what's at column x (0..9) */
static int bottom_key(int x) { return x < 2 ? K_SHIFT : x < 4 ? K_SYM : x < 7 ? K_SPACE : x < 8 ? K_BACK : K_OK; }
static void bottom_span(int key, int *x0, int *n) {
    switch (key) {
    case K_SHIFT: *x0 = 0; *n = 2; break;
    case K_SYM: *x0 = 2; *n = 2; break;
    case K_SPACE: *x0 = 4; *n = 3; break;
    case K_BACK: *x0 = 7; *n = 1; break;
    default: *x0 = 8; *n = 2; break;
    }
}
static char key_char(const Kbd *k, int x, int y) {
    char c = KROWS[k->sym][y][x];
    if (!k->sym && k->shift && c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (!k->sym && k->shift && y == 2 && x == 9) c = '"';
    if (!k->sym && k->shift && y == 3 && x >= 7) c = ";:_"[x - 7];
    return c;
}
static int key_at(int px, int py, int *kx, int *ky) {
    int x = (px - KB_X) / KB_KW, y = (py - KB_Y) / KB_KH;
    if (px < KB_X || x > 9 || py < KB_Y || y > 4) return 0;
    *kx = x; *ky = y; return 1;
}

void kbd_open(Kbd *k, const char *title, const char *prompt, const char *text) {
    memset(k, 0, sizeof *k);
    snprintf(k->title, sizeof k->title, "%s", title);
    snprintf(k->prompt, sizeof k->prompt, "%s", prompt);
    snprintf(k->text, sizeof k->text, "%s", text ? text : "");
    k->open = 1; k->kx = 0; k->ky = 1; k->touch_key = -1; k->key_t = -10;
    sfx(SFX_OPEN);
}

static int kbd_press(Kbd *k, int x, int y, double t) {
    k->key_last = y * 10 + x; k->key_t = t;
    rumble(6, 0.2f);
    if (y < 4) {
        size_t n = strlen(k->text);
        if (n + 1 < sizeof k->text && n < 63) { k->text[n] = key_char(k, x, y); k->text[n + 1] = 0; sfx_ex(SFX_CURSOR, 1.1f, 0.8f); }
        else sfx(SFX_BUMP);
        if (k->shift == 1) k->shift = 0;
        return -1;
    }
    switch (bottom_key(x)) {
    case K_SHIFT: k->shift = (k->shift + 1) % 3; sfx(SFX_TAB); break;
    case K_SYM: k->sym = !k->sym; sfx(SFX_TAB); break;
    case K_SPACE: { size_t n = strlen(k->text); if (n < 63) { k->text[n] = ' '; k->text[n + 1] = 0; } sfx_ex(SFX_CURSOR, 0.9f, 0.8f); break; }
    case K_BACK: { size_t n = strlen(k->text); if (n) { k->text[n - 1] = 0; sfx_ex(SFX_BACK, 1.2f, 0.7f); } else sfx(SFX_BUMP); break; }
    case K_OK: return 1;
    }
    return -1;
}

int kbd_update(Kbd *k, const PanelIn *in, float dt) {
    k->anim = clampf(k->anim + (k->open ? dt : -dt) / 0.22f, 0, 1);
    k->now = in->t;
    if (!k->open) return -1;
    int res = -1;
    if (rp(in, B_LEFT)) { if (k->ky == 4) { int x0, n; bottom_span(bottom_key(k->kx), &x0, &n); k->kx = x0 > 0 ? x0 - 1 : 9; } else k->kx = (k->kx + 9) % 10; sfx(SFX_CURSOR); }
    if (rp(in, B_RIGHT)) { if (k->ky == 4) { int x0, n; bottom_span(bottom_key(k->kx), &x0, &n); k->kx = x0 + n <= 9 ? x0 + n : 0; } else k->kx = (k->kx + 1) % 10; sfx(SFX_CURSOR); }
    if (rp(in, B_UP)) { k->ky = (k->ky + 4) % 5; sfx(SFX_CURSOR); }
    if (rp(in, B_DOWN)) { k->ky = (k->ky + 1) % 5; sfx(SFX_CURSOR); }
    if (rp(in, in->a)) res = kbd_press(k, k->kx, k->ky, in->t);
    if (rp(in, in->b)) {           /* B: back a letter; with nothing typed, away */
        size_t n = strlen(k->text);
        if (n) { k->text[n - 1] = 0; sfx_ex(SFX_BACK, 1.2f, 0.7f); } else res = 0;
    }
    if (pr(in, B_Y)) kbd_press(k, 4, 4, in->t);                /* Y: a space */
    if (pr(in, B_L1) || pr(in, B_SELECT)) kbd_press(k, 0, 4, in->t);   /* shift */
    if (pr(in, B_R1)) kbd_press(k, 2, 4, in->t);               /* the symbols */
    if (pr(in, B_START)) res = 1;
    /* touch: the key under the finger, typed when it lifts */
    int kx, ky;
    if (in->began) k->touch_key = key_at(in->x, in->y, &kx, &ky) ? ky * 10 + kx : -1;
    if (in->down && k->touch_key >= 0) {
        if (key_at(in->x, in->y, &kx, &ky)) { k->touch_key = ky * 10 + kx; k->kx = kx; k->ky = ky; } else k->touch_key = -1;
    }
    if (in->ended) {
        if (k->touch_key >= 0) res = kbd_press(k, k->touch_key % 10, k->touch_key / 10, in->t);
        k->touch_key = -1;
    }
    if (res == 1) { sfx(SFX_SELECT); rumble(15, 0.4f); k->open = 0; }
    else if (res == 0) { sfx(SFX_BACK); k->open = 0; }
    return res;
}

static void key_cap(Surf *s, float x, float y, float w, float h, const char *label, int focus, int down, int lit, int a) {
    Accent ac = accent();
    float py = y + (down ? 1 : 0);
    if (!down) rrect(s, x + 1, y + 2, w - 2, h - 2, 4, 0x000000, a * 35 / 255);
    rrect(s, x + 1, py, w - 2, h - 2, 4, focus ? ac.base : 0xb4b4b4, a);
    uint32_t t0 = down ? ac.pale : lit ? col_mix(ac.pale, 0xffffff, 120) : 0xffffff, t1 = down ? col_mix(ac.pale, ac.base, 60) : lit ? ac.pale : 0xe6e6e6;
    rrect_vgrad(s, x + 2, py + 1, w - 4, h - 4, 3, t0, t1, a);
    if (focus) rrect_line(s, x + 2, py + 1, w - 4, h - 4, 3, 1, col_mix(ac.base, 0xffffff, 100), a);
    text_center(s, FONT_MD, (int)(x + w / 2), (int)(py + h / 2) - 11, focus ? col_mix(ac.base, 0x000000, 120) : C_TEXT, a, label);
}

void kbd_draw(Kbd *k, Surf *bot, Surf *top) {
    float an = k->anim;
    if (an <= 0.001f) return;
    int a = (int)(255 * MIN(1.0f, an * 1.5f));
    double t = k->key_t;
    /* the bottom screen: the keys on a soft panel */
    float dy = (1 - ease_out_cubic(an)) * 150;
    rect_blend(bot, 0, 0, 256, 192, 0x000000, (int)(90 * an));
    rrect(bot, 2, 34 + dy, 252, 160, 8, 0xa8a8a8, a);
    rrect_vgrad(bot, 3, 35 + dy, 250, 158, 7, 0xf6f6f6, 0xe2e2e2, a);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 10; x++) {
        char lab[4] = { key_char(k, x, y), 0 };
        if ((uint8_t)lab[0] >= 0x80) lab[0] = '?';
        int id = y * 10 + x;
        int down = k->touch_key == id || (k->key_last == id && k->now - t < 0.09);
        key_cap(bot, KB_X + x * KB_KW, KB_Y + y * KB_KH + dy, KB_KW, KB_KH, lab, k->kx == x && k->ky == y, down, 0, a);
    }
    static const char *names[] = { "Shift", "#+=", "Space", "", "OK" };
    for (int i = 0; i < 5; i++) {
        int key = K_SHIFT + i, x0, n; bottom_span(key, &x0, &n);
        int focus = k->ky == 4 && bottom_key(k->kx) == key;
        int down = k->touch_key >= 40 && bottom_key(k->touch_key - 40) == key;
        int lit = (key == K_SHIFT && k->shift) || (key == K_SYM && k->sym) || key == K_OK;
        const char *lab = names[i];
        if (key == K_SHIFT && k->shift == 2) lab = "SHIFT";
        if (key == K_SYM && k->sym) lab = "abc";
        key_cap(bot, KB_X + x0 * KB_KW, KB_Y + 4 * KB_KH + dy, n * KB_KW, KB_KH, lab, focus, down, lit, a);
        if (key == K_BACK) {
            /* the back key's arrow, drawn: a little tag pointing left with an x in it */
            float cx = KB_X + x0 * KB_KW + KB_KW / 2.0f, cy = KB_Y + 4 * KB_KH + dy + KB_KH / 2.0f - 1 + (down ? 1 : 0);
            uint32_t c = focus ? col_mix(accent().base, 0x000000, 120) : C_TEXT;
            tri_fill(bot, cx - 8, cy, cx - 4, cy - 4.5f, cx - 4, cy + 4.5f, c, a);
            rect_blend(bot, (int)(cx - 4), (int)(cy - 4), 11, 9, c, a);
            for (int i = 0; i < 5; i++) { pblend(bot, (int)cx - 1 + i, (int)cy - 2 + i, 0xffffff, a); pblend(bot, (int)cx + 3 - i, (int)cy - 2 + i, 0xffffff, a); }
        }
    }
    /* the title above the keys, on a band of the accent */
    Accent ac = accent();
    float by = (1 - ease_out_cubic(an)) * -34;
    rect_vgrad(bot, 0, (int)by, 256, 30, ac.light, ac.base, a);
    hline(bot, 0, 255, (int)by + 30, ac.dark, a);
    text_center(bot, FONT_BD, 129, (int)by + 6, ac.dark, a * 160 / 255, k->title);
    text_center(bot, FONT_BD, 128, (int)by + 5, 0xffffff, a, k->title);
    /* the top screen: what's typed, in a big field, its cursor blinking */
    if (!top) return;
    float ty = 52 + (1 - ease_out_cubic(an)) * -80;
    rect_blend(top, 0, 14, 256, 178, 0xf6f6f6, (int)(160 * an));
    window(top, 12, ty, 232, 74, 0, a);
    text_wrap_center(top, FONT_SM, 128, (int)ty + 5, 216, 13, C_TEXT_SOFT, a, k->prompt);
    rrect(top, 22, ty + 34, 212, 28, 6, 0xb4b4b4, a);
    rrect(top, 23, ty + 35, 210, 26, 5, 0xffffff, a);
    char shown[96]; const char *txt = k->text;
    int tw = text_w(FONT_MD, txt);
    if (tw > 196) {     /* the end of a long one */
        const char *p = txt; while (*p && text_w(FONT_MD, p) > 190) p++;
        snprintf(shown, sizeof shown, "\xe2\x80\xa6%s", p); txt = shown; tw = text_w(FONT_MD, txt);
    }
    int ex = text(top, FONT_MD, 30, (int)ty + 36, 0x202020, a, txt);
    if (fmod(k->now, 1.0) < 0.6) rect_blend(top, ex + 1, (int)ty + 40, 2, 16, accent().base, a);   /* the cursor */
    (void)tw;
}

/* ======== working on it ======== */

void busy_draw(Surf *s, const char *line1, const char *line2, double t, float anim, float progress) {
    if (anim <= 0.001f) return;
    int a = (int)(255 * MIN(1.0f, anim * 1.5f));
    rect_blend(s, 0, 0, 256, 192, 0x000000, (int)(90 * anim));
    int h = line2 && line2[0] ? 74 : 60;
    if (progress >= 0) h += 14;
    float y = 96 - h / 2 + (1 - ease_out_back(anim)) * 100;
    window(s, 24, y, 208, h, 0, a);
    /* eight dots going round, the leading one darkest (the DSi's "please wait") */
    Accent ac = accent();
    for (int i = 0; i < 8; i++) {
        float an = i * PI_F / 4;
        int lead = ((int)(t * 10)) % 8, age = (lead - i + 8) % 8;
        disc(s, 128 + cosf(an) * 7, y + 15 + sinf(an) * 7, 1.9f, col_mix(ac.base, 0xffffff, MIN(220, age * 32)), a);
    }
    text_center(s, FONT_MD, 128, (int)y + 25, C_TEXT, a, line1);
    if (line2 && line2[0]) { char b[96]; text_fit(FONT_SM, line2, 190, b, sizeof b); text_center(s, FONT_SM, 128, (int)y + 43, C_TEXT_SOFT, a, b); }
    if (progress >= 0) {
        float py = y + h - 15;
        rrect(s, 40, py, 176, 8, 4, 0xc8c8c8, a);
        rrect(s, 41, py + 1, 174, 6, 3, 0xffffff, a);
        if (progress > 0) rrect_vgrad(s, 41, py + 1, MAX(6.0f, 174 * clampf(progress, 0, 1)), 6, 3, ac.light, ac.base, a);
    }
}
