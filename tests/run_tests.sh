#!/bin/sh
# PC tests. Needs: python3 with pillow, pycdlib, lz4; gcc.
set -e
cd "$(dirname "$0")"
python3 make_stick.py
# the EBOOT just built for the PSP, read back with Shelf's own reader (icon check)
mkdir -p ms0/BUILT && [ -f ../EBOOT.PBP ] && cp ../EBOOT.PBP ms0/BUILT/EBOOT.PBP
gcc -std=gnu99 -O1 -g -Wall -Wno-unused-parameter -Wno-unused-result -Wno-misleading-indentation -fsanitize=address,undefined -Ishim -include shim/psptypes.h \
    -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS \
    -o test_shelf test_shelf.c host_shim.c ../src/util.c ../src/iso.c ../src/sfo.c ../src/image.c ../src/scan.c ../src/data.c \
    runtime_track/track.c ../src/arkcfg.c ../src/gfx.c ../src/ui.c ../src/text.c ../src/jfont_data.c -x c ../src/lodepng.c
ASAN_OPTIONS=detect_leaks=0 ./test_shelf
