#!/usr/bin/env python3
"""Builds a fake Memory Stick in ./ms0 for Shelf's PC tests: real ISO / CSO / ZSO
images, PBP games of every kind (and the things that must not show: DLC, an update,
a UMD video, Shelf itself), and the files Sidequest, Checkpoint and Emblem write.
Pictures are drawn stand-ins (no game art). Run from tests/."""
import io, os, shutil, struct, sys, zlib
import pycdlib, lz4.block
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "render"))
import render_shelf as R
import render_shelf2 as R2

ROOT = "ms0"
shutil.rmtree(ROOT, ignore_errors=True)

def path(p):
    full = os.path.join(ROOT, p)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    return full

def png(img):
    b = io.BytesIO(); img.save(b, "PNG"); return b.getvalue()

def sfo(entries):
    keys = sorted(entries)
    kt, dt, idx = b"", b"", []
    for k in keys:
        v = entries[k]
        if isinstance(v, int):
            data, fmt, ln, mx = struct.pack("<I", v), 0x0404, 4, 4
        else:
            raw = v.encode() + b"\0"
            mx = (len(raw) + 3) // 4 * 4
            data, fmt, ln = raw.ljust(mx, b"\0"), 0x0204, len(raw)
        idx.append(struct.pack("<HHIII", len(kt), fmt, ln, mx, len(dt)))
        kt += k.encode() + b"\0"; dt += data
    kt = kt.ljust((len(kt) + 3) // 4 * 4, b"\0")
    head = struct.pack("<4sIIII", b"\0PSF", 0x101, 20 + 16 * len(keys), 20 + 16 * len(keys) + len(kt), len(keys))
    return head + b"".join(idx) + kt + dt

def make_iso(files):
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3)
    dirs = set()
    for p in files:
        parts = p.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            d = "/" + "/".join(parts[:i])
            if d not in dirs:
                iso.add_directory(d); dirs.add(d)
    for p, data in files.items():
        iso.add_fp(io.BytesIO(data), len(data), "/" + p + ";1")
    out = io.BytesIO(); iso.write_fp(out); iso.close()
    return out.getvalue()

def to_cso(data, lz4_mode=False):
    block = 2048
    n = (len(data) + block - 1) // block
    out, index = bytearray(), []
    pos = 24 + (n + 1) * 4
    for i in range(n):
        raw = data[i * block:(i + 1) * block].ljust(block, b"\0")
        if lz4_mode: comp = lz4.block.compress(raw, store_size=False)
        else:
            c = zlib.compressobj(9, zlib.DEFLATED, -15); comp = c.compress(raw) + c.flush()
        plain = len(comp) >= block
        index.append(pos | (0x80000000 if plain else 0))
        chunk = raw if plain else comp
        out += chunk; pos += len(chunk)
    index.append(pos)
    head = struct.pack("<4sIQIBB2x", b"ZISO" if lz4_mode else b"CISO", 24, len(data), block, 1, 0)
    return head + b"".join(struct.pack("<I", x) for x in index) + bytes(out)

def game_iso(title, disc, icon_lines, scene=None, boot_bin=False):
    files = {"PSP_GAME/PARAM.SFO": sfo({"TITLE": title, "DISC_ID": disc, "CATEGORY": "UG", "PARENTAL_LEVEL": 1}),
             "PSP_GAME/ICON0.PNG": png(R2.icon(icon_lines, title)),
             "PSP_GAME/SYSDIR/EBOOT.BIN": b"~PSP" + b"\0" * 300}
    if scene is not None: files["PSP_GAME/PIC1.PNG"] = png(scene.resize((480, 272)))
    if boot_bin: files["PSP_GAME/SYSDIR/BOOT.BIN"] = b"\x7fELF" + b"\1" * 60
    return make_iso(files)

def pbp(entries, icon=None, pic=None):
    secs = [sfo(entries), png(icon) if icon else b"", b"", b"", png(pic) if pic else b"", b"", b"\x7fELF" + b"\0" * 64, b""]
    off, cur = [], 40
    for s in secs: off.append(cur); cur += len(s)
    return struct.pack("<4sI8I", b"\0PBP", 0x10000, *off) + b"".join(secs)

def w(p, data, mode="wb"):
    with open(path(p), mode) as f: f.write(data)

# ---- ISOs ----------------------------------------------------------------------------
w("ISO/PSP/Ys The Oath in Felghana (USA) [Undub].iso", game_iso("Ys: The Oath in Felghana", "ULUS-10558", ["Ys: The Oath", "in Felghana"], R.thumb_town()))
w("ISO/PSP/Legend of Heroes Zero no Kiseki (Japan) [T-En Geofront v1.0].cso",
  to_cso(game_iso("英雄伝説　零の軌跡", "NPJH50311", ["Zero no", "Kiseki"], R.thumb_fields(), boot_bin=True)))
w("ISO/PSP/Shining Hearts (Japan) [T-En v1.2].zso", to_cso(game_iso("シャイニング・ハーツ", "NPJH50342", ["Shining", "Hearts"], R.thumb_coast()), lz4_mode=True))
w("ISO/PSP/Persona 3 Portable (USA) [Undub].iso", game_iso("Shin Megami Tensei: Persona 3 Portable", "ULUS10512", ["Persona 3", "Portable"], R.thumb_night()))
w("ISO/PSP/Crisis Core - Final Fantasy VII (EU) [Undub v1.0].iso", game_iso("CRISIS CORE -FINAL FANTASY VII-", "ULES01044", ["Crisis", "Core"]))
w("ISO/MINIS/Tetris (USA).iso", game_iso("TETRIS®", "NPUZ00003", ["Tetris"]))
w("ISO/Crimson Gem Saga (USA) [Undub v1.1].iso", game_iso("Crimson Gem Saga", "ULUS10469", ["Crimson", "Gem Saga"]))
w("ISO/VIDEO/Some Movie.iso", make_iso({"UMD_VIDEO/PLAYLIST.UMD": b"x" * 100}))        # not a game
w("ISO/PSP/readme.txt", b"not an image")

# ---- PSP/GAME ------------------------------------------------------------------------
w("PSP/GAME/PORTS/ZeldaPSP/EBOOT.PBP", pbp({"TITLE": "The Legend of Zelda: A Link to the Past", "CATEGORY": "MG", "DISC_ID": "UCJS10041"}, R2.icon(["Zelda", "LttP"], "zelda")))
w("PSP/GAME/PORTS/Snatc/EBOOT.PBP", pbp({"TITLE": "Snatcher", "CATEGORY": "MG"}))                      # no icon
w("PSP/GAME/PSX/Chrono Trigger/EBOOT.PBP", pbp({"TITLE": "Chrono Trigger", "CATEGORY": "ME", "DISC_ID": "SLUS01363"}, R2.icon(["Chrono", "Trigger"], "ct").resize((80, 80))))
w("PSP/GAME/NPUH10191/EBOOT.PBP", pbp({"TITLE": "The Legend of Heroes: Trails in the Sky SC", "CATEGORY": "EG", "DISC_ID": "NPUH10191"}, R2.icon(["Trails", "SC"], "sc"), R.thumb_fields()))
w("PSP/GAME/PSP5/EBOOT.PBP", pbp({"TITLE": "psp5", "CATEGORY": "MG"}, R2.icon(["psp5"], "psp5")))
w("PSP/GAME/SHELF/EBOOT.PBP", pbp({"TITLE": "Shelf", "CATEGORY": "MG"}))                              # Shelf itself
w("PSP/GAME/NPJH50332DL/PARAM.PBP", b"\0PBP" + b"\0" * 60)                                             # DLC
w("PSP/GAME/ULJS00318/PBOOT.PBP", pbp({"TITLE": "Update", "CATEGORY": "PG"}))                          # update only
w("PSP/GAME/UPDATER/EBOOT.PBP", pbp({"TITLE": "System update", "CATEGORY": "MSTKUPDATE"}))             # other category
w("PSP/GAME/%__SCE__OLD/EBOOT.PBP", b"x")                                                              # 1.50 folder

# ---- Sidequest -------------------------------------------------------------------------
def slot(boot, saved, title, disc):
    h = bytearray(4096)
    struct.pack_into("<III", h, 0, 0x31535153, 4, 0x1000)
    h[100:100 + len(title)] = title.encode(); h[228:228 + len(disc)] = disc.encode()
    h[244:244 + len(saved)] = saved.encode(); h[276:276 + len(boot)] = boot.encode()
    return bytes(h) + b"\0" * 64

def thm(img):
    img = img.convert("RGB").resize((240, 136))
    px = b"".join(struct.pack("<H", (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)) for r, g, b in img.getdata())
    return b"SQT1" + struct.pack("<HH", 240, 136) + px

ys = "ms0:/ISO/PSP/Ys The Oath in Felghana (USA) [Undub].iso"
w("SIDEQUEST/YS_THE_OATH_IN_FELGH_3A1F/SUSPEND.SQS", slot(ys, "2026-11-22 21:40", "Ys: The Oath in Felghana", "ULUS10558"))
w("SIDEQUEST/YS_THE_OATH_IN_FELGH_3A1F/SUSPEND.THM", thm(R.thumb_town()))
w("SIDEQUEST/YS_THE_OATH_IN_FELGH_3A1F/STATE2.SQS", slot(ys, "2026-11-21 19:00", "Ys: The Oath in Felghana", "ULUS10558"))
w("SIDEQUEST/YS_THE_OATH_IN_FELGH_3A1F/STATE2.THM", thm(R.thumb_snow()))
sh = "ms0:/ISO/PSP/Shining Hearts (Japan) [T-En v1.2].zso"
w("SIDEQUEST/SHINING_HEARTS__JAPA_77B2/SUSPEND.SQS", slot(sh, "2026-11-12 18:00", "Shining Hearts", "NPJH50342"))
w("SIDEQUEST/SHINING_HEARTS__JAPA_77B2/SUSPEND.THM", thm(R.thumb_coast()))
w("SIDEQUEST/sidequest.log", b"log")

# ---- Checkpoint journey -------------------------------------------------------------------
w("CHECKPOINT/JOURNEY/ULUS10558.TXT", (
    "# Journey log of ULUS10558, kept by Checkpoint: date | time played | description | save\r\n"
    "2026-11-20 20:02 | 0h 41m | Port of Redmont | ULUS10558_000\r\n"
    "2026-11-20 22:30 | 3h 15m | Town of Redmont | ULUS10558_000\r\n"
    "2026-11-21 18:20 | 5h 47m | Elbereth Crossing | ULUS10558_000\r\n"
    "2026-11-21 19:48 | 7h 04m | Tigray Quarry | ULUS10558_000\r\n"
    "2026-11-21 23:12 | 10h 31m | Illburns Ruins | ULUS10558_000\r\n"
    "garbage line\r\n"
    "2026-11-22 20:05 | 12h 58m | Ballacetine Castle | ULUS10558_000\r\n"
    "2026-11-22 21:40 | 14h 22m | Town of Redmont | ULUS10558_000\r\n").encode())
w("CHECKPOINT/JOURNEY/ULUS10558.TIM", b"51720\n")
w("CHECKPOINT/JOURNEY/NPJH50342.TXT", "2026-11-12 17:50 | 3h 40m | オカミ Lv12 Windaria Village | NPJH503420000\r\n".encode())
w("CHECKPOINT/JOURNEY/NPJH50342.TIM", b"13200\n")
w("CHECKPOINT/JOURNEY/ULUS10512.TXT", b"2026-11-03 22:10 | 22h 51m | Tartarus - Thebel | ULUS10512\r\n")
w("CHECKPOINT/JOURNEY/ULUS10512.TIM", b"82260\n")
w("CHECKPOINT/JOURNEY/ULES01044.TXT", b"2026-10-30 21:00 | 38h 12m | Midgar Wasteland | ULES01044\r\n")
w("CHECKPOINT/JOURNEY/ULES01044.TIM", b"137520\n")

# ---- Emblem -------------------------------------------------------------------------------
lines = ["# test file", "game ULUS10558 Ys: The Oath in Felghana", "level = u32 08BB9B30", "when main"]
for i in range(1, 96):
    lines.append("%d | Achievement %d | 5 | %s" % (i, i, "none" if i in (4, 5, 6, 13, 38, 95) else "level >= %d" % i))
w("EMBLEM/games/ULUS10558.txt", "\n".join(lines).encode())
w("EMBLEM/earned/ULUS10558.txt", (
    "# Achievements earned in Ys: The Oath in Felghana (ULUS10558), kept by Emblem\r\n"
    "3 | 2026-11-20 20:30 | For a Job Well Done | 5 pts\r\n"
    "7 | 2026-11-21 19:10 | Of Truth and Light | 5 pts\r\n"
    "57 | 2026-11-21 20:44 | Adol the Shipwrecker | 5 pts\r\n"
    "10 | 2026-11-22 20:31 | Proficient Plunderer | 5 pts\r\n"
    "9 | 2026-11-22 21:02 | The Crystalline Entity | 10 pts\r\n"
    "4 | 2026-11-22 21:03 | Not in the file any more | 5 pts\r\n"
    "1 | 2026-11-20 20:10 | The Creeping Shadow | 5 pts\r\n").encode())
w("EMBLEM/games/ULUS10512.txt", b"game ULUS10512 P3P\n12 | The Friggin Boss | 5 | u8 08E68435 bit 0 set\n14 | Hostel | 5 | u8 08E68435 bit 2 set\n15 | Desire | 5 | u8 08E68435 bit 3 set\n")
w("EMBLEM/earned/ULUS10512.txt", b"12 | 2026-11-02 21:00 | The Friggin Boss | 5 pts\r\n")

# ---- ARK-5 ----------------------------------------------------------------------------------
w("PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", (
    "# ARK settings\r\nalways, usbcharge, on\r\nalways, overclock, on\r\nalways, wpa2, on\r\nalways, highmem, off\r\n"
    "always, mscache, on\r\nalways, infernocache:lru, on\r\nalways, launcher, on\r\n"
    "ms0:/PSP/GAME/PORTS/MajoraPSP/EBOOT.PBP, highmem, on\r\nULUS10558, cpuclock:222, on\r\n").encode())
w("PSP/SAVEDATA/ARK_01234/VBOOT.PBP", pbp({"TITLE": "psp5", "CATEGORY": "MG"}))
w("SEPLUGINS/PLUGINS.TXT", (
    "game, ms0:/SEPLUGINS/sidequest.prx, on\r\ngame, ms0:/SEPLUGINS/checkpoint.prx, on\r\n"
    "game, ms0:/SEPLUGINS/scanline.prx, on\r\ngame, ms0:/SEPLUGINS/emblem.prx, on\r\n"
    "# a comment\r\nalways, ms0:/SEPLUGINS/OtherHud.prx, on\r\n"
    "ms0:/PSP/GAME/PORTS/MajoraPSP/EBOOT.PBP, ms0:/SEPLUGINS/sidequest.prx, off\r\n").encode())
print("stick built")
