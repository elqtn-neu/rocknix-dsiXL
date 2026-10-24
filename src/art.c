// art.c: see art.h. One loader thread, the newest wish first (the selection moves on; what it wanted a moment ago
// matters less); at most 48 games' pictures kept, the least recently drawn let go.
#define _GNU_SOURCE
#include "art.h"
#include "img.h"
#include "hgfx.h"
#include "config.h"
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define KEEP 48
#define QMAX 64
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
/* (the loader works on a copy of the game: a rescan may free the library meanwhile; gen tells old results apart) */
typedef struct { Game copy; Game *g; unsigned gen; } Want;
static Want queue[QMAX]; static int qn;
static unsigned gen;
typedef struct { Game *g; unsigned gen; Img im; int ok; uint32_t vivid, deep; } Done;
static Done done[QMAX]; static int dn;
static unsigned clock_;
static int started;

/* colours from a title, for a game with no picture: a hue of its own, always the same */
static void title_colors(const Game *g, uint32_t *vivid, uint32_t *deep) {
    unsigned h = 2166136261u;
    for (const char *p = g->title; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    float hue = (h % 360) / 60.0f, s = 0.55f, v = 0.85f;
    int i = (int)hue; float f = hue - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, gg, b;
    switch (i % 6) { case 0: r = v; gg = t; b = p; break; case 1: r = q; gg = v; b = p; break; case 2: r = p; gg = v; b = t; break;
                     case 3: r = p; gg = q; b = v; break; case 4: r = t; gg = p; b = v; break; default: r = v; gg = p; b = q; }
    *vivid = RGB((int)(r * 255), (int)(gg * 255), (int)(b * 255));
    *deep = h_mix(*vivid, 0x000000, 0.6f);
}

/* ---- save states' pictures: a DraStic .dss is a 0x44-byte header ("DraStic-SaveState") and a zlib stream whose
 * first 196,608 bytes are both DS screens, 256x384 RGB565 (SuperDrastic's menu reads them the same way) ---- */
typedef struct { char path[600]; long long mt; int state; Img pic; unsigned use; } Dss;   /* state 0 wanted, 1 there, -1 none */
#define NDSS 16
static Dss dss[NDSS]; static int dss_want = -1;
typedef struct { const uint8_t *next_in; unsigned avail_in; unsigned long total_in; uint8_t *next_out; unsigned avail_out;
                 unsigned long total_out; const char *msg; void *state, *zalloc, *zfree, *opaque; int data_type;
                 unsigned long adler, reserved; } zs_t;
static int (*z_init)(zs_t *, const char *, int), (*z_inflate)(zs_t *, int), (*z_end)(zs_t *);
static int read_dss(const char *path, Img *out) {
    if (!z_init) {
        void *h = dlopen("libz.so.1", RTLD_NOW | RTLD_LOCAL); if (!h) return -1;
        *(void **)&z_init = dlsym(h, "inflateInit_"); *(void **)&z_inflate = dlsym(h, "inflate"); *(void **)&z_end = dlsym(h, "inflateEnd");
        if (!z_init || !z_inflate || !z_end) { z_init = 0; return -1; }
    }
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    enum { BYTES = 256 * 384 * 2 };
    uint8_t hdr[0x44], in[32768]; uint16_t *px = malloc(BYTES); int ok = 0;
    if (px && fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "DraStic-SaveState", 17)) {
        zs_t z; memset(&z, 0, sizeof z);
        if (z_init(&z, "1.2.11", (int)sizeof z) == 0) {
            z.next_out = (uint8_t *)px; z.avail_out = BYTES; int r = 0;
            while (z.avail_out && r == 0) {
                if (!z.avail_in) { size_t n = fread(in, 1, sizeof in, f); if (!n) break; z.next_in = in; z.avail_in = (unsigned)n; }
                r = z_inflate(&z, 0);
            }
            ok = z.avail_out == 0; z_end(&z);
        }
    }
    fclose(f);
    if (ok && !img_alloc(out, 256, 384)) {
        for (int i = 0; i < 256 * 384; i++) {
            uint16_t c = px[i]; int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            out->px[i] = ARGB(255, r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2);
        }
    } else ok = 0;
    free(px);
    return ok ? 0 : -1;
}

static void *loader(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!qn && dss_want < 0) pthread_cond_wait(&cv, &mu);
        if (dss_want >= 0) {                            /* a save state's picture first: the page waits on it */
            int k = dss_want; char p[600]; snprintf(p, sizeof p, "%s", dss[k].path);
            pthread_mutex_unlock(&mu);
            Img im = { 0 }; int ok = !read_dss(p, &im);
            pthread_mutex_lock(&mu);
            if (!strcmp(dss[k].path, p) && dss[k].state == 0) { dss[k].pic = im; dss[k].state = ok ? 1 : -1; }
            else img_free(&im);
            dss_want = -1;
            for (int i = 0; i < NDSS; i++) if (dss[i].path[0] && dss[i].state == 0) { dss_want = i; break; }
            pthread_mutex_unlock(&mu);
            continue;
        }
        Want w = queue[--qn];                           /* the newest wish */
        pthread_mutex_unlock(&mu);
        Game *g = &w.copy;
        Done d = { w.g, w.gen, { 0 }, 0, 0, 0 };
        char p[900];
        static const char *kinds[] = { "box", "cart", "label" };
        for (int k = 0; k < 3 && !d.ok; k++)
            if (library_media(g, kinds[k], p, sizeof p) && !img_load_fit(p, 400, 440, FIT_CONTAIN, cfg.cache, &d.im)) d.ok = 1;
        if (d.ok) h_main_colors(&d.im, &d.vivid, &d.deep);
        else title_colors(g, &d.vivid, &d.deep);
        pthread_mutex_lock(&mu);
        if (dn < QMAX) done[dn++] = d; else img_free(&d.im);
        pthread_mutex_unlock(&mu);
    }
    return 0;
}

const Img *art_dss(const char *path) {
    struct stat st;
    if (stat(path, &st) || st.st_size < 0x44) return 0;
    long long mt = (long long)st.st_mtime * 1000000000LL + st.st_mtim.tv_nsec;
    pthread_mutex_lock(&mu);
    int k = -1;
    for (int i = 0; i < NDSS; i++) if (!strcmp(dss[i].path, path)) { k = i; break; }
    if (k >= 0 && dss[k].mt != mt && dss[k].state != 0) { img_free(&dss[k].pic); dss[k].state = 0; dss[k].mt = mt; }   /* saved again */
    if (k < 0) {
        k = 0; for (int i = 1; i < NDSS; i++) if (!dss[i].path[0] || (dss[k].path[0] && dss[i].use < dss[k].use)) k = i;
        if (dss[k].state == 0 && dss[k].path[0] && dss_want == k) dss_want = -1;
        img_free(&dss[k].pic);
        snprintf(dss[k].path, sizeof dss[k].path, "%s", path); dss[k].mt = mt; dss[k].state = 0;
    }
    dss[k].use = ++clock_;
    const Img *r = dss[k].state == 1 ? &dss[k].pic : 0;
    if (dss[k].state == 0 && dss_want < 0) { dss_want = k; pthread_cond_signal(&cv); }
    pthread_mutex_unlock(&mu);
    return r;
}

void art_init(void) {
    if (started) return;
    started = 1;
    pthread_t t;
    if (!pthread_create(&t, 0, loader, 0)) {
        pthread_detach(t);
        struct sched_param sp = { 0 }; pthread_setschedparam(t, SCHED_IDLE, &sp);   /* (never in the frames' way) */
    }
}

static void art_free(Art *a) { img_free(&a->cover); img_free(&a->sprite); img_free(&a->hero); img_free(&a->hero2); }

static void evict(void) {
    int n = 0;
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) { Art *a = lib[s].g[i].art; if (a && a->state == 1) n++; }
    while (n > KEEP) {
        Art *old = 0;
        for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) {
            Art *a = lib[s].g[i].art;
            if (a && a->state == 1 && (!old || a->use < old->use)) old = a;
        }
        if (!old) break;
        art_free(old); old->state = 0xdead; n--;     /* (asked for again when it's drawn next) */
    }
}

Art *art_get(Game *g) {
    Art *a = g->art;
    if (!a) { a = calloc(1, sizeof *a); if (!a) return 0; g->art = a; a->state = 0xdead; }
    a->use = ++clock_;
    if (a->state == 0xdead) {
        a->state = 0;
        pthread_mutex_lock(&mu);
        for (int i = 0; i < qn; i++) if (queue[i].g == g) { memmove(queue + i, queue + i + 1, (size_t)(qn - i - 1) * sizeof *queue); qn--; break; }
        if (qn == QMAX) { memmove(queue, queue + 1, (size_t)(QMAX - 1) * sizeof *queue); qn--; }
        queue[qn].copy = *g; queue[qn].g = g; queue[qn].gen = gen; qn++;
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&mu);
    }
    return a;
}

int art_step(void) {
    Done got[QMAX]; int n;
    pthread_mutex_lock(&mu);
    n = dn; memcpy(got, done, sizeof(Done) * (size_t)n); dn = 0;
    pthread_mutex_unlock(&mu);
    for (int i = 0; i < n; i++) {
        if (got[i].gen != gen) { img_free(&got[i].im); continue; }
        Art *a = got[i].g->art;
        if (!a || a->state != 0) { img_free(&got[i].im); continue; }
        a->cover = got[i].im; a->state = got[i].ok ? 1 : -1;
        a->vivid = got[i].vivid; a->deep = got[i].deep;
        img_free(&a->sprite); img_free(&a->hero); img_free(&a->hero2);
    }
    if (n) evict();
    return n > 0;
}

void art_forget(Game *g) {
    Art *a = g->art; if (!a) return;
    pthread_mutex_lock(&mu); gen++; pthread_mutex_unlock(&mu);   /* (a load of the old picture under way: dropped) */
    art_free(a); a->state = 0xdead;
}

void art_reset(void) {
    pthread_mutex_lock(&mu); qn = 0; gen++;
    for (int i = 0; i < dn; i++) img_free(&done[i].im);
    dn = 0; pthread_mutex_unlock(&mu);
    for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) {
        Art *a = lib[s].g[i].art; if (!a) continue;
        art_free(a); free(a); lib[s].g[i].art = 0;
    }
}
