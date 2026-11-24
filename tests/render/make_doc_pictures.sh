#!/bin/sh
# Draws docs/images from your own games' art. Run from tests/:
#   sh render/make_doc_pictures.sh <art folder> <Emblem games folder> [<states folder>]
# See render/doc_stick.py for what the art folder holds. Needs what run_tests.sh needs.
set -e
cd "$(dirname "$0")/.."
mkdir -p docrun && cd docrun && cp ../msx.bin .
python3 ../render/doc_stick.py "$@"
gcc -std=gnu99 -O1 -g -Wall -Wno-unused-parameter -Wno-unused-result -Wno-misleading-indentation -I../shim -include ../shim/psptypes.h \
    -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS \
    -o doc_shots ../render/doc_shots.c ../host_shim.c ../../src/util.c ../../src/iso.c ../../src/sfo.c ../../src/image.c ../../src/scan.c ../../src/data.c \
    ../../src/arkcfg.c ../../src/gfx.c ../../src/ui.c ../../src/text.c ../../src/jfont_data.c -x c ../../src/lodepng.c
rm -rf out && ./doc_shots
python3 - <<'PY'
import glob, os
from PIL import Image
for f in sorted(glob.glob("out/*.rgba")):
    im = Image.frombytes("RGBA", (480, 272), open(f, "rb").read()).convert("RGB")
    im.resize((960, 544), Image.NEAREST).save("../../docs/images/" + os.path.basename(f)[:-5] + ".png", optimize=True)
print("docs/images written")
PY
