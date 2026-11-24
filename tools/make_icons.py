# Draws res/ICON0.PNG (144x80, what the XMB and Shelf's own grid show) and
# res/PIC1.PNG (480x272, the XMB background): game cases on a shelf, one pulled out.
from PIL import Image, ImageDraw
font = open("tests/msx.bin", "rb").read()
BG=(14,17,22); PANEL=(24,28,36); CARD=(34,40,52); LINE=(48,56,70); AMBER=(255,176,0); GOLD=(222,184,70)
RED=(224,84,84); GREEN=(90,196,110); BLUE=(110,150,235); PINK=(230,120,190); DIM=(128,138,152); WHITE=(234,236,240)

def text(d, x, y, s, c, k=1):
    for ch in s:
        g = font[ord(ch)*8:ord(ch)*8+8]
        for r in range(8):
            for b in range(8):
                if g[r] & (0x80 >> b): d.rectangle([x+b*k, y+r*k, x+b*k+k-1, y+r*k+k-1], fill=c)
        x += 8*k

def shade(c, f): return tuple(max(0, min(255, int(v*f))) for v in c)

def case(d, x, y, w, h, c, k):
    """one game case seen spine-on: lighter left edge, darker right, a label band"""
    d.rectangle([x, y, x+w-1, y+h-1], fill=c)
    d.rectangle([x, y, x+k-1, y+h-1], fill=shade(c, 1.25))
    d.rectangle([x+w-k, y, x+w-1, y+h-1], fill=shade(c, 0.7))
    d.rectangle([x, y, x+w-1, y+k-1], fill=shade(c, 1.25))
    d.rectangle([x+2*k, y+3*k, x+w-2*k-1, y+3*k+max(1, h//6)], fill=shade(c, 0.55))

def shelf(d, x0, base, k):
    """a row of cases standing on a plank; the amber one is pulled up a little (the one you are playing)"""
    row = [(RED,7,30,0),(BLUE,6,34,0),(AMBER,8,36,5),(GREEN,6,28,0),(PINK,5,32,0),(GOLD,7,26,0),(BLUE,5,31,0)]
    x = x0
    for c, w, h, lift in row:
        case(d, x, base-(h+lift)*k, w*k, h*k, c, k)
        x += (w+1)*k
    w = x - x0 + k
    d.rectangle([x0-2*k, base, x0+w, base+2*k-1], fill=shade(DIM, 0.9))
    d.rectangle([x0-2*k, base+2*k, x0+w, base+3*k-1], fill=shade(DIM, 0.5))
    return w

im = Image.new("RGB", (144, 80), BG); d = ImageDraw.Draw(im)
for y in range(80): d.line([0, y, 143, y], fill=shade(PANEL, 0.75 + 0.35*y/80))
d.rectangle([0, 0, 143, 79], outline=AMBER)
d.rectangle([1, 1, 142, 78], outline=shade(AMBER, 0.35))
shelf(d, 9, 58, 1)
# the title, its line and the subtitle all end on x = 137 (measured to the last lit pixel of F and R)
text(d, 64, 22, "SHELF", AMBER, 2)
d.rectangle([64, 41, 137, 42], fill=AMBER)
text(d, 77, 48, "LAUNCHER", DIM)
im.save("res/ICON0.PNG")

im = Image.new("RGB", (480, 272), BG); d = ImageDraw.Draw(im)
for y in range(272): d.line([0, y, 479, y], fill=shade(PANEL, 0.55 + 0.5*y/272))
for y in range(0, 272, 4): d.line([0, y, 479, y], fill=shade(PANEL, 0.5 + 0.5*y/272))
shelf(d, 312, 196, 3)
text(d, 40, 118, "SHELF", AMBER, 4)
d.rectangle([40, 156, 187, 158], fill=AMBER)       # as wide as the letters above it
text(d, 40, 168, "LAUNCHER", DIM, 2)                # the large picture: left aligned
im.save("res/PIC1.PNG")
print("ok")
