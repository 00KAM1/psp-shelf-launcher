#!/usr/bin/env python3
"""Builds the Memory Stick the manual's pictures are drawn from, in ./ms0.

    python3 render/doc_stick.py <art folder> <Emblem games folder> [<states folder>]

<art folder> holds one folder per game, named like its ISO, with the ICON0.PNG,
PIC1.PNG and PARAM.SFO taken out of your own copy of that game, for example:

    7z e -o"art/<name>" "<name>.iso" PSP_GAME/ICON0.PNG PSP_GAME/PIC1.PNG PSP_GAME/PARAM.SFO

Each becomes a small ISO in ms0/ISO/PSP that Shelf reads like the real one. The
art itself is not in this repository: it belongs to the games' publishers.
What the plugins would have written (slots, journey, unlocks, marks) is example
data made up here.

<states folder> is optional: one folder per game, named like its ISO, holding
slot pictures taken from ms0:/SIDEQUEST/<game>/ (SUSPEND.THM, STATE1.THM ...).
With it Home shows a slot's picture, as it does on the PSP; without it, the
game's own picture. Those pictures are not in this repository either."""
import io, os, shutil, struct, sys
import pycdlib

ART, EMBLEM = sys.argv[1], sys.argv[2]
STATES = sys.argv[3] if len(sys.argv) > 3 else None
ROOT = "ms0"
shutil.rmtree(ROOT, ignore_errors=True)

def w(p, data):
    full = os.path.join(ROOT, p)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as f: f.write(data if isinstance(data, bytes) else data.encode())

def make_iso(files):
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3)
    iso.add_directory("/PSP_GAME"); iso.add_directory("/PSP_GAME/SYSDIR")
    for p, data in files.items(): iso.add_fp(io.BytesIO(data), len(data), "/" + p + ";1")
    out = io.BytesIO(); iso.write_fp(out); iso.close()
    return out.getvalue()

def sfo_get(b, key):
    magic, ver, kt, dt, n = struct.unpack_from("<4sIIII", b, 0)
    for i in range(n):
        ko, fmt, ln, mx, do = struct.unpack_from("<HHIII", b, 20 + 16 * i)
        if b[kt + ko:b.index(b"\0", kt + ko)].decode() == key:
            return b[dt + do:dt + do + ln].rstrip(b"\0").decode("utf-8", "replace")
    return ""

games = {}                                      # folder name -> (ms0 path, disc id)
for name in sorted(os.listdir(ART)):
    d = os.path.join(ART, name)
    if not os.path.isfile(os.path.join(d, "PARAM.SFO")) or not os.path.isfile(os.path.join(d, "ICON0.PNG")): continue
    files = {"PSP_GAME/PARAM.SFO": open(os.path.join(d, "PARAM.SFO"), "rb").read(),
             "PSP_GAME/ICON0.PNG": open(os.path.join(d, "ICON0.PNG"), "rb").read(),
             "PSP_GAME/SYSDIR/EBOOT.BIN": b"~PSP" + b"\0" * 300}
    if os.path.isfile(os.path.join(d, "PIC1.PNG")): files["PSP_GAME/PIC1.PNG"] = open(os.path.join(d, "PIC1.PNG"), "rb").read()
    w("ISO/PSP/%s.iso" % name, make_iso(files))
    games[name] = ("ms0:/ISO/PSP/%s.iso" % name, sfo_get(files["PSP_GAME/PARAM.SFO"], "DISC_ID").replace("-", ""))
print(len(games), "games")

def find(part):
    hit = [n for n in games if part in n]
    if len(hit) != 1: raise SystemExit("not exactly one game called %r: %r" % (part, hit))
    return games[hit[0]]

def mins(y, mo, d, h, mi):                      # minutes since 2000, as Shelf counts them
    import datetime
    return int((datetime.datetime(y, mo, d, h, mi) - datetime.datetime(2000, 1, 1)).total_seconds() // 60)

# ---- Sidequest: slots, with their pictures when a states folder was given ----
def slot(boot, saved, title, disc):
    h = bytearray(4096)
    struct.pack_into("<III", h, 0, 0x31535153, 4, 0x1000)
    h[100:100 + len(title)] = title.encode(); h[228:228 + len(disc)] = disc.encode()
    h[244:244 + len(saved)] = saved.encode(); h[276:276 + len(boot)] = boot.encode()
    return bytes(h) + b"\0" * 64

for part, folder, title, slots in (
        ("Ys The Oath In Felghana", "YS_THE_OATH_IN_FELGH_5696", "Ys: The Oath in Felghana", (("SUSPEND", "2026-11-23 21:40"),)),
        ("Silver Star Harmony", "SILVER_STAR_HARMONY__309F", "LUNAR: SILVER STAR HARMONY", (("STATE1", "2026-11-22 20:15"),)),
        ("Shining Hearts", "SHINING_HEARTS__T_EN_77B2", "Shining Hearts", (("STATE1", "2026-11-12 18:00"),))):
    path, disc = find(part)
    for s, when in slots:
        w("SIDEQUEST/%s/%s.SQS" % (folder, s), slot(path, when, title, disc))
        for name in (os.listdir(STATES) if STATES else ()):
            thm = os.path.join(STATES, name, s + ".THM")
            if part in name and os.path.isfile(thm): w("SIDEQUEST/%s/%s.THM" % (folder, s), open(thm, "rb").read())

# ---- Checkpoint: journey and time played -----------------------------------------------
def journey(part, secs, lines):
    path, disc = find(part)
    w("CHECKPOINT/JOURNEY/%s.TXT" % disc, "# Journey log of %s, kept by Checkpoint: date | time played | description | save\r\n" % disc +
      "".join("%s | %s | %s | %s_000\r\n" % (a, b, c, disc) for a, b, c in lines))
    w("CHECKPOINT/JOURNEY/%s.TIM" % disc, "%d\n" % secs)

journey("Ys The Oath In Felghana", 51720, (
    ("2026-11-20 20:02", "0h 41m", "Port of Redmont"), ("2026-11-20 22:30", "3h 15m", "Town of Redmont"),
    ("2026-11-21 18:20", "5h 47m", "Elderm Mountains"), ("2026-11-21 19:48", "7h 04m", "Tigray Quarry"),
    ("2026-11-21 23:12", "10h 31m", "Illburns Ruins"), ("2026-11-22 20:05", "12h 58m", "Valestein Castle"),
    ("2026-11-23 21:40", "14h 22m", "Town of Redmont")))
journey("Silver Star Harmony", 9000, (("2026-11-22 20:15", "2h 30m", "Burg"),))
journey("Persona 3 Portable", 82260, (("2026-11-03 22:10", "22h 51m", "Tartarus - Thebel"),))
journey("Crisis Core", 137520, (("2026-10-30 21:00", "38h 12m", "Midgar"),))
journey("Jeanne d'Arc", 30600, (("2026-11-08 19:30", "8h 30m", "Stage 12"),))

# ---- Emblem: the real game files, and example unlocks taken from them --------------------
for f in sorted(os.listdir(EMBLEM)):
    if f.endswith(".txt"): w("EMBLEM/games/" + f, open(os.path.join(EMBLEM, f), "rb").read())
def earn(part, picks):
    path, disc = find(part)
    rows = {}
    for line in open(os.path.join(EMBLEM, disc + ".txt"), encoding="utf-8", errors="replace"):
        p = [x.strip() for x in line.split("|")]
        if len(p) >= 4 and p[0].isdigit() and p[3] != "none": rows[int(p[0])] = (p[1], p[2])
    ids = sorted(rows)
    out = "# Achievements earned (%s), kept by Emblem\r\n" % disc
    for k, when in picks:
        i = ids[k]; out += "%d | %s | %s | %s pts\r\n" % (i, when, rows[i][0], rows[i][1])
    w("EMBLEM/earned/%s.txt" % disc, out)
earn("Ys The Oath In Felghana", ((0, "2026-11-20 20:10"), (1, "2026-11-20 20:30"), (2, "2026-11-21 19:10"), (3, "2026-11-21 20:44"),
                                 (4, "2026-11-22 20:31"), (5, "2026-11-22 21:02"), (6, "2026-11-23 21:15")))
earn("Persona 3 Portable", ((0, "2026-11-02 21:00"), (1, "2026-11-03 21:40")))
earn("Crisis Core", tuple((k, "2026-10-%02d 20:00" % (10 + k)) for k in range(9)))

# ---- homebrew, ports and PS1 games: names only (no pictures), so the category menu lists their folders ----
def sfo(entries):
    keys = sorted(entries)
    kt, dt, idx = b"", b"", []
    for k in keys:
        raw = entries[k].encode() + b"\0"
        mx = (len(raw) + 3) // 4 * 4
        idx.append(struct.pack("<HHIII", len(kt), 0x0204, len(raw), mx, len(dt)))
        kt += k.encode() + b"\0"; dt += raw.ljust(mx, b"\0")
    kt = kt.ljust((len(kt) + 3) // 4 * 4, b"\0")
    return struct.pack("<4sIIII", b"\0PSF", 0x101, 20 + 16 * len(keys), 20 + 16 * len(keys) + len(kt), len(keys)) + b"".join(idx) + kt + dt

def pbp(title, category):
    secs = [sfo({"TITLE": title, "CATEGORY": category}), b"", b"", b"", b"", b"", b"\x7fELF" + b"\0" * 64, b""]
    off, cur = [], 40
    for x in secs: off.append(cur); cur += len(x)
    return struct.pack("<4sI8I", b"\0PBP", 0x10000, *off) + b"".join(secs)

for folder, category, count in (("APPS", "MG", 10), ("PORTS", "MG", 5), ("PSX", "ME", 25)):
    for i in range(1, count + 1): w("PSP/GAME/%s/%s%02d/EBOOT.PBP" % (folder, folder, i), pbp("%s %d" % (folder.title(), i), category))

# ---- Shelf: marks and settings -------------------------------------------------------------
last = {"Ys The Oath In Felghana": mins(2026, 11, 23, 20, 55), "Silver Star Harmony": mins(2026, 11, 22, 19, 40),
        "Persona 3 Portable": mins(2026, 11, 3, 20, 0), "Jeanne d'Arc": mins(2026, 11, 8, 18, 0), "Shining Hearts": mins(2026, 11, 12, 17, 0),
        "Zero no Kiseki": mins(2026, 10, 12, 20, 0), "Tales of Eternia": mins(2026, 9, 30, 21, 0)}
done = {"Crisis Core": mins(2026, 10, 30, 21, 30), "Ys Seven": mins(2026, 8, 14, 22, 0),
        "Trails in the Sky (USA)": mins(2026, 7, 2, 23, 0), "Corpse Party (USA)": mins(2026, 6, 20, 1, 0)}
fav = ("Ys The Oath In Felghana", "Persona 3 Portable", "Jeanne d'Arc", "Zero no Kiseki", "Tales of Eternia", "Trails in the Sky (USA)",
       "Final Fantasy Tactics", "Valkyria Chronicles 2")
rows = "# Shelf: path | last started | finished | hidden | picture | favourite (times in minutes since 2000)\r\n"
for part in sorted(set(last) | set(done) | set(fav)):
    rows += "%s | %d | %d | 0 | -1 | %d\r\n" % (find(part)[0], last.get(part, 0), done.get(part, 0), 1 if part in fav else 0)
w("SHELF/games.txt", rows)
w("SHELF/config.txt", "pic_mode = %d\r\n" % (0 if STATES else 1))      # Home picture: the newest slot's when there are slot pictures

# ---- ARK-5, for the settings screens ---------------------------------------------------------
w("PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", "always, usbcharge, on\r\nalways, overclock, on\r\nalways, wpa2, on\r\nalways, highmem, off\r\n"
  "always, mscache, on\r\nalways, launcher, on\r\n")
w("PSP/SAVEDATA/ARK_01234/VBOOT.PBP", b"\0PBP" + b"\0" * 60)
w("SEPLUGINS/PLUGINS.TXT", "game, ms0:/SEPLUGINS/sidequest.prx, on\r\ngame, ms0:/SEPLUGINS/checkpoint.prx, on\r\n"
  "game, ms0:/SEPLUGINS/scanline.prx, on\r\ngame, ms0:/SEPLUGINS/emblem.prx, on\r\nalways, ms0:/SEPLUGINS/runtime.prx, on\r\n")
print("stick built")
