// gamecfg.h: a game's emulator settings, kept where ROCKNIX keeps them: ROCKNIX's system.cfg, as per-game keys
// (nds["<rom file>"].shader=ds-crisp). EmulationStation writes the same keys, ROCKNIXDS's DraStic session and
// ROCKNIX's RetroArch launcher read them: a setting made here is the one the game starts with, and the one
// EmulationStation shows.
#pragma once
#include "library.h"

#define GOPT_MAX 8
typedef struct {
    const char *label;              /* "Performance" */
    const char *key;                /* system.cfg's key: power_profile */
    const char *help;               /* what it is, for the top screen */
    int n, rec;                     /* how many choices; the recommended one */
    const char *names[GOPT_MAX];    /* "Balanced" */
    const char *values[GOPT_MAX];   /* "balanced" */
    const char *notes[GOPT_MAX];    /* what that choice does */
} GameOpt;

int gamecfg_opts(int sys, const GameOpt **opts);          /* the options of a system */
/* the choice each option has for a game: its own key, else the system's, else the recommended one */
void gamecfg_load(const Game *g, int *choice);
int gamecfg_save(const Game *g, const int *choice);       /* writes the game's keys; 0 on success */
