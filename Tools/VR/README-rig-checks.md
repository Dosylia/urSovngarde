# Checking sync and animations in the rig

The rig runs one real game; the bot is the other player, with no game of its own. So these checks do two things:
they compare what the game shows with what the bot was told (the server's truth), and they put an original and its
copy side by side in the one game. Players' builds are not involved: all of it is the bot, `live-check.py` and scripts.

## Run them

```
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 up
python Tools\VR\live-check.py --sync
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 down
```

Then open `build\windows\x64\release\logs\live-report.html`: each script's verdict, its checks, the jank it found and
its screenshots. `live-door-script` leaves the player in Helgen's keep like `live-door-motion`, so it runs by name
after a fresh `up`.

## What each test catches

| Test | What it does | On 206a1d36 |
| --- | --- | --- |
| `live-death-copy` | The bot's NPC dies; the game's copy must die, fall soon and lie still | ok |
| `live-death-protected` | The same with a protected NPC (Lydia), then the game takes her over | **failed**: the copy stayed alive, and the hand-over brought her back to life; fixed by 10b98ee5, not run on it yet |
| `live-death-replay` | An NPC dies in the game; the bot replays what the game sent onto its own NPC, so the game shows the copy beside the original | the copy's body is shifted 69 to 108 units after it lands (3 of 4 runs) |
| `live-death-replay-blast` | The same, the original killed by a Fire Storm, as in a fight | as above |
| `live-lock-range` | A lock changed one cell over | **fails**: a lock change reaches only the door's own cell |
| `live-door-script` | A door a script opens | **fails**: a script's `SetOpen` is never sent |
| `live-lever-bridge` | Mistwatch's lever and bridge, pulled in each game | **fails**: a lever pulled by the other player does not move the bridge here |
| `live-follower-stays-in` | The owner takes a follower indoors; the game's own one must stay in | ok |
| `live-door-motion` | A door used in the other game must open and close here once, with its animation | ok |

The failures are the bugs of the sync audit of 2026-10-26 and what the rig found since; `VR_TODO.md` tracks them.
A fix counts when its test turns from failing to ok.

## The checks, for writing new tests

The full forms are in the docstring at the top of `live-check.py`.

- **Jank detector.** After every script the driver prints `jank: ...` and keeps it in the history: what the client's
  own diagnostics say looked wrong on screen (`JANK` in `live-check.py`: corpse-jump, died-early, point-jump,
  getup-jump, stuck-down, refused, no-reports, ragdoll-apart, door-differs, copy-lost-3d).
  `log CHECK smooth all` (or `smooth <kind> ...`, `smooth except <kind> ...`) fails a script on it.
- **Desync scan.** The bot's script line `report` prints what the bot knows: its NPCs, door states and locks.
  `log CHECK desync all` compares that with the game (life, place, duplicates of placed NPCs, doors, locks).
  `log CHECK told door|lock <hex> <state>` checks the other way: what this game did reached the other player.
- **Recordings.** `log DO record <name> copy|npc|placed|player|ref <hex> <seconds>` samples in the background while the
  script goes on: a body's pelvis and height, and its whole body for the last 2 s; any other reference's open state.
  Then `CHECK pose <b> like <a> <units>`, `fall <b> within <ms>`, `fall <b> like <a> <ms>`, `place <b> like <a> <units>`,
  `still <a> <units>` and `door <a> once open|closed|toggled`.
- **Recorded deaths.** The bot's `capture` keeps the moment the captured NPC died, and `replay npc` kills its own NPC
  at the same moment, so a replay is the whole death as the other player would see it.

## What to know

- The bot's script does not wait for the driver's checks. Leave a few seconds (`wait`) after a group of checks before
  the next action, or the action happens while the driver is still asking.
- A `bot logged` pattern must keep a backslash (`info\] Captured the death of`), or it matches the bot's own echo of
  the line and passes whatever happened.
- DevBench answers about 27 Papyrus calls a second, one per game frame. A quick body sample is 4 calls (about 6 a
  second), a whole body 18. DevBench's `inspect` kind `refs` (filters `formType`, `radius`) lists nearby references
  with their type, position and, for actors, life and health in one call of 0.02 s. DevBench also takes inspect
  kinds registered by other plugins (`inspect kind=extensions`): the way to a fast bone sampler, if one is needed.
- Only doors show a moving state (opening, closing). A bridge or gate run by a lever goes straight from one end to the
  other, so `door ... once toggled` judges only where it ends and how often it changed.
- Helgen keep's door `9B0A5` is disabled in the test save: no 3D, so it jumps to a new state. `F8239`, its drawn twin,
  animates.
- Ragdolls fall differently every run. Over four runs the copy's pose differed by 0 to 33 units and it fell 0 to 1.4 s
  after its death. The limits in the scripts are first guesses: run a test three times before tuning one or judging a
  change.
- Console travel out of Helgen's keep crashed the rig in `CommunityShaders.dll+0x1208c8` (a testing profile's Community
  Shaders 0.8.7); that is why the Helgen tests go last.
- A testing profile with another Community Shaders version writes its shader cache into Mod Organizer's `overwrite`.
  On a PC whose main profile runs CSX, move `overwrite\ShaderCache` aside before `up` and back after `down`, or the
  next start of the main profile compiles shaders for an hour.
- The rig cannot swing a real weapon or show a second screen: kill moves and what the second screen looks like are
  still checked in a session.
