# The desync suite

`desync-suite.py` plays: the real game and the bot act in one world while a scanner compares them every second, and it says what differs, when, and on which NPC.
`play.py` is what plays: a VR body (a headset at standing height that turns to steer, hands that aim, the trigger) and pictures from two views.

## Run it

Nobody playing, the test profile selected, the shader cache moved aside (see the rig's notes), then:

```
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 up -AutoConnect
python Tools\VR\desync-suite.py --selftest            the scanner against planted mismatches, no game
python Tools\VR\desync-suite.py scanner-proof          the same on the real game; run this first
python Tools\VR\desync-suite.py --all --live           every test and the older live scripts
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 down
```

The report is `build\desync\report.html`, kept up to date as tests run, with `results.json` and a folder per test: the bot's log and the pictures taken when something failed.

## What the scanner compares

The game (DevBench), the bot (what the server told it) and the client's log, which says which form is which server id (`Entity: local actor X has server id Y`, `New entity remotely managed`).

| Finding | Means | Fails the test |
| --- | --- | --- |
| `life-differs` | dead on one side, alive on the other, for 5 s | yes |
| `place-differs` | over 300 units apart (a body: 150) for 5 s | yes |
| `came-back` | dead in this game, then alive again | yes |
| `cannot-finish` | alive at no health for 5 s | yes |
| `missing-here` | the server has it alive near this player, this game has lost it, for 10 s | yes |
| `duplicate-player` | the other player drawn twice | yes |
| `player-place` | this player is over 400 units from where the other is told it is | yes |
| `game-ended` | the game was gone at the end of a test; it is brought up again | yes |
| `health-differs` | another health for 4 s | reported |
| `unknown-to-server` | near this player, alive, not known to the server, for 15 s | reported |
| `flicker`, `3d-blinks` | jumping away and back, or losing its body, again and again | reported |

## What it cannot see

There is one real game, so what only a second screen would show (a wrong animation on the other side) cannot be judged.
The bot has no VR body and no swing animation, its height comes from what it is told, and it does not take NPCs over when a game leaves.
Doors, levers and bridges come from the older live scripts (`--live`), whose verdicts are in the same report.

## The two-game test

An extra test, not part of `--all`: two real games on this PC, one server, each showing the other player's real body.
Everything it needs is off unless asked for: the marker file for the first three lines below, the variable `TWO_GAMES_SECOND`, set by the anchor, for the second game's SteamVR role.

```
create the empty file %LOCALAPPDATA%\SkyrimTogetherVR\two-games.on
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 up -AutoConnect -Twin
python Tools\VR\two-games.py --minutes 10
powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 down            twice: the second puts the saves back
delete the marker file
```

Results are in `build\desync\two-games`: `watch.json` and a picture from each game; the script prints the rest.
On a server elsewhere, add `-ServerAddress <address:port> -ServerPassword <password>` to `up`: no local server starts, and the password goes only into the connect file, which `down` puts back.
That server has to be built from the same protocol: `Tools\VR\check-linux-build.ps1` builds for Debian 12, and `urSovngardeServer` and `libSTServer.so` go together.
Every minute one player walks for 5 seconds, and the script reports how far the other game's copy is behind.

| What stood in the way | What the test does |
| --- | --- |
| The game quits when it finds its own window | With the marker, it skips that check (`Two games: a second game may start on this PC`) |
| Every start through Mod Organizer remounts its virtual folder | `two-games-anchor.py` is started once by Mod Organizer and starts both games |
| Plugins keep the address library in one named shared block | With the marker, each game has its own block |
| SteamVR kills the running scene application when another connects (`Kill process ...: SkyrimVR because it didn't quit in time`) | The second game joins as an overlay (`Two games: this game joins SteamVR as an overlay`) |

What differs from play: the second game has no compositor pacing, so it runs at about 120 fps, and has no dashboard tab (`VRDashboard: CreateDashboardOverlay failed (17)`).
Both games load the same save, so both players are the same character.
It does not cover the relay (see `VR_TODO.md`).

`two-games-bugs.py [--until HH:MM] [test ...]` plays reported bugs on the two clients: `spells-baseline`, `spells`, `respawn`, `corpse`, `drops`, `reconnect-body`, `door`, `npc-animation`, `brawl`.
Results are in `build\desync\two-games-bugs`: `results.json`, `findings.txt` and pictures.
F6 reaches both games, so the game that should stay connected is suspended for a moment while the key is pressed.

## Adding a test

A function `t_name(r)` in `desync-suite.py`, listed in `TESTS` and `ORDER`, with `r.spawn`, `r.kill`, `r.bot.send`, `r.expect` and `r.picture`; the scanner runs by itself.
A new finding goes in `Judge` and gets a planted case in `selftest`.
