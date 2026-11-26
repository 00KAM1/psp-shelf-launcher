# Shelf 1.2: known issues and limits

Tested on one console: PSP-3000, firmware 6.61, ARK-5. Other models are untried. Build in this release: `built Nov 26 2026 09:00:17`. This build has not run on the PSP itself: it is 1.0, which has, with fifteen on-screen texts given a capital letter (1.1) and the Playing row's cards filled by the game's icon (1.2).

## Disclosure

Shelf was written by Claude, Anthropic’s AI model, from my ideas and my testing on a PSP-3000. I’m not a PSP programmer, and nothing has been tested beyond my own console.

- No one else has reviewed the code.
- Shelf replaces ARK's launcher file and can change ARK's settings and `PLUGINS.TXT` when you ask it to. Keep a copy of both before relying on it.
- It is not a kernel plugin: it runs as a normal program, started by ARK.

## Behaviour to expect

- **Starting from the Game-menu icon is slow.** Shelf hands over to its launcher copy, which costs one extra restart with a black screen. Custom Launcher and "Start Shelf at power on" do not have it. Hold L at start to skip the hand-over.
- **A resumed game boots first.** A game started from Home loads its save state a few seconds after the game itself has started.
- **Delete your Sidequest states before installing or updating Shelf, Sidequest, any other plugin, or ARK.** A Sidequest state holds the whole system as it was when it was saved: the launcher and every plugin, in the versions installed then. Loading it, from Sidequest's menu or by resuming a game from Shelf's Home tab, brings all of that back. A state from before an install or update can end in a black screen instead of Shelf (hold POWER up for ten seconds to switch off), can run old versions of the plugins or run without one added since, and can leave files on the Memory Stick damaged. On my PSP, Shelf's saved game list and logs in `ms0:/SHELF/` were damaged after loading such a state; deleting that folder and letting Shelf make it again put it right, at the cost of the marks and settings kept in it. So: save in the game's own save menu, delete every slot (△ in Sidequest's menu, the suspend included; or the game folders inside `ms0:/SIDEQUEST/` with a computer, keeping `config.txt`), install, then take new states.
- **Shelf replaces ARK's launcher file.** The previous `VBOOT.PBP` is kept once as `VBOOT_BEFORE_SHELF.PBP`. Shelf does not put it back; rename it yourself to go back.
- **A new build needs a restart.** Files copied over USB while Shelf is open take effect at the next start.

## Limits

- **600 games** at most.
- **Time on, without the Runtime plugin:** time asleep counts as on, and a power-off during a game or in the XMB is only noticed at the next start of Shelf, up to 12 hours later.
- **Scanline loads into Shelf.** Unlike Sidequest, Checkpoint and Emblem it has no check for the launcher.
- **Other people's plugins** loaded at ARK's `game` or `always` level also load into Shelf. Shelf does not change `PLUGINS.TXT` on its own; use Settings > Plugins, or edit the file.

## Not confirmed on hardware

- Models other than the PSP-3000, and the PSP Go's internal storage.

## When something is wrong

`ms0:/SHELF/shelf.log` records every start: the build, where Shelf was started from, whether it handed over, how many games were read, and every USB attempt.
