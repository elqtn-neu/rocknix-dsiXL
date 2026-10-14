// synth.c: see synth.h. Everything is made from a handful of voices: an FM electric piano (a mellow tine), FM bells
// (glassy, a little inharmonic), a marimba-like wooden bar, a plucked sine bass, and three soft drums (a shaker, a
// rim, a kick), all ringing in a small stereo room (a Freeverb-style reverb: combs and all-passes).
#define _GNU_SOURCE
#include "synth.h"
#include "audio.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

/* ---- the basics ---- */

#define TAB 4096
static float SIN[TAB + 1];
static int sin_ready;
static void sin_init(void) {
    if (sin_ready) return;
    for (int i = 0; i <= TAB; i++) SIN[i] = sinf(2 * PI_F * i / TAB);
    sin_ready = 1;
}
/* sin(2 pi ph), ph in cycles (any value) */
static inline float fsin(float ph) {
    ph -= floorf(ph);
    float x = ph * TAB; int i = (int)x; float f = x - i;
    return SIN[i] + (SIN[i + 1] - SIN[i]) * f;
}
static inline float mtof(float m) { return 440.0f * powf(2, (m - 69) / 12); }
static uint32_t rng = 0x9e3779b9u;
static inline float noise(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(int32_t)rng / 2147483648.0f; }

/* the rate the voices run at: 48 kHz for the effects, 24 kHz for the music (half the work; it's mellow) */
static float RATE_NOW = SYN_RATE;

/* ---- the room ---- */

typedef struct {
    float comb[2][4][1700]; int clen[2][4], ci[2][4]; float cf[2][4];
    float ap[2][2][720]; int alen[2][2], ai[2][2];
    float fb, damp;
} Room;
static void room_init(Room *r, float size, float damp) {
    static const int C[4] = { 1214, 1293, 1390, 1475 }, A[2] = { 605, 480 };
    memset(r, 0, sizeof *r);
    float k_ = RATE_NOW / SYN_RATE;     /* (the same room at either rate) */
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < 4; k++) r->clen[c][k] = (int)((C[k] + c * 25) * k_);
        for (int k = 0; k < 2; k++) r->alen[c][k] = (int)((A[k] + c * 25) * k_);
    }
    r->fb = size; r->damp = damp;
}
/* in: mono; out: the wet signal, stereo */
static inline void room_tick(Room *r, float in, float *l, float *rr) {
    float o[2];
    for (int c = 0; c < 2; c++) {
        float s = 0;
        for (int k = 0; k < 4; k++) {
            float *buf = r->comb[c][k]; int i = r->ci[c][k];
            float y = buf[i];
            r->cf[c][k] = y * (1 - r->damp) + r->cf[c][k] * r->damp;
            buf[i] = in + r->cf[c][k] * r->fb;
            if (++r->ci[c][k] >= r->clen[c][k]) r->ci[c][k] = 0;
            s += y;
        }
        for (int k = 0; k < 2; k++) {
            float *buf = r->ap[c][k]; int i = r->ai[c][k];
            float b = buf[i];
            buf[i] = s + b * 0.5f;
            s = b - s;
            if (++r->ai[c][k] >= r->alen[c][k]) r->ai[c][k] = 0;
        }
        o[c] = s;
    }
    *l = o[0] * 0.25f; *rr = o[1] * 0.25f;
}

/* ---- the voices ---- */

enum { V_EP, V_BELL, V_GLASS, V_WOOD, V_BASS, V_SHAKER, V_RIM, V_KICK, V_CLICK, V_AIR };
typedef struct {
    int kind, on;
    float f, ph[4], amp, gl, gr;
    float env, envk, relk, idx, idxk, idx2, idx2k, t, dt;
    int left;                           /* samples until it's let go (then relk); <0: let go */
    float lp, bp, hp, aux;              /* filters, the kick's pitch */
} Voice;

static void voice_start(Voice *v, int kind, float midi_or_hz, float dur, float vel, float pan) {
    memset(v, 0, sizeof *v);
    v->kind = kind; v->on = 1;
    v->f = midi_or_hz > 200 || kind >= V_SHAKER ? midi_or_hz : mtof(midi_or_hz);
    v->amp = vel;
    float p = clampf(pan, -1, 1) * 0.5f + 0.5f;
    v->gl = cosf(p * PI_F * 0.5f); v->gr = sinf(p * PI_F * 0.5f);
    v->env = 1; v->left = (int)(dur * RATE_NOW); v->dt = 1.0f / RATE_NOW;
    float R = RATE_NOW;
    switch (kind) {
    case V_EP:    v->envk = expf(-1.1f / R); v->relk = expf(-9 / R); v->idx = 1.4f * (0.4f + vel); v->idxk = expf(-5 / R); v->idx2 = 0.9f; v->idx2k = expf(-40 / R); break;
    case V_BELL:  v->envk = expf(-2.4f / R); v->relk = expf(-5 / R); v->idx = 2.0f; v->idxk = expf(-6 / R); break;
    case V_GLASS: v->envk = expf(-3.2f / R); v->relk = expf(-7 / R); v->idx = 1.3f; v->idxk = expf(-9 / R); break;
    case V_WOOD:  v->envk = expf(-11 / R); v->relk = expf(-25 / R); v->idx = 1.0f; v->idxk = expf(-60 / R); break;
    case V_BASS:  v->envk = expf(-2.2f / R); v->relk = expf(-14 / R); break;
    case V_SHAKER: v->envk = expf(-60 / R); v->relk = v->envk; break;
    case V_RIM:   v->envk = expf(-55 / R); v->relk = v->envk; break;
    case V_KICK:  v->envk = expf(-9 / R); v->relk = v->envk; v->aux = 1; break;
    case V_CLICK: v->envk = expf(-900 / R); v->relk = v->envk; break;
    case V_AIR:   v->envk = 1; v->relk = expf(-6 / R); break;
    }
}

/* one sample; the voice ends itself when it's quiet */
static inline float voice_tick(Voice *v) {
    float out = 0, f = v->f, R = RATE_NOW;
    switch (v->kind) {
    case V_EP: {                        /* a body (1:1 FM) and a tine (a quick 1:14 ping) */
        float m = fsin(v->ph[1]) * v->idx;
        out = fsin(v->ph[0] + m * 0.16f) * 0.8f + fsin(v->ph[2] + fsin(v->ph[3]) * v->idx2 * 0.3f) * 0.22f * v->idx2;
        v->ph[0] += f / R; v->ph[1] += f / R; v->ph[2] += f / R; v->ph[3] += f * 14 / R;
        v->idx *= v->idxk; v->idx2 *= v->idx2k;
        break;
    }
    case V_BELL: {                      /* 1:3.5: glassy, and a slow shimmer */
        float m = fsin(v->ph[1]) * v->idx;
        out = fsin(v->ph[0] + m * 0.18f) * (1 + 0.08f * fsin(v->ph[2]));
        v->ph[0] += f / R; v->ph[1] += f * 3.5f / R; v->ph[2] += 5.5f / R;
        v->idx *= v->idxk;
        break;
    }
    case V_GLASS: {                     /* 1:2.76: a struck glass */
        float m = fsin(v->ph[1]) * v->idx;
        out = fsin(v->ph[0] + m * 0.2f) + 0.25f * fsin(v->ph[2]) * v->idx;
        v->ph[0] += f / R; v->ph[1] += f * 2.76f / R; v->ph[2] += f * 5.4f / R;
        v->idx *= v->idxk;
        break;
    }
    case V_WOOD:                        /* a bar of wood: the note and its 4th partial, quick */
        out = fsin(v->ph[0]) + 0.5f * fsin(v->ph[1]) * v->idx + 0.15f * fsin(v->ph[2]) * v->idx * v->idx;
        v->ph[0] += f / R; v->ph[1] += f * 3.93f / R; v->ph[2] += f * 9.2f / R;
        v->idx *= v->idxk;
        break;
    case V_BASS:
        out = fsin(v->ph[0]) + 0.22f * fsin(v->ph[0] * 2) * v->env;
        out *= MIN(1.0f, v->t / 0.006f);
        v->ph[0] += f / R;
        break;
    case V_SHAKER: {                    /* noise, high-passed */
        float n = noise();
        v->lp += (n - v->lp) * 0.35f;
        out = (n - v->lp) * 0.9f;
        break;
    }
    case V_RIM: {
        float n = noise();
        v->lp += 0.5f * v->bp; v->bp += 0.5f * (n - v->lp - 0.3f * v->bp);   /* a band around 2 kHz-ish */
        out = v->bp * 0.5f + fsin(v->ph[0]) * 0.6f;
        v->ph[0] += 1650 / R;
        break;
    }
    case V_KICK:
        out = fsin(v->ph[0]);
        v->ph[0] += (f + 75 * v->aux) / R; v->aux *= expf(-38 / R);
        break;
    case V_CLICK: {                     /* a tiny filtered snap: the physical "tick" under the tones */
        float n = noise();
        v->lp += (n - v->lp) * (f / R * 6.0f);
        out = v->lp * 2.0f + fsin(v->ph[0]) * 0.5f;
        v->ph[0] += f / R;
        break;
    }
    case V_AIR: {                       /* breath: noise through a resonance that rises */
        float fc = f * (1 + v->t * 5), k = 2 * fsin(MIN(0.24f, fc / R * 0.5f));
        v->lp += k * v->bp; v->bp += k * (noise() - v->lp - 0.5f * v->bp);
        out = v->bp * 0.6f * MIN(1.0f, v->t / 0.08f);
        break;
    }
    }
    float e = v->env;
    if (v->left > 0) { v->left--; v->env *= v->envk; }
    else v->env *= v->relk;
    if (v->kind != V_BASS && v->kind != V_SHAKER && v->kind != V_RIM && v->kind != V_KICK && v->kind != V_CLICK && v->kind != V_AIR)
        e *= MIN(1.0f, v->t / 0.0015f);  /* a 1.5 ms attack: no click */
    v->t += v->dt;
    if (v->env < 0.0015f) v->on = 0;     /* (-56 dB: gone) */
    return out * e * v->amp;
}

/* ---- sound effects: notes placed in time, rendered once, with a touch of the room ---- */

typedef struct { float *l, *r; int n; Room *room; } Buf;
static Room sfx_room;

static void place(Buf *b, int kind, float at, float note, float dur, float vel, float pan) {
    RATE_NOW = SYN_RATE;
    Voice v; voice_start(&v, kind, note, dur, vel, pan);
    for (int i = (int)(at * SYN_RATE); i < b->n && v.on; i++) {
        float s = voice_tick(&v);
        b->l[i] += s * v.gl; b->r[i] += s * v.gr;
    }
}

static int16_t *render(Buf *b, float wet, int *frames) {
    RATE_NOW = SYN_RATE;
    room_init(&sfx_room, 0.80f, 0.3f);
    int16_t *o = malloc((size_t)b->n * 4);
    if (o) for (int i = 0; i < b->n; i++) {
        float wl, wr; room_tick(&sfx_room, (b->l[i] + b->r[i]) * 0.5f, &wl, &wr);
        float l = b->l[i] + wl * wet, r = b->r[i] + wr * wet;
        o[i * 2] = (int16_t)CLAMP((int)(l * 32767 * 0.9f), -32767, 32767);
        o[i * 2 + 1] = (int16_t)CLAMP((int)(r * 32767 * 0.9f), -32767, 32767);
    }
    free(b->l); free(b->r);
    *frames = o ? b->n : 0;
    return o;
}

int16_t *synth_sfx(int id, int *frames) {
    sin_init();
    static const float LEN[SFX_COUNT] = {
        [SFX_BOOT] = 3.2f, [SFX_CURSOR] = 0.35f, [SFX_SELECT] = 0.9f, [SFX_LAUNCH] = 1.4f, [SFX_SLAM] = 0.9f,
        [SFX_BACK] = 0.6f, [SFX_BUMP] = 0.35f, [SFX_TAB] = 0.45f, [SFX_WHOOSH] = 0.6f, [SFX_OPEN] = 0.8f,
        [SFX_CONTINUE] = 1.6f, [SFX_SCROLL] = 0.15f, [SFX_ERROR] = 0.7f, [SFX_NOTCH] = 0.25f, [SFX_KEY] = 0.25f,
        [SFX_TOGGLE] = 0.5f, [SFX_CLOSE] = 0.6f, [SFX_STAR] = 1.4f, [SFX_DONE] = 1.6f, [SFX_PAGE] = 0.45f,
        [SFX_WAKE] = 1.6f, [SFX_BRIGHT] = 0.4f,
    };
    if (id < 0 || id >= SFX_COUNT || LEN[id] <= 0) return 0;
    Buf b; b.n = (int)(LEN[id] * SYN_RATE); b.l = calloc((size_t)b.n, 4); b.r = calloc((size_t)b.n, 4);
    if (!b.l || !b.r) { free(b.l); free(b.r); return 0; }
    float wet = 0.18f;
    switch (id) {
    case SFX_BOOT:      /* the start-up: a soft electric-piano chord, and a bell arpeggio rising over it */
        place(&b, V_EP, 0, 60, 2.2f, 0.20f, -0.3f); place(&b, V_EP, 0, 64, 2.2f, 0.18f, 0.1f);
        place(&b, V_EP, 0, 67, 2.2f, 0.16f, 0.3f); place(&b, V_EP, 0, 71, 2.2f, 0.14f, -0.1f);
        place(&b, V_BELL, 0.05f, 84, 0.5f, 0.26f, -0.4f); place(&b, V_BELL, 0.17f, 88, 0.5f, 0.24f, -0.1f);
        place(&b, V_BELL, 0.29f, 91, 0.5f, 0.22f, 0.2f); place(&b, V_BELL, 0.41f, 95, 1.5f, 0.24f, 0.45f);
        place(&b, V_GLASS, 0.41f, 96, 1.2f, 0.10f, 0.0f);
        wet = 0.32f;
        break;
    case SFX_CURSOR:    /* a small wooden tick, a snap under it */
        place(&b, V_WOOD, 0, 88, 0.05f, 0.30f, 0); place(&b, V_CLICK, 0, 3200, 0.002f, 0.12f, 0);
        wet = 0.10f;
        break;
    case SFX_SELECT:    /* "pon-pin": two glass notes, a fifth up */
        place(&b, V_CLICK, 0, 2600, 0.002f, 0.12f, 0);
        place(&b, V_GLASS, 0, 91, 0.15f, 0.30f, -0.15f); place(&b, V_GLASS, 0.07f, 96, 0.4f, 0.28f, 0.15f);
        place(&b, V_EP, 0, 79, 0.25f, 0.10f, 0);
        break;
    case SFX_LAUNCH: {  /* glass running up, and air rushing */
        static const float n[] = { 84, 88, 91, 96, 100, 103 };
        for (int i = 0; i < 6; i++) place(&b, V_GLASS, i * 0.045f, n[i], 0.2f, 0.20f, -0.5f + i * 0.2f);
        place(&b, V_AIR, 0, 500, 0.35f, 0.25f, 0);
        wet = 0.28f;
        break;
    }
    case SFX_SLAM:      /* the card going home: a soft thud, a firm double clack, the slot's ring */
        place(&b, V_KICK, 0, 48, 0.1f, 0.85f, 0);
        place(&b, V_CLICK, 0, 1800, 0.002f, 0.5f, -0.1f); place(&b, V_CLICK, 0.014f, 2600, 0.002f, 0.35f, 0.1f);
        place(&b, V_WOOD, 0.0f, 55, 0.08f, 0.35f, 0);
        place(&b, V_GLASS, 0.02f, 79, 0.3f, 0.07f, 0.3f);
        wet = 0.20f;
        break;
    case SFX_BACK:      /* two wooden notes down */
        place(&b, V_WOOD, 0, 84, 0.06f, 0.26f, 0.1f); place(&b, V_WOOD, 0.065f, 79, 0.08f, 0.26f, -0.1f);
        place(&b, V_CLICK, 0, 2000, 0.002f, 0.08f, 0);
        break;
    case SFX_BUMP:      /* a muffled knock: nothing further */
        place(&b, V_WOOD, 0, 55, 0.05f, 0.42f, 0); place(&b, V_KICK, 0, 70, 0.04f, 0.25f, 0);
        wet = 0.06f;
        break;
    case SFX_TAB:       /* a quick double tick, up */
        place(&b, V_WOOD, 0, 86, 0.04f, 0.24f, -0.2f); place(&b, V_WOOD, 0.05f, 91, 0.05f, 0.24f, 0.2f);
        place(&b, V_CLICK, 0, 3000, 0.002f, 0.10f, 0); place(&b, V_CLICK, 0.05f, 3400, 0.002f, 0.08f, 0);
        break;
    case SFX_WHOOSH:
        place(&b, V_AIR, 0, 350, 0.3f, 0.35f, 0);
        wet = 0.25f;
        break;
    case SFX_OPEN:      /* a window: a soft chord rising, a sparkle on top */
        place(&b, V_EP, 0, 76, 0.2f, 0.14f, -0.2f); place(&b, V_EP, 0.03f, 83, 0.2f, 0.12f, 0.2f);
        place(&b, V_BELL, 0.05f, 95, 0.2f, 0.10f, 0.3f);
        place(&b, V_CLICK, 0, 2400, 0.002f, 0.08f, 0);
        break;
    case SFX_CONTINUE:  /* the touch that leaves the health and safety screen: bright, three notes */
        place(&b, V_CLICK, 0, 2800, 0.002f, 0.14f, 0);
        place(&b, V_GLASS, 0, 88, 0.2f, 0.26f, -0.3f); place(&b, V_GLASS, 0.06f, 91, 0.2f, 0.24f, 0);
        place(&b, V_GLASS, 0.12f, 96, 0.8f, 0.26f, 0.3f);
        wet = 0.30f;
        break;
    case SFX_SCROLL:
        place(&b, V_WOOD, 0, 96, 0.02f, 0.14f, 0); place(&b, V_CLICK, 0, 3800, 0.001f, 0.07f, 0);
        wet = 0.05f;
        break;
    case SFX_ERROR:     /* a gentle "uh-uh": two low notes, a little apart */
        place(&b, V_EP, 0, 62, 0.12f, 0.30f, 0); place(&b, V_EP, 0.14f, 61, 0.2f, 0.30f, 0);
        break;
    case SFX_NOTCH:     /* a detent: a firm snap with a little body */
        place(&b, V_CLICK, 0, 2200, 0.002f, 0.34f, 0); place(&b, V_WOOD, 0, 74, 0.02f, 0.18f, 0);
        place(&b, V_KICK, 0, 160, 0.01f, 0.12f, 0);
        wet = 0.04f;
        break;
    case SFX_KEY:       /* a keyboard key: a soft plastic tap */
        place(&b, V_CLICK, 0, 2600, 0.002f, 0.22f, 0); place(&b, V_WOOD, 0, 81, 0.02f, 0.14f, 0);
        wet = 0.04f;
        break;
    case SFX_TOGGLE:    /* a switch: tick-tock up (played lower for off) */
        place(&b, V_CLICK, 0, 2400, 0.002f, 0.2f, 0); place(&b, V_WOOD, 0, 81, 0.03f, 0.2f, -0.1f);
        place(&b, V_GLASS, 0.05f, 88, 0.15f, 0.16f, 0.1f);
        break;
    case SFX_CLOSE:     /* a window closing: two soft glass notes down */
        place(&b, V_GLASS, 0, 91, 0.1f, 0.16f, 0.2f); place(&b, V_GLASS, 0.05f, 84, 0.2f, 0.16f, -0.2f);
        place(&b, V_CLICK, 0, 2000, 0.002f, 0.07f, 0);
        break;
    case SFX_STAR: {    /* a favourite: a sparkling run of bells */
        static const float n[] = { 91, 96, 100, 103, 108 };
        for (int i = 0; i < 5; i++) place(&b, V_BELL, i * 0.04f, n[i], 0.15f, 0.16f, -0.6f + i * 0.3f);
        place(&b, V_GLASS, 0.18f, 108, 0.6f, 0.08f, 0.4f);
        wet = 0.34f;
        break;
    }
    case SFX_DONE:      /* done: a warm major chime */
        place(&b, V_EP, 0, 72, 0.6f, 0.16f, -0.2f); place(&b, V_EP, 0, 76, 0.6f, 0.14f, 0.2f);
        place(&b, V_BELL, 0, 84, 0.2f, 0.20f, -0.3f); place(&b, V_BELL, 0.08f, 88, 0.2f, 0.20f, 0);
        place(&b, V_BELL, 0.16f, 91, 0.8f, 0.22f, 0.3f);
        wet = 0.30f;
        break;
    case SFX_PAGE:      /* a page turning: a short brush of air and a tick */
        place(&b, V_AIR, 0, 900, 0.08f, 0.25f, 0.2f); place(&b, V_WOOD, 0.04f, 89, 0.03f, 0.18f, 0);
        wet = 0.12f;
        break;
    case SFX_WAKE:      /* the lid opened: a soft chord blooming */
        place(&b, V_EP, 0, 65, 1.0f, 0.12f, -0.3f); place(&b, V_EP, 0.06f, 69, 1.0f, 0.12f, 0);
        place(&b, V_EP, 0.12f, 72, 1.0f, 0.12f, 0.3f); place(&b, V_BELL, 0.18f, 88, 0.6f, 0.12f, 0.2f);
        wet = 0.35f;
        break;
    case SFX_BRIGHT:    /* a brightness step: a small glass tick (higher for brighter) */
        place(&b, V_GLASS, 0, 91, 0.06f, 0.18f, 0); place(&b, V_CLICK, 0, 3000, 0.001f, 0.08f, 0);
        wet = 0.12f;
        break;
    }
    return render(&b, wet, frames);
}

/* ---- the music: an original tune in the DSi menu's spirit (not its theme): a light bossa in F, 100 beats a
 * minute, 32 bars (about 80 seconds) going round. Electric piano chords, a plucked bass, a shaker, a rim, a soft
 * kick; the tune on bells, the second time round on wood an octave up. ---- */

#define BPM 100.0
enum { MAJ7, MAJ9, MIN7, MIN9, DOM7, DOM9, DOM13, SUS7, MIN6, DOM7B9 };
static const int VOICING[][4] = {
    [MAJ7] = { 4, 7, 11, -1 }, [MAJ9] = { 4, 7, 11, 14 }, [MIN7] = { 3, 7, 10, -1 }, [MIN9] = { 3, 7, 10, 14 },
    [DOM7] = { 4, 7, 10, -1 }, [DOM9] = { 4, 10, 14, -1 }, [DOM13] = { 4, 10, 14, 21 }, [SUS7] = { 5, 10, 14, -1 },
    [MIN6] = { 3, 7, 9, -1 }, [DOM7B9] = { 4, 10, 13, -1 },
};
/* 16 bars, two halves each: (root pitch class, quality) */
static const struct { int r, q; } CHORDS[16][2] = {
    /* A: F, Dm, Gm, C, twice round, home on F */
    { { 5, MAJ7 }, { 5, MAJ7 } },  { { 2, MIN7 }, { 2, MIN7 } },  { { 7, MIN7 }, { 7, MIN7 } },  { { 0, DOM9 }, { 0, DOM9 } },
    { { 5, MAJ9 }, { 5, MAJ9 } },  { { 2, MIN9 }, { 2, MIN9 } },  { { 7, MIN7 }, { 0, DOM7 } },  { { 5, MAJ7 }, { 5, MAJ7 } },
    /* B: up to Bb, Am, Gm, C, and back round */
    { { 10, MAJ7 }, { 10, MAJ7 } }, { { 9, MIN7 }, { 9, MIN7 } },  { { 7, MIN7 }, { 7, MIN7 } },  { { 0, DOM13 }, { 0, DOM13 } },
    { { 10, MAJ7 }, { 10, MAJ7 } }, { { 9, MIN7 }, { 2, DOM7 } },  { { 7, MIN7 }, { 7, MIN7 } },  { { 0, SUS7 }, { 0, DOM7 } },
};
/* the tune: one motif (an F chord climbed and stepped down from) and its answer, said twice; then a higher B part
 * that climbs a little further each time and falls home. Bar, 16th, length in 16ths, note */
static const struct { uint8_t bar, at, len, note; } MEL[] = {
    { 0, 0, 2, 69 }, { 0, 2, 2, 72 }, { 0, 4, 4, 77 }, { 0, 8, 4, 76 }, { 0, 12, 4, 72 },
    { 1, 0, 4, 74 }, { 1, 4, 2, 72 }, { 1, 6, 6, 69 }, { 1, 12, 4, 65 },
    { 2, 0, 2, 67 }, { 2, 2, 2, 69 }, { 2, 4, 4, 70 }, { 2, 8, 4, 74 }, { 2, 12, 4, 72 },
    { 3, 0, 4, 70 }, { 3, 4, 4, 69 }, { 3, 8, 8, 67 },
    { 4, 0, 2, 69 }, { 4, 2, 2, 72 }, { 4, 4, 4, 77 }, { 4, 8, 4, 76 }, { 4, 12, 4, 72 },
    { 5, 0, 4, 74 }, { 5, 4, 2, 77 }, { 5, 6, 6, 76 }, { 5, 12, 4, 74 },
    { 6, 0, 4, 72 }, { 6, 4, 4, 70 }, { 6, 8, 4, 69 }, { 6, 12, 4, 67 },
    { 7, 0, 12, 65 }, { 7, 12, 4, 72 },
    { 8, 0, 8, 74 }, { 8, 8, 4, 77 }, { 8, 12, 4, 74 },
    { 9, 0, 8, 72 }, { 9, 8, 4, 76 }, { 9, 12, 4, 72 },
    { 10, 0, 4, 70 }, { 10, 4, 4, 74 }, { 10, 8, 4, 79 }, { 10, 12, 4, 77 },
    { 11, 0, 12, 76 }, { 11, 12, 4, 72 },
    { 12, 0, 4, 74 }, { 12, 4, 4, 77 }, { 12, 8, 4, 81 }, { 12, 12, 4, 79 },
    { 13, 0, 4, 77 }, { 13, 4, 4, 76 }, { 13, 8, 8, 78 },
    { 14, 0, 4, 79 }, { 14, 4, 4, 77 }, { 14, 8, 4, 74 }, { 14, 12, 4, 70 },
    { 15, 0, 14, 72 }, { 15, 14, 2, 67 },
};

#define MV 40
#define MRATE (SYN_RATE / 2)
static struct {
    Voice v[MV];
    double pos, step_len; long step;
    Room room;
    int ready, half;
    float prev[2], cur[2];
} M;

static Voice *m_voice(void) {
    for (int i = 0; i < MV; i++) if (!M.v[i].on) return &M.v[i];
    /* all busy: the quietest */
    int q = 0; for (int i = 1; i < MV; i++) if (M.v[i].env * M.v[i].amp < M.v[q].env * M.v[q].amp) q = i;
    return &M.v[q];
}
static void m_note(int kind, float note, int len16, float vel, float pan) {
    voice_start(m_voice(), kind, note, (float)(len16 * M.step_len / MRATE), vel, pan);
}
static float human(long s, int k) { return 0.88f + 0.24f * (float)((hash3((int)s, k, 41) & 255) / 255.0); }

static void m_step(long s) {
    int bar = (int)((s / 16) % 32), st = (int)(s % 16), b16 = bar % 16, half = st >= 8;
    int r = CHORDS[b16][half].r, q = CHORDS[b16][half].q;
    int two = CHORDS[b16][0].r != CHORDS[b16][1].r || CHORDS[b16][0].q != CHORDS[b16][1].q;
    /* the piano: the bossa's comping, the chord voiced between E3 and E5 */
    static const int COMP[] = { 0, 3, 6, 10, 12 };
    for (int k = 0; k < 5; k++) if (COMP[k] == st || (two && st == 8)) {
        int base = 48 + r; if (base < 52) base += 12;
        float vel = (st == 0 || st == 8 ? 0.11f : 0.085f) * human(s, 1);     /* (under the tune) */
        int len = st == 0 || st == 8 ? 3 : 2;
        for (int i = 0; i < 4; i++) if (VOICING[q][i] >= 0) {
            int n = base + VOICING[q][i]; while (n > 72) n -= 12;
            m_note(V_EP, (float)n, len, vel * (i == 3 ? 0.8f : 1), -0.35f + i * 0.2f);
        }
        break;
    }
    /* the bass: root, fifth, root, the fifth again leading on */
    int bass = 36 + r; if (bass > 43) bass -= 12;
    if (st == 0) m_note(V_BASS, (float)bass, 6, 0.32f, 0);
    if (st == 6) m_note(V_BASS, (float)(bass + 7), 2, 0.24f, 0);
    if (st == 8) m_note(V_BASS, (float)(two ? 36 + CHORDS[b16][1].r - (36 + CHORDS[b16][1].r > 43 ? 12 : 0) : bass), 6, 0.30f, 0);
    if (st == 14) m_note(V_BASS, (float)(bass + 7), 2, 0.22f, 0);
    /* the drums: a shaker on every 16th (the off-beats stronger), the clave on the rim, a soft kick */
    m_note(V_SHAKER, 0, 1, (st % 4 == 2 ? 0.055f : st % 2 ? 0.03f : 0.04f) * human(s, 2), 0.35f);
    static const int CLAVE[2][3] = { { 0, 6, 12 }, { 4, 8, -1 } };
    for (int k = 0; k < 3; k++) if (CLAVE[bar & 1][k] == st) m_note(V_RIM, 0, 1, 0.10f * human(s, 3), -0.25f);
    if (st == 0 || st == 8) m_note(V_KICK, 46, 1, 0.30f, 0);
    if (st == 6 || st == 14) m_note(V_KICK, 46, 1, 0.12f, 0);
    /* the tune */
    for (int i = 0; i < ARRAY_LEN(MEL); i++) if (MEL[i].bar == b16 && MEL[i].at == st) {
        /* (out in front: the bells, and a soft electric piano under them for body) */
        if (bar < 16) { m_note(V_BELL, MEL[i].note, MEL[i].len, 0.22f * human(s, 4), 0.15f); m_note(V_EP, MEL[i].note, MEL[i].len, 0.07f, 0.1f); }
        else { m_note(V_WOOD, MEL[i].note + 12, MEL[i].len, 0.16f * human(s, 4), 0.2f); m_note(V_GLASS, MEL[i].note, MEL[i].len, 0.05f, -0.2f); }
    }
}

void synth_music_reset(void) {
    sin_init();
    RATE_NOW = MRATE;
    memset(M.v, 0, sizeof M.v);
    M.half = 0; M.prev[0] = M.prev[1] = M.cur[0] = M.cur[1] = 0;
    M.step_len = MRATE * 60.0 / BPM / 4;
    M.pos = M.step_len;                 /* the first step at once */
    M.step = -1;
    room_init(&M.room, 0.82f, 0.35f);
    M.ready = 1;
}

void synth_music_render(float *acc, int frames, float gain) {
    if (!M.ready) synth_music_reset();
    RATE_NOW = MRATE;
    for (int i = 0; i < frames; i++) {
        /* a new 24 kHz sample every other output sample; between them, halfway */
        if (!M.half) {
            if (M.pos >= M.step_len) { M.pos -= M.step_len; M.step++; m_step(M.step); }
            M.pos += 1;
            float l = 0, r = 0;
            for (int k = 0; k < MV; k++) {
                Voice *v = &M.v[k];
                if (!v->on) continue;
                float s = voice_tick(v);
                l += s * v->gl; r += s * v->gr;
            }
            float wl, wr; room_tick(&M.room, (l + r) * 0.5f, &wl, &wr);
            M.prev[0] = M.cur[0]; M.prev[1] = M.cur[1];
            M.cur[0] = l + wl * 0.35f; M.cur[1] = r + wr * 0.35f;
            acc[i * 2] += (M.prev[0] + M.cur[0]) * 0.5f * 32767 * gain;
            acc[i * 2 + 1] += (M.prev[1] + M.cur[1]) * 0.5f * 32767 * gain;
        } else {
            acc[i * 2] += M.cur[0] * 32767 * gain;
            acc[i * 2 + 1] += M.cur[1] * 32767 * gain;
        }
        M.half ^= 1;
    }
    RATE_NOW = SYN_RATE;
}
