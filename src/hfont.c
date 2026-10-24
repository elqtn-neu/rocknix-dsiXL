// hfont.c: see hfont.h.
#include "hfont.h"

uint32_t utf8_next(const char **s);
uint32_t utf8_next(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xc0 && c < 0xe0 && (p[0] & 0xc0) == 0x80) { c = ((c & 0x1f) << 6) | (p[0] & 0x3f); p += 1; }
    else if (c >= 0xe0 && c < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) { c = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f); p += 2; }
    else if (c >= 0xf0 && (p[0] & 0xc0) == 0x80) { c = '?'; while ((*p & 0xc0) == 0x80) p++; }
    else if (c >= 0x80 && c < 0xc0) c = '?';
    *s = (const char *)p;
    return c;
}
#include <string.h>
#include <stdio.h>

static const HGlyph *glyph(const HFontData *f, uint32_t cp) {
    int lo = 0, hi = f->count - 1;                       /* (generated in code point order, the extras after) */
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glyphs[m].cp == cp) return &f->glyphs[m];
        if (f->glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].cp == cp) return &f->glyphs[i];
    return 0;
}

int ht_h(int f) { return hfont_data[f].ascent + hfont_data[f].descent; }

int ht_w(int f, const char *s, int track) {
    const HFontData *F = &hfont_data[f]; int w = 0, n = 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        const HGlyph *g = glyph(F, cp); if (!g) g = glyph(F, '?');
        if (g) { w += g->adv; n++; }
    }
    return w + (n > 1 ? (n - 1) * track : 0);
}

static void draw_glyph(Surf *s, const HFontData *F, const HGlyph *g, int x, int y, uint32_t c, int alpha) {
    if (!g->w || !g->h) return;
    int cr = c >> 16 & 255, cg = c >> 8 & 255, cb = c & 255;
    int x0 = x + g->ox, y0 = y + g->oy, rowb = (g->w + 1) / 2;
    const uint8_t *bits = F->bits + g->off;
    for (int j = 0; j < g->h; j++) {
        int yy = y0 + j;
        if (yy < s->cy0 || yy >= s->cy1) continue;
        uint32_t *row = s->px + (size_t)yy * s->pitch;
        const uint8_t *b = bits + (size_t)j * rowb;
        for (int i = 0; i < g->w; i++) {
            int xx = x0 + i;
            if (xx < s->cx0 || xx >= s->cx1) continue;
            int v = (b[i >> 1] >> ((i & 1) * 4)) & 15;
            if (!v) continue;
            int a = v * 17 * alpha / 255;
            if (a >= 255) { row[xx] = (row[xx] & 0xff000000u) | (c & 0xffffff); continue; }
            uint32_t d = row[xx];
            int dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
            dr += (cr - dr) * a / 255; dg += (cg - dg) * a / 255; db += (cb - db) * a / 255;
            row[xx] = (d & 0xff000000u) | (uint32_t)dr << 16 | (uint32_t)dg << 8 | (uint32_t)db;
        }
    }
}

int ht(Surf *s, int f, int x, int y, uint32_t c, int alpha, int track, const char *str) {
    const HFontData *F = &hfont_data[f];
    if (alpha <= 0) return x + ht_w(f, str, track);
    while (*str) {
        uint32_t cp = utf8_next(&str);
        const HGlyph *g = glyph(F, cp); if (!g) g = glyph(F, '?');
        if (!g) continue;
        draw_glyph(s, F, g, x, y, c, alpha > 255 ? 255 : alpha);
        x += g->adv + track;
    }
    return x - track;
}
void ht_center(Surf *s, int f, int cx, int y, uint32_t c, int alpha, int track, const char *str) {
    ht(s, f, cx - ht_w(f, str, track) / 2, y, c, alpha, track, str);
}
int ht_right(Surf *s, int f, int rx, int y, uint32_t c, int alpha, int track, const char *str) {
    int w = ht_w(f, str, track); ht(s, f, rx - w, y, c, alpha, track, str); return rx - w;
}

void ht_fit(int f, const char *str, int max_w, char *out, int out_sz) {
    snprintf(out, (size_t)out_sz, "%s", str);
    if (ht_w(f, out, 0) <= max_w) return;
    int ell = ht_w(f, "\xe2\x80\xa6", 0);
    int n = (int)strlen(out);
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xc0) == 0x80) n--;     /* (whole characters) */
        out[n] = 0;
        while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
        if (ht_w(f, out, 0) + ell <= max_w) break;
    }
    if ((int)strlen(out) + 4 < out_sz) strcat(out, "\xe2\x80\xa6");
}

/* the next line of str that fits w (whole words), into line; returns where the rest starts */
static const char *next_line(int f, const char *str, int w, char *line, int sz) {
    while (*str == ' ') str++;
    int best = 0; const char *p = str;
    for (;;) {
        const char *e = p; while (*e && *e != ' ') e++;
        int n = (int)(e - str); if (n >= sz) n = sz - 1;
        char t[512]; snprintf(t, sizeof t, "%.*s", n, str);
        if (ht_w(f, t, 0) > w && best) break;
        best = n;
        if (!*e) { p = e; break; }
        p = e + 1;
    }
    snprintf(line, (size_t)sz, "%.*s", best, str);
    return str + best;
}
int ht_wrap_lines(int f, int w, int max_lines, const char *str) {
    char line[512]; int n = 0;
    while (*str && n < max_lines) { str = next_line(f, str, w, line, sizeof line); n++; while (*str == ' ') str++; }
    return n;
}
int ht_wrap(Surf *s, int f, int x, int y, int w, int line_h, int max_lines, uint32_t c, int alpha, const char *str) {
    char line[512]; int n = 0;
    while (*str && n < max_lines) {
        const char *rest = next_line(f, str, w, line, sizeof line);
        while (*rest == ' ') rest++;
        if (n == max_lines - 1 && *rest) { char t[600]; snprintf(t, sizeof t, "%s %s", line, rest); ht_fit(f, t, w, line, sizeof line); }
        ht(s, f, x, y + n * line_h, c, alpha, 0, line);
        str = rest; n++;
    }
    return n;
}
