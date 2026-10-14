// common.h: small helpers shared by every file.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SGN(x) ((x) > 0 ? 1 : (x) < 0 ? -1 : 0)
#define PI_F 3.14159265f

/* the DS's screens: everything is drawn at this size and scaled up by whole pixels */
#define SCR_W 256
#define SCR_H 192

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sat(float t) { return clampf(t, 0, 1); }

/* easing, t in 0..1 */
static inline float ease_out_cubic(float t) { t = sat(t); float u = 1 - t; return 1 - u * u * u; }
static inline float ease_in_cubic(float t) { t = sat(t); return t * t * t; }
static inline float ease_in_out(float t) { t = sat(t); return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
static inline float ease_out_back(float t) { t = sat(t); const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
static inline float ease_out_elastic(float t) {
    t = sat(t); if (t == 0 || t == 1) return t;
    return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * PI_F / 3)) + 1;
}
static inline float smoothstep(float a, float b, float x) { float t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }

/* a damped spring toward a target: the "tactile" motion everything in the menu uses. k stiffness, d damping */
typedef struct { float x, v; } Spring;
static inline void spring_step(Spring *s, float target, float k, float d, float dt) {
    /* a few substeps keep stiff springs stable at a 60 Hz frame */
    for (int i = 0; i < 4; i++) {
        float h = dt / 4, a = k * (target - s->x) - d * s->v;
        s->v += a * h; s->x += s->v * h;
    }
}

/* a stable hash of a few integers (procedural textures, sparkles) */
static inline uint32_t hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline uint32_t hash_str(const char *s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }
