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

Both players must run the current tools folder (v1.9.3-12-gf7decbde-dirty.9340761 or later). Most fixes act in the game
of the player who watches: in the session of 2026-10-14 evening Seen still ran 4ccd5ff, so nothing that his game draws
or simulates (the arrow, the bow on join, the copy's ragdoll) could show the new code.

Session of 2026-10-16 evening (both on d3c1974; player 2 joined first). Build fb88297 (deployed, client and server):

- [ ] **Player 1 cannot see her own arrows** (new on 2026-10-16; player 2 saw them on her bow and saw her shots).
      Her log has every draw (states 8, 9, 10) and nothing of ours touches her own arrow; no echo of her ArrowNocked
      came back. Since fb88297 `ArrowDiag:` logs her own arrow nodes (VR room nodes, first and third person) with
      parent, hidden flag and distance from each hand, whenever they change while a weapon is out. Rig: builds and runs,
      but the rig cannot nock. To see in play: draw once before F6 and once after; send the log either way.
- [ ] **Arrow held in the hand before nocking** not seen by the other player: not sent at all. The same `ArrowDiag:`
      lines show which node the game makes for it, which is what to send.
- [ ] **Player 2's hits did not hurt player 1 much** (21:02-21:03): on player 2's game, player 1's sword and shield
      bodies were taken out of the world while the world was busy; we dropped our record anyway, and the next frame
      left the bodies alone as someone else's (`already in a world; left alone`) for the rest of the fight. Player 2's
      game then saw no touch at all, so it never blocked, while player 1's game blocked half of player 2's hits. Since
      fb88297 the record stays until the body is really out (`could not be taken out yet ... tried again next frame`).
      Rig: `live-clash` 10 of 10. To see in play: blocks on both sides; no `left alone` line for a player copy.
- [ ] **Held objects flicker for the other player; can't be lifted after** (21:16): three causes in the logs. A carried
      object dropped off the watch list mid-carry never sent its rest (8 of 20 carries); a hand pausing 600 ms counted
      as a put-down, which the other side lets go of after 500 ms; a carry near the other player's copy was never sent.
      Since fb88297: carried objects stay watched until they rest, rest needs 1 s of stillness, and a restart within 3 s
      of this player's own carry is sent next to the other player. Rig: `live-world-object` 8 of 8. Not settled: why
      the other player can't lift it after (our state is always handed back to physics; HIGGS may refuse while the
      other game still moves it). To see in play: carry a cup past the other player, then let them take it.
- [ ] **Downed essential NPCs slide on the floor** (player 2, 2026-10-16): on player 2's screen no NPC was a copy, so it
      is his own NPCs (our SetPosition hook, PLANCK or HIGGS). `Tools/VR/bleedout-slide.py` knocks an essential bandit
      down in the rig, connected then offline: first run 180 against 142 units, but the bandit stood up inside the 12 s,
      so the run needs to measure only while it lies down. On player 1's screen a copy stayed downed after its owner
      got up (21:16:54 `'bleedOutStop' refused`).
- [ ] **Two-handed sword grip sometimes wrong** (player 2): his greatsword was equipped on the copy by the event path
      with the right-hand slot after a respawn (the snapshot path uses both hands since 9340761). Not shown to cause the
      grip; ask which screen it was.
- [ ] **Player 2's crash at 20:55** (not connected, 2 s after a load in Sovngarde): a freed scene-graph node visited by
      culling, the same family as KNOWN_ISSUES's +0xCBFD24; nothing of ours in it. Needs his
      `logs\crash_UTC_2026-10-16_18-55-15.small.dmp` (collect-logs leaves small dumps out).
- [ ] `Character with remote id ... is already spawned` is routine (the server re-sends at each cell crossing): debug
      level since fb88297.

- [ ] **Blades:** does your blade rest on his, and does it feel right (`BladeStop: ... met`, `... let go`)? Open design
      point: sliding off his hilt end lets go, so the blade carries on into his hand and that hit counts.
- [ ] **Grabbing:** grab an NPC Seen's game runs, then Seen's arm, and say what the hand does (nothing, holds, pulls).
      No log line tells it any more (see the NPC pull in Open work).
- [ ] **PLANCK 0.8.x:** since 0.7, PLANCK patches Character::SetPosition and leaves an actor out of its active ragdolls
      for 2.5-5 s after each call; copies were moved through it every frame, so on 2026-10-14 no copy had a ragdoll in
      the world (no sword body, no clash, no NPC grab). Copies are now moved with the game's own SetPosition (logged
      once: `ForcePosition: Character::SetPosition is replaced by activeragdoll.dll`). Rig with 0.8.1: the copy's
      ragdoll in the world, clashes and sparks again; seen in play 2026-10-14 evening (sword fights back to normal).
      The game's own NPCs went the same way (an inherited hook routed every NPC move, about 1,000 a second, through the
      patched slot); since the build of 2026-10-14 22:00 they too move with the game's own SetPosition. To see in play:
      grabbing an NPC with HIGGS, both games on the current build.
- [ ] **Arrow on the bow:** seen 2026-10-15 (both on a720309): placed right while the string is pulled. It was missing
      while the arrow only sat on the string: our attack-state numbers were off by one (CommonLibVR: 8 bow draw,
      9 arrow on the string, 10 pulled), so "arrow on" was sent only at 10. Since 9340761 it is sent from 9. Rig:
      `live-arrow-nock` 9 of 9, but the bot sends "arrow on" itself, so only the receiving side is tested. To see in
      play: the arrow shows as soon as it sits on the string, unpulled. An arrow held in the hand before nocking is
      not sent at all.
- [ ] **A drawn bow when the other player joins:** the joiner sees it without a re-equip. Since 9340761 a weapon whose
      own slot is both hands (bow, two-hander) is equipped there (`equips ... (right hand, both hands)`), anything else
      in the hand it is held in. Rig: `live-arrow-nock` and `live-equip-twice` pass. Not seen in play yet.
- [ ] **Hitting his sword sounds like hitting him** (2026-10-15): PLANCK counts the copy's sword as part of the copy and
      runs the game's whole melee hit (body sound, blood, reaction); only the damage was dropped. Since 9340761 that hit
      is cancelled when it lands where his weapon was just touched or a blade stop holds
      (`PvP: a swing into remote player ...'s weapon is a clash; the game's hit on him is cancelled`); the block sound,
      sparks and buzz stay. Rig: `live-blade` 6 of 6, but the rig's slow swings make no PLANCK hit, so the new line never
      fired there. To see in play: only the block sound on a blade hit.
- [ ] **Leveled NPC copies without their weapon** (2026-10-15, a Draugr Deathlord in Skuldafn unarmed in player 2's
      game, doing small unarmed hits): a copy rebuilt for the owner's leveled pick re-equips its outfit, and a copy
      refuses the game's own equips. Since 9340761 the owner's weapons are put back when the rebuild completes
      (`Leveled copy ... rebuilt: the owner's weapons put back, now holding left ..., right ...`). Rig: `live-levelled`
      passes, but its bear has no weapon, so the put-back is untested. To see in play: leveled draugr and bandits
      armed on both sides.
- [ ] **No animation-graph crash** like player 2's of 2026-10-14 21:47 (`SkyrimVR.exe+0xB3C2F1`, Irileth in
      Dragonsreach, a float where Havok expected an index): synced animation values are now written only into variables
      whose word is the value, and the variable list is taken again when an actor's behavior project changes
      (upstream 115b5019; `Animation graph project of actor ... changed`).
- [ ] **Items:** after one player puts an item down, the other can grab it within about a second
      (`WorldObject: ... was put down over there; handed back to physics here`, then no new `is being moved over there`
      while it rests; restarts now name their trigger, `(moved N units, turned N rad)`).
- [ ] **A crash in play leaves a line:** `SevereException: ...` for heap corruption, stack overflow and the like, and
      `Exit: the game is closing normally` on a menu quit. Friends' `collect-logs` zips now hold Windows' crash
      records (`windows-events.txt`, `WER-Report-*.txt`).
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
- [ ] **NPCs pulled back when a player joins** (2026-10-14): NPCs that walked away in the host's game, then were removed
      by the server for want of an owner, come back where the joiner's save has them. A fix that remembered the last
      released place was tried and removed the same evening: some release positions are bad ((0, 0, 0) for an unloaded
      actor, far under the floor), and Imperial guards ended up stacked. A redo must accept only sane positions.
- [ ] **Civil War battle mods** (installed on the host PC 2026-10-16: Open Civil War 2.8.0 and Populated Skyrim Civil War 3.0,
      plugins before DynDOLOD.esp; profile backups `*.bak-20261016-civilwar`). Populated places its 324 soldiers in its plugin,
      the best case for sync; Crowded Civil War (downloaded, not installed) spawns its soldiers by script and is kept for later.
      The other player installs the same two.
      Rig 2026-10-16, `live-cw-battle` (build v1.9.3-13-g8fa68305-dirty.d3c1974): Populated's Great Battle 2 near Whiterun
      (32 soldiers, always on, no quest needed), player and bot both there. Passes: the bot held 59 copies, every soldier
      once (no base id under two server ids), the host stayed at 60 fps (p99 under 33 ms, mod update under 3 ms), the
      fight played out, no crash in the battle. Not tested: Open Civil War's sieges and reinforcements (need a quest
      start), a real second game. Seen in play: not yet.
      One run crashed in CommunityShaders.dll+0x1208c8 (read at 0x144) at the start of the loading screen out of Mistwatch;
      the same crash came three times on 2026-10-12, all at Mistwatch loading screens in the rig, so it is not the battle
      mods. Watch for it in play: a crash right as a loading screen starts.
- [ ] **The copy's body turns with the head** (2026-10-14): the game turns the actor with the headset and that heading
      is sent, so the copy's hips and legs follow every head turn. First a measurement for one session (heading,
      headset yaw, the VRIK body's yaw from the thighs), then send the VRIK body's facing as the heading and the head
      relative to it (VRBodySync::CaptureLocalPose, AnimationSystem::Serialize; the walk animation needs care).
- [ ] **Map greyed out indoors** (player 2, 2026-10-11 and 2026-10-14): no code of ours touches the map indoors and it
      worked outside. To settle it: open the map inside any building with the mod not connected.
- [ ] **An in-headset menu:** the urSovngarde menu as a SteamVR dashboard tab with large text, a small overlay for chat.
- [ ] **A flat (non-VR) player with VR players** (asked on Reddit, 2026-10-09). Possible in principle: the repo still
      builds the flat client, and `Code/encoding` has no VR-only message, so both builds share the protocol and the
      server. Missing: (1) the VR player on the flat screen -- VR swings are not animations, and the poses we send
      (`VRPose`) are applied only in VR builds, so the flat side would pose arms and weapon from them, the bulk of the
      work; (2) form ids meaning the same thing on both sides (`SkyrimVR.esm` against the Creation Club files, VR-only
      plugins), with a rule for the expected differences; (3) a flat block counted in the defender's rule, which knows
      only VR blade contact; (4) the flat build untested since the fork, testing needs a Skyrim SE copy. Only once VR to
      VR is solid.
