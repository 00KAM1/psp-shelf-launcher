import sys, glob, os
from PIL import Image
for f in sorted(glob.glob("out/*.rgba")):
    im = Image.frombytes("RGBA", (480, 272), open(f, "rb").read()).convert("RGB")
    im.resize((960, 544), Image.NEAREST).save(f[:-5] + ".png")
print("ok")
