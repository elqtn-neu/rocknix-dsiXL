// gamecfg.h: the emulators' settings, for every game of a system, kept where ROCKNIX keeps them: ROCKNIX's
// system.cfg, as the system's keys (nds.shader=ds-crisp, gba.integerscale=1). EmulationStation writes the same keys
// (its system settings), ROCKNIXDS's DraStic session and ROCKNIX's RetroArch launcher read them. A game with a
// setting of its own (set in EmulationStation) keeps that one.
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
/* the choice each option has: the system's key, else the recommended one */
void gamecfg_load(int sys, int *choice);
int gamecfg_save(int sys, const int *choice);             /* writes the system's keys; 0 on success */
/* the recommended choices, written only where the system has none yet (the installer's first setup) */
int gamecfg_defaults(int sys);

/* BananaOS's settings files ("key = value" lines; KV_TIGHT "KEY=value", KV_QUOTED key = "value"), the way its own
 * Settings writes them; /data may be DSIXL_DATA in tests. bcfg_set with val NULL removes the key */
enum { KV_SPACED, KV_TIGHT, KV_QUOTED };
int bcfg_get(const char *path, const char *key, char *out, int sz);
int bcfg_set(const char *path, const char *key, const char *val, int style);
