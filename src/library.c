// library.c: scans the ROM folders and reads the ROM headers. A DS ROM carries its own title and a 32x32 icon (the
// one the DSi menu shows); a GBA ROM a 12-character title and a game code. Art is found by file name in the media
// folder, then where EmulationStation's scraper puts it (<roms>/images/<name>-marquee.png etc.).
#define _GNU_SOURCE
#include "library.h"
#include "config.h"
#include "img.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <strings.h>

Library lib[SYS_COUNT];

const char *sys_name(int sys) { return sys == SYS_NDS ? "Nintendo DS" : "Game Boy Advance"; }
const char *sys_short(int sys) { return sys == SYS_NDS ? "NDS" : "GBA"; }

static const char *const EXT[SYS_COUNT][6] = {
    { ".nds", ".dsi", ".ids", ".srl", ".zip", ".7z" },
    { ".gba", ".agb", ".bin", ".mb", ".zip", ".7z" },
};

static int has_ext(int sys, const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; i < 6; i++) if (!strcasecmp(dot, EXT[sys][i])) return 1;
    return 0;
}

/* "Mario Kart DS (USA, Australia) (En,Fr,De,Es,It)" -> "Mario Kart DS"; "0123 - Name (U)" -> "Name" */
static void clean_name(const char *base, char *out, int sz) {
    const char *s = base;
    const char *p = s; while (isdigit((uint8_t)*p)) p++;
    if (p > s && p - s <= 5 && (!strncmp(p, " - ", 3))) s = p + 3;
    int n = 0;
    for (; *s && n < sz - 1; s++) {
        if (*s == '(' || *s == '[') break;
        out[n++] = *s == '_' ? ' ' : *s;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    /* "Legend of Zelda, The - ..." -> "The Legend of Zelda - ..." */
    char *c = strstr(out, ", The");
    if (c && (c[5] == 0 || c[5] == ' ' || c[5] == ':')) {
        char tmp[256]; *c = 0;
        snprintf(tmp, sizeof tmp, "The %s%s", out, c + 5);
        snprintf(out, (size_t)sz, "%s", tmp);
    }
    if (!out[0]) snprintf(out, (size_t)sz, "%s", base);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* UTF-16LE line of a DS banner title into UTF-8 */
static void u16_line(const uint8_t *p, int maxc, int line, char *out, int sz) {
    int n = 0, cur = 0;
    for (int i = 0; i < maxc; i++) {
        uint32_t c = p[i * 2] | p[i * 2 + 1] << 8;
        if (!c) break;
        if (c == '\n') { if (cur == line) break; cur++; continue; }
        if (cur != line) continue;
        if (c < 0x80) { if (n < sz - 1) out[n++] = (char)c; }
        else if (c < 0x800) { if (n < sz - 2) { out[n++] = (char)(0xc0 | c >> 6); out[n++] = (char)(0x80 | (c & 63)); } }
        else if (n < sz - 3) { out[n++] = (char)(0xe0 | c >> 12); out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63)); }
    }
    out[n] = 0;
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
}

static void read_nds(Game *g) {
    FILE *f = fopen(g->path, "rb");
    if (!f) return;
    uint8_t h[0x200];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); return; }
    for (int i = 0; i < 4; i++) g->code[i] = isprint(h[0x0c + i]) ? (char)h[0x0c + i] : 0;
    uint32_t boff = rd32(h + 0x68);
    uint8_t b[0x840];
    if (boff && boff < 0x40000000u && !fseek(f, (long)boff, SEEK_SET) && fread(b, 1, sizeof b, f) == sizeof b) {
        /* the icon: 4x4 tiles of 8x8, 4 bits a pixel; colour 0 is transparent */
        if (!img_alloc(&g->icon, 32, 32)) {
            uint32_t pal[16];
            for (int i = 0; i < 16; i++) {
                int c = b[0x220 + i * 2] | b[0x221 + i * 2] << 8;
                int r = (c & 31) << 3, gg = ((c >> 5) & 31) << 3, bb = ((c >> 10) & 31) << 3;
                pal[i] = ARGB(i ? 255 : 0, r | r >> 5, gg | gg >> 5, bb | bb >> 5);
            }
            for (int t = 0; t < 16; t++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                int v = b[0x20 + t * 32 + y * 4 + x / 2];
                v = x & 1 ? v >> 4 : v & 15;
                g->icon.px[((t / 4) * 8 + y) * 32 + (t % 4) * 8 + x] = pal[v];
            }
        }
        /* the English title (the second of the six), first line, and its last line (the publisher) */
        char t0[128];
        u16_line(b + 0x340, 128, 0, t0, sizeof t0);
        if (t0[0]) {
            char l1[128], l2[128];
            u16_line(b + 0x340, 128, 1, l1, sizeof l1);
            u16_line(b + 0x340, 128, 2, l2, sizeof l2);
            /* three lines: title, subtitle, publisher; two: title, publisher */
            if (l2[0]) { snprintf(g->title, sizeof g->title, "%s %s", t0, l1); snprintf(g->sub, sizeof g->sub, "%s", l2); }
            else { snprintf(g->title, sizeof g->title, "%s", t0); snprintf(g->sub, sizeof g->sub, "%s", l1); }
        }
    }
    fclose(f);
}

static void read_gba(Game *g) {
    FILE *f = fopen(g->path, "rb");
    if (!f) return;
    uint8_t h[0xc0];
    if (fread(h, 1, sizeof h, f) == sizeof h && h[0xb2] == 0x96) {     /* the fixed byte of a real header */
        for (int i = 0; i < 4; i++) g->code[i] = isprint(h[0xac + i]) ? (char)h[0xac + i] : 0;
    }
    fclose(f);
}

static int cmp_game(const void *a, const void *b) { return strcasecmp(((const Game *)a)->title, ((const Game *)b)->title); }

static void scan_dir(int sys, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !has_ext(sys, e->d_name)) continue;
        Library *L = &lib[sys];
        Game *ng = realloc(L->g, sizeof(Game) * (size_t)(L->n + 1));
        if (!ng) break;
        L->g = ng;
        Game *g = &L->g[L->n++];
        memset(g, 0, sizeof *g);
        g->sys = sys;
        snprintf(g->file, sizeof g->file, "%s", e->d_name);
        snprintf(g->path, sizeof g->path, "%s/%s", dir, e->d_name);
        snprintf(g->base, sizeof g->base, "%s", e->d_name);
        char *dot = strrchr(g->base, '.'); if (dot) *dot = 0;
        const char *ext = strrchr(e->d_name, '.');
        if (sys == SYS_NDS && ext && strcasecmp(ext, ".zip") && strcasecmp(ext, ".7z")) read_nds(g);
        if (sys == SYS_GBA && ext && strcasecmp(ext, ".zip") && strcasecmp(ext, ".7z")) read_gba(g);
        /* the file name is usually the better title (No-Intro names), the banner the fallback for codes like "0001" */
        char clean[128]; clean_name(g->base, clean, sizeof clean);
        int digits = 1; for (const char *c = clean; *c; c++) if (!isdigit((uint8_t)*c) && *c != ' ') digits = 0;
        if (!g->title[0] || !digits) snprintf(g->title, sizeof g->title, "%s", clean);
    }
    closedir(d);
}

void library_scan(void) {
    for (int s = 0; s < SYS_COUNT; s++) {
        for (int i = 0; i < lib[s].n; i++) { game_free_art(&lib[s].g[i]); img_free(&lib[s].g[i].icon); }
        free(lib[s].g); lib[s].g = 0; lib[s].n = 0;
        scan_dir(s, s == SYS_NDS ? cfg.dir_nds : cfg.dir_gba);
        if (lib[s].n > 1) qsort(lib[s].g, (size_t)lib[s].n, sizeof(Game), cmp_game);
    }
}

static int try_ext(const char *stem, char *out, int sz) {
    static const char *const ex[] = { ".png", ".jpg", ".jpeg", ".PNG", ".JPG" };
    for (int i = 0; i < 5; i++) { snprintf(out, (size_t)sz, "%s%s", stem, ex[i]); if (file_exists(out)) return 1; }
    return 0;
}

int library_media(const Game *g, const char *kind, char *out, int sz) {
    char stem[900];
    const char *sd = g->sys == SYS_NDS ? "nds" : "gba";
    const char *romdir = g->sys == SYS_NDS ? cfg.dir_nds : cfg.dir_gba;
    snprintf(stem, sizeof stem, "%s/%s/%s/%s", cfg.media, sd, kind, g->base);
    if (try_ext(stem, out, sz)) return 1;
    /* EmulationStation's scraped media, as ROCKNIX keeps it beside the ROMs */
    static const struct { const char *kind, *suffix; } es[] = {
        { "logo", "-marquee" }, { "logo", "-wheel" }, { "box", "-thumb" }, { "box", "-boxart" }, { "box", "-image" },
    };
    for (int i = 0; i < ARRAY_LEN(es); i++) {
        if (strcmp(es[i].kind, kind)) continue;
        snprintf(stem, sizeof stem, "%s/images/%s%s", romdir, g->base, es[i].suffix);
        if (try_ext(stem, out, sz)) return 1;
        snprintf(stem, sizeof stem, "%s/media/%s%s", romdir, g->base, es[i].suffix);
        if (try_ext(stem, out, sz)) return 1;
    }
    return 0;
}

/* the sizes art is kept at: the largest the menu ever draws it (the cartridge in the launch animation, at the
 * 2x the 3D is drawn at; the logo across the top screen) */
#define LABEL_W 132
#define LABEL_H 128
#define LOGO_W 224
#define LOGO_H 120

void game_load_art(Game *g) {
    char p[900];
    if (!g->label_state) {
        g->label_state = -1;
        /* a label scan wins; else the box art, cropped to the label's shape (the art on a DS card is the box's) */
        if (library_media(g, "label", p, sizeof p) && !img_load_fit(p, LABEL_W, LABEL_H, FIT_COVER, cfg.cache, &g->label)) g->label_state = 1;
        else if (library_media(g, "box", p, sizeof p) && !img_load_fit(p, LABEL_W, LABEL_H * 2, FIT_CONTAIN, cfg.cache, &g->label)) g->label_state = 2;
    }
    if (!g->logo_state) {
        g->logo_state = -1;
        if (library_media(g, "logo", p, sizeof p) && !img_load_fit(p, LOGO_W * 2, LOGO_H * 2, FIT_CONTAIN_DOWN, cfg.cache, &g->logo)) {
            img_trim(&g->logo);
            /* now to its drawn size (trimming first: the margins would have taken some of it) */
            if (g->logo.w > LOGO_W || g->logo.h > LOGO_H) {
                float k = MIN((float)LOGO_W / g->logo.w, (float)LOGO_H / g->logo.h);
                Img t; img_resize_area(&g->logo, &t, MAX(1, (int)(g->logo.w * k)), MAX(1, (int)(g->logo.h * k)));
                img_free(&g->logo); g->logo = t;
            }
            g->logo_state = 1;
        }
    }
}

void game_free_art(Game *g) {
    img_free(&g->label); img_free(&g->logo);
    g->label_state = g->logo_state = 0;
}
