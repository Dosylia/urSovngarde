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
on ce86908). Current build 0ff0808 (deployed, client and server; protocol 33ef1cbe, as ce86908). Both players need it.

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
      along the bow as before. Compiled, TPTests 52 of 52 (two arrow round trips). Rig: `live-arrow-hand` (new: the
      bot's copy holds the arrow at the hand, then on the string; the game's player draws and the bot hears the arrow
      in the hand) not run yet. There is no red run: before this, nothing at all was sent for an arrow in the hand.
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
- [ ] **CommunityShaders.dll+0x1208c8 (read at 0x144) at a loading screen**: four times in the rig (2026-10-12 three
      times, 2026-10-16 once), always the first instant of a load out of Mistwatch, never yet in play. Nothing of ours
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
      Not done: the "You died" screen, giving up early, the revive gesture (options first, as the owner asked).
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
