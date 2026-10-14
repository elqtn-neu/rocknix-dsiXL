// audio.h: the menu's sounds. Every sound is a file in the sound pack folder (cfg.sounds) when there is one there,
// else a synthesized stand-in. Put the DSi's own sounds there (see README.md, "Sounds") to hear the real thing.
#pragma once

enum {
    SFX_BOOT,       /* boot.wav     the chime with the logo */
    SFX_CURSOR,     /* cursor.wav   moving between games */
    SFX_SELECT,     /* select.wav   A on a game */
    SFX_LAUNCH,     /* launch.wav   the game starting (the cartridge flies up) */
    SFX_SLAM,       /* slam.wav     the cartridge landing in its slot */
    SFX_BACK,       /* back.wav     B, closing things */
    SFX_BUMP,       /* bump.wav     the end of the list */
    SFX_TAB,        /* tab.wav      NDS <-> GBA */
    SFX_WHOOSH,     /* whoosh.wav   things sliding on and off */
    SFX_OPEN,       /* open.wav     a window opening (the options) */
    SFX_CONTINUE,   /* continue.wav the touch that leaves the health and safety screen */
    SFX_SCROLL,     /* scroll.wav   the scroll bar */
    SFX_ERROR,      /* error.wav    */
    SFX_NOTCH,      /* notch.wav    the selection locking into the frame */
    SFX_COUNT
};

void audio_init(void);
void sfx(int id);
void sfx_pitch(int id, float pitch);        /* 1 = as is */
void sfx_ex(int id, float pitch, float vol); /* vol 1 = as is */
void music_play(void);                      /* music.mp3 / music.wav from the pack, looped; nothing without one */
void music_fade(float target, float seconds);
void music_stop(void);
int sound_from_pack(int id);                /* 1 when the pack has this sound */
