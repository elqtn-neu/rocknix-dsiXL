// font.c: draws the baked fonts (font_data.c).
#include "font.h"
#include <stdlib.h>
#include <stdio.h>

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

static const Glyph *find(int font, uint32_t cp) {
    const FontData *f = &font_data[font];
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glyphs[m].cp == cp) return &f->glyphs[m];
        if (f->glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    if (cp == 0x2019 || cp == 0x2018) return find(font, '\'');
    if (cp == 0x201c || cp == 0x201d) return find(font, '"');
    return cp == '?' ? 0 : find(font, '?');
}

int text_h(int font) { return font_data[font].ascent + font_data[font].descent; }

int text_w(int font, const char *s) {
    int w = 0;
    while (*s) { const Glyph *g = find(font, utf8_next(&s)); if (g) w += g->adv; }
    return w;
}

static const uint8_t lv[4] = { 0, 96, 176, 255 };    /* the 2-bit levels, slightly bold (the DSi's text is dark) */

int text(Surf *s, int font, int x, int y, uint32_t c, int alpha, const char *str) {
    const FontData *f = &font_data[font];
    while (*str) {
        const Glyph *g = find(font, utf8_next(&str));
        if (!g) continue;
        const uint8_t *b = f->bits + g->off;
        for (int j = 0; j < g->h; j++) for (int i = 0; i < g->w; i++) {
            int k = j * g->w + i, v = (b[k >> 2] >> ((k & 3) * 2)) & 3;
            if (v) pblend(s, x + g->ox + i, y + g->oy + j, c, lv[v] * alpha / 255);
        }
        x += g->adv;
    }
    return x;
}
void text_center(Surf *s, int font, int cx, int y, uint32_t c, int alpha, const char *str) { text(s, font, cx - text_w(font, str) / 2, y, c, alpha, str); }
int text_right(Surf *s, int font, int rx, int y, uint32_t c, int alpha, const char *str) { return text(s, font, rx - text_w(font, str), y, c, alpha, str); }

int text_wrap_center(Surf *s, int font, int cx, int y, int w, int line_h, uint32_t c, int alpha, const char *str) {
    char line[256], word[128]; int n = 0, used = 0;
    line[0] = 0;
    const char *p = str;
    for (;;) {
        /* the next word (with the newline as its own break) */
        int wl = 0; while (*p == ' ') p++;
        int nl = 0;
        while (*p && *p != ' ' && *p != '\n' && wl < (int)sizeof word - 1) word[wl++] = *p++;
        word[wl] = 0;
        if (*p == '\n') { nl = 1; p++; }
        char test[384];
        snprintf(test, sizeof test, "%s%s%s", line, n ? " " : "", word);
        if (n && text_w(font, test) > w) {
            if (s) text_center(s, font, cx, y + used, c, alpha, line);
            used += line_h; snprintf(line, sizeof line, "%s", word); n = wl ? 1 : 0;
        } else if (wl) { snprintf(line, sizeof line, "%s", test); n++; }
        if (nl || !*p) {
            if (n) { if (s) text_center(s, font, cx, y + used, c, alpha, line); used += line_h; }
            line[0] = 0; n = 0;
            if (!*p) break;
        }
    }
    return used;
}

void text_fit(int font, const char *str, int max_w, char *out, int out_sz) {
    snprintf(out, (size_t)out_sz, "%s", str);
    if (text_w(font, out) <= max_w) return;
    int ell = text_w(font, "\xe2\x80\xa6");
    int len = (int)strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && ((uint8_t)out[len] & 0xc0) == 0x80) len--;     /* whole characters */
        out[len] = 0;
        while (len > 0 && out[len - 1] == ' ') out[--len] = 0;
        if (text_w(font, out) + ell <= max_w) break;
    }
    if (len + 4 < out_sz) strcat(out, "\xe2\x80\xa6");
}

int text_to_img(int font, const char *str, Img *out, uint32_t c) {
    int w = text_w(font, str) + 2, h = text_h(font) + 2;
    if (img_alloc(out, w, h)) return -1;
    const FontData *f = &font_data[font];
    int x = 1;
    while (*str) {
        const Glyph *g = find(font, utf8_next(&str));
        if (!g) continue;
        const uint8_t *b = f->bits + g->off;
        for (int j = 0; j < g->h; j++) for (int i = 0; i < g->w; i++) {
            int k = j * g->w + i, v = (b[k >> 2] >> ((k & 3) * 2)) & 3;
            int px = x + g->ox + i, py = 1 + g->oy + j;
            if (v && px >= 0 && py >= 0 && px < w && py < h) out->px[(size_t)py * w + px] = (c & 0xffffff) | ((uint32_t)lv[v] << 24);
        }
        x += g->adv;
    }
    return 0;
}
