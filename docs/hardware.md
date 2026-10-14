# The RG DS Plus, and what the menu is built around

| | Anbernic RG DS Plus |
|---|---|
| SoC | Rockchip RK3568: 4x Cortex-A55 up to ~2.0 GHz, Mali-G52 2EE GPU |
| RAM | **1 GB** (the original RG DS has 3 GB for Android) |
| Screens | **two 4.3" IPS, 1024x768 (4:3), 60 Hz**: DSI-2 is the top panel, DSI-1 the bottom; both touch |
| Battery | 4000 mAh |
| Storage | microSD only |
| OS here | ROCKNIX with ROCKNIXDS (`plus-beta`: DraStic with libdsflip, both panels driven directly) |

## What follows from it

- **4x, exactly.** 1024x768 is the DS's 256x192 times four in both directions. The menu draws both screens at
  256x192, exactly as a DS would, and the platform layer turns every DS pixel into a 4x4 block of panel pixels. No
  filtering, no "HD" redraw: it looks like a DSi at the panel's size. On the original RG DS (640x480) it is 2x,
  centred.
- **No compositor, no GPU.** The menu owns both panels through DRM/KMS, the way ROCKNIXDS's DraStic does
  (`src/plat/plat_kms.c` is ROCKNIXDS's, MIT). Each panel gets a pair of dumb buffers and its own atomic commits.
  The Mali sits at its lowest clock while the menu is up (`session.sh` sets it to powersave).
- **1 GB of RAM.**
  - The binary is about 330 KB and links only glibc. libdrm, ALSA and SDL are opened when needed.
  - Art is shrunk once to the size it's drawn at and cached (`/storage/.config/dsixl/cache`). A 2 MB box scan
    becomes a few KB.
  - Only the games near the selection keep their art and cartridge textures in memory.
  - Music streams from its MP3.
  - **The menu exits while a game runs:** DraStic or RetroArch gets the whole 1 GB, and the menu is back in a
    fraction of a second when the game ends.
- **Every frame animated, cheaply.** About 1.6 ms of CPU a frame on a desktop core, which should leave plenty of
  the A55's 16.7 ms frame budget. It hasn't been measured on the handheld yet.
  - The 3D cartridges are a small software rasterizer at 2x2 supersampling (the DS's edge antialiasing).
  - Only cartridges whose pose changed are drawn again. One that only floats up and down is the same picture
    moved, which is also how the DS moved its sprites: by whole pixels.
  - A panel whose picture didn't change isn't sent again.
