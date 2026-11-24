#!/usr/bin/env python3
"""Shelf render 2: every screen and popup. Stand-in pictures only (no game art).
Needs render_shelf.py (helpers) and msx.bin beside it."""
import hashlib
from PIL import Image, ImageDraw, ImageFilter
import render_shelf as R
from render_shelf import (text, tw, box, bar, btn, wrap, W, H, BG, CARD, LINE, WHITE, GREY, DIM,
                          AMBER, RED, GOLD, GREEN, BLUE, PINK)

GAMES = R.GAMES  # Playing tab: Felghana, Trails SC, Shining Hearts, Persona 3 Portable, Lunar SSH

# ---------- stand-in ICON0 (real one: PSP_GAME/ICON0.PNG inside the ISO, 144x80) ----------
def hue(name):
    h = hashlib.md5(name.encode()).digest()
    return (40 + h[0] % 150, 40 + h[1] % 150, 40 + h[2] % 150), (h[3] % 80, h[4] % 80, h[5] % 80)

def icon(lines, name, w=144, h=80):
    a, b = hue(name)
    im = Image.new("RGB", (w, h)); R.grad(im, a, b); d = ImageDraw.Draw(im)
    d.ellipse([w * 0.55, -h * 0.3, w * 1.2, h * 0.9], fill=tuple(min(255, c + 30) for c in a))
    y = h // 2 - len(lines) * 5 + (2 if h < 60 else 0)
    for ln in lines:
        x = (w - tw(ln)) // 2
        text(im, x + 1, y + 1, ln, (0, 0, 0)); text(im, x, y, ln, WHITE); y += 10
    return im

ALL = [  # (full title, 2-line stand-in label, status) status: None / playing / suspend / finished
    (".hack//Link", [".hack", "Link"], None), ("7th Dragon 2020", ["7th", "Dragon"], None),
    ("7th Dragon 2020-II", ["7th Drgn", "2020-II"], None), ("Arms Heart", ["Arms", "Heart"], None),
    ("Blaze Union", ["Blaze", "Union"], None), ("Blazing Souls Accelate", ["Blazing", "Souls"], None),
    ("Corpse Party", ["Corpse", "Party"], None), ("Corpse Party: Sweet Sachiko's...", ["Corpse P", "Sachiko"], None),
    ("Crimson Gem Saga", ["Crimson", "Gem Saga"], None), ("Crisis Core: Final Fantasy VII", ["Crisis", "Core"], "finished"),
    ("Danganronpa", ["Dangan-", "ronpa"], None), ("Densha de Go! Chuuousen", ["Densha", "Chuuou"], None),
    ("Densha de Go! Osaka", ["Densha", "Osaka"], None), ("Densha de Go! Tokaido", ["Densha", "Tokaido"], None),
    ("Densha de Go! Yamanote", ["Densha", "Yamanote"], None), ("Digimon World Re:Digitize", ["Digimon", "ReDigit"], None),
    ("Disgaea 2: Dark Hero Days", ["Disgaea", "2 DHD"], "playing"), ("Disgaea: Afternoon of Darkness", ["Disgaea", "AoD"], None),
    ("Dissidia 012", ["Dissidia", "012"], None), ("Elminage III", ["Elminage", "III"], None),
    ("Fate/Extra", ["Fate/", "Extra"], "suspend"), ("Fate/Extra CCC", ["Fate/Ext", "CCC"], None),
    ("Final Fantasy III", ["Final", "Fant III"], None), ("Final Fantasy IV Complete", ["Final", "Fant IV"], "finished"),
]

# ---------- shared chrome: tabs live in the title bar on every screen ----------
def titlebar(im, d, tab):
    box(d, 0, 0, W, 15, (22, 26, 34)); d.line([0, 15, W, 15], fill=LINE)
    text(im, 6, 4, "SHELF", AMBER)
    x = 6 + tw("SHELF") + 14
    text(im, x, 4, "L", DIM); x += 14
    for t in ["Playing", "All 109", "Finished"]:
        on = t.startswith(tab)
        text(im, x, 4, t, AMBER if on else GREY)
        if on: d.line([x, 13, x + tw(t) - 1, 13], fill=AMBER)
        x += tw(t) + 12
    text(im, x - 2, 4, "R", DIM)
    s = "14:05  BATT 82%"; text(im, W - 6 - tw(s), 4, s, GREY)

def keybar(im, d, items, y=260):
    box(d, 0, y - 4, W, H - y + 4, (20, 24, 31)); d.line([0, y - 4, W, y - 4], fill=LINE)
    x = 6
    for k, label in items:
        if k in ("X", "O", "S", "T"): btn(d, x, y, k); x += 10
        else: text(im, x, y, k, WHITE); x += tw(k) + 3
        text(im, x, y, label, GREY); x += tw(label) + 10

KEYS_HOME = [("X", "Play"), ("S", "Info"), ("T", "Sort"), ("START", "Game"), ("SELECT", "Settings"), ("O", "Exit")]

def dim(im, f=35): return im.point(lambda v: v * f // 100)

def pause_badge(d, x, y):
    box(d, x, y, 9, 9, (0, 0, 0)); d.rectangle([x + 2, y + 2, x + 3, y + 6], fill=AMBER); d.rectangle([x + 5, y + 2, x + 6, y + 6], fill=AMBER)
def check_badge(d, x, y):
    box(d, x, y, 9, 9, (0, 0, 0)); d.line([x + 1, y + 4, x + 3, y + 6, x + 7, y + 1], fill=GREEN, width=2)
def dot_badge(d, x, y):
    box(d, x, y, 9, 9, (0, 0, 0)); d.ellipse([x + 2, y + 2, x + 6, y + 6], fill=WHITE)

# ---------- Playing tab ----------
P3P_ICON = icon(["Persona 3", "Portable"], "Persona 3 Portable")

def hero_picture(g):
    """Sidequest picture if the game has one; else PIC1 backdrop with ICON0 on top."""
    if g["suspend"] or g.get("state"):
        return g["thumb"]
    pic = dim(g["thumb"].filter(ImageFilter.GaussianBlur(2)), 55)  # stand-in PIC1, darkened
    ic = g.get("icon") or icon(wrap(g["title"], 9)[:2], g["title"])
    pic.paste(ic, ((240 - 144) // 2, (136 - 80) // 2))
    return pic

def playing(sel=0):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    titlebar(im, d, "Playing")
    g = GAMES[sel]
    box(d, 6, 20, 468, 144, CARD, LINE)
    im.paste(hero_picture(g), (10, 24)); d.rectangle([9, 23, 250, 160], outline=(60, 68, 84))
    tag = "SUSPENDED" if g["suspend"] else ("STATE 2" if g.get("state") else "ICON0 + PIC1")
    box(d, 14, 146, tw(tag) + 6, 11, (0, 0, 0)); text(im, 17, 148, tag, AMBER if g["suspend"] else GREY)
    x, y = 260, 28
    for ln in wrap(g["title"], 26): text(im, x, y, ln, WHITE); y += 10
    y += 4; text(im, x, y, "Last journey entry", GREY); y += 10
    for ln in wrap(g["journey"], 26)[:2]: text(im, x, y, ln, RED); y += 10
    y += 6
    text(im, x, y, "Played", GREY); text(im, x + 72, y, g["played"], WHITE); y += 12
    text(im, x, y, "Last", GREY); text(im, x + 72, y, g["ago"], WHITE); y += 12
    text(im, x, y, "Emblem", GREY)
    if g["em"]:
        a, b = g["em"]; text(im, x + 72, y, "%d / %d" % (a, b), GOLD); y += 11; bar(d, x, y, 204, 4, a / b, GOLD); y += 10
    else: text(im, x + 72, y, "no game file", DIM); y += 14
    if g["suspend"]: y += 2; text(im, x, y, "Suspend", GREY); text(im, x + 72, y, "Sun 21:40", AMBER)
    btn(d, x, 148, "X"); text(im, x + 12, 148, "Resume suspended game" if g["suspend"] else "Start game", AMBER if g["suspend"] else WHITE)
    text(im, 8, 170, "PLAYING", WHITE); text(im, 8 + tw("PLAYING "), 170, "(5)", GREY)
    s = "Last played"; btn(d, W - 8 - tw(s) - 11, 170, "T"); text(im, W - 8 - tw(s), 170, s, GREY)
    cw, ch, gap, cx, cy = 86, 49, 7, 8, 183
    for i, gg in enumerate(GAMES):
        x0 = cx + i * (cw + gap)
        if gg["suspend"] or gg.get("state"):
            im.paste(gg["thumb"].resize((cw, ch), Image.BOX), (x0, cy))
        else:
            box(d, x0, cy, cw, ch, (10, 12, 16))
            ic = icon(gg["lab"], gg["title"], 72, 40)
            im.paste(ic, (x0 + 7, cy + 4))
        d.rectangle([x0 - 2, cy - 2, x0 + cw + 1, cy + ch + 1], outline=AMBER if i == sel else (52, 60, 74), width=2 if i == sel else 1)
        if gg["suspend"]: pause_badge(d, x0 + cw - 11, cy + 2)
        text(im, x0, cy + ch + 4, gg["short"][:10], WHITE if i == sel else GREY)
        text(im, x0, cy + ch + 13, gg["played"], DIM)
    keybar(im, d, KEYS_HOME)
    return im

# Trails SC has a state slot picture, P3P and Lunar have no Sidequest pictures at all
GAMES[1]["state"] = True
GAMES[3]["icon"] = P3P_ICON
GAMES[3]["lab"] = ["Persona", "3 Port."]
GAMES[4]["lab"] = ["Lunar", "SSH"]

# ---------- All tab ----------
def all_tab(sel=8):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    titlebar(im, d, "All")
    cw, chh, gx, gy, x0, y0 = 72, 40, 6, 6, 8, 22
    for i, (title, lab, st) in enumerate(ALL):
        c, r = i % 6, i // 6
        x, y = x0 + c * (cw + gx + 1), y0 + r * (chh + gy)
        im.paste(icon(lab, title, cw, chh), (x, y))
        d.rectangle([x - 2, y - 2, x + cw + 1, y + chh + 1], outline=AMBER if i == sel else (44, 52, 64), width=2 if i == sel else 1)
        if st == "suspend": pause_badge(d, x + cw - 10, y + 1)
        if st == "finished": check_badge(d, x + cw - 10, y + 1)
        if st == "playing": dot_badge(d, x + cw - 10, y + 1)
    # info strip for the selected game
    y = 210
    box(d, 6, y, 468, 42, CARD, LINE)
    t, _, st = ALL[sel]
    text(im, 14, y + 6, t, WHITE); s = "Name A-Z   9 of 109"; text(im, 466 - tw(s), y + 6, s, GREY)
    text(im, 14, y + 18, "Not started", GREY); text(im, 14 + tw("Not started") + 24, y + 18, "Emblem", GREY)
    text(im, 14 + tw("Not started") + 24 + 64, y + 18, "no game file", DIM)
    lx = 14
    for f, lab in [(dot_badge, "playing"), (pause_badge, "suspended"), (check_badge, "finished")]:
        f(d, lx, y + 28); text(im, lx + 12, y + 29, lab, DIM); lx += 12 + tw(lab) + 14
    keybar(im, d, [("X", "Play"), ("S", "Info"), ("T", "Sort"), ("START", "Game"), ("SELECT", "Settings"), ("O", "Exit")])
    return im

# ---------- Finished tab ----------
FINISHED = [("Final Fantasy IV Complete", ["Final", "Fant IV"], "Nov 17", "46h 03m", (22, 29), "Lunar Subterrane"),
            ("Ys I & II Chronicles", ["Ys I&II", "Chron."], "Nov 9", "21h 40m", (12, 12), "Shrine of Solomon"),
            ("Crisis Core: Final Fantasy VII", ["Crisis", "Core"], "Oct 30", "38h 12m", None, "Midgar Wasteland")]

def finished_tab(sel=0):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    titlebar(im, d, "Finished")
    text(im, 8, 22, "FINISHED", WHITE); text(im, 8 + tw("FINISHED "), 22, "(3)", GREY)
    s = "Date finished"; btn(d, W - 8 - tw(s) - 11, 22, "T"); text(im, W - 8 - tw(s), 22, s, GREY)
    y = 36
    for i, (t, lab, when, hrs, em, last) in enumerate(FINISHED):
        box(d, 6, y, 468, 52, CARD if i != sel else (34, 40, 52), AMBER if i == sel else LINE)
        im.paste(icon(lab, t, 72, 40), (12, y + 6)); check_badge(d, 12 + 72 - 10, y + 7)
        text(im, 94, y + 6, t, WHITE); s = "Finished " + when; text(im, 466 - tw(s), y + 6, s, GREEN)
        text(im, 94, y + 18, "Played", GREY); text(im, 94 + 56, y + 18, hrs, WHITE)
        text(im, 94 + 136, y + 18, "Emblem", GREY)
        if em: text(im, 94 + 192, y + 18, "%d / %d" % em, GOLD); bar(d, 94 + 192 + tw("%d / %d" % em) + 8, y + 20, 80, 4, em[0] / em[1], GOLD)
        else: text(im, 94 + 192, y + 18, "no game file", DIM)
        text(im, 94, y + 31, "Last entry", GREY); text(im, 94 + 88, y + 31, last, RED)
        y += 58
    text(im, 8, y + 6, "Games land here when you pick 'Mark as finished' (START).", DIM)
    keybar(im, d, [("X", "Play"), ("S", "Info"), ("T", "Sort"), ("START", "Game"), ("SELECT", "Settings"), ("O", "Exit")])
    return im

# ---------- Square: journey & emblems ----------
def info(base, empty=False):
    im = dim(base); d = ImageDraw.Draw(im)
    box(d, 12, 20, 456, 232, (24, 28, 36), AMBER)
    if not empty:
        g = GAMES[0]; title, played = g["title"], g["played"] + " played"
    else:
        title, played = "Crimson Gem Saga", "not started"
    text(im, 22, 28, title, WHITE); text(im, 458 - tw(played), 28, played, GREY)
    d.line([20, 40, 460, 40], fill=LINE); d.line([240, 46, 240, 236], fill=LINE)
    text(im, 22, 48, "JOURNEY", RED); text(im, 250, 48, "EMBLEM", GOLD)
    if empty:
        text(im, 232 - tw("0 entries"), 48, "0 entries", DIM)
        for i, ln in enumerate(["No entries yet.", "", "Checkpoint writes a", "line here each time", "a save shows a new", "place."]):
            text(im, 30, 70 + i * 11, ln, DIM)
        for i, ln in enumerate(["No Emblem file for", "this game.", "", "Games with a file in", "ms0:/EMBLEM/games/", "show their list here."]):
            text(im, 258, 70 + i * 11, ln, DIM)
        icn = icon(["Crimson", "Gem Saga"], "Crimson Gem Saga")
        im.paste(icn, (240 - 72, 156)); d.rectangle([240 - 73, 155, 240 + 72, 236], outline=LINE)
        text(im, 240 + 80, 228, "ICON0", DIM)
    else:
        text(im, 232 - tw("18 entries"), 48, "18 entries", DIM)
        y = 64
        for date, place, t in R.JOURNEY:
            text(im, 22, y, date, RED); text(im, 232 - tw(t), y, t, DIM); y += 10
            text(im, 30, y, place, WHITE); y += 14
        a, b = g["em"]; s = "%d / %d" % (a, b); text(im, 458 - tw(s), 48, s, GOLD); bar(d, 250, 58, 208, 3, a / b, GOLD)
        y = 68
        for got, name, when in R.EMBLEMS:
            if not got: continue
            d.ellipse([250, y, 257, y + 7], fill=GOLD); text(im, 262, y, name[:24], WHITE)
            text(im, 262, y + 10, "Unlocked " + when, DIM); y += 24
        y += 4; d.line([250, y, 458, y], fill=LINE); y += 8
        d.ellipse([250, y, 257, y + 7], outline=DIM); text(im, 262, y, "%d still locked" % (b - a), DIM)
    keybar(im, d, [("L/R", "Page"), ("X", "Play"), ("O", "Back")])
    return im

# ---------- popups ----------
def popup(base, title, rows, cur, hint, w=232, note=None):
    im = dim(base); d = ImageDraw.Draw(im)
    h = 30 + len(rows) * 16 + (14 if note else 0)
    x0, y0 = (W - w) // 2, (H - 16 - h) // 2
    box(d, x0, y0, w, h, (24, 28, 36), AMBER)
    text(im, x0 + 10, y0 + 8, title, AMBER); d.line([x0 + 6, y0 + 20, x0 + w - 7, y0 + 20], fill=LINE)
    y = y0 + 26
    for i, (label, val, col) in enumerate(rows):
        if i == cur: box(d, x0 + 4, y - 3, w - 8, 14, (50, 58, 74))
        text(im, x0 + 12, y, label, col or (WHITE if i == cur else GREY))
        if val: text(im, x0 + w - 12 - tw(val), y, val, AMBER if i == cur else GREY)
        y += 16
    if note: text(im, x0 + 12, y + 2, note, DIM)
    keybar(im, d, hint)
    return im

def sort_popup():
    rows = [("Last played", "*", None), ("Name A-Z", "", None), ("Time played", "", None),
            ("Emblem progress", "", None), ("Recently added", "", None)]
    return popup(playing(0), "SORT  Playing", rows, 0, [("X", "Choose"), ("O", "Cancel")], note="Each tab keeps its own")

def game_popup():
    rows = [("Mark as finished", "", None), ("Hide from Playing", "", None),
            ("Picture", "< Suspend >", None), ("Journey & Emblems", "", None), ("Cancel", "", None)]
    return popup(playing(0), "Ys: The Oath in Felghana", rows, 2, [("X", "Choose"), ("<>", "Change"), ("O", "Back")], w=260,
                 note="Also: State 1-5, Icon")

def settings():
    im = dim(playing(0)); d = ImageDraw.Draw(im)
    box(d, 40, 22, 400, 226, (24, 28, 36), AMBER)
    text(im, 52, 30, "SETTINGS", AMBER); s = "Shelf 1.0  built Nov 24 2026"; text(im, 428 - tw(s), 30, s, DIM)
    d.line([46, 42, 434, 42], fill=LINE)
    rows = [("Open on tab", "< Playing >"), ("Playing picture", "< Suspend, else icon >"),
            ("Show PS1 games", "< Off >"), ("Show minis", "< Off >"), ("Show hidden games", "< Off >"),
            ("Rescan library", "X"), ("Rebuild pictures", "X")]
    y = 52
    for i, (k, v) in enumerate(rows):
        if i == 0: box(d, 44, y - 3, 392, 14, (50, 58, 74))
        text(im, 56, y, k, WHITE if i == 0 else GREY)
        if v == "X": btn(d, 424 - 8, y, "X")
        else: text(im, 424 - tw(v), y, v, AMBER if i == 0 else GREY)
        y += 18
    d.line([46, y, 434, y], fill=LINE); y += 8
    for ln in ["Last scan  Tue 24 Nov 14:02", "109 ISO games   15 PS1   5 minis", "Pictures cached in ms0:/SHELF/CACHE/"]:
        text(im, 56, y, ln, DIM); y += 11
    keybar(im, d, [("<>", "Change"), ("X", "Run"), ("O", "Back")])
    return im

def launch():
    g = GAMES[0]
    pic = g["thumb"].resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(3))  # stand-in PIC1
    im = dim(pic, 45); d = ImageDraw.Draw(im)
    ic = icon(["Ys: The Oath", "in Felghana"], "Ys: The Oath in Felghana")
    im.paste(ic, ((W - 144) // 2, 60)); d.rectangle([(W - 144) // 2 - 1, 59, (W + 144) // 2, 140], outline=(0, 0, 0))
    for i, (s, col) in enumerate([(g["title"], WHITE), ("Resuming suspended game", AMBER),
                                   ("Sidequest will offer: Sun 21:40", GREY)]):
        text(im, (W - tw(s)) // 2, 154 + i * 14, s, col)
    bar(d, 160, 206, 160, 3, 0.4, AMBER)
    s = "PIC1 behind, ICON0 in front"; text(im, W - 6 - tw(s), H - 12, s, (110, 110, 110))
    return im

if __name__ == "__main__":
    shots = [("1-playing", playing(0)), ("2-playing-icon-fallback", playing(3)), ("3-all", all_tab()),
             ("4-finished", finished_tab()), ("5-info", info(playing(0))), ("6-info-not-started", info(all_tab(), True)),
             ("7-sort", sort_popup()), ("8-game-options", game_popup()), ("9-settings", settings()), ("10-launch", launch())]
    for n, im in shots:
        im.resize((W * 2, H * 2), Image.NEAREST).save("shelf2-%s.png" % n)
    print("ok", len(shots))
