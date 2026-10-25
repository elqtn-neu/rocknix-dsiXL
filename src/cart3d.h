// cart3d.h: the game cartridges in 3D, at the panels' own resolution: a DS game card or a Game Boy Advance Game Pak,
// its real outline extruded to its real thickness, its face a picture of the cartridge with the game's art on the
// label, lit from the upper left with a glint, turning, tilting, squashing.
#pragma once
#include "library.h"

typedef struct {
    float sx, sy;               /* the cartridge's centre on the screen (its pixels) */
    float scale;                /* pixels per millimetre (a DS card is 35 x 33 mm) */
    float yaw, pitch, roll;     /* radians */
    float alpha;                /* 0..1 */
    float desat;                /* 0..1 toward grey */
    float bright;               /* 1 lit as is */
    float gloss, gloss_pos;     /* a band of light sweeping over its face: strength, where (-0.5 .. 2.5) */
    float stretch_x, stretch_y; /* squash and stretch (0 = 1), from its bottom edge */
    float reflect;              /* > 0: its reflection on the floor below it, that strength (0..1) */
} CartPose;

void cart3d_size(int sys, float *w, float *h, float *d);   /* mm */
/* drawn: a pose it was drawn in before is the same picture again (smooth edges); a new one is drawn quickly */
void cart3d_draw(Surf *s, Game *g, const CartPose *p);
/* its picture made now (on a frame with time to spare), so it's ready before it's seen; 1 when that was done */
int cart3d_prepare(Game *g);
void cart3d_forget(Game *g);
/* a new frame: a cartridge not made yet is made at most two a frame (more wait, unseen, a frame or two) */
int cart3d_frame(void);   /* 1: one waited last frame (draw again) */    /* its art changed */
