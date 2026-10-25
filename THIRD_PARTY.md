# Third-party code and data

- **src/plat/** (platform layer: DRM/KMS on both panels, evdev input, ALSA, SDL2 window, headless PNGs) is from
  Döda Kvarter in [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), MIT License,
  Copyright (c) 2026 JorreFog and the ROCKNIXDS contributors. Changed here: the DS's 256x192 at whole-pixel scale,
  libdrm opened at run time (`drm_dl.h`), names. `device/session.sh` and `device/launch.sh` follow ROCKNIXDS's
  session scripts.
- **src/third_party/stb_image.h**: Sean Barrett, public domain (or MIT).
- **src/third_party/minimp3.h**: lieff, CC0 1.0 (`minimp3.LICENSE`).
- **tools/fonts/** M PLUS 1p (Medium, Bold, ExtraBold, Black), subset to Latin: Coji Morishita and M+ Fonts
  Project, SIL Open Font License 1.1 (`tools/fonts/OFL.txt`). `src/font_data.c` is rasterized from it.

Nintendo, Nintendo DS, Nintendo DSi and Game Boy Advance are trademarks of Nintendo. This is a personal, unofficial
project. No Nintendo artwork, sounds or code are included: the boot logo is drawn from the font above, the sounds
are synthesized, and box art and logos are fetched by the user's own scraper run.
