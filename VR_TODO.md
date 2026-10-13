# Skyrim Together VR: TODO

What is still to do, and nothing else. The full story -- session reports, investigations, the evidence for every fix
-- is in git and in `VR_HISTORY.md` (everything this file held up to 2026-10-07, word for word); `KNOWN_ISSUES.md` says
what is known to be broken and why. Cleaned on 2026-10-13: proven and closed entries removed (git keeps them), the
rig-tested work folded into the checklist below.

`[ ]` open, `[!]` waiting on a test or action. An item the players confirm is deleted, not ticked.

**Aims, in order** (2026-09-25): no crashes; the game in sync for both players; VRIK and HIGGS interactions seen by
everyone; dead bodies in sync; NPCs never below the floor; weapon hits land where the weapon is. Since 2026-10-06:
sword fights that feel like Blade & Sorcery -- the other player's sword and body are physics objects in your game, and
the defender's screen decides whether a hit was blocked.

The launcher and the online side (relay, invite codes, the public server page, crash triage) are tracked in their own
repos' `TODO.md`: `C:\dev\urSovngarde-launcher` and `C:\dev\urSovngarde-hub`.

## Next session with Seen: what to look at

Everything here is built and tested in the rig, waiting to be seen in play. Seen needs the current tools folder: the
public server (v1.9.3) refuses this build (protocol change: the arrow and the book). After the session, read both logs
for the lines named.

- [ ] **Blades:** does your blade rest on his, and does it feel right (`BladeStop: ... met`, `... let go`)? Open design
      point: sliding off his hilt end lets go, so the blade carries on into his hand and that hit counts.
- [ ] **Grabbing:** grab an NPC Seen's game runs, then Seen's arm, and say what the hand does (nothing, holds, pulls).
      No log line tells it any more (see the NPC pull in Open work).
- [ ] **Arrow on the bow:** on the string as the other player sees it, along the bow (across it: the arrow model's axis
      `kArrowForward` is wrong), and gone after the shot.
- [ ] **Bow string:** pulled back to the string hand while the arrow is on, the limbs not bent, straight again after.
      A torn-looking string means the other bone (`Bow_StringBone1`) is the middle.
- [ ] **Head:** a nod and a look up show on the other player's copy; at rest the copy looks straight ahead (the sent
      pitch is the headset's less 13 degrees).
- [ ] **Book:** one player stashes a placed book and drops it, the other takes it (`WorldObject: book X taken here` on
      one side, `... removed here` on the other).
- [ ] **Skeletons and corpses:** no runs of `Received remote actor` / `Actor removed` for one actor, no corpse moved
      twice a second, a corpse left after two useless moves (`left there until that moves`). Since 2026-10-13 an
      owner no longer revives an actor killed while its assignment was in flight (upstream 8f08d405): watch for
      `died or revived while its assignment was in flight` and `Keeping actor ... dead`.
- [ ] **Leveled creatures:** the same creature on both screens (the Snow Bear / Ice Wraith of 820B9; `rolled again by
      the game`).
- [ ] **Followers:** a follower stays with her player's game, no back-and-forth (`given to player`, `Gained
      ownership` once).
- [ ] **Map icons:** other players show as map icons only; nothing in the journal's Miscellaneous list.
- [ ] **Dialogue:** a quest scene heard once by both (`Dialogue: ... says <file>; sent`, `... played here`, `... no
      actor here`). Also seen once: quest NPCs out of sync (Bastianus Axius), message boxes shown to both players.
- [ ] **A fight:** hitting his sword does not hurt him, blocks on your screen hold, damage does not feel late
      (`PvP: ... not sent`, `Defender's rule`); one fight with Emma hosting (the server id 0 fix); `equip skipped`
      lines in the watcher's log, and his blade still meets hers.
- [ ] **One drag each way:** Seen drags a body your game owns, then you drag one of his (`asking for it`,
      `Transferred ownership ... for party leader claim`, `posing body`). Also open from 2026-10-11 18:23: Seen could
      not grab a bandit body after Emma dragged it -- no motion of it was seen in his game.
- [ ] **A death:** the respawn's health lines (after the respawn, when god mode ends) for the second down of
      2026-10-11 21:12; in a dungeon, the respawn at its own spot (`RespawnPlayer: in cell ... at its override
      position`).
- [ ] **A load within a minute of a death:** it crashes the rig every time (`skyrimvrtools.dll+0x71B5`); no real session
      has done it yet.
- [ ] **A real dragon flying** in one game: the other game never takes it over until it lands.
- [ ] **Playback delay:** the `InterpDiag players` lines say how much of the 225 ms the network really needs; then decide
      whether the delay follows the connection. Upstream (23901108, a4a33744, 0a6784b8) gets about 65 ms at 20 ms
      ping, and the lessons apply here: we send movement and the VR pose reliably, so one lost packet holds back every
      later snapshot; upstream sends movement unreliably (at most 8 actors a message, actions in their own reliable
      message), relays it on the server at once with Nagle off (ours batches at 50 Hz), sends NPCs in combat at 30 Hz,
      and sizes the delay per actor (snapshot interval + 95th-percentile jitter + 8 ms, eased at up to 10% speed).
      A shorter delay also moves the hit timing (`kHitSeenAfter` in ActorValueService).
- [ ] **Threads and stutters:** the `Health` lines near 200 threads in both games once Seen is updated (Waiting on a
      test or action); Seen's freezes (no frame for 46 s, 2026-10-11 18:11 and 21:36) gone with it.
- [ ] **Crash lines on a real crash:** `CrashPlace: ...` (send the log's path to the launcher worker, which parses it)
      and `small crash dump ... (N KB)`. Watch for the PLANCK travel crash (`+0x3AC1A8`, `+0xCBFD24`), not seen since
      2026-10-03.

## Waiting on a test or action

- [!] **Seen's update:** DynDOLOD DLL NG Alpha-43 (Nexus 97720, replacing his `DynDOLOD DLL NG` files) and the host's
      combined VR Address Library file (`E:\FUS\mods\VR Address Library for SKSEVR\SKSE\Plugins\version-1-4-15-0.csv`;
      his lacks id 19363, so Alpha-43 refuses to load, and a plain newer library crashes Community Shaders, KNOWN_ISSUES
      section 1). The host PC has both since 2026-10-12 (threads flat near 150 all session).
- [!] **VirusTotal key** in `%USERPROFILE%\.str-vt-key`, for `Tools\VR\scan-release.ps1` (scans a release before it is
      published). If a helper `.bat` is ever flagged: move the helpers to a separate manual-tools zip.
- [!] **PLANCK 0.8.1:** the rig shows it safe (`E:\FUS\PLANCK 0.8.1 (tested 2026-10-10, ready to install)`); updating
      from 0.6.6 changes how grabbing and hits feel, so it is a choice, not a fix.
- [!] **Parties on a public server:** with `bAnnounceServer` off, every stranger lands in one party (shared XP, quest
      sync, teleport to party members). Decide what a public server does with parties.
- [!] **Stillborn Cave crash in the rig:** console travel out of that cave crashes Community Shaders 0.8.7
      (`CommunityShaders.dll+0x1208c8`, read at 0x144 during the loading screen's draw, no other mod on the stack), 3 of
      3, with or without a death (`live-cave-travel`). Not seen in play. To know whether the mod is involved at all:
      the same trip with it unloaded (a modlist switch for one run).
- [!] **A friend's orc shown as "a naked white nordic skin with black face"** (Discord, yamb7161, 2026-10-11, v1.9.3,
      their own server): every player copy in their log has an empty name and level 1, and their two mod lists differ.
      Needs the server owner's log and both `plugins.txt`; the `Player copy appearance` line (next release) then says
      which part of the appearance did not load.

## Open work

- [ ] **Handling a living NPC the other game runs: send the pull** (decided 2026-10-10: the grabber's game sends the
      pull to the game that runs the NPC, which applies it). Needs a safe way to tell when a HIGGS hand holds a copy:
      the `HandHold` diagnostic read Havok constraints under the world lock from a game thread, corrupted contacts
      (rig crash at `SkyrimVR+0xa10860`) and was removed. A redo should read them inside the physics step. The rig
      cannot make a HIGGS grab: DevBench answers the controller state itself, so its buttons never reach HIGGS.
- [ ] **Grabbing the other player: a buzz on the grabbed hand** (decided 2026-10-10, no stagger). A `GrabRequest` from
      the grabber's game, passed on by the server as `NotifyGrab`, pulses that hand (`VRHaptics::Pulse`, like
      `PulseClashHaptics`); a protocol change. Waits on the same grab detection.
- [ ] **Your body pushing loose items** (decided 2026-10-12: the walker's game owns a pushed item). Since 2026-10-12 the
      other game no longer sends a move that starts next to a player's copy, so a walked-into item stays put on both
      sides. To do: collision bodies on the local player's legs and torso (as the weapon bodies are made) so the
      walker's own game pushes the item and sends the move. Neither HIGGS nor PLANCK has a setting for it.
- [ ] **The other player's body looks off:** hands a little low with the palms together (`VRPose`, structural), legs
      not walking or running with smooth locomotion, the face "looks off" (FaceGen on VR).
- [ ] **Your own sword hitting where the blade is not** (2026-09-24 screenshots; VR_HISTORY "4. Position accuracy").
- [ ] **Less delay for hands and weapons:** after the playback delay is measured (checklist).
- [ ] **Movable objects too broad:** `IsMovableObject` takes form type 36, MovableStatic (waterfalls, banners, large
      animated scenery), not only loose objects.

## Parked and later

- [ ] **Mounts** (parked 2026-10-12). The rider's `MountRequest` crosses and the other game gives the rider's copy the
      mount package, which it accepts, but the copy never gets on: copies run no AI of their own. Tried in the rig and
      reverted: `Actor::PutActorOnMountQuick` (vtable 0xAE on VR) returns false; letting the copy's AI run with its
      position free for 10 s left it beside the horse. Next if taken up: build the rider/mount `ExtraInteraction` on
      the copy, then `PutActorOnMountQuick` (real reverse work on VR); or ask cmpayc's fork whether it has mounts.
- [ ] **A re-rolled creature keeps the old animation variable list** (upstream 115b5019, Havok crash): the synced
      variables are picked once per actor (`GraphDescriptorHash` in TESObjectREFR.cpp) and never again, so a leveled
      actor re-rolled into another creature gets out-of-range values. Upstream recomputes the list when the behavior
      project changes and writes only word variables; porting needs the project name and variable types of the VR
      behavior graph structs checked first.
- [ ] **An in-headset menu:** the urSovngarde menu as a SteamVR dashboard tab with large text, a small overlay for chat.
- [ ] **A flat (non-VR) player with VR players** (asked on Reddit, 2026-10-09). Possible in principle: the repo still
      builds the flat client, and `Code/encoding` has no VR-only message, so both builds share the protocol and the
      server. Missing: (1) the VR player on the flat screen -- VR swings are not animations, and the poses we send
      (`VRPose`) are applied only in VR builds, so the flat side would pose arms and weapon from them, the bulk of the
      work; (2) form ids meaning the same thing on both sides (`SkyrimVR.esm` against the Creation Club files, VR-only
      plugins), with a rule for the expected differences; (3) a flat block counted in the defender's rule, which knows
      only VR blade contact; (4) the flat build untested since the fork, testing needs a Skyrim SE copy. Only once VR to
      VR is solid.
