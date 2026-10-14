#!/bin/sh
# build.sh: builds the menu.
#   sh build.sh              this machine (build/dsixl): try it in a window (--backend sdl), run the tests
#   sh build.sh aarch64      the RG DS Plus (build/dsixl-aarch64), with aarch64-linux-gnu-gcc
#                            (Debian/Ubuntu: apt install gcc-aarch64-linux-gnu libdrm-dev libsdl2-dev)
# libdrm, SDL2 and ALSA are all loaded at run time, so the binary links only against glibc: only their headers
# are needed to build.
set -e
cd "$(dirname "$0")"
V=$(cat VERSION 2>/dev/null || echo dev)
SRCS="src/main.c src/ui.c src/widgets.c src/cart.c src/r3d.c src/library.c src/img.c src/font.c src/font_data.c
      src/gfx.c src/audio.c src/config.c src/rumble.c src/gamecfg.c src/power.c src/job.c src/panels.c src/synth.c
      src/plat/plat.c src/plat/plat_kms.c src/plat/plat_sdl.c src/plat/plat_headless.c src/plat/input_evdev.c
      src/plat/audio_alsa.c src/plat/png.c"
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -Wno-sign-compare"
INC="-Isrc -Isrc/plat -I/usr/include/libdrm -I/usr/include/SDL2"
mkdir -p build
case "$1" in
aarch64)
    CC=${CC:-aarch64-linux-gnu-gcc}
    # Debian's SDL2 headers keep their config per architecture: the host's will do (both 64-bit little-endian,
    # and only SDL's declarations are used: the library itself is opened at run time)
    if [ -z "$SYSROOT_SDL" ] && [ ! -e /usr/include/aarch64-linux-gnu/SDL2/_real_SDL_config.h ]; then
        mkdir -p build/sdlshim/SDL2
        R=$(ls /usr/include/*/SDL2/_real_SDL_config.h 2>/dev/null | head -n1)
        [ -n "$R" ] && echo "#include \"$R\"" > build/sdlshim/SDL2/_real_SDL_config.h
        INC="$INC -Ibuild/sdlshim -DSDL_DISABLE_IMMINTRIN_H -DSDL_DISABLE_MMINTRIN_H -DSDL_DISABLE_XMMINTRIN_H -DSDL_DISABLE_EMMINTRIN_H -DSDL_DISABLE_PMMINTRIN_H"
    fi
    $CC -O2 -mcpu=cortex-a55 $WARN -DDSIXL_VERSION="\"$V\"" $INC -o build/dsixl-aarch64 $SRCS -lm -lpthread -ldl
    ${STRIP:-aarch64-linux-gnu-strip} --strip-unneeded build/dsixl-aarch64
    ls -l build/dsixl-aarch64
    ;;
*)
    CC=${CC:-cc}
    $CC -O2 -g $WARN ${DSIXL_CFLAGS:-} -DDSIXL_VERSION="\"$V\"" $INC -o build/dsixl $SRCS -lm -lpthread -ldl
    ls -l build/dsixl
    ;;
esac
