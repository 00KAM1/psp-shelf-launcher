#!/usr/bin/env python3
"""Shelf mock-up renders (480x272, PSP debug 8x8 font). Stand-in thumbnails, no game art."""
import math, random
from PIL import Image, ImageDraw

FONT = open("msx.bin", "rb").read()
W, H = 480, 272

BG     = (14, 17, 22)
CARD   = (27, 32, 41)
LINE   = (48, 56, 70)
WHITE  = (234, 236, 240)
GREY   = (128, 138, 152)
DIM    = (78, 86, 100)
AMBER  = (255, 176, 0)
RED    = (224, 84, 84)
GOLD   = (222, 184, 70)
GREEN  = (90, 196, 110)
BLUE   = (110, 150, 235)
PINK   = (230, 120, 190)

def text(im, x, y, s, col, scale=1):
    px = im.load()
    for i, ch in enumerate(s):
        g = FONT[(ord(ch) & 0xFF) * 8:(ord(ch) & 0xFF) * 8 + 8]
        for r in range(8):
            b = g[r]
            for c in range(8):
                if b & (0x80 >> c):
                    for dy in range(scale):
                        for dx in range(scale):
                            xx, yy = x + (i * 8 + c) * scale + dx, y + r * scale + dy
                            if 0 <= xx < im.width and 0 <= yy < im.height:
                                px[xx, yy] = col

def tw(s, scale=1): return len(s) * 8 * scale

def box(d, x, y, w, h, fill, outline=None):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=outline)

def bar(d, x, y, w, h, frac, col):
    box(d, x, y, w, h, (40, 46, 58))
    if frac > 0: box(d, x, y, max(1, int(w * frac)), h, col)

# ---- PSP button glyphs (8x8) ----
def btn(d, x, y, kind):
    if kind == "X":
        d.line([x + 1, y + 1, x + 6, y + 6], fill=BLUE, width=2); d.line([x + 6, y + 1, x + 1, y + 6], fill=BLUE, width=2)
    elif kind == "O":
        d.ellipse([x, y, x + 7, y + 7], outline=RED, width=2)
    elif kind == "S":
        d.rectangle([x + 1, y + 1, x + 6, y + 6], outline=PINK, width=2)
    elif kind == "T":
        d.polygon([(x + 4, y), (x + 8, y + 7), (x, y + 7)], outline=GREEN); d.polygon([(x + 4, y + 2), (x + 6, y + 6), (x + 2, y + 6)], outline=GREEN)

def keybar(im, d, items, y=260):
    box(d, 0, y - 4, W, H - y + 4, (20, 24, 31)); d.line([0, y - 4, W, y - 4], fill=LINE)
    x = 8
    for k, label in items:
        if k in ("X", "O", "S", "T"):
            btn(d, x, y, k); x += 11
        else:
            text(im, x, y, k, WHITE); x += tw(k) + 3
        text(im, x, y, label, GREY); x += tw(label) + 16

# ---- stand-in thumbnails (240x136, like Sidequest .THM) ----
def grad(img, top, bot, y0=0, y1=None):
    d = ImageDraw.Draw(img); y1 = y1 or img.height
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        d.line([0, y, img.width, y], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)))

def hills(d, base, amp, freq, phase, col, w=240, h=136):
    pts = [(0, h)] + [(x, base + amp * math.sin(x * freq + phase) + amp * 0.5 * math.sin(x * freq * 2.3 + phase)) for x in range(0, w + 4, 4)] + [(w, h)]
    d.polygon(pts, fill=col)

def thumb_town():
    im = Image.new("RGB", (240, 136)); grad(im, (60, 40, 90), (240, 140, 70), 0, 96); d = ImageDraw.Draw(im)
    d.ellipse([170, 50, 196, 76], fill=(255, 210, 120))
    hills(d, 82, 6, 0.03, 1, (70, 50, 70));
    for i, (x, hgt) in enumerate([(20, 26), (52, 34), (90, 22), (124, 30), (160, 38), (200, 24)]):
        d.rectangle([x, 104 - hgt, x + 26, 110], fill=(40, 28, 40)); d.polygon([(x - 3, 104 - hgt), (x + 13, 92 - hgt), (x + 29, 104 - hgt)], fill=(30, 20, 32))
        d.rectangle([x + 9, 110 - hgt // 2, x + 15, 116 - hgt // 2], fill=(255, 200, 90))
    box(d, 0, 110, 240, 26, (52, 40, 44))
    d.rectangle([112, 112, 117, 124], fill=(200, 40, 40)); d.rectangle([111, 106, 118, 112], fill=(220, 70, 50))  # small red-haired figure
    return im

def thumb_fields():
    im = Image.new("RGB", (240, 136)); grad(im, (90, 160, 230), (190, 225, 250), 0, 90); d = ImageDraw.Draw(im)
    for cx, cy in [(40, 24), (150, 34), (210, 16)]:
        for k in range(3): d.ellipse([cx + k * 12 - 14, cy - 6 + (k % 2) * 3, cx + k * 12 + 10, cy + 8], fill=(250, 250, 255))
    d.ellipse([60, 44, 120, 58], fill=(200, 90, 60)); d.rectangle([84, 58, 96, 64], fill=(120, 60, 40))  # airship stand-in
    hills(d, 92, 8, 0.02, 0, (80, 150, 80)); hills(d, 108, 6, 0.035, 2, (60, 120, 60))
    d.line([0, 124, 240, 116], fill=(190, 170, 120), width=5)
    return im

def thumb_coast():
    im = Image.new("RGB", (240, 136)); grad(im, (120, 200, 240), (220, 240, 250), 0, 70); d = ImageDraw.Draw(im)
    grad(im, (40, 120, 190), (90, 180, 220), 70, 110); d = ImageDraw.Draw(im)
    for y in range(74, 108, 7): d.line([random.randint(0, 120), y, random.randint(130, 240), y], fill=(170, 220, 240))
    d.polygon([(0, 136), (0, 104), (90, 112), (240, 100), (240, 136)], fill=(236, 214, 160))
    d.rectangle([180, 60, 186, 104], fill=(120, 80, 50)); d.ellipse([162, 46, 206, 66], fill=(60, 150, 70))
    return im

def thumb_night():
    im = Image.new("RGB", (240, 136)); grad(im, (6, 18, 20), (20, 60, 52)); d = ImageDraw.Draw(im)
    d.ellipse([168, 14, 204, 50], fill=(190, 240, 150))
    random.seed(3)
    for i in range(14):
        x = i * 18 - 6; hgt = random.randint(30, 90)
        d.rectangle([x, 136 - hgt, x + 15, 136], fill=(10, 24, 26))
        for wy in range(136 - hgt + 5, 130, 9):
            for wx in (x + 3, x + 9):
                if random.random() < 0.3: d.rectangle([wx, wy, wx + 2, wy + 3], fill=(120, 200, 140))
    return im

def thumb_snow():
    im = Image.new("RGB", (240, 136)); grad(im, (70, 90, 150), (180, 190, 220), 0, 100); d = ImageDraw.Draw(im)
    d.polygon([(10, 110), (80, 34), (150, 110)], fill=(230, 236, 248)); d.polygon([(100, 110), (170, 50), (240, 110)], fill=(205, 214, 236))
    d.polygon([(64, 52), (80, 34), (96, 52), (86, 48), (80, 56), (72, 48)], fill=(255, 255, 255))
    box(d, 0, 108, 240, 28, (240, 244, 252))
    return im

random.seed(7)
GAMES = [
    dict(title="Ys: The Oath in Felghana", short="Felghana", thumb=thumb_town(), journey="Town of Redmont",
         played="14h 22m", em=(14, 89), ago="2 days ago", suspend=True, level="Lv 23"),
    dict(title="Trails in the Sky SC", short="Trails SC", thumb=thumb_fields(), journey="Ruan - Jenis Academy",
         played="31h 05m", em=(1, 3), ago="6 days ago", suspend=False),
    dict(title="Shining Hearts", short="Shining H.", thumb=thumb_coast(), journey="Windaria Village",
         played="3h 40m", em=None, ago="12 days ago", suspend=True),
    dict(title="Persona 3 Portable", short="Persona 3P", thumb=thumb_night(), journey="Tartarus - Thebel",
         played="22h 51m", em=(3, 9), ago="3 weeks ago", suspend=False),
    dict(title="Lunar: Silver Star Harmony", short="Lunar SSH", thumb=thumb_snow(), journey="Saith - White Dragon Cave",
         played="9h 12m", em=(0, 4), ago="1 month ago", suspend=False),
]

def titlebar(im, d, left, right="Tue 24 Nov 14:05   BATT 82%"):
    box(d, 0, 0, W, 14, (22, 26, 34)); d.line([0, 14, W, 14], fill=LINE)
    text(im, 6, 3, left, AMBER); text(im, W - 6 - tw(right), 3, right, GREY)

def wrap(s, n):
    out, line = [], ""
    for w in s.split():
        if len(line) + len(w) + (1 if line else 0) > n: out.append(line); line = w
        else: line = (line + " " + w) if line else w
    if line: out.append(line)
    return out

def main_screen(sel=0):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    titlebar(im, d, "SHELF 1.0")
    g = GAMES[sel]
    # hero card
    box(d, 6, 20, 468, 144, CARD, LINE)
    im.paste(g["thumb"], (10, 24)); d.rectangle([9, 23, 250, 160], outline=(60, 68, 84))
    if g["suspend"]:
        box(d, 14, 146, 96, 11, (0, 0, 0)); text(im, 17, 148, "SUSPENDED", AMBER)
    x = 260; y = 28
    for ln in wrap(g["title"], 26): text(im, x, y, ln, WHITE); y += 10
    y += 4
    text(im, x, y, "Last journey entry", GREY); y += 10
    for ln in wrap(g["journey"], 26)[:2]: text(im, x, y, ln, RED); y += 10
    y += 6
    text(im, x, y, "Played", GREY); text(im, x + 72, y, g["played"], WHITE); y += 12
    text(im, x, y, "Last", GREY); text(im, x + 72, y, g["ago"], WHITE); y += 12
    text(im, x, y, "Emblem", GREY)
    if g["em"]:
        a, b = g["em"]; s = "%d / %d" % (a, b); text(im, x + 72, y, s, GOLD); y += 11
        bar(d, x, y, 204, 4, a / b, GOLD); y += 10
    else:
        text(im, x + 72, y, "no game file", DIM); y += 14
    if g["suspend"]:
        y += 2; text(im, x, y, "Suspend", GREY); text(im, x + 72, y, "Sun 21:40", AMBER)
    if g["suspend"]:
        btn(d, x, 148, "X"); text(im, x + 12, 148, "Resume suspended game", AMBER)
    else:
        btn(d, x, 148, "X"); text(im, x + 12, 148, "Start game", WHITE)
    # shelf row
    text(im, 8, 170, "PLAYING", WHITE); text(im, 8 + tw("PLAYING "), 170, "(5)", GREY)
    tabs = ["Playing", "All 109", "Finished"]; tx = W - 16
    for t in reversed(tabs): tx -= tw(t) + 12
    text(im, tx - 20, 170, "L", DIM)
    for i, t in enumerate(tabs):
        text(im, tx, 170, t, AMBER if i == 0 else GREY)
        if i == 0: d.line([tx, 179, tx + tw(t) - 1, 179], fill=AMBER)
        tx += tw(t) + 12
    text(im, tx - 4, 170, "R", DIM)
    cw, ch, gap, cx, cy = 86, 49, 7, 8, 183
    for i, gg in enumerate(GAMES):
        x0 = cx + i * (cw + gap)
        t = gg["thumb"].resize((cw, ch), Image.BOX)
        im.paste(t, (x0, cy))
        d.rectangle([x0 - 2, cy - 2, x0 + cw + 1, cy + ch + 1], outline=AMBER if i == sel else (52, 60, 74), width=2 if i == sel else 1)
        if gg["suspend"]: box(d, x0 + cw - 10, cy + 2, 8, 8, (0, 0, 0)); d.rectangle([x0 + cw - 8, cy + 4, x0 + cw - 7, cy + 7], fill=AMBER); d.rectangle([x0 + cw - 5, cy + 4, x0 + cw - 4, cy + 7], fill=AMBER)
        text(im, x0, cy + ch + 4, gg["short"][:10], WHITE if i == sel else GREY)
        text(im, x0, cy + ch + 13, gg["played"], DIM)
    keybar(im, d, [("X", "Play"), ("S", "Journey & Emblems"), ("T", "Sort"), ("O", "Exit")])
    return im

JOURNEY = [("Sun Nov 22 21:40", "Town of Redmont", "14h 22m"),
           ("Sun Nov 22 20:05", "Ballacetine Castle", "12h 58m"),
           ("Sat Nov 21 23:12", "Illburns Ruins", "10h 31m"),
           ("Sat Nov 21 19:48", "Tigray Quarry", "7h 04m"),
           ("Sat Nov 21 18:20", "Elbereth Crossing", "5h 47m"),
           ("Fri Nov 20 22:30", "Town of Redmont", "3h 15m"),
           ("Fri Nov 20 20:02", "Port of Redmont", "0h 41m")]
EMBLEMS = [(True, "The Crystalline Entity", "Nov 22"), (True, "Proficient Plunderer", "Nov 22"),
           (True, "Adol the Shipwrecker", "Nov 21"), (True, "Of Truth and Light", "Nov 21"),
           (True, "For a Job Well Done", "Nov 20"),
           (False, "The Three Flying Succubi", ""), (False, "Guardian of the Icy Depths", ""),
           (False, "Adol the Red", "")]

def info_screen():
    im = main_screen(0).point(lambda v: v * 35 // 100); d = ImageDraw.Draw(im)
    box(d, 12, 20, 456, 232, (24, 28, 36), AMBER)
    g = GAMES[0]
    text(im, 22, 28, g["title"], WHITE); s = g["played"] + " played"; text(im, 458 - tw(s), 28, s, GREY)
    d.line([20, 40, 460, 40], fill=LINE); d.line([240, 46, 240, 236], fill=LINE)
    # left: journey
    text(im, 22, 48, "JOURNEY", RED); s = "18 entries"; text(im, 232 - tw(s), 48, s, DIM)
    y = 64
    for date, place, t in JOURNEY:
        text(im, 22, y, date, RED); text(im, 232 - tw(t), y, t, DIM); y += 10
        text(im, 30, y, place, WHITE); y += 14
    # right: emblems
    a, b = g["em"]
    text(im, 250, 48, "EMBLEM", GOLD); s = "%d / %d" % (a, b); text(im, 458 - tw(s), 48, s, GOLD)
    bar(d, 250, 58, 208, 3, a / b, GOLD)
    y = 68
    for got, name, when in EMBLEMS:
        if not got: continue
        d.ellipse([250, y, 257, y + 7], fill=GOLD); text(im, 262, y, name[:24], WHITE)
        text(im, 262, y + 10, "Unlocked " + when, DIM); y += 24
    y += 4; d.line([250, y, 458, y], fill=LINE); y += 8
    d.ellipse([250, y, 257, y + 7], outline=DIM); text(im, 262, y, "%d still locked" % (b - a), DIM)
    keybar(im, d, [("L/R", "Page"), ("X", "Play"), ("O", "Back")])
    return im

if __name__ == "__main__":
    for name, im in [("shelf-main", main_screen()), ("shelf-info", info_screen())]:
        im.save(name + ".png"); im.resize((W * 2, H * 2), Image.NEAREST).save(name + "-2x.png")
    print("ok")
