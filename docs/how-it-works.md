# Shelf: how it works

Shelf is a user-mode homebrew program (`EBOOT.PBP`, built as a PRX, `PSP_FW_VERSION = 660`). It needs ARK-5 for launching games and for the few kernel calls it makes through `kuKernelCall`.

## Source layout

| File | Does |
|---|---|
| `src/app.c` | `main`: start-up order, the main loop, input, USB mode, the uptime count |
| `src/ui.c`, `src/ui.h` | Every screen and all input handling. No PSP calls except file I/O through `data.c`, so it runs unchanged in the PC tests |
| `src/gfx.c`, `src/text.c`, `src/jfont_data.c` | Drawing into a 32-bit frame in RAM; MSX 8x8 font plus a Japanese glyph table (Fusion Pixel), UTF-8 aware |
| `src/scan.c`, `src/iso.c`, `src/sfo.c`, `src/image.c` | Library scan: ISO / CSO / ZSO, PBP games, PS1, homebrew; titles and icons; the picture cache |
| `src/data.c` | Shelf's own files, and reading what Sidequest, Checkpoint and Emblem wrote |
| `src/arkcfg.c` | ARK's `SETTINGS.TXT` and `PLUGINS.TXT`; the launcher bookkeeping |
| `src/sys.c` | Everything that touches the system: launching, exit, power, USB, the PS button |
| `src/lodepng.c` | PNG decoding (third party) |
| `tools/` | `make_icons.py` (ICON0 / PIC1), `make_jfont.py` (the Japanese glyph table) |
| `tests/` | PC test harness |

## Drawing

The frame is drawn by the CPU into a 480x272 32-bit buffer in RAM (stride 512), copied to one of two VRAM pages (`sceGeEdramGetAddr`) and shown with `sceDisplaySetFrameBuf`. Nothing is drawn unless something changed.

## The library

`scan_library` walks `ms0:/ISO/` and `ms0:/PSP/GAME/`, including sub-folders. It keeps games, PS1 titles and homebrew, and leaves out DLC, updates, UMD video and Shelf itself. Each game's title and icon are read once and kept in `ms0:/SHELF/CACHE/`:

- `LIBRARY.BIN`: the list as last scanned, so an unchanged library is not read again
- `<hash>.IMG`: one icon per game
- `SMALL.BIN`: all 72x40 grid icons in one file, loaded in one read

A sweep removes pictures of games that are gone. The cap is 600 games.

Categories come from the folder a game sits in. "PSP Games" and "PS1 Games" are only added when no folder already says the same; "Other Homebrew" and "Other PSP Games" hold what is in no folder.

## What Shelf reads from the plugins

| Plugin | Read from | Used for |
|---|---|---|
| Sidequest | `ms0:/SIDEQUEST/<folder>/SUSPEND.SQS`, `STATE1..5.SQS` and their `.THM` pictures | Newest slot, its picture and date; which states exist |
| Checkpoint | `ms0:/CHECKPOINT/JOURNEY/<id>.TXT` and `<id>.TIM` | Journey entries and time played |
| Emblem | `ms0:/EMBLEM/games/<id>.txt` and `ms0:/EMBLEM/earned/<id>.txt` | Total, unlocked count, the five latest unlocks |

A game counts as "playing" when it has a slot or a journey.

## Launching

All launches go through `sctrlKernelLoadExecVSHWithApitype`, the way ARK's own menu does it:

| Kind | Key | Apitype |
|---|---|---|
| ISO / CSO / ZSO | `umdemu` | 0x123, or 0x124 with a `PBOOT.PBP` |
| PSN game (PBP) | `umdemu` | 0x123 / 0x124 |
| PS1 | `pops` | 0x144 |
| Homebrew | `game` | 0x141 |

### Resume without the question

When a game with a Sidequest slot is started from Home, Shelf writes one line, the game's path, to `ms0:/SIDEQUEST/RESUME.REQ`. Sidequest 1.2 or later loads the newest slot without asking, if the path names the running game, and removes the file. Shelf removes a request nobody picked up at its next start. The game always boots first; Sidequest loads the state a few seconds in, because a state can only be loaded into the running game.

## Shelf as ARK's launcher

ARK-5 re-reads `SETTINGS.TXT` at every restart. With its `launcher` line on, every game exit and every power-on goes to `ARK_01234/VBOOT.PBP`. Shelf uses that:

- **In place.** `put_in_place` copies Shelf to `VBOOT.PBP`. A different launcher found there is renamed once to `VBOOT_BEFORE_SHELF.PBP`. `launcher_update` refreshes `VBOOT.PBP` when the Game-menu copy is newer (size and date kept in `launcher_source.txt`).
- **Start at power on.** The setting switches ARK's launcher line on or off.
- **Back to Shelf after a game.** Before a game starts, `launcher_return_arm` switches the launcher line on and leaves a marker (`return.once`). At its next start, `launcher_return_done` switches it off again and removes the marker. So a game comes back to Shelf without the PSP booting into Shelf at power on.
- **Exit to XMB with start-at-power-on.** `launcher_pause` switches the line off and leaves `launcher.paused`; the next start from the XMB switches it back on.

### Hand-over from the Game menu

Plugins loaded at ARK's `game` level also load into homebrew. Sidequest, Checkpoint and Emblem leave when the program's path contains `VBOOT.PBP` or `/ARK_01234/`, but they stay in a program called `EBOOT.PBP`. So in the Game-menu copy they would run inside Shelf, and Checkpoint would take the PS button.

When Shelf is started from the Game menu it therefore makes sure `VBOOT.PBP` is Shelf (`launcher_ready`) and starts it (`sys_run_program`, apitype 0x141). The cost is one extra restart. Holding L at start skips the hand-over. If the hand-over fails, Shelf logs the code and carries on in the Game-menu copy.

## The PS button

User-mode code cannot see the PS button: its bit only says the system's own quit screen is showing. Shelf finds the kernel's button read (`sceCtrl_driver` 0x3A622550) and the switch for the quit screen (`sceImpose_driver` 0x5595A71A) with `sctrlHENFindFunction` and calls them with `kuKernelCall`, as psp5 and ARK's menu do. It takes the button over only while it is up, hands it back before a game, the XMB, USB mode and sleep, and a watchdog thread switches the system's quit screen back on if Shelf has not read the buttons for 3 seconds.

## USB

The USB drivers are loaded and started in psp5's order. If USB mode is already active it is switched off first. If the cable is seen but no connection comes within 5 seconds, USB is switched off and on once. Every attempt is logged with its state codes. After USB mode the library is scanned again.

## Time on and battery

`ms0:/SHELF/uptime.txt` holds one line: `start start% seen seen% flag` (times in minutes since 2000, local time; -1 for no battery reading).

- Shelf's own count: written every 5 minutes and at every hand-over. Flag 1 means "Shelf expects to be started again in this power-on" (game, XMB, restart). A start that finds flag 0, or a gap over 12 hours, begins a new power-on and writes a line about the old one to `battery.log`.
- With the Runtime plugin: Runtime keeps the same kind of record, with flag 2 plus the minutes asleep and its own bookkeeping, in `ms0:/RUNTIME/uptime.txt`. Shelf only reads that folder. While the record is under 5 minutes old, Shelf shows it. A record gone stale means Runtime was removed, and Shelf shows its own count. A file that is empty or does not read as a record means the folder is damaged: Shelf then shows no time or battery lines. Shelf's own count carries on in `ms0:/SHELF/` throughout.

## Tests

`tests/run_tests.sh` builds a stand-in Memory Stick (`make_stick.py`: real ISO / CSO / ZSO images, PBPs of every kind, and the files the plugins write), compiles everything except `app.c` and `sys.c` for the PC with AddressSanitizer and UBSan against small shims, and runs 178 checks. Every screen is written to `tests/out`; `to_png.py` turns them into PNGs. `tests/runtime_track/` is a copy of Runtime's record logic, so the two cannot drift apart unnoticed.

The pictures in `docs/images` are drawn the same way by `tests/render/make_doc_pictures.sh`, from a Memory Stick that `doc_stick.py` builds out of the icons and pictures of your own games (the script's first lines say how to take them out of an ISO). That art is not in the repository, and `tests/docrun/`, where the stick is built, is ignored by Git.

What the PC tests cannot cover: launching, the PS button, USB, the hand-over, and anything else in `app.c` and `sys.c`.
