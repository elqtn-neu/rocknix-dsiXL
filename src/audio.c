// audio.c: a small mixer (48 kHz stereo, 16 voices and the music), WAV and MP3 sound files, and the synthesized
// stand-ins for a missing sound pack. The music is decoded from its MP3 as it plays (a 3-minute track would be 34 MB
// decoded; the 1 GB handheld has better uses for that).
#define _GNU_SOURCE
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD_WARNING
#include "third_party/minimp3.h"
#include "audio.h"
#include "config.h"
#include "plat/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define RATE 48000
#define VOICES 16

typedef struct { int16_t *d; int frames; } Snd;      /* stereo, interleaved, at RATE */
typedef struct { const Snd *s; double pos, step; float vol; } Voice;

static Snd snd[SFX_COUNT];
static int from_pack[SFX_COUNT];
static Voice voice[VOICES];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int started;

/* the music: an MP3 decoded frame by frame (or a WAV held whole), resampled on the fly */
static struct {
    uint8_t *file; size_t len, off;
    mp3dec_t dec;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]; int pcm_n, pcm_pos, ch, rate;
    Snd wav; double wpos;
    float vol, target, rate_per_s;
    int on;
    double frac; int16_t prev[2], cur[2];
} mu_;

static const char *const NAMES[SFX_COUNT] = { "boot", "cursor", "select", "launch", "slam", "back", "bump", "tab", "whoosh", "open", "continue", "scroll", "error", "notch" };

/* ---- files ---- */

static int16_t *resample(const int16_t *in, int frames, int rate, int *out_frames) {
    if (rate == RATE) { int16_t *o = malloc((size_t)frames * 4); if (o) memcpy(o, in, (size_t)frames * 4); *out_frames = frames; return o; }
    int n = (int)((long long)frames * RATE / rate);
    int16_t *o = malloc((size_t)MAX(1, n) * 4);
    if (!o) return 0;
    for (int i = 0; i < n; i++) {
        double p = (double)i * rate / RATE; int k = (int)p; double f = p - k;
        int k1 = MIN(k + 1, frames - 1);
        for (int c = 0; c < 2; c++) o[i * 2 + c] = (int16_t)(in[k * 2 + c] * (1 - f) + in[k1 * 2 + c] * f);
    }
    *out_frames = n;
    return o;
}

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 << 20) { fclose(f); return 0; }
    uint8_t *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = 0; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static int load_wav(const char *path, Snd *out) {
    size_t len; uint8_t *b = slurp(path, &len);
    if (!b) return -1;
    int ok = -1;
    if (len > 44 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4)) {
        int fmt = 0, ch = 0, rate = 0, bits = 0; const uint8_t *data = 0; uint32_t dlen = 0;
        size_t p = 12;
        while (p + 8 <= len) {
            uint32_t cl = b[p + 4] | b[p + 5] << 8 | b[p + 6] << 16 | (uint32_t)b[p + 7] << 24;
            if (!memcmp(b + p, "fmt ", 4) && p + 24 <= len) {
                fmt = b[p + 8] | b[p + 9] << 8; ch = b[p + 10] | b[p + 11] << 8;
                rate = (int)(b[p + 12] | b[p + 13] << 8 | b[p + 14] << 16 | (uint32_t)b[p + 15] << 24); bits = b[p + 22] | b[p + 23] << 8;
                if (fmt == 0xfffe && p + 34 <= len) fmt = b[p + 32] | b[p + 33] << 8;   /* WAVE_FORMAT_EXTENSIBLE */
            } else if (!memcmp(b + p, "data", 4)) { data = b + p + 8; dlen = (uint32_t)MIN((size_t)cl, len - p - 8); break; }
            p += 8 + cl + (cl & 1);
        }
        int bps = bits / 8;
        if (data && ch >= 1 && ch <= 8 && rate > 0 && ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24)) || (fmt == 3 && bits == 32))) {
            int frames = (int)(dlen / (uint32_t)(bps * ch));
            int16_t *st = malloc((size_t)MAX(1, frames) * 4);
            for (int i = 0; st && i < frames; i++) for (int c = 0; c < 2; c++) {
                const uint8_t *q = data + ((size_t)i * ch + (size_t)MIN(c, ch - 1)) * bps;
                int v;
                if (fmt == 3) { float f; memcpy(&f, q, 4); v = (int)(clampf(f, -1, 1) * 32767); }
                else if (bits == 8) v = (q[0] - 128) << 8;
                else if (bits == 16) v = (int16_t)(q[0] | q[1] << 8);
                else v = (int16_t)(q[1] | q[2] << 8);
                st[i * 2 + c] = (int16_t)v;
            }
            if (st) { out->d = resample(st, frames, rate, &out->frames); free(st); ok = out->d ? 0 : -1; }
        }
    }
    free(b);
    return ok;
}

static int load_mp3_whole(const char *path, Snd *out) {
    size_t len; uint8_t *b = slurp(path, &len);
    if (!b) return -1;
    mp3dec_t d; mp3dec_init(&d);
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t *acc = 0; int n = 0, cap = 0, rate = 0;
    size_t off = 0;
    while (off < len) {
        mp3dec_frame_info_t fi;
        int s = mp3dec_decode_frame(&d, b + off, (int)(len - off), pcm, &fi);
        if (!fi.frame_bytes) break;
        off += (size_t)fi.frame_bytes;
        if (!s) continue;
        rate = fi.hz;
        if (n + s > cap) { cap = (n + s) * 2; int16_t *t = realloc(acc, (size_t)cap * 4); if (!t) break; acc = t; }
        for (int i = 0; i < s; i++) { acc[(n + i) * 2] = pcm[i * fi.channels]; acc[(n + i) * 2 + 1] = pcm[i * fi.channels + (fi.channels > 1)]; }
        n += s;
    }
    free(b);
    if (!n || !rate) { free(acc); return -1; }
    out->d = resample(acc, n, rate, &out->frames);
    free(acc);
    return out->d ? 0 : -1;
}

static int load_pack(const char *name, Snd *out) {
    char p[400];
    snprintf(p, sizeof p, "%s/%s.wav", cfg.sounds, name);
    if (!load_wav(p, out)) return 0;
    snprintf(p, sizeof p, "%s/%s.mp3", cfg.sounds, name);
    if (!load_mp3_whole(p, out)) return 0;
    return -1;
}

/* ---- the stand-ins: small synths tuned to sit near the DSi's sounds ---- */

static Snd alloc_snd(float seconds) { Snd s; s.frames = (int)(seconds * RATE); s.d = calloc((size_t)s.frames, 4); return s; }
static void put(Snd *s, int i, float v) {
    if (i < 0 || i >= s->frames) return;
    int a = s->d[i * 2] + (int)(clampf(v, -1, 1) * 32767);
    s->d[i * 2] = s->d[i * 2 + 1] = (int16_t)CLAMP(a, -32767, 32767);
}
/* a struck tone: sine with a little of its octave, exponential decay, a 2 ms attack */
static void tone(Snd *s, float at, float f, float dur, float amp, float decay) {
    int i0 = (int)(at * RATE), n = (int)(dur * RATE);
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE, env = expf(-t * decay) * MIN(1.0f, t / 0.002f);
        float v = sinf(2 * PI_F * f * t) + 0.25f * sinf(4 * PI_F * f * t) * expf(-t * decay * 2);
        put(s, i0 + i, v * env * amp);
    }
}
/* a bell (two-operator FM): the boot chime */
static void bell(Snd *s, float at, float f, float dur, float amp) {
    int i0 = (int)(at * RATE), n = (int)(dur * RATE);
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE, env = expf(-t * 2.6f) * MIN(1.0f, t / 0.003f);
        float mod = sinf(2 * PI_F * f * 3.5f * t) * 2.2f * expf(-t * 5);
        put(s, i0 + i, sinf(2 * PI_F * f * t + mod) * env * amp);
    }
}
static uint32_t nseed = 22222;
static float noise(void) { nseed = nseed * 1664525u + 1013904223u; return (float)(nseed >> 8) / 8388608.0f - 1; }

static void synth(int id, Snd *s) {
    switch (id) {
    case SFX_BOOT:      /* a bright two-note chime */
        *s = alloc_snd(2.2f);
        bell(s, 0, 1046.5f, 2.0f, 0.22f); bell(s, 0.0f, 1568.0f, 2.0f, 0.12f); bell(s, 0.11f, 2093.0f, 2.0f, 0.16f); bell(s, 0.11f, 2637.0f, 1.8f, 0.07f);
        break;
    case SFX_CURSOR:    /* a soft wooden pop */
        *s = alloc_snd(0.09f);
        for (int i = 0; i < s->frames; i++) {
            float t = (float)i / RATE, f = 1500 + 900 * expf(-t * 120);
            put(s, i, sinf(2 * PI_F * f * t) * expf(-t * 60) * 0.32f * MIN(1.0f, t / 0.0015f));
        }
        break;
    case SFX_SELECT:    /* "pon": two bright notes */
        *s = alloc_snd(0.35f);
        tone(s, 0, 1318.5f, 0.3f, 0.26f, 14); tone(s, 0.045f, 1975.5f, 0.3f, 0.22f, 12);
        break;
    case SFX_LAUNCH:    /* a rising sparkle */
        *s = alloc_snd(0.7f);
        { static const float n[] = { 1046.5f, 1318.5f, 1568.0f, 2093.0f, 2637.0f }; for (int i = 0; i < 5; i++) tone(s, i * 0.055f, n[i], 0.4f, 0.18f, 9); }
        for (int i = 0; i < (int)(0.35f * RATE); i++) { float t = (float)i / RATE; put(s, i, noise() * 0.05f * sinf(PI_F * t / 0.35f)); }
        break;
    case SFX_SLAM:      /* a thump with a click on top: the card going home */
        *s = alloc_snd(0.45f);
        for (int i = 0; i < s->frames; i++) {
            float t = (float)i / RATE, f = 55 + 110 * expf(-t * 30);
            float v = sinf(2 * PI_F * f * t) * expf(-t * 11) * 0.65f + noise() * expf(-t * 70) * 0.25f;
            v += sinf(2 * PI_F * 2900 * t) * expf(-t * 160) * 0.18f + sinf(2 * PI_F * 4100 * (t - 0.012f)) * (t > 0.012f ? expf(-(t - 0.012f) * 220) : 0) * 0.12f;
            put(s, i, v);
        }
        break;
    case SFX_BACK:      /* two notes down */
        *s = alloc_snd(0.25f);
        tone(s, 0, 1174.7f, 0.2f, 0.22f, 18); tone(s, 0.06f, 880.0f, 0.2f, 0.2f, 18);
        break;
    case SFX_BUMP:      /* a dull bonk */
        *s = alloc_snd(0.12f);
        for (int i = 0; i < s->frames; i++) { float t = (float)i / RATE; put(s, i, (sinf(2 * PI_F * 230 * t) > 0 ? 0.5f : -0.5f) * sinf(2 * PI_F * 115 * t) * expf(-t * 40) * 0.35f); }
        break;
    case SFX_TAB:       /* a double tick */
        *s = alloc_snd(0.14f);
        tone(s, 0, 1760, 0.06f, 0.2f, 70); tone(s, 0.05f, 2349, 0.08f, 0.2f, 60);
        break;
    case SFX_WHOOSH:    /* air: noise swept through a resonator */
        *s = alloc_snd(0.32f);
        { float lp = 0, bp = 0;
          for (int i = 0; i < s->frames; i++) {
              float t = (float)i / RATE, fc = 400 + 3200 * t / 0.32f, k = 2 * sinf(PI_F * fc / RATE);
              lp += k * bp; bp += k * (noise() - lp - 0.4f * bp);
              put(s, i, bp * 0.22f * sinf(PI_F * t / 0.32f));
          } }
        break;
    case SFX_OPEN:
        *s = alloc_snd(0.2f);
        tone(s, 0, 880, 0.15f, 0.18f, 20); tone(s, 0.05f, 1318.5f, 0.15f, 0.18f, 20);
        break;
    case SFX_CONTINUE:
        *s = alloc_snd(0.45f);
        tone(s, 0, 1568.0f, 0.4f, 0.22f, 9); tone(s, 0.07f, 2093.0f, 0.4f, 0.18f, 9);
        break;
    case SFX_SCROLL:
        *s = alloc_snd(0.03f);
        for (int i = 0; i < s->frames; i++) { float t = (float)i / RATE; put(s, i, sinf(2 * PI_F * 3000 * t) * expf(-t * 200) * 0.15f); }
        break;
    case SFX_NOTCH:     /* a firm little click: a detent */
        *s = alloc_snd(0.05f);
        for (int i = 0; i < s->frames; i++) {
            float t = (float)i / RATE;
            put(s, i, (sinf(2 * PI_F * 2400 * t) * 0.22f + noise() * 0.18f) * expf(-t * 260) + sinf(2 * PI_F * 620 * t) * expf(-t * 90) * 0.2f);
        }
        break;
    case SFX_ERROR:
        *s = alloc_snd(0.3f);
        for (int i = 0; i < s->frames; i++) { float t = (float)i / RATE; put(s, i, (sinf(2 * PI_F * 196 * t) > 0 ? 0.25f : -0.25f) * expf(-t * 9)); }
        break;
    }
}

/* ---- mixing ---- */

static void music_next(int16_t out[2]) {
    if (mu_.wav.d) {
        int k = (int)mu_.wpos; out[0] = mu_.wav.d[k * 2]; out[1] = mu_.wav.d[k * 2 + 1];
        mu_.wpos += 1; if (mu_.wpos >= mu_.wav.frames) mu_.wpos = 0;
        return;
    }
    while (mu_.pcm_pos >= mu_.pcm_n) {
        mp3dec_frame_info_t fi;
        if (mu_.off >= mu_.len) { mu_.off = 0; mp3dec_init(&mu_.dec); }   /* loop */
        mu_.pcm_n = mp3dec_decode_frame(&mu_.dec, mu_.file + mu_.off, (int)(mu_.len - mu_.off), mu_.pcm, &fi);
        mu_.pcm_pos = 0;
        if (!fi.frame_bytes) { mu_.off = mu_.len; mu_.pcm_n = 0; if (++mu_.ch > 4) { mu_.on = 0; out[0] = out[1] = 0; return; } continue; }
        mu_.off += (size_t)fi.frame_bytes;
        mu_.ch = 0;
        mu_.rate = fi.hz;
        if (fi.channels == 1) { for (int i = mu_.pcm_n - 1; i >= 0; i--) { mu_.pcm[i * 2] = mu_.pcm[i]; mu_.pcm[i * 2 + 1] = mu_.pcm[i]; } }
    }
    out[0] = mu_.pcm[mu_.pcm_pos * 2]; out[1] = mu_.pcm[mu_.pcm_pos * 2 + 1];
    mu_.pcm_pos++;
}

static void mix(int16_t *buf, int frames) {
    static float acc[2048 * 2];
    if (frames > 2048) frames = 2048;
    memset(acc, 0, (size_t)frames * 2 * sizeof(float));
    pthread_mutex_lock(&mu);
    for (int v = 0; v < VOICES; v++) {
        Voice *vo = &voice[v];
        if (!vo->s) continue;
        for (int i = 0; i < frames; i++) {
            int k = (int)vo->pos;
            if (k >= vo->s->frames - 1) { vo->s = 0; break; }
            float f = (float)(vo->pos - k);
            const int16_t *d = vo->s->d + k * 2;
            acc[i * 2] += (d[0] + (d[2] - d[0]) * f) * vo->vol;
            acc[i * 2 + 1] += (d[1] + (d[3] - d[1]) * f) * vo->vol;
            vo->pos += vo->step;
        }
    }
    if (mu_.on) {
        for (int i = 0; i < frames; i++) {
            /* the track's rate to ours: linear between its samples */
            double step = (mu_.wav.d ? RATE : (mu_.rate ? mu_.rate : RATE)) / (double)RATE;
            mu_.frac += step;
            while (mu_.frac >= 1) { mu_.frac -= 1; mu_.prev[0] = mu_.cur[0]; mu_.prev[1] = mu_.cur[1]; music_next(mu_.cur); }
            float f = (float)mu_.frac;
            acc[i * 2] += (mu_.prev[0] + (mu_.cur[0] - mu_.prev[0]) * f) * mu_.vol;
            acc[i * 2 + 1] += (mu_.prev[1] + (mu_.cur[1] - mu_.prev[1]) * f) * mu_.vol;
            if (mu_.vol != mu_.target) {
                float d = mu_.rate_per_s / RATE;
                mu_.vol = mu_.vol < mu_.target ? MIN(mu_.target, mu_.vol + d) : MAX(mu_.target, mu_.vol - d);
            }
        }
    }
    pthread_mutex_unlock(&mu);
    for (int i = 0; i < frames * 2; i++) {
        float x = acc[i];
        if (x > 30000 || x < -30000) x = x > 0 ? 30000 + (x - 30000) * 0.2f : -30000 + (x + 30000) * 0.2f;   /* soft ceiling */
        buf[i] = (int16_t)CLAMP((int)x, -32767, 32767);
    }
}

void audio_init(void) {
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!load_pack(NAMES[i], &snd[i])) { from_pack[i] = 1; plat_log("audio: %s from the sound pack", NAMES[i]); }
        else synth(i, &snd[i]);
    }
    started = plat_audio_start(RATE, mix) == 0;
    if (!started) plat_log("audio: no output");
}

int sound_from_pack(int id) { return id >= 0 && id < SFX_COUNT && from_pack[id]; }

void sfx_pitch(int id, float pitch) { sfx_ex(id, pitch, 1); }
void sfx_ex(int id, float pitch, float vol) {
    if (!started || !cfg.sfx || id < 0 || id >= SFX_COUNT || !snd[id].d) return;
    pthread_mutex_lock(&mu);
    int best = 0; double far = -1;
    for (int v = 0; v < VOICES; v++) {
        if (!voice[v].s) { best = v; break; }
        if (voice[v].pos > far) { far = voice[v].pos; best = v; }      /* all busy: the one furthest along */
    }
    /* the same sound twice within a few ms (a held d-pad) replaces itself: no phasing */
    for (int v = 0; v < VOICES; v++) if (voice[v].s == &snd[id] && voice[v].pos < RATE * 0.02) { best = v; break; }
    voice[best].s = &snd[id]; voice[best].pos = 0; voice[best].step = pitch; voice[best].vol = vol;
    pthread_mutex_unlock(&mu);
}
void sfx(int id) { sfx_ex(id, 1.0f, 1.0f); }

void music_play(void) {
    if (!started || !cfg.music || mu_.on) return;
    char p[400]; size_t len;
    pthread_mutex_lock(&mu);
    snprintf(p, sizeof p, "%s/music.mp3", cfg.sounds);
    if (!mu_.file && (mu_.file = slurp(p, &len))) { mu_.len = len; mu_.off = 0; mp3dec_init(&mu_.dec); }
    if (!mu_.file && !mu_.wav.d) { snprintf(p, sizeof p, "%s/music.wav", cfg.sounds); load_wav(p, &mu_.wav); mu_.wpos = 0; }
    if (mu_.file || mu_.wav.d) { mu_.on = 1; mu_.vol = 0; mu_.target = 0.55f; mu_.rate_per_s = 0.8f; mu_.ch = 0; }
    pthread_mutex_unlock(&mu);
}
void music_fade(float target, float seconds) {
    pthread_mutex_lock(&mu);
    mu_.target = target; mu_.rate_per_s = seconds > 0 ? fabsf(mu_.vol - target) / seconds + 1e-4f : 100;
    pthread_mutex_unlock(&mu);
}
void music_stop(void) { pthread_mutex_lock(&mu); mu_.on = 0; pthread_mutex_unlock(&mu); }
