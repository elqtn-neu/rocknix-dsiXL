// cart.h: the game cartridges in 3D: a DS game card or a Game Boy Advance cartridge, with the game's label art.
#pragma once
#include "library.h"

typedef struct {
    float sx, sy;           /* screen position of the cartridge's centre */
    float scale;            /* pixels per millimetre (a DS card is 35 x 33 mm) */
    float yaw, pitch, roll; /* radians */
    float alpha;            /* 0..1 */
    float desat;            /* 0..1: the cartridges that aren't selected are greyer */
    float bright;           /* 1 = lit as is */
    float gloss, gloss_pos; /* the glossy streak (see r3d.h) */
    float stretch_x, stretch_y; /* squash and stretch, across and along its height (0 = 1) */
} CartPose;

void cart_draw(Surf *s, Game *g, const CartPose *p);
/* the size of the cartridge of a system, mm */
void cart_size(int sys, float *w, float *h, float *d);
void cart_free(Game *g);
