// art.h: each game's picture for the menu (its box art, else its label or cartridge scan, else nothing: the menu
// makes one from its title and colours), loaded off the main thread so moving through the shelf never waits, and
// the picture's two main colours (the top screen's backdrop, the cartridge's tint).
#pragma once
#include "library.h"

typedef struct {
    int state;                      /* 0 asked for, 1 there, -1 none (a made-up cover is drawn) */
    Img cover;                      /* fitted inside 400 x 440: the box art (else the label, else a cartridge scan) */
    Img face; int face_kind;        /* for the cartridge's label: a scan of the whole cartridge (1) or of its label
                                     * (2), fitted inside 600 x 600; 0 none (the box art is cropped to a label) */
    uint32_t vivid, deep;           /* its colours (made-up ones from the title when there's no picture) */
    Img hero, hero2;                /* ui.c: the cover at the two sizes drawn last (the home's, the game page's) */
    unsigned use;
} Art;

void art_init(void);
/* the art of a game: there (state 1), being loaded (state 0, NULL picture), or none (-1); asks for it */
Art *art_get(Game *g);
/* the main thread: takes what the loader finished; 1 when anything arrived */
int art_step(void);
/* forget one game's picture (new art arrived for it): loaded again when it's drawn next */
void art_forget(Game *g);
/* forget everything (a rescan) */
void art_reset(void);

/* the two DS screens a DraStic save state holds (256 x 384, top above bottom), decoded off the main thread: NULL
 * while it's being read or when the file has none; asks for it */
const Img *art_dss(const char *path);
