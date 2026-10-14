// library.h: the games: the ROM folders, what the ROMs say about themselves (a DS ROM's own title, game code and
// banner icon) and the art found for them.
#pragma once
#include "gfx.h"

enum { SYS_NDS, SYS_GBA, SYS_COUNT };

typedef struct {
    char file[256];                 /* the ROM's file name */
    char path[600];
    char base[256];                 /* the file name without its extension: how art is found */
    char title[128];                /* shown: the ROM's own title (DS banner), else the cleaned file name */
    char sub[128];                  /* the DS banner's second line (publisher) */
    char code[8];                   /* DS/GBA game code from the header ("AMCE"), for GameTDB */
    int sys;
    Img icon;                       /* the DS banner icon, 32x32 (empty for GBA) */
    /* art, loaded on demand: state 0 not tried, 1 there, -1 none */
    Img label; int label_state;     /* 1 a label scan, 2 box art (cropped to fit), 3 a scan of the whole cartridge */
    Img logo; int logo_state;       /* the title logo for the top screen */
    void *cart;                     /* cart.c's texture for this game */
    uint32_t tint; int tint_state;  /* the main colour of its art (the bottom screen takes it on); 1 there, -1 none */
} Game;

typedef struct { Game *g; int n; } Library;

extern Library lib[SYS_COUNT];

void library_scan(void);
/* finds the art of a game: kind "box", "label" or "logo"; returns 1 and the path when there is one */
int library_media(const Game *g, const char *kind, char *out, int out_sz);
/* loads (or drops) the art of a game */
void game_load_art(Game *g);
void game_free_art(Game *g);
/* its main colour (g->tint, when g->tint_state is 1) */
void game_tint(Game *g);
const char *sys_name(int sys);       /* "Nintendo DS", "Game Boy Advance" */
const char *sys_short(int sys);      /* "NDS", "GBA" */
