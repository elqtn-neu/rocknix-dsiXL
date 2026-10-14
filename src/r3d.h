// r3d.h: a tiny software 3D renderer for the cartridges: convex polygons, perspective-correct texturing, flat
// lighting with a specular glint, drawn at 2x2 supersampling into a scratch buffer and resolved onto the screen
// (the DS's own edge antialiasing look, at the DS's resolution).
#pragma once
#include "gfx.h"

typedef struct { float x, y, z; } V3;
typedef struct { float m[3][3]; } M3;

static inline V3 v3(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static inline V3 v3_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3_scale(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static inline float v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3_cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 v3_norm(V3 a) { float l = sqrtf(v3_dot(a, a)); return l > 1e-9f ? v3_scale(a, 1 / l) : a; }
M3 m3_rot(float yaw, float pitch, float roll);     /* radians: about y, then x, then z */
static inline V3 m3_mul(const M3 *m, V3 v) {
    return v3(m->m[0][0] * v.x + m->m[0][1] * v.y + m->m[0][2] * v.z,
              m->m[1][0] * v.x + m->m[1][1] * v.y + m->m[1][2] * v.z,
              m->m[2][0] * v.x + m->m[2][1] * v.y + m->m[2][2] * v.z);
}

typedef struct {
    const Img *tex;             /* NULL: flat colour */
    uint32_t color;
    float shade;                /* light: 1 = as is */
    float spec;                 /* white added: 0..1 */
    float desat;                /* 0..1 toward grey */
    float gloss;                /* 0..1: the strength of the moving glossy streak (the selected cartridge) */
    float gloss_pos;            /* where the streak is, in texture u+v units (-0.5..2.5 sweeps across) */
    float sheen;                /* 0..1: a clear-coat gradient, brighter at the top of the face */
    int nearest;                /* texels as they are (the DS's look) rather than smoothed between */
} Mat;

/* the camera: screen centre of the object, focal length in pixels; vertices are given in camera space (z ahead) */
void r3d_camera(float cx, float cy, float f);
/* the screen box the object can cover (1x pixels) */
int r3d_begin(Surf *dst, int x0, int y0, int x1, int y1);
/* a convex polygon, n <= 16, uv per vertex (texture 0..1); the caller culls back faces */
void r3d_poly(const V3 *v, const float *uv, int n, const Mat *m);
/* onto the screen at alpha 0..255 */
void r3d_end(int alpha);
/* or into an image (began with dst NULL): straight alpha; its top-left in screen pixels in ox, oy */
int r3d_end_img(Img *out, int *ox, int *oy);
/* where a camera-space point lands on screen */
void r3d_project(V3 p, float *sx, float *sy);
