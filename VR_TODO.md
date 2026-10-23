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

Sessions of 2026-10-16 evening (both on d3c1974), 2026-10-17 morning (both on 5342bc0) and 2026-10-17 evening (both
on ce86908). Current build 440f5e0 (deployed, client and server; protocol ade54224, changed by the arrow in the hand).
Both players need it.

- [ ] **Cutscene voice lines and subtitles out of step in a party** (upstream ea0f6c39, ported 2026-10-17; a protocol
      change, so both players need 5342bc0 or later). Every game plays a quest's scenes itself, and the relayed line
      stopped the local one and ended the scene phase early. Lines now carry their scene, and a game that plays the same
      scene keeps its own line and subtitle (`Dialogue: ... in scene ..., which plays here too; the local line is kept`).
      Also: the game spoke most lines twice and each copy was sent (351 of 515 last session), which restarted the line
      on the other side; a repeat within a second is no longer sent. Rig: `live-scene-dialogue` 3 of 3 (Whiterun's
      market scene 5B1BA sent with its scene, each line once). The skip itself needs two real games. To see in play: a
      quest scene with both players near, voices in step and no line cut short.
- [ ] **Arrow held in the hand before nocking** not seen by the other player: not sent at all (again 2026-10-17
      22:06:46 on ce86908). From the `ArrowDiag:` lines in play: at attack state 0 the `ArrowNode` under the player's
      world node moves to about 67 units from the right hand and gains an `Arrow:0` child.
      In the tree since 28995d2 (a protocol change, ade54224: both players and the server must update): the archer's
      game finds that arrow at the hand's hold node, ten times a second, and sends it with where it sits against the
      third-person hand bone (`ArrowNocked: sent this player's arrow in the hand (...)`). The other game attaches the
      arrow on its copy and keeps it where the archer holds it. From the hand to the string it is the same arrow, laid
      along the bow as before. Compiled, TPTests 52 of 52 (two arrow round trips). Rig on 440f5e0 (c1's build, which
      holds it): `live-arrow-hand` (new) 9 of 10. The rig player drew a bow and the bot heard the arrow in the right
      hand, its head 82 units from the hand bone. The bot's arrow went into the copy's right hand, its head 57 units
      from it, then onto the string and off again. The one failure was the check: it expected 60-74 units and forgot
      the hand bone's scale (67 times a scale under 1). It now takes 40-79, and the line prints the hand scale from the
      next build. The screenshots show nothing (the copy too far and bent over), so only the log says where the arrow
      is drawn. `live-arrow-nock` 9 of 9 (regression). There is no red run: before this, nothing at all was sent for
      an arrow in the hand.
      To see in play: draw a bow in front of the other player; they see the arrow in your hand. Read on their side
      `ArrowNocked: ... arrow held in the right hand as its owner holds it, its head N units from the hand`.
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
      of this player's own carry is sent next to the other player. Rig: `live-world-object` 8 of 8.
      Lifting after the other player: `live-world-object-grab` (new, 97b6cd0) has the bot carry the plate and put it down,
      then carry it again and drop it mid-carry; each time the game's HIGGS hand grabs it at once. 6 of 6: HIGGS pulls it
      and the pull goes out as this player's carry (the rig's hands mostly cannot close, so the hold itself is not
      checked; once it even held). So an object is free here as soon as the other side lets go. What players met was
      most likely the other game still sending (held 2 s after a carry stopped without a rest, fixed above).
      2026-10-17 (5342bc0): carries now end with a put-down every time, but the cup still blinked nearly invisible
      while carried, and player 2 could not lift it after. Measured in the rig: every move from the carrier rebuilt the
      object's 3D on the other side (`6 of its 6 moves here rebuilt its 3D`): MoveTo builds the 3D again. Since f56dc9e
      a move of under 1000 units uses the game's own SetPosition, which moves the 3D in place (`0 of its 6 moves`);
      MoveTo stays for a jump. Rig: `live-world-object` 8 of 8, `live-world-object-grab` 6 of 6 (the hand held the plate
      at once after the put-down), `live-world-object-reload` (new) 6 of 6: the plate keeps its 3D after a carry and
      after its cell loads again. To see in play: carry a cup past the other player; it moves smoothly in steps, and
      they can lift it once it is down.
      2026-10-17 evening (ce86908): both players lifted the same cup in turn, but neither saw the other carry it, and
      player 2 found it where it first stood. The game's SetPosition moves only the reference, not the 3D nor its
      collision, and the rig read the reference. Red on c09bd58 (`live-world-object` now reads where the 3D is drawn
      when it is let go): 216 units from where the bot put it, and it came to rest where it started. Since 0ff0808 each
      move also warps the 3D and its collision there (`Update3DPosition`): 0 units, at rest where the bot put it,
      `live-world-object` 9 of 9. Not seen in play yet. To see in play: carry a cup in front of the other player; read
      `its 3D is N units from the last place it was put` on the other side (a few units at most).
- [ ] **Downed essential NPCs slide on the floor** (player 2, 2026-10-16 and 2026-10-17): the copy refused the
      owner's get-up (`'bleedOutStop' refused ... (not taken)`) and followed the owner's walk lying down.
      Cause, reproduced in the rig: at zero health the copy's own game puts it down with a ragdoll fall; each replayed
      action then wrote the owner's knock state over the copy's, the copy's own update is skipped for copies, so its
      get-up never ran, and interpolation moved the body lying on the floor.
      Fixed in 502d883 (deployed, client and server): NPC copies keep their own knock state, a knocked-down copy runs
      its own update and is not moved while it lies, and a get-up still queued after 1.5 s gets `GetUpBegin`.
      Rig: `live-copy-bleedout` (the copy falls in its own ragdoll; on 7a9e33a without the last part it lay there:
      `still lies after 15 s`; on 502d883 up after 5.3 s, 8 units of travel while down) and `live-copy-knockdown`
      (the owner's NPC thrown over while down; the copy lies 6 s, gets up by itself). Not seen in play yet.
      Both scripts now REQUIRE that the recording holds the owner's get-up and that the copy lay flat, and judge the
      body against its feet (`npc slide below 100`, `npc standing`). Red run 2026-10-17 on a build with the fix off:
      `live-copy-bleedout` RED (the lying copy carried 1287 units along the floor, still lying at the end), the slide
      players saw.
      Seen in play 2026-10-17 evening (ce86908): the downed NPC stood back up, then jumped a little.
      The copy gets up where it fell and then followed its owner's NPC at once. Red on c09bd58: up 219 units from where
      its owner had it, crossed in one frame (12389 units a second). Since 0ff0808 it walks the gap at up to 300 units a
      second on top of its owner's motion (a gap over 512 still closes at once). Rig on 0ff0808: `live-copy-knockdown`
      5 of 5 (325 units, at most 301 units a second), `live-copy-bleedout` 4 of 4 (371 units, at most 464); both also
      still pass the slide and standing checks. The legs play the owner's animation meanwhile, so with the owner
      standing still the copy may glide for about a second; to be judged in play.
      To see in play: knock an essential NPC down, let it get up, watch it on the other player's screen: no jump.
      Read there: `KnockDown: ... is up again after N ms, D units from where its owner has it` and `... moved at most
      N units a second in its first N ms up` (a few hundred; thousands is a jump); `... still lies after 15 s` means a
      copy still stuck.
- [!] **Player 2's crash at 20:55** (not connected, 2 s after a load in Sovngarde): a freed scene-graph node visited by
      culling, the same family as KNOWN_ISSUES's +0xCBFD24; nothing of ours in it. Needs his
      `logs\crash_UTC_2026-10-16_18-55-15.small.dmp`, sent by hand (it is a day old by the next session).
      collect-logs left the small dumps out (it looked in the client folder, not its `logs`); since 97b6cd0 it takes
      the two newest small dumps (two crashes, not one crash's two dumps), and the client deploy copies the players'
      scripts (collect-logs, setup-connect, update) into the tools folder each time, so an old one is never sent.
- [ ] **CommunityShaders.dll+0x1208c8 (read at 0x144) at a loading screen**: five times in the rig (2026-10-12 three
      times, 2026-10-16 once, 2026-10-18 on `cow` out of Shroud Hearth Barrow back to Mistwatch), always the first
      instant of a load, never yet in play. Nothing of ours
      in it: no deleted actor in a register, the render thread, and the actors spawning that instant are the game's
      own cell load. Watch for it in play.
- [ ] **Freeze in the Alduin fight** (player 1, 2026-10-17 08:10): no frame from 08:10:05, music still playing,
      SteamVR "Waiting"; no crash entry. The watchdog: the main thread waits inside the game (`SkyrimVR+0x5b8d4c`, the
      main-loop frames the Community Shaders crashes also go through), and the thread that last ran our update waits
      inside the game too; none of our code on either stack. What the main thread waited for is not in the log. Since
      f56dc9e the watchdog writes every thread's stack to `logsreeze.small.dmp` once per stall (one file, the latest
      freeze; collect-logs takes it). To see in play: after a freeze, that file.
- [ ] **HIGGS crash on a scripted grab** in the rig (higgs_vr.dll+0x605ae, 2026-10-16 22:09): HiggsVR.GrabObject on a
      plate placed by PlaceAtMe a moment before. A test-only call; noted in `live-world-object-grab`.
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
      position`). With the revive build, a respawn goes to the last safe point instead (`RespawnPlayer: at the safe
      point in cell ...`), and the override only applies without one.
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
      Measured in play 2026-10-17 morning (fixed 225 ms): the newest update was a median 198 ms ahead of playback, so
      the network used about 27 ms of the 225.
      Session A, in 3d63d8e (deployed, client and server; no protocol change, but both players need it, since the
      relayed tick now means the owner's): TiltedConnect at e6f5759; the server relays each movement message at once with the owner's tick
      (the 50 Hz batch is gone, and with it the dropped `Flying` bit); each remote actor plays at its own delay
      (upstream's PlaybackClock and its tests, extrapolation up to 150 ms with the error faded out); the defender's
      rule takes the attacker copy's real delay. Still reliable: unreliable movement and actions split out are session
      B, after this one is seen in play. Rig (100 fps): the bot's copy is shown 52-69 ms behind (225 before), the
      newest update a median 31-42 ms ahead, 0-12 of 1000 updates with nothing ahead; live-walk 8/8, live-defender 22/22,
      live-blade 6/6, live-creatures 7/7. With 40 ms lag, 2% loss and 10% late on the bot: delay 108-128 ms, 3-4% of
      updates extrapolated, live-walk 8/8. Lines: `PlaybackDiag:` every 10 s per remote player, read by
      `Tools/VR/session-report.py`. To see in play: the other player's hands, sword and walk closer to real time and as
      smooth as before; blocks still blocking (`Defender's rule: ... is blocked`).
      In play 2026-10-17 evening (both on ce86908, which holds session A): the other player shown a median 74 ms behind
      (player 1) and 76 ms (player 2), 90% of readings under 88 ms; 0.3% and 1.7% of updates with nothing ahead.
      From 22:03:10 to 22:03:50, fighting right after player 1 was revived, player 2's delay for player 1 sat at its
      250 ms cap (jitter up to 344 ms); gaps of up to 2.6 s with nothing ahead came at 22:08 and 22:13. Causes not
      looked at; if it shows again, compare with player 1's frame times at that moment.
      Blocks still decided: 9 blocked, 26 landed. No complaint about movement from the players. Next: session B
      (unreliable movement, actions split), a protocol change.
- [ ] **Threads and stutters:** the `Health` lines near 200 threads in both games once Seen is updated (Waiting on a
      test or action); Seen's freezes (no frame for 46 s, 2026-10-11 18:11 and 21:36) gone with it.
- [ ] **Crash lines on a real crash:** `CrashPlace: ...` (send the log's path to the launcher worker, which parses it)
      and `small crash dump ... (N KB)`. Watch for the PLANCK travel crash (`+0x3AC1A8`, `+0xCBFD24`), not seen since
      2026-10-03.
- [ ] **The other player's body no longer turns with their head** (3d63d8e): the heading sent is the VRIK body's facing
      (from the thighs) instead of the headset's, the pose is captured relative to it, and the walk direction values are
      re-based on it; no protocol change. Rig: `live-head-turn` 5/5 (a 35 deg head turn with the hands still turned the
      copy 9 deg), `live-walk` 8/8. To see in play: the copy's hips and legs stay still while its player looks around;
      walking forward, sideways and back still plays the right legs (the rig cannot show that: its body and headset
      differ by only 7 deg); `HeadingDiag:` sent heading off the body near 0 and off the headset during head turns.

## Waiting on a test or action

- [!] **Two crashes of ours in a public player's report** (v1.9.5 release; hub report 20261021-231708-78ce801d, named
      with the release pdb).
      - `+0x5785B91`, 9 minutes in, as the game connected: `CharacterService::RequestServerAssignment` read
        `pActor->parentCell->formID` for an actor held outside any cell (`mov edx, [rdx+14h]` after loading the cell at
        +0x60). Called from `OnConnected` > `ProcessNewEntity`. Now such an actor is not announced (`Actor X has no
        cell`).
      - `+0x117C20`, a read at 9 a second after a load: inside the game's `InventoryChanges::RemoveItem`, called by
        `DroppedItemService::OnNotifyDroppedItem` placing another player's dropped item. The placement gives the
        item to the player, then drops it again with the same extra data list. If the game merged that list into an
        equal stack, the drop handed it a list it no longer held; not proven from the dump. Now the drop uses the list
        while the inventory still holds it, else an equal one, else none (`the game did not keep the item's extra
        data as given`).
      - Also: an item whose enchantment this game cannot find was given an empty enchantment (`TP_ASSERT` only
        logs); it is now given without it.
      - Build 0567985 (client, FUS and MGO folders). Not reproduced in the rig, which is down (item below). To see:
        no more crashes in these two places in the hub.
      - Release gate for v1.9.6 on 0567985: bot pairs `run-all-pairs` 14/14; rig `live-door-echo` 6/6, `live-equip`
        9/9, `live-copy-removed` 4/4, `live-revive` 10/10, `live-death` 9/9, `live-load` 9/9, `live-teleport-far` 5/5,
        `live-teleport-far-loaded` 5/5. A first `live-load` froze in the rig's own VR plugins
        (`skyrimvrtools.dll`, PapyrusVR, with Crash Logger hung on it), nothing of ours on the stack; it passed on a
        fresh rig.

- [!] **The rig needs its null driver copied back after the SteamVR 2.18.4 update** (2026-10-21 20:42). The update
      put Valve's stock `driver_null.dll` (no controllers) back into
      `SteamVR\drivers\null\bin\win64`. `headless.ps1 up` now stops at `the null driver did not add its
      controllers`. The rig's build of it is copied there by hand, as on 2026-10-01; Valve's original stays as
      `driver_null.dll.valve`.
      - With 2.18.4 and `activateMultipleDrivers`, the PlayStation VR2 add-on is also loaded a second time once the
        null headset is active, and that second cleanup never returns. `headless.ps1` now switches it off for the
        run when `steamvr.vrsettings` has no setting of its own for it, and takes it out again at `down`.
      - After that, SteamVR started (monitor and compositor up) but launched Room Setup: the null headset's room was
        uncalibrated after the update (`calibration state was 201`). Not yet seen whether that stops a run.
      - Waiting on it: the red and green runs of `live-teleport-far-loaded`, and `live-equip` for the inventory
        change.
      - Also waiting: `Tools/VR/light-compare.py`, for the players' report that the game is darker with the mod.
        Same place, noon, clear weather, a fixed free camera and the HUD hidden; the game's screenshots measured by
        mean luminance, three ways: without the mod (`up -Shortcut SKSE`), with it offline (`up -NoConnect`), with it
        connected (`up`, `--connected`). Code read so far: our plugin has no weather, image space, lighting or
        exterior cell records, and the client only reads game settings. While connected, a game keeps the
        server's clock, and a party member gets the leader's weather. The VR death fade passes its first argument
        inverted, which only matters after a death.
- [!] **The connect request of a large mod list was cut short** (both players on MGO 4.0 RC4.1, 1992 plugins,
      2026-10-20 evening). `TransportService::Send` wrote every message into a 64 KB buffer from a 256 KB scratch
      area. The connect request carries every plugin's name (about 78 KB for MGO's list), so it was cut after the
      1792nd name without any error. Everything written after the list was lost too: username, worldspace, cell,
      level, player time, and the hide-from-public-page flag. That's why the server called both players `'Player'`.
      Neither game could map the last 200 plugins, among them AlternatePerspective.esp, DynDOLOD.esp and
      SkyrimTogether.esp. So their forms travelled as Skyrim.esm's (AP's inn C6005900 became `0:5900`), and
      `Failed to find mod , is lite?` was logged 200 times at connect.
      - Effects: Ingrid in the inn for one player and in the street for the other; stand-ins made for AP guards
        under the wrong id, so each guard had two bodies; AP items and actors not found.
      - Now `SendBuffer` (`Code/client/Services/SendBuffer.h`) gives each message a 1 MB buffer from a 4 MB
        scratch area. A message that still doesn't fit is not sent and is logged.
      - No protocol change: `Code/encoding` is untouched.
      - `TPTests [sendbuffer]`, a 1992-plugin connect request written the way the client does: red with the old
        sizes (last name, username, cell, level and the hide flag all empty), green with the new ones. 58 test cases
        pass.
      - Build bb793e2, deployed to the tools folder.
      - Rig, 2026-10-21: `live-door-echo` 6/6. The server logged `New player 'Queen Emma' ... connected with 300
        mods`, and the client logged no `Failed to find mod`. The rig plays FUS, so it can't show the 1992-plugin
        case; the unit test covers that.
      - Each client sends its own list, so both players need the build.
      - Also: `Object not found for form id` printed true or false instead of the id; it now prints the id and the
        server's mod and base.
      - To see in play on MGO: no `Failed to find mod` lines at connect, the right names in the server log, Ingrid
        and the AP guards the same in both games.
- [!] **Every game patch checks the bytes it replaces** (from comparing with the Cydaphex port, 2026-10-21). Six
      patches wrote over game code without looking at it first: the call that lets menus run unpaused
      (AddToActiveQueue), the intro movie branch, the favorites branch, the jump that replaces SetThreadName, the VR
      frame end's StopTimer call (VR body sync runs from it) and the projectile handle read. Written over other
      bytes, any of them would corrupt the game's code. That happens when another mod has already patched the same
      place, or when an address id doesn't resolve: the old code then wrote at a small address and crashed.
      - Now each one checks first (`Code/client/PatchCheck.h`): a call whose target is executable code, a two-byte
        conditional jump (or the same patch already made), or the exact instruction. When the check fails, the patch
        stays off and the log says `... leaving it alone` with the bytes it found. That turns one feature off
        instead of crashing.
      - `TPTests [patchcheck]`: calls into data or reserved memory, a near jump and other bytes are all refused. 61
        test cases pass.
      - Build c6524b0, deployed to the tools folder.
      - Rig, 2026-10-21: no `leaving it alone` line at startup. `live-menu-hold` 3/3: the inventory "runs unpaused",
        pause counter 0. `live-hands` 3/3: 16 hands posed from the frame end, median 66.1 units against the
        script's reference of 65.
      - Nothing to see in play. To watch for: a `leaving it alone` line in a player's log names another mod that
        patches the same place.
- [!] **A copy whose owner goes quiet walked on the spot** (from comparing with the Cydaphex port, 2026-10-21). A copy
      is drawn from its owner's newest report until the next one comes. When none came (a crash, a frozen game, a
      stalled connection), the newest report's walk played on the spot every frame, until the server removed the
      player about ten seconds after a crash.
      - Now, after 1.5 s without a report, a walking copy on its feet (not sitting, riding, sleeping or flying) gets
        the graph's own `moveStop`, and its speed is held at zero.
      - When reports say it moves again, it gets `moveStart`; it would slide otherwise, since its owner's own start is
        long gone. The first reports back can say it stands and then speed up, so it is watched until it moves. One
        that stands through 2 s of reports is left to them.
      - The log says `QuietStop: copy X had no report for N s while moving` and `... moves again, N ms after its
        reports came back`.
      - New rig test `live-copy-quiet`: a bot walks a real player's recording, sends nothing for 7 s, then walks on
        without the recording's actions. The copy's feet are read against its pelvis.
      - Red on c6524b0: the feet swung 56 units while the bot was quiet. Red again on cfb43c9 for the restart: the
        first reports back said speed 0, so no `moveStart` went out, and the copy slid (feet 20 units).
      - Build 587f7d8, deployed to the tools folder. Green: walking 57 units, quiet 0, walking on 77 (started 85 ms
        after reports came back). `live-walk` 8/8. `live-creatures`: 6 pass, 1 skipped, because the game's own
        lurker never moved.
      - Not seen in play yet. To see: when the other player's game crashes or freezes mid-walk, their character stands
        instead of walking on the spot.
      - Not tested: an owner in a menu that pauses the game (the journal) keeps sending reports, so this stop never
        applies there. If they opened it mid-step, their copy may still walk on the spot.
- [!] **Loot taken by hand off a body the other game runs stayed on that game's body** (from comparing with the
      Cydaphex port, 2026-10-21). A hand takes loot off a body through the game's own drop (HIGGS does): the item
      comes out into the world, with no container in between. For a body another game runs, only a container
      transfer to the player was sent, so the owner's body kept the item and its owner could still take it there.
      One item became two.
      - Now a drop out of a dead body (dead, not dying: a dying body's drop is the death drop each game makes for
        itself) of a non-player that another game runs, within 400 units of this player, is sent with the owner's
        token (`Utils::GetBodyTakeToken`, both drop paths: `RemoveItem` and `Actor::DropObject`). The server already
        takes such a change from a nearby player, as for looting, and passes it on to the owner.
      - The log says `BodyTake: X xN taken off Y, a body another game runs`.
      - New rig test `live-body-take`: the bot owns a dead NPC carrying an iron dagger (`additem ... npc`), and the
        game drops the dagger off its copy next to the player.
      - Red on 587f7d8: the bot, still the owner, got no change for the dagger. Build cdec699, deployed to the tools
        folder. Green: the bot got the dagger's -1. `live-game-drops` 2/2.
      - Also fixed in live-check: `DO pickup own` called `Activate` with one argument, which DevBench now refuses
        (HTTP 400, nothing run), so `live-game-drops` failed without testing anything.
      - Not seen in play. To see: one player takes a weapon off a body with their hand, and the other player no
        longer finds it on that body.
      - Not tested: that the game's own death drop happens while the body is dying (it is left out on that
        assumption, as the Cydaphex port does). Not covered: the item taken exists only in this player's world. If
        they let it fall instead of keeping it, the other player never sees it lying there.
- [!] **Papyrus natives never called while missing** (a public player on v1.9.4, 2026-10-18 16:40-17:13 their time,
      on the Synergy VR ENB list: four crashes out of four, each 12 ms after a save finished loading). The first door
      an NPC used after the load crashed the game: `HookActivate` asked `ObjectReference.GetOpenState` for the door's
      state, and the pointer looked up for it was null (execute at 0x0, return address `HookActivate+0xab` in the
      v1.9.4 pdb). The same code ran on their v1.9.3 session without a crash, so on that setup the function was
      missing from what we recorded; why is not in the log. 63fc8efe guarded `GetOpenState` alone. All 18
      `PAPYRUS_FUNCTION` calls (Delete, Enable, SetMotionType, StartCombat, Kill, PlayAnimation, SetCurrentStageID and
      the rest) had the same flaw: looked up once, called even when null. Now each wrapper looks its function up until
      it is found, never calls a missing one (the call does nothing and returns a default value) and logs the first
      miss (`Papyrus: X.Y not found among the N functions recorded; not called until it is`); the record is locked
      against the worker threads that read it. Each log now says `Papyrus: N native functions recorded at the first
      call` (2938 in the rig). Build 36cf307 (client and server deployed, no protocol change). Rig: `live-door-echo`
      6/6 (doors, no function missing), `live-copy-removed` 4/4 (Delete after the wait). Release gate for v1.9.5 on
      36cf307: bot pairs 14/14, rig `live-load` 9/9, `live-revive` 10/10, `live-death` 9/9, `live-teleport-far` 5/5,
      no crash in the runs. No red run: the rig records
      every function, and a missing one cannot be made there. To see: a public log with the count line, and no crash
      at the first door after a load. Their Root Builder and game-folder damage came from the crashes (Root Builder
      puts its files back when the game exits normally): Root Builder's Clear, then Steam's file check.
- [!] **Healing perks on another player** (protocol bb9f5d95; every player and the server need the build): a heal or
      buff cast on another player carries the caster's own perks. Before, the healed player's game applied it with its
      copy of the caster, which has no perks, and only a flat 1.5 for perk 581F8 came through; in FUS that record is
      Adamant's Light Weaver, not a healing perk, and Adamant's healing perks (Healer, Recovery) never arrived. Now the
      caster's game measures what its perks did (`ActiveEffect::AdjustForPerks`) and sends that factor
      (`CasterPerkFactor`); the healed player's game applies it in the same function. The message's magnitude is not
      what the effect uses: a bot heal of Heal Other gave 40 whether it said 10, 30 or 80.
      Rig `live-heal-perks` 9 of 9, bot 6 of 6: factors sent 1.50 (the rig character's Recovery 20) for Heal Other and
      Healing Hands, 1.95 with Healer 50 added; the bot's Heal Other on the game's player gave 40 without a factor and
      60 with 1.5. Not seen in play.
      To see in play: heal the other player with Healing Hands or Heal Other; the caster's log has `Magic: spell ... on
      remote player ...: the caster's perks make it N times as strong`, and the healed player gets what an NPC would.

- [!] **Revive and respawn** (merged 2026-10-17; with the review fixes protocol 33ef1cbe, so every player and the public
      server need the build): a player at
      zero health stays down; a party member who holds a hand on them for 4 s revives them (up with 30 % health, 3 s of
      god mode). The server ends every wait (ReviveService, rules in ReviveRules): not revived within
      `Gameplay:uDownedSeconds` (60), nobody in the party left standing (all down, or the others left the party), or down
      alone (at once). The player then respawns at the party leader's last safe point, else its own, else the game's old
      spot. A safe point is where the game would have autosaved: after a loading screen (a load door, fast travel, a
      loaded save), a sleep or wait, or on connecting, never in combat. No world reset: the owner asked for "a better
      respawn", not the last save. A refused revive tells the reviver why, and the hold starts over; a member who joins
      while someone is down is told; a save loaded while down stands the player up, with no respawn and no gold lost.
      Found and fixed in the rig: a connection dropped while down let the game kill the player and load the last save
      (the health now comes back first); on VR the respawn's knock (upstream `KnockExplosion`) threw the player
      thousands of units (5761 in one run), so VR skips it. The messages are HUD text until the "You died" screen is
      decided. Tests 2026-10-17 on the final build: `TPTests [revive]` 5 cases; bot pairs `downed` (in run-all-pairs;
      its watcher failed once in 5 runs, cause unknown, the server's side of that run was right), `partyfall`,
      `downedtime`, `downedleave` (alone), `run-all-pairs` 14/14; rig `live-revive` 7/7, `live-revive-hand` 5/5 (only right after
      live-revive), `live-revive-timeout` 12/12 (back within 1 unit of the safe point; a VR bleedout lasts the whole
      60 s), `live-partyfall` 9/9, `live-down-alone` 5/5, `live-death` 9/9, `live-down-disconnect` 9/9,
      `live-safepoint-travel` 4/4. Not run: `live-respawn-cell` (the coc out of Mistwatch hit the known Community
      Shaders crash), `live-load` and `live-load-dead` (a load after a death crashes the rig). Not seen in play.
      To read in a session: `PlayerService: safe point in cell ...` (where they are made), `RespawnPlayer: at the safe
      point in cell ...`, the server's `Revive: ... respawning at the party leader's safe point`, `Revive: holding a
      hand on player ..., N units from its body` (the 50-unit reach is a guess), `the revived player is up; health N`.
      Open: whether a downed player's copy shows the bleedout pose on the other side; whether a VR player who died
      ragdolled gets stuck now that the respawn does not knock it; a safe point in an interior (not tested in the rig).
      Not done: the revive gesture (options first, as the owner asked).
      Review fixes (2026-10-17, ce86908): a downed player who leaves the party or the server is announced to the members
      told it was down (`NotifyDownedState::kLeft`; before, they kept trying to revive it); a respawn this game starts
      on its own ends the down on the server at once and goes to the server's respawn point (`PlayerDownedRequest`
      `Respawning`, answered `kRespawned`); the last one standing going down is no longer announced as "down" before
      "party fallen"; the "party fallen" message only when it applies; a disconnect ends a revive's or respawn's god
      mode and fade-in and forgets this game's safe point; a hold ends 20 units past the reach that starts it; the bot's
      `safepoint` and `respawnpoint` no longer throw. Bot pairs, each failing on the build before the fix: `partyfall`
      (new `downcount` check), `downedgone`, `downeddrop`, `downedgiveup`; all eight revive pairs and TPTests pass on
      ce86908. Rig on ce86908 (a regression check: none of these was written to fail without the client fixes):
      `live-revive` 7/7, `live-revive-hand` 5/5, `live-revive-timeout` 12/12, `live-partyfall` 9/9,
      `live-down-disconnect` 9/9. Open, for the author: the VR respawn knock skipped instead of fixed; any loading
      screen taken as a loaded save (a teleport during the fade calls the respawn off; `TESLoadGameEvent` is exact);
      `live-revive-hand` passes from the resting hand, never the held one.
      After the play session of 2026-10-17 evening (the owner's choices, 440f5e0, no protocol change of its own):
      while down, a slow double heartbeat in both controllers and the game's heartbeat sound (`UIHealthHeartbeatALPSD`);
      on a revive one strong pulse in both hands and the restoration spell's sound (the game has no breath-in sound);
      both grips held 3 s gives up the wait (a rising buzz; letting go cancels; the server answers with the party's
      respawn point, as for any respawn this game starts); the server ends the wait at once when every standing member
      is in another interior or worldspace or over 7000 units (100 m) away, checked on going down and every second;
      "YOU DIED" in red on a head-locked overlay as the screen fades, then the place's name in gold for 3 s on waking.
      The grips are read from OpenVR and from the game's own controller devices (Oculus Touch reports the grip as
      Axis2, so both buttons count). Tests: `TPTests` 52/52 (new case for the distance rule); bot pair `downedfar` red on
      28995d2 (the actor stayed down), green with all nine revive pairs on 8b9d192 (its server is 440f5e0's); rig
      `live-revive-giveup` red on 28995d2, 12/12 on 440f5e0 (a 1.5 s hold let go cancels, the 3 s hold respawns 1 unit
      from the safe point, both banners logged); on 8b9d192 (same cues, grips read from OpenVR only) `live-revive`
      10/10 (heartbeat on and off, revive sound played) and `live-blade` 6/6 on 440f5e0 (the clash sound moved).
      In the rig the grips came from the game's devices (`OpenVR 0, game 2`); the OpenVR read, the feel of the pulses,
      the sounds' volume and the banner's size and place are only judged in a headset. Not seen in play.
      To read: `both grips held while down (OpenVR N, game N)`, `the player gave up the wait`, `heartbeat ... playing`,
      `the heartbeat sound stopped`, `VRBanner: '...' shown`, `the revive sound did not play` (should never show), the
      server's `is down with every standing member elsewhere or too far to come`.
      Death card, death line, downed sound, "is reviving you" (the owner's request of 2026-10-18; build 0bb07c2, client
      and server deployed; **protocol change**: `ReviveRequest` gained a hold stage, `NotifyDownedState` two changes, so
      every player and the public server need this build). The "YOU DIED" banner is now a card: the title in the game's
      Gaelic book letters between thin rules under an emblem, and below it how the player died in spaced capitals, as
      Minecraft says it ("Lydia was slain by Bandit Chief using Iron War Axe", "was shot by", "was killed by X using
      <spell>", "was bashed by", "fell from a high place", "was doomed to fall by", "died"). The killer comes from the
      game's hit events on the player (`DeathCause`, the last blow up to 10 s before going down) or from another
      player's network hit; a fall is a 500-unit drop within 1.2 s before going down with no blow in the last 0.5 s.
      Going down plays the sneak attack's sting (`UISneakAttack`; the Slow Time shout's sound would not play, with or
      without a position). The reviver's game says when its hand goes on and comes off; the server passes it to the
      downed player only, who sees "<name> is reviving you" in gold until the hand comes off or the revive lands.
      Tests: `TPTests` 57 cases (new `[death]` sentence cases, `[revive]` round trips for the hold stages, `[banner]`);
      rig on 0bb07c2 `live-revive-held` 8/8 (down sound playing, held and let-go lines, revive), `live-death-line` 9/9
      on the second run ("<character> died" by console, "was slain by Netch" from a Netch's blow; the first run on
      0bb07c2 was INVALID, the Netch never attacked), on a7e448d `live-revive-hand` 5/5 (the bot told it was held);
      on 77e162e the same death-line run also gave "was killed by Netch using Shock Touch" (spell path). The card the
      game drew is `logs\banner-death.png`. The new checks are new log lines, so the earlier build could not pass
      them; no red run. Not checked in the rig: the fall line, a bow's "shot by", another player's name (PvP), and
      how the card and the sting look and sound in a headset. Not seen in play.
      To read: `death card: ...`, `the last blow before going down: attacker ...`, `DeathCause: the game's hits on the
      player arrive`, `the down sound playing` (NOT playing would be the sting refused), `is reviving this player`,
      `stopped reviving this player`, the server's `Revive: player N '...' has a hand on` / `took the hand off`.

- [!] **Quest items lying in the world reach the other players** (player 2 stuck at Korvanjund's claw door,
      2026-10-18 21:49; build 154378b, client and server deployed, no protocol change). The keyhole script checks the
      local player's own inventory for the claw. A claw looted from a body reached the other game through the
      inventory sync, whose receiving game hands its own player one (the Golden and Diamond claws worked). A claw
      picked up from the world (the Ebony Claw `5AF48`, reference `5AF49`) was sent to the others only when the
      reference was temporary, so player 2 never got one and a reconnect could not help. Now a pick-up whose
      reference a quest alias holds as a quest object is passed on too (`QuestItem: picked up quest object ...; sent
      to the other players`). Rig: `live-quest-item` (TGTQ01Handler's 44E13 in Pinewatch, the bot in the same cell
      counting what the server passes on) red on 0bb07c2 (0 changes passed on), 3/3 on 154378b (the change arrives
      flagged as a quest item). The receiving game's hand-over to its own player needs two games: not checked in the
      rig. Likely the same before: the Emerald, Ruby, Glass and Sapphire claws and Valthume's. To see in play: one
      player picks up a claw lying in the world, the other's game logs nothing odd and the other player has a claw;
      both doors open. A player stuck like this: `player.additem 5AF48 1` (the Ebony Claw), then the keyhole.
- [!] **No more pending script disables in saves** (player 2's crash at 21:51, `SkyrimVR.exe+0x1939100`, a write
      to 0x1 in RTTI data, called from `SkyrimScript::DisableFunctor`; build 154378b). Our disable of a copy went
      through Papyrus's `ObjectReference.Disable`, which queues a task in the script engine. A loading screen opened
      56 ms after a held copy was disabled, the autosave kept the task, and loading that save ran it on form
      FF00126F, by then a base NPC: the task called a reference's function on it. 776fac00 made such disables
      frequent (every copy removed while held). `TESObjectREFR::Disable` now calls the game's own disable
      (`DisableImpl`, already used by the leveled-NPC reconciliation), for copies, dropped items and projectiles
      alike. Rig on 154378b: `live-copy-removed` 4/4 (copies disabled and queued as before) and the raced copies of
      `live-copy-door-race` read `IsDisabled` true. No red run: a save made in that window is not scriptable here.
      Settled by player 2's `Autosave3_..._Korvanjund02_..._20261018194801` (.ess and .skse), read without loading it:
      it should hold a DisableFunctor for FF00126F. Player 2 must not load that save. To see in play: no crash at a
      load right after a player left the area.
- [!] **A copy removed before it loaded is removed** (player 1's copy naked in front of Dragonsreach in player 2's
      game, 2026-10-18 21:08; build 154378b). Player 2 arrived by fast travel as player 1 went through the door: the
      server sent player 1's character, then removed it, before player 2's game had recorded the copy. The removal
      then only stripped the copy's components, so the game kept the actor, announced it as an NPC of its own, and it
      never got its gear. Now the removal finds the copy by the id the spawn kept (`Removed before loading: server
      character N is gone before its copy N loaded; the copy goes too`). Rig: `live-copy-door-race` (the bot comes
      back into view and enters Dragonsreach 60, 40, 25 and 10 ms later) red on 0bb07c2 (two raced copies left in the
      game, enabled, not deleted), 5/5 on 154378b (four removed before loading, all disabled, no copy claimed, the
      copy dressed when the bot came out). To see in play: no naked copy of the other player after arrivals at doors.
- [!] **Player 2's Engine Fixes VR:** set `FormCaching = false` and `TreeLODReferenceCaching = false` in
      `EngineFixesVR.ini`, as on the host since 2026-09-30. His crash at 21:34 (`SkyrimVR.exe+0x93CE11` while loading
      a save, KNOWN_ISSUES section 6) took a freed form from a `tbb::concurrent_hash_map` node (R8: key 0x12F6, the
      stale pointer as value), the layout of Engine Fixes VR's form cache; the game's own form map looks different.
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
      An empty name and level 1 on every copy is also what a cut connect request produces (see "The connect request
      of a large mod list was cut short"). Check that server's `connected with N mods` line: from about 1800 plugins
      up, it was this.
- [ ] **Our browser helper outlives a crashed game and freezes Mod Organizer** (2026-10-20 evening). After the MGO
      game crashed at 21:18, one `TPProcess.exe` (Chromium's "on-device model" utility, started 21:04, parent the
      dead game) kept running. It still held MGO's usvfs (0.5.6.1) loaded. FUS's Mod Organizer, started at 22:21, then
      waited for good inside its own usvfs (0.5.5.1) at startup: no window, log stopped after "checking for blocked
      files". A PC restart cleared it.
      - Likely what the Synergy players reported as Root Builder broken after crashes.
      - The helper's entry is `Libraries/TiltedUI/Code/ui_process/src/OverlayProc.cpp`.
      - Two options: the helper exits when the game process ends (wait on the parent), or the on-device model
        service is switched off in CEF's command line (we never use it). The first covers every helper.
      - Not reproduced yet: a rig crash test, then a check that no TPProcess is left.
- [ ] **Smaller items from the first MGO session** (2026-10-20 evening):
      - The Helgen prisoner carriage crashes HIGGS when grabbed (`SkyrimVR+0xADF694`, both games). HIGGS walks the
        carriage's links and reads its anti-tip action (`bhkTiltPreventAction`) as one. Not our code; don't grab it.
        Unproven without our mod.
      - A copy the server removes in a cell the receiving game can't reach left the loaded real reference
        unannounced (Ingrid on player 2's side: `OnRemoveCharacter` strips the components and nothing announces it
        again).
      - `Successfully teleported actor` is logged even after `MoveActor: failed to fetch cell` (`CharacterService.cpp`
        about 2263).
      - Lokir lay dead in player 2's copy while alive in player 1's game; it was corrected at 21:18:43 ("came back
        alive").
      - An ice wolf (33482) bounced owners and its body jumped about 8000 units into Helgen at 21:08; the cause is
        unknown.
      - VR players connect with F6 and never see the connect page's "don't connect during the Helgen intro" warning.

## Open work

- [ ] **Fourth MGO session, 2026-10-22 20:03-20:31** (both players on v1.9.6, host's server; player 1 hosted). Helgen
      from the cart to outside the cave, with Ralof. Smooth for most of it, no crash. Logs read; nothing changed yet.
      - Stutter outside is memory, not our code. Inside: 31-41 fps. From the cave exit at 20:26: private memory
        23 to 31 GB on player 1's PC (31 GB of RAM), 28 to 31 GB on player 2's. There, 22-29 fps, 35-71 frames over
        50 ms per 30 s, p99 up to 233 ms, and a 25 s load at the exit. Our update stayed at 0.34-0.63 ms.
      - Ours, bodies flying: three NPC copies lay dead in player 2's game while their owner (player 1's game) still
        had them alive. No death ever came from the owner: no `Death sync` line for them. The corpse placement pulls a
        dead copy to its owner's position, and here that position belonged to a living NPC on the move. Bandit 977F7,
        outside the cave, was pulled 125 times in 3.5 minutes, from 67 units off up to 8505
        (`CorpseDiag: ... moved there`). B1694, in the keep, went on firing at its owner's while lying dead here.
        977F6 was the third. Each new owner position starts new tries, so the "left there" stop (two moves that did
        not bring it nearer) never holds.
        - Not known: how the copies died here. No `health broadcast killed` line for them; `NPC 977F7 health
          corrected from 35 to its owner's 10` four seconds before.
        - Candidate fix (not made): the corpse is placed only once the owner's game has said the NPC is dead (a death
          sync, a spawn that came dead, or the health broadcast). Without that word, the copy lies where it fell.
          Bringing it back to life instead would be closer to the truth, but `Respawn` re-rolls a leveled NPC. Red
          first in the rig: a copy killed here with papyrus while the bot walks it alive.
      - Ours, a body changing hands: 5CEEE (Imperial soldier) died in player 1's game. Player 2's copy was pulled 343,
        195, 343 and 175 units (`CorpseDiag`), then kept moving. Player 2's game then took it with a hand within
        40 units (`Body 5CEEE ... is being moved here; asking for it`). The server logged it as a `party leader
        claim` at 20:15:03. Player 1's copy was then pulled 300-564 units. Possible fix: ignore motion for 1.5 s
        after our own corpse placement, as is done for the owner's moves. Better: require a real HIGGS hold on the
        body (`HiggsBodyHolding`), but that reads Havok outside its step.
      - Ours, Ralof flickering in player 2's game. Player 2's game ran its own AI on its copy of Ralof (player 1's).
        That AI walked the copy through load doors: 909D2 into the keep (20:11:49-20:12:04) and 91608 out of the cave
        (20:25:53-20:26:22). Each time the copy went to the other side, was removed and was spawned back: 15 removals
        in 3 s cycles (`Activation of 909D2 by 2BF9E, another game's actor, not sent` / `Actor removed, form id:
        2BF9E`). Candidate fix: a remote copy never goes through a load door in this game (blocked in the activation
        hook); the owner reports where it went. The owner's own activation, replayed through `OnActivateNotify`, would
        stay allowed.
      - Ours, a door activation dropped: 938F3 opened in player 1's game by a Stormcloak soldier player 1 owned
        (FF001B35, server id 100086). Player 2's game had no copy of that soldier: `OnActivateNotify: could not find
        actor server id 100086`. The door stayed shut there.
        - Fixed in build a547eb5 (client only, no protocol change; FUS tools folder): a door that does not lead
          elsewhere is set to the state it ended in at the sender (`ObjectReference.SetOpen`; the message already
          carries the state before the activation). It is no longer used again with the actor's copy. No actor is
          needed, and setting it is not an activation, so nothing is sent back: two games cannot pass a door to and
          fro. Opening a door that is locked here also unlocks it (the game's SetOpen does that). It was unlocked at
          the sender, since a locked door's use is never sent. Load doors keep the old way (the copy uses the door).
          Logged: `Door X: opened|closed in another game (by ...); set open|closed here from state N[, and unlocked:
          it was locked here]` or `already N here`.
        - Rig, `live-door-state` (new): red on f39baec6 (`could not find actor server id 7FFFFF0`, the door stayed
          shut), green on a547eb5, 14 checks. The test covers: opened by an actor the game lacks; the same opening
          again (nothing done); closed by the bot's copy; the game's own player still sends its use; a door locked
          here opens and unlocks. After each door the game sends nothing back (the bot counts activations). The
          regression test `live-door-echo` (load doors) passes, 6 checks. Not seen in play yet: both players need
          this client, since each game applies the doors it receives.
        - Still not made: the sender sends a door's use even when a script blocks it (BlockActivation), so the
          receiver can open a door the sender's game kept shut. That is harmless for getting through, and not seen
          in the logs.
      - Not ours, a scene limit: player 2 stayed tied. MQ101's stages did sync both ways (250-800, `Remote quest
        updated`). Freeing the player is a scene in each game: Ralof walks up to that game's player. In player 2's
        game Ralof is player 1's copy, so that scene never finishes. The same holds for the keep doors Ralof opens in
        his scene. With a party, the vanilla intro needs a console command for player 2, or an alternate start.
      - "Doors synced the moment a player joins": the server keeps no door open state, only locks and container
        contents per cell (`AssignObjectsRequest`). It would need the open state kept per door and sent on cell entry.
        That is a protocol change: the owner's call.
      - "NPCs not following" (about 20:21): nothing in the logs. No ownership changes then, no quiet stops. Quest NPCs
        follow the game that runs them (player 1's), not player 2.
      - Player 1 rejoined once (player 1, then player 2 on the server); player 2 was player 3. Player 2's clock runs
        about 6 s ahead of the server's.

- [ ] **Third MGO session, 2026-10-21 22:01-22:21** (both players on d7e7105, after tweaks to the list): a lot more
      playable. It ended when player 2's game crashed.
      - The crash, while player 2 put on the Imperial Captain's armor taken off her body: a call to address 0
        (`SkyrimVR+0x11D28E`, our handler: `in no module`). On the stack: HDT-SMP sending its armor-attach event,
        The New Gentleman's genital cover, an `Underwear.dll` plugin and Engine Fixes' allocator; nothing of ours.
        HDT-SMP also crashed twice on that PC in sessions without our mod (crash triage, 2026-10-21). Not ours as far
        as the stack shows.
      - Doors did not open for both. Neither log names the door: the receiver logged `Failed to retrieve object
        to activate` without an id (3 times on player 1's side, twice on player 2's), and skipped silently when the
        two games disagreed on a door's state. Player 2's game could not send one lock change
        (`FE0DC090`: light-plugin slot 0xDC is not in its table).
      - Fixed in build ac5cace (client, FUS and MGO folders, no protocol change): the sender skipped every object
        whose lock had any flag (`flags & 0xFF`). A leveled lock keeps its flag once unlocked, so such a door's
        openings were never sent. Now only the locked bit counts.
      - Logged now: each door activation sent (`Door X ... sent to the players in cell`), each one received (done,
        or left as it is when the states differ, and `locked here` when the copy's activation cannot open it), and
        the id of an object not found. Not rig-tested: the rig is down (SteamVR item above).
      - Still open, the owner's call: the server relays an activation only to players in the door's exact cell
        (one cell over outdoors, it never arrives), and a door locked here by this game's quest stays shut when the
        other player opened theirs.
- [ ] **Second MGO session, 2026-10-21 20:20-20:41** (both players on cdec699, 1992 mods each, names right: the
      connect fix works). AP's intro, then Helgen. Painful for both players.
      - Lag is the list in VR, not our code. Player 1 ran 24-32 fps before connecting (our update idle at 0.08 ms) and
        16-26 fps in the Helgen fight; player 2 17-33 fps. Our update cost 0.1-0.6 ms of a 35-64 ms frame. Private
        memory went from 22 GB in the inn to 29-31 GB once Helgen's exterior loaded.
      - Ours, two stalls, both fixed in build d7e7105 (deployed to the FUS and MGO folders, client and server):
        - 622 ms at 20:28:54: player 2's game let go of a wisp 17000 units away. The teleport moved player 1's
          copy, which stood loaded next to them, into that cell, and `TESWorldSpace::LoadCell` read the cell from
          disk in that frame. Now a teleport out to the exterior beyond the loaded area is not made: `Teleport of
          actor X not made: it goes out beyond the loaded area here`. Into an interior, it is made as before.
          `Successfully teleported` is no longer logged after a cell was not found.
        - 612 ms at 20:27:03: 26 copies arrived after the load, and setting their inventories took 436 ms
          (`inventory apply 1.15 ms/frame (26 calls)`). Setting one takes every item off and puts it back. Now
          an inventory that already holds the owner's items, worn the same way, is not set again
          (`InventoryCompare.h`). `InventoryApply: N of M copies' inventories already held their owner's items`
          says how often, every 30 s.
        - Tests: `TPTests [inventorycompare]` 2 cases, 63 pass. The new rig test `live-teleport-far-loaded` (Lydia
          next to the player, let go far away) is not run yet; see the SteamVR item below.
        - To see in play: no `Mod update took` over 100 ms when a lot of NPCs arrive or leave, and the
          `InventoryApply` counts.
      - Ours, a claim burst: right after a 25 s load out of the inn (20:26:21-20:26:46, main thread waiting in
        ntdll), player 1's game took 16 NPCs from player 2's at once (`party leader assignment`). It gave back
        those not loaded yet (`Declined ownership ... the actor is not ready`), and a minute of transfers back and
        forth followed. The server hands NPCs to the party leader on purpose, so quest NPCs follow the leader's
        game. A grace period after the leader's load would stop the jumps, but the leader's scene would run
        without its NPCs for those seconds. Not changed: the owner's call.
      - Ours, scripted scenes: each game ran AP's Thalmor ambush, so each spawned its own Thalmor and also got the
        other's as copies (twice the enemies). Their bodies were pulled 1000-2400 units to where the other game had
        them (CorpseDiag). Player 2 found no loot on them; the log does not show why.
      - Ours, NPCs popping in: the spawn path re-enables an NPC the other game has active and moves it to the
        player's cell (`Disabled actor is being re-enabled`, five AP Helgen guards at 20:36:28).
      - Ours, Alduin: player 2's game ran him. Player 1's copy refused the replayed `roarStart`, while his Thu'um
        voice lines were synced, so he roared without attacking.
      - Not synced: the Helgen keep door stayed locked for player 1 and opened for player 2. Each game runs its own
        scene, and neither log has a stage change for it.
- [ ] **Freeze at a load: the game's allocator purging cells waits for good** (player 1, 2026-10-18 21:29, fast
      travel near Whiterun's battle; the same main-thread stack as a public player's freeze at 08:52 and player 2's of
      2026-10-11 21:36). From the bottom: a model copied for a load (`NiObject::Clone`), our pass-through
      `HookFormAllocate`, `MemoryManager::Allocate`, a memory-manager callback, `TES::PurgeBufferedCells`, then a wait on
      a semaphore that never comes (player 1's handle checked live: a Semaphore, not signalled). Private memory was
      13.3, 15.5 and 16.3 GB; threads 703, 2494 and 152, so the thread pile-up below is not what they share. Engine
      Fixes VR's `MemoryManager` is off in FUS (`MemoryManager = false`), so this is the game's own allocator; turning
      it on replaces that allocator (our client chains to Engine Fixes' allocator), a mod-list setting the owner
      decides. The small freeze dump keeps too little of the other threads' stacks to name the one that should
      release the semaphore. Ralof spawned in the load of both the 08:52 and the 21:29 freezes.
- [ ] **Freeze after DynDOLOD's threads piled up** (a public player on v1.9.4, 2026-10-18 08:52 their time, same
      server as the crash reports; `freeze.small.dmp` with it). The game's threads went from 222 to 703 in 20 minutes,
      then the main thread waited in the game (`SkyrimVR+0x15bb0a`) for 100 s right after a loading screen.
      522 threads start in the C runtime's thread starter; in the freeze dump 188 of them stand in DynDOLOD DLL NG
      (Alpha-32, in FUS too): `DynDOLOD::ObjectsUpdate` with `LoadWorlds`/`LoadData` above it (nearest symbols of its own
      pdb), waiting; the other 334 had too little stack in the dump to say. They grew only in the minutes with our
      `Successfully teleported actor` lines (08:33-08:35, 08:38, 08:41-08:44) and stayed flat in the minutes without.
      Likely cause, not proven: our far teleports load far exterior cells (`MoveActor` > `LoadCell`), and DynDOLOD
      starts an update thread for each. The fix in 4c41069 (no teleport an actor and a place both not loaded here)
      would remove those loads. Rig, 2026-10-18 (DynDOLOD DLL NG Alpha-32 is in FUS too): four long jumps of the
      player (`cow`) left 2 to 4 more threads each (151 to 161), not identified; ten papyrus SetPosition moves of
      Lydia far away left none (that path may not load the far cell). Not yet tested: our own path, which needs
      persistent NPCs the bot owns let go far away (the server sends no teleport for temporary ones). The fifth
      jump hit the known Community Shaders crash at a load (`+0x1208c8`). Other options: DynDOLOD DLL NG Alpha-33
      exists (a mod list change), and the freeze dump could go to DynDOLOD's author.
- [ ] **A freed form cast in `DiscoveryService::VisitForms`** (player 2, 2026-10-18 17:35 local, on the v1.9.4
      release; hub report 20261018-195126). `VCRUNTIME140+0x4AE1` < `+0x50E5` (`__RTDynamicCast` in 14.51)
      < `VisitForms+0x4c1` < `OnUpdate` < `World::Update` < `HookVMUpdate`, with a `BSMultiBoundNode*` where a form
      was expected and two `type_info*` in the registers. `TESForm::GetById(formId)` handed back a freed form, and the
      cast to `Actor` read it. Likely the same cause as player 2's 21:34 crash: Engine Fixes VR's form cache keeps
      freed forms (`FormCaching = true` on his side). Defensive option, not done: keep each known form's ref handle
      and validate removals through the handle table instead of `GetById`. Not reproduced in the rig.
      - Lead from crash triage (2026-10-21): in that same second (15:35:30 UTC), the other machine crashed at
        `+0x714EB2`, our temporary actor disabled after its disposal (fixed since). Both games crashing at once
        points at one synced event, a temporary actor removed while still referenced, rather than at Engine Fixes'
        cache alone.
- [ ] **An NPC that follows its owner indoors stays frozen outside** for the player left outside (Lydia in front of
      Windhelm's palace, 2026-10-18 21:15, until player 2 went in 44 s later). The server relays an NPC's movement only
      to players in range of its new cell and never removes it for the ones it left; `RemoveWhatLeftRange` runs only
      when the viewer moves. Needs a removal from the server when an NPC's cell takes it out of a viewer's range: the
      server's `CharacterService` relay belongs to the playback session (BOARD.md), so agree it there first.
- [ ] **Placing another player's corpse costs 20-47 ms a move** (player 2 in Korvanjund, 2026-10-18 21:44-21:47):
      every `CorpseDiag: ... moved there (19 bodies)` came in the same millisecond as a `Mod update took 20-47 ms,
      slowest section CharacterService::RunRemoteUpdates`, 2-5 a 30 s window (`VRBodySync::PlaceCorpse` moving 18-22
      rigid bodies under the world's write lock). Most of his 13-28 frames over 50 ms a window were not ours (three
      1.1-1.6 s stops outside our update, frame rate already down to 36 fps outdoors). Measure inside `PlaceCorpse`
      before changing it.
- [ ] **PvP spell damage carries none of the caster's perks** (rig 2026-10-18, PvP on): Firebolt on the bot's
      character was sent as a health change of 40, its base magnitude, without the caster's Augmented Flames, its burn
      or difficulty; the same spell on an NPC dealt 30 plus a burn of 7.4. `HookAddTarget` sends `-fMagnitude` before
      `AdjustForPerks` runs and does not apply the effect. Not changed: PvP rules are the host's design. Not known: whether
      the victim's game also applies the caster's replayed projectile (a second hit); that needs two games.
      Spell damage on an NPC another game runs is fine: it arrives once, as health changes with the caster's perks; the
      AddTarget that comes with it is not applied there (`live-spell-damage`, 2 of 2, bot 6 of 6).

- [ ] **A public group of four on a 1400-mod list** (logs of two of them, 2026-10-17, both on v1.9.3; the same group
      sent the Ralof and start-up crash reports). Fps fine (median 80-87); private memory 10 to 28 GB over 2h15.
      - **Corpses flung around (ours):** a Draugr corpse copy (380B7) was moved back to its owner's corpse twice a
        second for 3 minutes, each time found 100-1183 units away: every move throws the ragdoll off again. 1437
        `CorpseDiag: ... moved there` lines in one player's 40 minutes. PlaceCorpse needs to stop when a move does
        not stick (and to find why the ragdoll flies: Physical Collision VR is in their list).
      - **Freeze (player 2, 19:06, 44 s then killed):** the main thread waits in the game (`SkyrimVR+0x5b8d4c`, as
        player 1's Alduin freeze) under HDT-SMP; the other thread reported is in RaceMenu's skee64 and OBody. None of
        our code on either stack; it came 13 s after a burst of NPC copies we spawned (412 `already spawned`
        warnings in the minute before).
      - **Fingers not synced on their skeleton:** 536 flattened bones and the forearm-to-hand parent check fails
        (`no usable flattened bone array`), so no copy's fingers move and the local ones are not sent. The body,
        arms and head are posed from nodes. Fix: accept a twist bone between forearm and hand; needs their skeleton.
      - **Other players' projectiles refused by the game:** `No projectile found` 112 and 200 times (the game's
        Launch gave a handle with no projectile); those arrows or spells were not seen.
      - The reconnects were the name check during character creation (the intro's name, then the chosen one); v1.9.4 compares the
        name only after a loading screen. `OnNotifyActorTeleport: failed to retrieve actor` 61 and 50 times.
- [ ] **Crash applying the owner's inventory to an NPC with no AI process** (hub reports 20261017-211633-bb01369d and
      20261017-211914-2214ee75, a public v1.9.3 player in the Helgen intro, twice). The triage calls it "back after its
      fix" because it matches the address only (SkyrimVR.exe+683850); it is not that bug: our crash handler finds no
      actor of ours in any register, and RSI is Ralof (2BF9E), alive. Both crashes come right after `Received remote
      actor, form id: 2BF9E`, on that thread: ReconcileActorData > SetActorInventory > the game's
      InventoryChanges::RemoveItem (15821) > AIProcess::ClearFurniture (38773) with no process (RCX 0). The same path
      has no guard in the current code. What set it off: VRConnectService reconnects when the character's name
      changes, and that player's name flipped between 'Prisoner' and 'Prisoner2' a few seconds after each save load
      (not our code), so it reconnected five times in 15 minutes and re-applied every inventory each time. Ralof also
      sent 182 Unequip actions in a second there. Two more reports (2026-10-17 23:17, same player): the same crash on
      Legate Quentin Cipius (BF561) 1.2 s after he appeared, with no reconnect, an Imperial Sword in RAX.
      Correction: the function names above come from CrashLogger's nearest known id and are not reliable. What the
      registers show: `mov rax, [rcx+0x08]` with RCX 0 is a read of an AI process's `middleProcess` (AIProcess+0x08),
      where the equipped hands are kept. The actor had no AI process, while our inventory apply equipped a weapon on it.
      Fixed in 8397d86 (deployed, client and server; protocol unchanged since 440f5e0):
      - A full inventory for an actor with no loaded AI process (`currentProcess` and its `middleProcess`) waits until
        it has one (`Inventory of X kept until the actor is loaded here`, then `... applied now that the actor is
        loaded here`). No rig run reproduces the crash: an actor in that state could not be made on demand.
        `live-copy-bleedout` 4 of 4 and `live-temp-reconnect` 8 of 8 as a regression, and no inventory held back in
        them.
      - The client compares the character's name once, 5 s after each loading screen, not every 2 s in play.
        `live-rename` (new): the game renames the player while connected. RED on 440f5e0 (it reconnected), 4 of 4 on
        8397d86. Not tested: loading another character's save while connected (the rig has one save).
      The same two fixes are in the hotfix v1.9.4 prepared in `C:\dev\TiltedEvolution-hotfix` (v1.9.3 plus these).
      Not seen in play yet. To see in play: no `connecting again` line except after loading another character.
      The `+24E6D0` crash in the same reports (3 ms after our teleports of actors into cells outside the loaded
      area) is not fixed; it came 20 s after one of the name-flip reconnects.
- [ ] **`+24E6D0`: the game's streaming thread on a cell that is gone, after our teleports** (ours; twice on v1.9.4,
      2026-10-18 15:15 and 15:22 UTC, reports 20261018-151548-79ee996d and -152224-4aefa2b9; once on v1.9.3 the
      evening before; once for player 1 on 2026-10-03). Not "the AI thread": the crashing thread runs a
      `QueuedPromoteLargeReferencesTask` (loading a cell's large distant references) for a static whose cell is null,
      in `TESObjectCELL::GetWorldSpace`. Each time it comes 3 to 47 ms after a burst of `Successfully teleported
      actor` lines (4 and 12 in one update, which took 335 and 210 ms), and R15 held the cell we had just moved Ralof
      into. The server sends `NotifyActorTeleport` to every player whenever an owner relinquishes an actor
      (server `CharacterService::OnOwnershipTransferRequest`), and `MoveActor` calls `TESWorldSpace::LoadCell` for an
      exterior cell outside the loaded area, queueing streaming work on cells this player never enters. Upstream
      has the same code. In the tree since 4c41069, not built into a release: a teleport is not made when the actor is
      not loaded here and the place it goes is not loaded here either (`Teleport of actor X not made`). Rig test
      `live-teleport-far` (new; bot `release ref:<hex> at <x> <y> <z>`): Lydia taken by the bot, the player travels
      away, the bot lets her go as last seen in a third place. RED with the fix switched off (the game loaded that far
      cell and moved her there), 5 of 5 on 4c41069. Not seen in play yet.
- [ ] **Copies the server removes were freed while the game held them** (ours; 2026-10-18 15:35 UTC, two players
      7 s apart: hub reports 20261018-153552-c5fa034b, `SkyrimVR.exe+714EB2`, labelled "fixed", and
      20261018-153541-63d12066, `VCRUNTIME140.dll+4AE1` from our code; player 2's logs of 2026-10-18 17:35 hold the second).
      Both right after the server removed dozens of copies at once: our crash handler found the copy deleted 270 ms
      earlier with 3 handles still in R15 (FF001213), and the other game crashed 27 ms after deleting two such copies.
      `DeleteTempActor` (the server's removal) freed a copy at once whatever its handles, and the wait list for
      unloaded copies (24 slots) overflowed in the same bursts (10 and 41 `waiting list was full` frees).
      In the tree since 4c41069: the server's removal goes through the same wait (disabled and queued until the game
      lets go), and the list holds 256 instead of 24 (TPTests `[copyremoval]`, a 130-copy burst). Rig test
      `live-copy-removed` (new; bot `npc temp <base> summon`): six summoned bears next to the player, let go, so the
      server removes them. RED with the fix switched off (all six freed with 3 handles held), 4 of 4 on 4c41069 (all
      six disabled and queued). Every rig script's end showed the same before: the bot's player copy freed with 3
      handles held when the bot left (216 times in the rig logs). Not seen in play yet.
- [ ] **SteamVR would not start** (2026-10-18 17:38): `bind: 10013` on 127.0.0.1:27062, SteamVR's web port, inside a
      range Windows reserved (27018-27117, Hyper-V, WSL or Docker), then "Failed to start server with error 146". It
      blocks the headset too. Cleared by `net stop winnat` and `net start winnat` as administrator; if it comes back,
      `netsh int ipv4 set dynamic tcp start=49152 num=16384` keeps the reservations away from SteamVR's ports.
- [ ] **Rig game hung once at a bot's arrival** (2026-10-18 18:01, 4c41069 with both fixes switched off): every thread
      waiting, no log line after the bot's player copy got its 3D, no freeze dump (the watchdog's thread stopped too).
      Not seen again in the next four runs. No debugger on this PC to read the stacks; if it comes back, a dump made
      by hand (Task Manager, "Create memory dump file") before closing the game.
- [ ] **Trustworthy rig tests** (asked 2026-10-17: fixes that pass in the rig keep failing in play, and builds and runs
      cost time, so a test must be worth it). Seen on 2026-10-16/17: `live-copy-bleedout` passed while testing nothing
      (its copy replayed no actions); `live-world-object` passed while the cup flickered (it read our own log lines,
      not the object); `live-blade` passed though the rig's slow swings never make the PLANCK hit the fix was about; a
      slide check passed only by accident (per-frame steps of stepwise playback); `live-defender` hard-coded the old
      225 ms delay; scripted HIGGS grabs crash the game (higgs_vr.dll+0x605ae); the rig runs at 60 or 100 fps while
      the players run at about 45. Plan:
      1. Audit every live script: what it guards, whether its checks read the game (state, positions, screenshots) or
         only our log lines, its preconditions, its false-pass risks, its timing assumptions (in progress).
      2. Rig realism: frame rate pinned near the players', fake network on by default for timing tests.
      3. A test counts only when shown red on the build without the fix and green with it; a precondition that fails
         makes the run INVALID, never a pass; results kept in a history file, flaky tests marked.
      4. Checks on what the player sees, not on what our code says it did.
      5. After each play session, each rig-tested item gets "rig said / play said", to learn which tests predict play.
      Done 2026-10-17 (tools only, no build): the audit of all 68 live scripts (top false passes: copy-bleedout and
      copy-knockdown passed with a copy that never went down; hands passes at 65-74 units, above the original 27-32
      unit bug; world-object read our log lines and ignored the flicker count; defender passed when no hit was sent;
      blade can never make the PLANCK hit; look, temp-reuse, creatures and game-drops). `live-check.py` now gives
      INVALID (failed `log REQUIRE`, or the bot could not do a line's part), INCONCLUSIVE (SKIPPED is no longer a
      pass), RED / NOT RED (`--expect-fail` on a build without the fix), reads across a log rotation, and keeps every
      run in the toolbox's `test-history/live.jsonl` with build, frame rate and network. Scripted swings now carry the
      controller's velocity. `live-world-object` samples the plate's 3D through the whole carry (`ref 92D04 3d steady
      3`) and requires `0 of N moves rebuilt its 3D`; `live-defender` needs one log line per hit. The rules
      (toolbox `rules/working.md`): red before the fix, REQUIRE the preconditions, report rig results as rig results.
      The rig's frame rate follows the desktop monitors (SteamVR's null headset draws into a desktop window): 60 Hz
      normally, 100 since one monitor went up on 2026-10-17 morning. HIGGS takes two physics steps a frame below 70
      fps (the players near 45, the rig at 60) and one above, so the weapon bodies read 85-100 units off at 100 fps and
      0.3 on average at 60 (32 readings, none over 30). `headless.ps1 up` now prints the monitors' rates and warns
      above 60. Rig at 60 fps on 3d63d8e: live-defender ok; live-world-object's new checks pass (the plate's 3D never
      unloaded in 81 and 83 samples during the carry; `0 of its 6 moves rebuilt its 3D`, a pattern the old code's
      `6 of its 6` fails); its opening game-to-bot move was missed in one of two runs, unexplained so far (a log line
      for a full watch list goes into the next build).
      Next: the copy-bleedout and copy-knockdown scripts (the session that owns them is reworking them, with a red
      run), the hands threshold (80 units against a 27-32 unit bug), the stale default suite, a precise bot-side
      `objectmoves` condition, and a choice for the owner: let the rig set the monitors to 60 Hz while it runs.

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
- [ ] **Interesting things to add**
      - Corpses: they read a ragdoll's bodies directly and pull the body towards the owner's spot instead of
        teleporting it. Aims at our slow corpse placement (20-47 ms a move) and flung corpses. 1-2 days.
      - Held objects: drawn from the carrier's hand, and the server gives the object one owner. Aims at "held objects
        flicker". 2-3 days, protocol change.
      - NPC "hand claim": reach a living NPC and your game takes it over, to grab and shove it. Another route to the
        NPC pull and the grab buzz above; it replaces the design decided on 10-10, so it is the owner's choice.
        Protocol change, risky around ownership changes.
      - Holsters shown on the other player: we have none. 3-5 days, protocol change.
      - Cheap: mouth movement from the voice audio we already receive; skin colour on player copies, which we never
        set (maybe the "white skin" half of the orc report).

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
