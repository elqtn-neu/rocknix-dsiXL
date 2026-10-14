// synth.h: the menu's own sounds and its own music, made here rather than recorded: a few soft instruments in the
// spirit of the DSi's (glassy bells, a mellow electric piano, wooden ticks), a small room for them to ring in, and an
// original bossa-style tune played by a sequencer in real time (no file, no memory to speak of). A sound pack's
// files (the README's "Sounds") take the place of any of them.
#pragma once
#include <stdint.h>

#define SYN_RATE 48000

/* a sound effect: rendered once at start into stereo 16-bit at SYN_RATE; NULL when the id has no recipe */
int16_t *synth_sfx(int id, int *frames);

/* the music: the next frames, added into acc (stereo floats, 16-bit scale) at gain */
void synth_music_reset(void);
void synth_music_render(float *acc, int frames, float gain);
