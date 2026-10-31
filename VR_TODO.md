# Skyrim Together VR: TODO

What is still to do, and nothing else. The full story -- session reports, investigations, the evidence for every fix
-- is in git and in `VR_HISTORY.md` (everything this file held up to 2026-10-07, word for word); `KNOWN_ISSUES.md` says
what is known to be broken and why. Cleaned on 2026-10-13: proven and closed entries removed (git keeps them), the
rig-tested work folded into the checklist below. Cleaned on 2026-10-31: finished work and the stories of open items moved to
`VR_HISTORY.md`, "Moved from the TODO, 2026-10-31", word for word.

`[ ]` open, `[!]` waiting on a test or action. An item play confirms moves to `VR_HISTORY.md`, word for word. Open bugs come first
under "Open work".

**Aims, in order** (2026-09-25): no crashes; the game in sync for both players; VRIK and HIGGS interactions seen by
everyone; dead bodies in sync; NPCs never below the floor; weapon hits land where the weapon is. Since 2026-10-06:
sword fights that feel like Blade & Sorcery -- the other player's sword and body are physics objects in your game, and
the defender's screen decides whether a hit was blocked.

The launcher and the online side (relay, invite codes, the public server page, crash triage) are tracked in their own
repos' `TODO.md`: `C:\dev\urSovngarde-launcher` and `C:\dev\urSovngarde-hub`.

## Waiting on a test or action

### Next play session

All of this needs build 4716f50 or later, on both players and the server (a protocol change: an older client cannot join). Do what you can, in any order. Each line says what
to do, what you should see, and what to read in the log if you don't. "It looked fine" is a result too: say so.

Before you start: don't play the vanilla Helgen intro together. Player 2 stays tied, because Ralof is a copy in that game and
his scene never finishes. Use an alternate start, or connect after the intro.

- **PvP on, spells and arrows.** Hit each other with fire, frost and shock spells, aimed ones and beams, and with arrows. A spell that
  hits on the caster's screen takes about what it takes from an NPC, once, even if it looked like a miss on yours. An arrow takes one
  hit's health, not two, and a dodged arrow takes nothing. If not: `PvP: spell X hits on remote player` in the caster's log, `PvP: spell hits from player` in the other's.
- **A fight with NPCs, then look at the bodies.** Bodies stay put while you look at them; one in the wrong place moves when you
  look away. No NPC lies dead on one screen while it fights on the other, and a dead one never stays on its feet. A named townsperson the other
  player kills dies in both games, and any bounty is theirs.
  If not: `CorpseDiag: X's ragdoll lies N units ...`, `already dead here`, `KillingBlow: X (server id S) killed here by this player`.
- **Drag a body in front of the other player,** hold it still, then let go. On their screen it stays in your hand and never
  flashes back to where you picked it up. Take a weapon off a body with your hand: they no longer find it on that body.
  If not: `CorpseDiag:`, `BodyTake: X xN taken off Y, a body another game runs`.
- **Ride away from bodies.** After a fight, leave the bodies behind and go on. No body flickers or jumps.
  If not: `CorpseDiag: own body X sent N units`.
- **Rejoin near bodies.** One player disconnects after a fight and comes back. Bodies stay with the game that killed them, unless a
  hand takes them. A creature that died while that game was offline lies dead, not standing. No NPC killed earlier gets up.
  If not: `Gained ownership of actor`, `DeathWins: X (server id S) is dead here but alive in its owner's game`.
- **Doors in a civil war quest,** with the party leader running the quest or the siege. A door a script opens or closes moves for
  both players, and the member sees the siege's camps, barricades and catapults.
  If not, on the leader: `Door X (...) opened|closed by a script here; sent`. On the member: `Door X: opened|closed in another
  game`, `ReferenceEnable: N reference(s) the leader's scripts switched`.
- **An Ice Wraith.** Both PCs need its behaviour folder, `SkyrimTogetherRebornBehaviors/IceWraith`. Fight one with both players
  near. Its copy moves as it does in its owner's game and does not slide.
  If not: `SlideDiag`, `Ghost: reference X was enabled again`.
- **A follower out of an inn.** One player takes a follower out of an inn while the other stays inside. She is gone for the
  player left inside. (Through a city gate she stays behind for the other player, as on v1.9.7.)
  If not: `Assignment: X left this place with its owner`.
- **An NPC handed over mid-walk (needs the next build).** Stay in a town with people walking about, and let one of you go through a
  door and come back: that hands NPCs over. No NPC stands with one leg forward, rocking, on either screen, and none walks on the spot
  or slides. Say which screen if one does. If not: `Takeover: NPC X was moving as a copy here`, `TakeoverDiag:`, `SwimDiag: own NPC`.
- **A weapon taken by hand off a body the other game runs (needs the next build).** Do it with the other player watching, holding the same
  kind of weapon if you can. They see the item once, in your hand, and it goes when you stow it or lies where you let it fall. It
  is gone from the body, and their own weapon stays in their hand.
  If not: `BodyTake: X xN taken off Y`, then `DroppedItem: dropped X`.

- **Walk up to NPCs in a town, moving in and out through a door or gate.** An NPC that changes hands mid-walk does not freeze in a stride. Note
  the time of any NPC sliding, crouching or swimming: those did not reproduce and are still unexplained.
  If not: `Takeover: NPC X was moving as a copy here`, `TakeoverDiag: NPC X as it is gained`.
- **Pull an item off a body the other player's game runs,** then drop something from your inventory. Both show for the other player.
  If not: `BodyTake: X xN taken off Y`, `Dropped item`.

If there is time:

- **A revive:** the revived player's copy stands up on the reviver's screen. Read `Revive: player N '...' 5 s after the revive,
  copy X here: ... head H ...` (H about 105 standing, under 30 lying).
- **A heal:** Healing Hands or Heal Other on the other player heals what it would on an NPC. Read `the caster's perks make it N times as strong`.
- **Map icons:** open the map, zoom in close and back out, and look for the other player's icon.
- **The map, player 2 and together:** 60 closes online, no freeze. Read `Map menu: the player's world node left out` at some closes,
  and no `put on a list again` or `led back`.
- **One follower in both saves:** she settles with one game. Read `she is that player's follower too, so she stays there`, once,
  and no more `asked for back`.
- **Look at the other player:** stand in front of them while they hold their hands out, palms up, then have them run past you with
  smooth locomotion. Hands and palms are as they hold them, the legs run, and the face looks like theirs. Only a headset can tell.

After the session the logs answer the rest: `Stall:`, `Silence:`, `ThreadOwners:`, `Connection:`, `VRBodySync: actor X body --`, and
`logs/process-end.log` if a game vanishes.

- [!] **An NPC taken over while it walks** (play session 2026-10-31 07:55 on c382599)
      Players saw NPCs frozen, sliding, and swimming on snow. All three reports were on player 1, just after that game gained the NPC:
      claims at 08:08:28 and 08:08:53, a leader assignment at 08:11:38. That session had 117 gains in 22 min, against 43 in 25 on
      10-30. Nothing in c382599 was found that causes it: `VR_HISTORY.md` has the same symptom around 10-04. Nothing resets a graph
      or `actorState` at a gain (`SetRemote(false)` only clears a bit).
      - Rig, deployed c382599, read from the game's own actor. The bot's Ralof walks as the game's copy, from a player's recording,
        and the game claims him, through the follower path because `replay` holds the bot's script:
        - gained mid-walk, 2 runs of 2: he stops dead within 0.6 s and holds a stride, feet rocking 5-28 units from the pelvis about
          every 6 s, with `Speed` 0. No slide. It lasted 3.5 min, until an AI order moved him;
        - gained after he stopped: nothing, feet 11-15;
        - gained mid-walk with an AI order already given: his AI walks him on, `Speed` 315 to 328, and stops him cleanly;
        - gained standing with an AI order already given: `Speed` 363 at once, 344 units in about 1.2 s, then he stood with `Speed`
          361 and relaxed legs for the 27 s watched. Not explained, and the change below does not touch it;
        - `moveStop` sent to a held stride cleared it on the next sample, and an AI order right after walked him 735 units with his
          legs moving.
      - In the tree, not built (client only, no protocol change): an NPC its owner's last two reports had moving when it was gained is
        looked at again 500 ms later. If this game's AI has it at no speed, it gets the graph's `moveStop`. If the AI walks it on, it is
        left alone. Dead, dying, downed or knocked bodies, mounts and anything not on its feet are left out. One line each time:
        `Takeover: NPC X was moving as a copy here (speed N), and this game's AI has it standing|walks it on`.
      - Build 1299bb5 read the graph's speed at the gain, and the stop never fired: `live-takeover-walk` read 0 with every report
        saying 315, and `live-takeover-aiwalk` read 305. So this game's AI writes the graph's speed on a copy, and reports load over it
        later in the tick. Reports are the only reading no local write reaches, and every kind of actor has them. `iState` is
        no test: it is a posture (`iState_NPCBowDrawn`, `iState_NPCBlocking`, `iState_NPC1HM`), 21 of the 32 graphs do not sync it, and on
        the rig it stayed 6 after a `moveStop` and after his AI had walked him and stopped him.
      - Rig test `live-takeover-walk`, judged on his legs read from the game: RED on c382599 on `npc feet still` (a foot moved 46
        units). Its controls, which must pass with and without the stop: `live-takeover-stand`, an NPC that stood (4 units), and
        `live-takeover-aiwalk`, an NPC his AI walks on (legs moving, 856 units in 3 s, no stop), both ok on c382599. All three to
        run on the next build, where the walk also has to log the stop. Not seen in play.
      - The game that loses the NPC: `live-takeover-lost`, 2 runs of 2 on c382599, shows no held stride, so nothing is changed there.
        The game's own Lydia follows the player by her AI, the bot claims her mid-walk and sends back what the game reported while she
        stood, about 8 reports a second. She settles, a foot moving 9 units in 5 s, once with a jump of 1355 units at the claim
        and once with 43. Not known why she settles and a gained NPC does not: one NPC, a bot as the new owner.
      - TEMPORARY, in the tree: `TakeoverDiag: NPC X as it is gained|3 s after the gain` (flags1, flags2, weapon state, speed, graph
        speed, `iState`) and `SwimDiag: own NPC X starts|stops swimming`.
      - `ActionFlood: ... 46BAF (Unequip)` starts 1.0 s after the gain for 21 of the 23 gained actors that flooded on player 1, and it
        proves nothing yet. It is in the logs since 10-26, actors never gained flood too (15 of 34 on player 2), only 23 of 54 gains
        on player 1 and 9 of 63 on player 2 show it, and on the rig it came only with the teammate flag. The weapon state in
        `TakeoverDiag` should say more.
      - Rig limits: the copy's source is a bot replaying a player's run, with that player's VR pose on the NPC. That is why
        `live-takeover-walk-walking.png` shows him prone: no `SwimDiag`, `IsSwimming` False. The claim goes through the follower path, where
        a follower keeps its gear.
      - Limits of the stop:
        - 500 ms is wall-clock: a gain inside a pausing menu reads the report's speed, logs "walks it on" and is not looked at again;
        - an AI starting to walk in the very tick of the look can lose that start;
        - a body walking on the spot against a wall has legs moving and reports no speed, so it is not looked at;
        - the stop is a graph event, so it is not relayed: the other games get only the reports;
        - a copy frozen on its way here is not queued, as it reads no speed.
      - QuietStop reads the speed the same way: see "QuietStop misses a walking NPC copy whose own AI stands", in open work.
      - Not known: whether the held stride is what players saw; how often a gain finds a moving copy (the `Takeover:` lines will
        count it); what the sliding and the swimming were; how long a normal NPC's own AI takes to move it (Ralof's package is a
        sandbox in Riverwood, and it never moved him). A Ralof that was never a copy did not take `KeepOffsetFromActor` at all, near
        the player or with the teammate flag and `EvaluatePackage`, and one gained did: not explained.
      - Copies running the game's AI all along: the 305 and the 0 above say something local writes a copy's graph each frame, and
        `HookActorProcess` skipped nothing in the rig on 1299bb5, one run: `ActorProcessDiag: of 2 calls, 2 had their argument on the
        stack, 0 saw an actor, 0 skipped it`. Two calls in a whole game also say that hook barely runs. Not seen in play.
        If that hook is ever made to skip, a walking AI starts cold at the gain and this has to be tested again.
      - Only two real games can show: a walking patrol NPC or a follower gained on the next build, legs against travel in the first 5 s;
        the previous owner's and a third player's screens after a real leader claim; that NPC asked back; a player coming into
        range after such a takeover, as the server replays `moveStart`; `Takeover:` lines from combat, swimming and idles, taken or not taken.

- [!] **The desync suite: playing to find desyncs** (`Tools/VR/desync-suite.py`, `play.py`, guide `Tools/VR/README-desync-suite.md`; test tools, a bot `listen`/`moveto`/`reportevery`, one client line `Entity: local actor`, no protocol change).
      The real game and the bot fight, die, drop and return while a scanner compares them every second. Stands at: built and run on FUS on 2026-10-30, results in `build\desync\report.html`.
      `scanner-proof` (planted mismatches on the real game) saw all three in 7 of 9 runs; the 2 misses came during full runs, cause unknown, and the test now writes what it knew when it misses.
      Not covered yet: real VR arms and HIGGS holds on the other player, its swing animation, a second real screen, NPCs below the floor, and the bot taking NPCs over when a game leaves.
      Leads it found on v1.9.7, none seen in play yet (all rig, FUS):
      - After this game reconnects, NPCs it owns stand far from where the server has them for 10 s or more: Sabre Cat 1372 units, Imperial Captain 1438 (`reconnect-game-repeated`, 2 of 5 runs).
      - A player who rejoins is drawn twice for about 9 s, the old copy beside the new one: `FF0012DC` removed 9 s after `FF0012D7` appeared (`duplicate-player`, 3 runs).
      - Actors jump away and back: Stormcloak Archer `8C0059F1`, Imperial Archer `8C00C3DA` (`flicker`, 3 runs). Cause unknown.
      - The game ended in `live-dropmove`, with a crash block at `rip 0`, Mistwatch, 2026-10-31 01:27, dump `crash_UTC_2026-10-31_00-27-51.small.dmp`; 945 threads at the time. Once; cause unknown.
      - Threads rose by 177 in 20 minutes of fights with no travel (`soak`), and reached 1096 after many cell travels, nearly all `ucrtbase.dll+0x2cd00`. Alpha-32 of DynDOLOD's DLL is enabled in the test profile.
      - Action floods: `13002` 5713 refusals in the soak, `46BAF` (Unequip) 5219 and `132AF` in bandit fights. What they cost is unknown.
      - On the bot's side a killing blow arrives after an offline kill (`reconnect-fox`), so the fox of the 2026-10-30 session lies in what the owner's game does with it: needs a look in play.

- [!] **The two-game test: two real games on one PC** (`Tools/VR/two-games.py`, `two-games-anchor.py`, `headless.ps1 up -Twin`, guide in `Tools/VR/README-desync-suite.md`; an extra test, not in `--all`; no protocol change).
      Stands at: two runs on 2026-10-31, FUS, build v1.9.7-1-g34fe6f91 plus this work, 10 minutes each, both games alive and connected, no window stalled, each drew the other player.
      Local server: first 60 fps, second about 118 fps, ping 0.
      A Debian 12 server on a test VPS, over the internet (`headless.ps1 up -ServerAddress`): first 60 fps, second about 220 fps, ping 13 to 15 ms, quality 1.00, no disconnects; the copy stood 1 unit from a standing player and typically 23 units (at most 32) behind a walking one.
      Not played in these two runs: fights, hits, HIGGS or real VR arms.
      Waiting on: the relay. One launcher cannot be both host and join (its program holds a `single-instance` setting and an `ownGame` answer; not tried), it has no command line, and its source is not readable from this PC.
      It needs either a small command-line relay join in the launcher, or the relay's protocol, so a test tool can join from this PC.
      What the two games disagree on so far is all far from both players, 9000 units or more:
      - Living NPCs up to 2399 units apart: the game that has them out of range gets no more updates (`InterpDiag starved: ... stale 90973 ms, 2.9 cells away`), so its copy stands still. It has not been checked that they catch up when a player comes near.
      - Imperial Archer `8C00C3DA` died with `ActionFlood: actor 8C00C3DA performed action 937F4 (JumpFallDirectional) 60 times in a second`, and its corpse lay 5406 units apart between the games, with `CorpseDiag: ... in the player's view; it waits`. Whether it is put right when a player arrives has not been checked; why it died is unknown.
      - Refused actions repeat per frame, not per time: the same `46BAF` (Unequip) flood is 120 a second at 60 fps and about 420 at 220 fps, on NPCs this far away.
      `Tools/VR/two-games-bugs.py` plays the 2026-10-31 session's bug list on two real clients with a scanner comparing them; run for an hour on 2026-10-31 on the test server, no game ended. What it found, all rig, none of it fixed:
      - **Reproduced: bodies after a reconnect, in both games.** A body lies drawn twice, and one can fall for good.
        Falling: Old Orc copy `FF00127E` (server id 600049) went -263, -10061, -61814, -189177, -394730 units below where it lay in the 30 s after a rejoin, in the paced first game, with its 3D on. Others: 600046 down to -533718, 300052 by -12687. All three had `Transferring ownership of local actor ... worldspace: 0, cell: 0, position: (0, 0, 0)` before; 14 ids per game had that line, and whether all of them fell was not counted. The real session has that line too (08:15:30), for another id. Cause unknown.
        Twice: a player who leaves and rejoins registers the body it owns again under a new server id, while the other game keeps the old one: `FF00122E` was 50004C, then 10003C, then 60004C (`Entity: local actor FF00122E has server id`), and the other game got a further copy (`Character 60004C (FF001227) came dead`), still there 30 s later. An Aspiring Mage was 400003 and came back as 200001, two bodies for 25 s.
        Not seen in the case where the owner stays and the other player rejoins: there the old copy loses its 3D and one body is drawn.
      - **Seen, may be the rig: a dead copy's ragdoll thrown up.** Bandit Highwayman copy at +891 while its owner's corpse lay at -10477, 5 s after death, in the second game, which runs at up to 220 fps; `CorpseDiag` put it back 105 s later. Also 3930 units up once.
      - **Partly: respawn.** The copy is removed and a new one made, about 0.2 s with nothing drawn (`FF001235` to `FF00123C`). The head was there in pictures every 0.5 s from the start of the respawn; they are dark.
      - **Not settled: spells on NPCs.** From the hands, swept, on this game's own NPC, two passes each: Sparks took 59 and 76 online, 88 and 101 offline (the 101 ended with the NPC nearly dead); Firebolt took 0 and 225 online, 306 and 343 offline. So they can hurt online, but one online pass of 39 charged Firebolts did nothing, and offline never did that. Two passes are too few, and the sweep's aim varies.
      - **Open: spells on the other player.** Sparks from the hands: the target's log says `127 hit(s), 5 damage ... taken` each second, and with regeneration off its health stayed at 2000.0 for the 6 s. A Firebolt fired by the game took it to 1990.4, so the reading works. Spells from the hands on an NPC the other game owns have not been measured with a sweep.
      - **Not reproduced: drops.** Every drop reached the other game, both ways and with it in a paused menu, except one taken for an item already lying there (`DroppedItem: 5 was already lying here as FF001230, 145 units from where it was dropped`). The session's log has no `DroppedItem` line at all, so the dropper's log is still needed.
      - **Not reproduced: a body disappearing when a player comes close** (walked up to 20 units, both ways), but real dragging needs hands.
      - **Not reproduced in one pass: NPC animations at Windhelm.** 7 NPCs, up to 89 samples each, no sneak or swim difference and no moving without walking; the weapon drawn differed on Stormcloak Chief (18 of 18) and Soldier (14 of 14). With no change of owner in that pass; the pass with owners changing did not run.
      - **Not tested: doors.** The one pass never got a player through a door.

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
        assumption, as the Cydaphex port does).
      - The item taken existed only in the taker's world (play session 2026-10-31 08:04:55 and 08:05:05, a body player 2's game ran).
        In the tree, not built (client only): such a take also fires `ItemDroppedEvent`, with its worn flags cleared, so the
        other game places it where it lies. `Actor::DropObject` stays as it is: it sends `Drop`, so that game drops the item
        itself, and an announcement would make two. `live-body-take` now also checks that the other game is told: RED on c382599
        (the bot was told nothing), green to run on the next build. Not seen in play.
      - Limits of that change:
        - a take off a body the taker's own game runs is still not shown to the other player. That is the next step, held until
          this case has been seen working in a session;
        - the take's -1 can be refused (stale epoch, out of range) while the item is still announced, so it is on the body and on the floor;
        - a same-base item within 200 units on the other game is adopted instead of a new one placed;
        - stowed before the server answers, one round trip, the server's item stays, as for a player's own drop;
        - two players taking at the same moment both keep one, as for any drop;
        - two takes of the same base inside a round trip can swap ids, as pending items match by base;
        - a dead follower's body in her player's game may keep the item, if she still counts as a teammate;
        - a script dropping something off a dead copy within 400 units is announced too: with three or more players extra items can appear.
      - Only two real games can show: the other player sees it once, at the hand, following it, and it goes on stow; a worn weapon
        taken while the other player holds the same kind stays in their hand; a take with the same item lying near; a take
        that shoves the body, with a claim in flight; a fast stow on a real ping.
- [!] **The published Linux server archive crashes when a client joins, on the newer systems it can run on** (fixed on b06cceb8 + the
      local `gamenetworkingsockets` recipe in `xmake-repo`; Linux server only, not released yet). To see: the next push rebuilds every
      Linux package and CI passes on amd64 and on arm64, then a game client joins the next release's archive on a Linux host. The public
      server is not affected; which build it runs is still not written down.
- [!] **Quest scenes together** (the quest port, build 3d08be4, and the narrower hold, 626a969). A stage goes once per party, a member's stage
      is held only while it changes a check of a playing scene's running phase or the next one, and lines carry the sender's scene phase.
      Not confirmed in play. To see: a quest scene with both players near, nobody's scene stuck, no stage played twice, few holds, each
      naming a scene's next step. Read `Holding member quest stage`, `QuestHold:`. Still possible: a branch phase gated on a held stage is
      skipped on the leader.
- [!] **Quest items lying in the world reach the other players** (build 154378b, then 4ef72be). Player 2's log on 2026-10-24 14:42:
      `QuestItem: picked up quest object 5AF48 (reference 5AF49); sent to the other players`, the Ebony Claw. Sent, but the other game's side
      is in no log kept, and it is not confirmed in play. To see: one player picks up a claw lying in the world, the other has a claw, both
      doors open. A player stuck like this: `player.additem 5AF48 1` (the Ebony Claw), then the keyhole.
- [!] **Healing perks on another player** (protocol bb9f5d95). A heal cast on another player carries the caster's perks. The caster's side
      ran in play, 45 times from 2026-10-18 to 10-28 (`Magic: spell X on remote player Y: the caster's perks make it 1.50 times as strong`).
      What the healed player got is not logged, and it is not confirmed in play. To see: "A heal", in "Next play session".
- [!] **Dead NPCs in two games** (builds 14b3d55, d6b97f6, 10b98ee5). A copy of a protected or essential NPC dies on its owner's word, and
      this player's killing blow on another player's protected NPC is taken in the killer's game, any bounty theirs (`KillingBlow: X (server
      id S) killed here by this player`, not yet in a real log; rig only). `DeathWins` ran in play on 10-28, 10-30 and 10-31. Not known from
      10-26: why bandit CC591 took 27 killing blows and never died in its owner's game. To see: "A fight with NPCs, then look at the bodies".
- [!] **A dead copy standing, when its owner's health got there before its death** (fixed in build 474ec2b, not seen in play). `live-copy-health-first`
      red, then green; rig only. How often it happens in play is not known. The fox of the 10-30 rejoin was the same kind (`ValuesUntilKilled`,
      build aa045e7): `Character N came dead; killed now that it has its 3D` ran in play on 10-31, and whether a copy then stood is not known.
- [!] **A copy's body is shifted after it lands** (build 4df623a). A move waits until the player sees neither end of it, and a body its owner
      moves is moved the moment the bones stop. The wait ran in play: `CorpseDiag: X's ragdoll lies N units ..., in the player's view; it waits
      until the player looks away`, 8 lines on 2026-10-31. A body held still in a hand keeps being sent (fa5e36c). Not confirmed in play.
      To see: "A fight with NPCs, then look at the bodies", "Drag a body in front of the other player". Left as it is: a body put on a
      table within 64 units falls back when the bones stop.
- [!] **A door a script opens (`SetOpen`) is never sent** (build 4df623a, client only). The party leader's game sends the doors its scripts move.
      Rig red, then ok; no quest has moved a door in play yet: no `Door X (...) opened|closed by a script here; sent` in a real log. To see:
      "Doors in a civil war quest". Limits, none shown in play: a door a member's script moved ignores the kept state until someone uses it in
      that game's sight, through a save loaded while connected too; a leader's script that moves a door in the one round trip after
      arriving can be undone; a script reaches every player in the door's cell, party or not.
- [!] **A siege for the member** (f6e9063). The battle quests run on the leader only, so its game relays the references its scripts switch
      (`ReferenceEnable:`, in play since 10-25). Not yet seen: a siege's camps, barricades and catapults for player 2. The hostile faction
      (CWSoldierPlayerEnemyFaction 74A4F) reaches NPCs the leader's game owns through the faction sync; NPCs player 2's game owns never get
      it (the bounty). To see: "Doors in a civil war quest".
- [!] **Another player's follower left standing in your interior** (fixed in build f997b6a, not seen in play). `live-follower-left` red on d2fd00c,
      green on 6f521cc; rig only. To see: "A follower out of an inn". The `Assignment:` lines of 2026-10-31 were the gate, not this case: see
      "A follower left at a city gate", in open work.
- [!] **A follower in both players' saves bounces between the two games** (build 4df623a; KNOWN_ISSUES "Shared follower"). Three quick passes leave
      her with the other game. `live-follower-shared` red, then ok; rig only, no line of it in play yet. To see: "One follower in both saves".
- [!] **An arrow takes one hit** (92ff338d, then f997b6a). The defender's replayed arrow decides, and the attacker's report is dropped while one
      may be in flight (`PvP: a hit of N from player P not taken: its projectile is replayed here, and that copy's hit decides`). No red run:
      the bot cannot launch a projectile. Not confirmed in play. To see: "PvP on, spells and arrows".
- [!] **An Ice Wraith's copy** (the sliding of 2026-10-24 19:27: a game with no descriptor for its graph sends empty animation variables). Decided
      2026-10-31: a replacer from the capture, `GameFiles/.../SkyrimTogetherRebornBehaviors/IceWraith`, goes out with the release, and both
      players need it. `live-creature-icewraith`: 324 of 324 updates with variables; the bot reads what is sent, no copy's look. To see: "An Ice Wraith".
- [!] **Other players on the world map** (the owner's request, 2026-10-23). The markers follow the players and go on the map's list
      (`Player markers: X put on the world map's list`, in play), and nobody has seen an icon. The map gathers markers only when its camera
      height crosses 10000 upward, so opening it alone never gathers. To see: "Map icons". If zooming shows the icon, make the client force
      the regather. Not yet checked: the icon type VR's map shows for type 11, whether the list goes into saves, fast travel between
      worldspaces. A marker for a far player loads that exterior cell from disk; left as it is.
- [!] **The map freeze, player 2 and together** (cause and fix in KNOWN_ISSUES, "Freeze after closing the unpaused map"). Seen fixed by player 1,
      30 closes online on 2026-10-30. To see: "The map, player 2 and together".
- [!] **Fingers not synced on MGO's skeleton** (since build f997b6a the check takes up to three hops). 536 flattened bones, and `no usable flattened
      bone array` up to 2026-10-23; no MGO session since. FUS has no such skeleton, so no rig run. To see on MGO: no such line, and fingers
      move on the copy.
- [!] **Our browser helper outlived a crashed game and froze Mod Organizer** (2026-10-20; each helper now ends with the game, build 1a569f4).
      `helper-exit-check -StuckMain` red, then ok; rig only, and not confirmed in play. To see: after a crash, no `TPProcess.exe` left in Task
      Manager and Mod Organizer starts.
- [!] **Freeze 2026-10-25 14:51: the autosave entering the Palace of the Kings never finished** (fixed on a98ad845 + the ExtraDataList lock change).
      A race microseconds wide, so no rig test makes it fail. To see: no freeze after a load with the other player's marker near. Not seen
      again in the 349 loading screens of 10-25 to 10-31, which is no proof.
- [!] **Crash fixes the hub has yet to show** (hub counts of 2026-10-31). v1.9.7 is a day old, with 3 player reports, and none of these is on it:
      that is no data yet, not gone.
      - `+0xC53E1B`, the game started before SteamVR or the headset was ready: 24 reports, last on v1.9.6. Now a "VR could not start" dialog
        (build 1a569f4). It has never shown in a real log; `VRStart: OpenVR started` is in every one.
      - `+0x5785B91`, an actor with no cell at connect, and `+0x117C20`, placing another player's dropped item: 1 report each, v1.9.5 (build 0567985).
      - `VisitForms`, a freed form cast: 1 report, v1.9.4 (build 1a569f4).
      - `AnimationSystem::Update`, `VCRUNTIME140.dll+0x5052` and `+0x4C3A`: 2 reports, v1.9.6. `GetById` now refuses a dead object (KNOWN_ISSUES section 6).
      - `+0xD8A96D`, 19 ms after a copy used a load door: 1 report, v1.9.6. A copy no longer uses one on its own (`DoorCopy:`, in play).
      - `DisableFunctor`, hub #24: 3 reports on 1 install, none on a release build; the last two came from saves made before the fix (build 154378b).
        No autosave from the 2026-10-18 evening in Korvanjund should be loaded.
- [!] **Small fixes no session has shown** (no red run possible, or rig only; each one's story is in `VR_HISTORY.md`).
      - Close the game in the "VR could not start" dialog ends the game at once (`Exit:`).
      - A removed actor is no longer looked up by its id (`Discovery: X came back at the same address with another handle`).
      - A new copy under a reused id no longer gets up, slides or stays put on the old copy's account.
      - Both grips held 3 s give up the wait when down (`the player gave up the wait`).
      - A door whose activation a script blocks is not sent (`Door X activated by Y, but a script blocks its activation; not sent`).
      - A door locked here is never opened on arrival (`left shut, it is locked here`).
      - A quest object reaches the picker's copy once (`QuestItem: X already reached copy Y through its owner's pick-up; not added again`).
      - A waiting copy's reused id; equipping an NPC with no AI process (`Equipment sync: remote actor X has no AI process here; change not
        applied`); an actor with no cell at connect (`Actor X has a cell now; announced to the server`).
- [!] **The other player's body looks off:** hands a little low with the palms together, legs not walking or running with smooth locomotion, the
      face "looks off". An old report, from before 2026-10-13. Measured on the session of 2026-10-30: hands not low (451 lines), no general
      sliding; palms and face not measured. Only a headset can tell: "Look at the other player".
- [!] **The website's "What a report holds":** the launcher's SteamVR logs (done in the launcher, not committed) need the same addition once a
      launcher release carries them.
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

## Open work

- [ ] **QuietStop misses a walking NPC copy whose own AI stands** (next item, left out of the takeover build on purpose)
      `InterpolationSystem::Update` reads `LegSpeed` before that tick's variables load, so it reads what this game's AI wrote on the
      copy, not the owner's report (see "An NPC taken over while it walks"). A walking NPC copy whose AI stands reads 0. It is not stopped
      when its owner goes quiet, the `had no report ... but was not stopped` line needs a speed too, and the load goes on feeding the
      last report. It would walk on the spot, silently, until handed off or removed: what QuietStop exists to prevent.
      - Seen: nothing directly, as such a copy logs nothing. From the code. 7 of the 9 retained NPC stops were `not taken`, at speeds
        like 4 and 7, or 35-82 in corpse runs, which look like local values and not walking legs.
      - First: an NPC version of `live-copy-quiet`, which uses a player's copy only. It has to be RED on what the player sees: the copy's
        feet still swinging after its owner goes quiet.
      - Then: read `LegSpeed` after the load, as `WatchQuietCopy` does. The stop will then fire for more copies than today.
      - Old code, older than v1.9.7, so not a regression. Not built, not tested.
- [!] **PvP spells: the caster's game decides, as for a sword** (play session 2026-10-31 07:55 on c382599: "the spells used to work but
      do not work anymore now. They do no damage"). The rule of that build, that the defender's replayed projectile decides, failed in play:
      - Flames and Sparks: the caster's game reported nothing, and the copy's beam touched the other player about a third of the time,
        so 15 damage in 8 s and 29 in 12 s, with player 2 back at 250 after each. The copy's aim is not the caster's.
      - Chain Lightning: the caster's game saw a hit for 30, the replay here missed, and the hit was dropped. Why it missed is not known.
      - `Flames 19`, `Fireball 30` and `live-pvp-projectile 17/17` came from a bot aiming a copy at a still player, and proved nothing about a real
        game. Nothing was measured against one. Those rig numbers do not describe the code as it is now.
      - v1.9.7 did no better: a beam's ticks were `not taken` and nothing reported them (code; rig on e20c11e), and Chain Lightning's report
        was dropped (2026-10-30 18:23:41). Beams last counted before b7e842c2 (2026-10-26).
      What worked is the path now used. An effect on a copy in the caster's game sent its hit and its burn on 2026-10-30 18:23:41 (`blow of 50`, then `blow of 0`, on an
      NPC that was a copy there), and a beam's small hits arrive every 250 ms (2026-10-31 08:01:35, though on the caster's own NPC). Melee on players: 18 of 18 hits of 21
      landed. Arrow replays landed on 2026-10-30, so arrows keep their rule. Whether a beam registers on a player's copy in the caster's game no log shows: the first
      session on this build answers it (`PvP: spell X hits on remote player`).
      In the tree, not built, not run: a spell's or staff's damage runs on the copy in the caster's game as on an NPC, with the caster's perks, its burn and
      the multiplier for hits by a player (0.6 at the rig's difficulty, as swords; decided 2026-10-31). Each hit goes as a health change with
      no tick, so the defender's rule does not weigh it: a blade cannot block a spell. The defender's replayed effect is only seen, its damage not
      taken, and it is not sent back. What a hand or a weapon delivers (a weapon's strike enchantment, a touch spell, a combat-hit spell) stays as on v1.9.7, weighed by the
      defender's rule, with no burn. A sword blow after a spell is no longer taken for the spell.
      Limits: a dodge on the defender's screen does not help, as with a sword. To look at in play: a ward in the defender's game does not stop a spell; only the copy's ward in
      the caster's game can, and whether it holds is not known. Absorb spells do nothing to a player, as on v1.9.7. A third player does not see the burn on the defender. A
      non-hostile damaging effect sends nothing. A burn on the defender from a replay that landed does its last ticks at the game's own multiplier if its caster's copy goes,
      and as well as the report if the copy is rebuilt mid-burn. A PROTOCOL CHANGE (a spell's hit has no tick): both players and the server update together.
      Scripts changed to this: `live-pvp-projectile`, `live-pvp-concentration`, `live-pvp-spell-perks`, `live-pvp-spell-elements`; not run on it, and they check one
      side only. The real result waits for two real games: each player casts Flames, Sparks and Chain Lightning on the other, and holds a ward against one, then read
      `PvP: spell X hits on remote player` on the caster (it names the spell), `PvP: spell hits from player` and its health on the other.
- [ ] **A revived player's copy still lying down, and below the floor while down** (session 2026-10-30, once). Player 2's copy sat below the floor
      while down (18:26:44, 10 s, its hands at foot level). At 18:26:54 `bleedOutStop` was refused and the fix fired (`not taken; IdleForceDefaultState
      taken`), yet 5 s later it still lay: `Revive: player 2 ... 5 s after the revive`, copy head 27.5 (about 105 standing, under 30 lying).
      The other four revives logged read 101.5 to 114.7, and the owner has seen nothing wrong with revives, so this is the log's word alone.
      Its owner's knock state 5 was copied onto a copy with no ragdoll (`keptMask` in AnimationSystem). `live-revive-copy` does not reproduce it.
      Each 5 s a copy with a VR pose logs `VRBodySync: actor X body -- reference z, 3D root z, head above the root, knock state`.
- [ ] **Player 2's game piles up threads, and they stand in DynDOLOD.DLL** Highest counts: 4959 on 2026-10-24, 2987 on 10-25, 998 on 10-28, 802 on
      10-30, 1195 on 10-31 (205 at 08:06, 1195 at 08:16). `ThreadOwners:` at 08:14 read DynDOLOD.DLL in 23 of 24. Player 1's game stays near 170 to 270,
      on v3.0.43. Player 2 has since updated DynDOLOD DLL and the address library. Whether that came before the session of 10-31 is not
      known: its log names no version, and player 2's last crash log before it (10-31 00:27) still lists DynDOLOD.DLL v3.0.32, so the update
      may not be tested yet. To see: `Health: ... threads` and `ThreadOwners:` next session, and the DynDOLOD.DLL line in any crash log.
- [ ] **A follower left at a city gate** One player takes a follower through a walled city's gate while the other stays outside: her teleport is
      refused and she stays a live, unsynced NPC where she stands, as on v1.9.7. Moving her into the city's persistent cell (build 4df623a) was a
      mistake and is taken out: on 2026-10-31 08:10 she stood stuck in Windhelm's gate. Nothing solves it yet.
- [ ] **A player seen twice at a load door, and gone late** (session 2026-10-31 08:10, player 1 through Windhelm's gate, seen by player 2). Not changed; the
      same removal shows on 10-30, so probably old. Player 2's log: `Door 55FCB: FF0012F7 activated it in another game ...; done here too`, 23 ms later
      `CopyGone: FF0012F7 removed as remote; the actor is still there, no 3D`, then a new copy FF000C25 892 units away (`CharacterSpawnRequest, server id: 1`),
      which stood there until player 1's load ended (`Temporary Remote Held FF000C25`, 10 s later). It should go when the load starts. Open: `live-door-copy-replay`
      says the guard stops a replayed load door, yet this log has no `DoorCopy:` line for that copy. Which is it?
- [ ] **A dead NPC doubled after a reconnect** (session 2026-10-31 08:15, the Old Orc). Not changed; the rule involved predates the commit of 2026-10-31. One of the two
      copies fell through the floor for player 2; player 1 sees both. Player 2's game made the temporary NPC (FF0012D4, server id 100073). At player 2's disconnect it
      went to player 1 (`Gained ownership of actor 100073`). On reconnect player 2's game announced its own again (`Temporary actor FF0012D4 was made by the game, not by
      this client; kept on connect`) as a new character, 200008, so each game shows a copy of the other's. A temporary form cannot be matched between games by id.
- [ ] **Body flickering and head missing for a few seconds after a respawn** (session 2026-10-31 08:00, player 2 respawning, an older bug by the players' account).
      Not looked at yet.
- [ ] **`SkyrimVR.exe+0x117B10`, probably moved to `+0x11E941`** A garbage pointer under Skjor's sword in Whiterun, 5 s after this game spawned him
      (1 report, v1.9.6, 2026-10-29). `+0x11E941` on v1.9.7 has the same machine, actor and sword on the stack (hub, 2026-10-31), so it
      probably moved rather than stopped. The two games had Skjor under two server ids, and his AI asked to unequip 150 times a second. Not
      proven; needs that player's `crash_UTC_2026-10-29_01-46-45.small.dmp`.
- [!] **Engine Fixes' form cache: build the guard?** `+0x93CE11` and its group, hub #2: 5 reports, last on 2026-10-30, v1.9.6. The cache is suspected, not
      proven. How it can keep a freed form, and a candidate guard for the game's own lookups, are in KNOWN_ISSUES section 6. Not built. A game
      without `FormCaching` is the test that would prove or clear the cache, and player 2's now has it off, so player 2's next crashes are that test.
- [!] **Freeze at a load: the game's allocator purging cells waits for good** (player 1, 2026-10-18 21:29; the same stack as a public player's freeze
      at 08:52 and player 2's of 2026-10-11 21:36). FUS then ran the game's own allocator. Since 2026-10-25 it runs Engine Fixes VR 7.7.1 with
      `bOverrideMemoryManager = true`, which replaces it; whether that ended the freeze is not known. Ralof's `Spawn Actor` in those loads is the
      game building his 3D, not our spawn.
- [ ] **Freeze after DynDOLOD's threads piled up** (a public player on v1.9.4, 2026-10-18 08:52). 188 of 703 threads stood in DynDOLOD DLL NG, and the
      main thread waited 100 s after a loading screen. Likely cause, not proven: our far teleports loaded far exterior cells, and DynDOLOD starts an
      update thread for each; 4c41069 stops those teleports. Not yet tested: our own path, which needs persistent NPCs the bot owns let go far
      away. Other options: DynDOLOD DLL NG Alpha-33 exists, and the freeze dump could go to DynDOLOD's author.
- [ ] **A body's reported place jumps with the players** (session 2026-10-30, the flickering corpse at 18:33). `PlaceCorpse` moved player 1's ragdoll of
      the captain 134 times in 4 minutes. Limited in build d2fd00c (moves in a row wait 1, 2, 4, 8, then 16 s; rig only), cause open. 20 of 162 moves of
      1000 units or more came within 1 s after that game's own grid change, where chance gives 0.5. A body put where there is no floor falls, and
      the game puts it back about 1270 units from the player; whether play did the same is not known. `CorpseDiag: own body X sent N units` names
      each jump of 300 units or more, and ran in play on 2026-10-31. To see: "Ride away from bodies".
- [ ] **Late updates in a fight** (session 2026-10-30). Player 1 sees player 2 stop for 0.3 to 2.2 s, never the other way, and 21 of the 22 windows with a
      gap over 300 ms lie in the fight. The two longest match `Silence:` lines on player 2's side: that game stood still, the connection did
      not. What stops the game in a fight is open. `Stall:` and `Silence:` from 250 ms were in the logs of 2026-10-31, not read for this yet.
- [ ] **Slow frames with a rabbit held alive** (session 2026-10-30, 18:37:01 to 18:37:31, player 1: 178 frames over 50 ms, our update 0.37 ms). `Stutter:` now
      logs where the main thread and the busiest thread are, under 15 frames in a second. Forced in the rig it caught `D3DCOMPILER_47.dll`, a shader
      being compiled, a lead for hitches in play. No rig run of a held rabbit: it runs from the hand.
- [ ] **Seen once or twice, not explained, and what to read if it comes back**
      - Player 1's game ended mid-fight with no trace (2026-10-25 17:21:22): `logs/process-end.log`, then `Exit:`. A game that vanishes with neither was
        killed from outside, failed fast, or overflowed its stack.
      - A freeze on the loading screen into the Whiterun battle (player 1, 2026-10-24 20:03:30): the next freeze dump answers it, as a dump now keeps every stack.
      - A crash in the game's Havok world step 15 s after arriving through Dragonsreach's door in the same battle (`+0x1e05db`, player 1, 2026-10-25
        09:53:52, 1 report in the hub): a garbage pointer in Irileth's animation graph's data.
      - A confirmation box over the open map, then a 31 s loading screen, then the main menu (player 2, 2026-10-29 19:13:55). None of ours on the stack.
      - The prison's cell doors missing for player 2 (2026-10-25 19:40): `ReferenceEnable: X (base Y, form type T) switched off here` names them next time.
      - An ice wolf bounced owners and its body jumped about 8000 units into Helgen (2026-10-20 21:08): `Gained ownership`, `CorpseDiag`, `JumpDiag`.
      - Player 2's sliding lurker on MGO, not reproduced on this PC's MGO.
      - A freeze under HDT-SMP, 44 s (a public group, 2026-10-17, v1.9.3), 13 s after a burst of NPC copies. None of our code on either stack.
      - The Helgen prisoner carriage crashes HIGGS when grabbed (`+0xADF694`, 2 reports in the hub, v1.9.5): HIGGS reads its anti-tip action as a link.
        Not our code, unproven without our mod. Don't grab it.
- [ ] **Stutter while a party member sleeps** (2026-10-25). The Sleep/Wait menu no longer pauses while connected, so the world runs the slept hours in
      a few seconds: 16 and 15 frames over 50 ms in the two 30 s windows with the menu open, 0 in the one between. The slow frames are the game's.
      Candidate, not made: let the sleeper's menu pause as in the game, while the confirmation still waits for the server's grant. The rig cannot
      reproduce a long sleep: the arrow keys do not move the hour slider in VR.
- [ ] **Scripted scenes run in each game** (second MGO session, 2026-10-21). AP's Thalmor ambush ran in both games, so each spawned its own Thalmor and
      got the other's as copies: twice the enemies. Player 2 found no loot on them; the log does not show why. The Helgen keep door stayed locked
      for player 1 and opened for player 2, each game running its own scene.
- [ ] **NPCs popping in: which world wins** The spawn path re-enables an NPC the other game has active and moves it here (`Disabled actor is being
      re-enabled`, up to 10-30). The two games' worlds disagree: one game's quest scripts put a group away while the other still had it. A game
      that refused would have NPCs it cannot see attacking its player. A design question, not a bug in that line.
- [ ] **A load door locked here stays shut when the other player opened theirs** (the owner's call, 2026-10-21). A door that does not lead elsewhere
      now opens and unlocks here.
- [ ] **A flying dragon whose owner goes silent or out of range is still handed over and falls** (older code). Freezing it instead needs a decision.
- [ ] **A lantern moved by hand showed nothing on the other side** (2026-10-24 19:28, outdoors east of Windhelm). Neither game logged a world-object
      move, and which lantern, and what kind of object, is not known. Other carried objects do show (`WorldObject: X was put down over there`).
      Not confirmed either way.
- [ ] **Does a held arrow float ahead of the fingers in the archer's own headset?** (2026-10-24 15:03, seen on the other player's copy). Measured: the copy
      puts the arrowhead where the archer's game sends it, about 80 units from the hand bone, so it shows the archer's body hand as it is. The rig
      cannot show the archer's own view. Not confirmed either way.
- [ ] **Spells made at run time stay unseen in the other game** All 468 `No projectile found` events in the crash reports were launches of forms a mod
      made at run time (on MGO, NPC Spell Variance's copies; Alduin's breath too). Such an id means nothing in another game. Since bd2adc0 the
      launch is not made (`Remote projectile from X not launched: ...`). A real fix needs the other game to recognise the spell, which is not small.
- [ ] **Session B review items left unchanged for lack of evidence** (movement in packet-sized messages since 840c92c).
      - An NPC rider sent every 100 ms on a horse sent every 33 ms plays up to 100 ms behind it. Left with the parked Mounts item.
      - Each packet-sized chunk after a frame's first may still straddle two packets: it needs a TiltedConnect flag to send each chunk on its own.
- [ ] **Small known gaps** (from reviews and sessions; no report of any in play).
      - An owner in a menu that pauses the game keeps sending reports, so a quiet stop never applies: opened mid-step, their copy may still walk on the spot.
      - A copy waiting for its 3D keeps a death and inventory changes, but equipment changes made meanwhile are lost unless a spawn is re-sent.
      - A copy refused at a load door tries again while its package lasts, so it may stand there until its owner's position moves it.
      - Something moves a dead copy's reference with its living owner while the ragdoll stays (a bear in the rig, +600). Players do not see it; it
        may matter to distance checks on the reference.
- [ ] **Revive and respawn, what is left** (confirmed in play; the story is in `VR_HISTORY.md`).
      - A safe point in an interior: no respawn in a kept log was at one, and the rig has not tested it.
      - The respawn's knock (`KnockExplosion`) is skipped on VR, not fixed: it threw the player thousands of units.
      - The revive gesture is not done (options first, as the owner asked).
- [!] **A dead copy near the player lost its 3D and was made again** (rig only: `live-death-copy` once in 3 runs on 10-26, `live-body-grab` twice on 10-30;
      none in play). Cause unknown. `CopyGone: dead copy X lost its 3D N units from the player; ...` says more when it comes again.
- [ ] **What holds a removed copy with 4 handles** (a copy at 2 has been a normal end since build 4df623a: `the game let go of it`, 35 lines in play on
      2026-10-31). `CopyFreedHeld: X freed after 60 s with 4 handle(s)`: 55 of 161 lines before that build, 3 on 2026-10-31, any kind of actor,
      ghost or not. Not seen in the rig's runs.
- [ ] **Console travel can crash the rig's game** (2026-10-30, 4 in about 9; rig only, none of our frames on the stack, and the hub has none of these
      addresses). Scripts and `headless.ps1 up` now use Papyrus `MoveTo`. Still on console travel, each saying it needs a fresh `headless.ps1 up`:
      cave-travel, cw-battle, door-join, door-join-locked, npc-grace, respawn-cell, scene-dialogue, teleport-far, whiterun, world-object-reload.
- [ ] **`live-defender` can time its hits off a stray clash** (rig only; 2026-10-31, failed on 3 of 6 runs across c382599, 1299bb5 and 4716f50 with nothing else changed). The bot takes
      `atclash` from the first clash of the run. One before the script's own clash leaves its hits about 40 s old, past the 30 s the game keeps a clash, so they land
      where the script expects a block (`a hit of 30 ... lands, 43468 ms after it happened`). Clean runs read `64 ms before`. What touches the blade first is not known.
- [ ] **Trustworthy rig tests** (asked 2026-10-17: fixes that pass in the rig keep failing in play). The rules: red before the fix, REQUIRE the preconditions,
      checks on what the player sees, report rig results as rig results, and after each play session "rig said / play said".
      Next: the scripts that leave the copy behind the headset (`live-revive-copy`, `live-creatures` and more), not judged one by one yet; the new
      suite as a whole; `live-copy-knockdown`'s `getup-jump 1 (up to 149 units)`, counted but not judged in its passing run; `live-copy-quiet` on
      MGO, on player 2's PC (red on v1.9.6, green on 1b7b5b5 or later), as this PC's MGO profile cannot walk in the rig; and a choice for the
      owner: let the rig set the monitors to 60 Hz while it runs.
- [!] **Handling a living NPC the other game runs: send the pull** (decided 2026-10-10: the grabber's game sends the
      pull to the game that runs the NPC, which applies it). Needs a safe way to tell when a HIGGS hand holds a copy:
      the `HandHold` diagnostic read Havok constraints under the world lock from a game thread, corrupted contacts
      (rig crash at `SkyrimVR+0xa10860`) and was removed. A redo should read them inside the physics step. The rig
      cannot make a HIGGS grab: DevBench answers the controller state itself, so its buttons never reach HIGGS.
- [!] **Grabbing the other player: a buzz on the grabbed hand** (decided 2026-10-10, no stagger). A `GrabRequest` from
      the grabber's game, passed on by the server as `NotifyGrab`, pulses that hand (`VRHaptics::Pulse`, like
      `PulseClashHaptics`); a protocol change. Waits on the same grab detection.
- Checked 2026-10-24 for both items: Cydaphex's port does not detect a HIGGS hold either. Its NPC "hand claim" takes
  the NPC over when a hand comes into reach, a flag in the ownership claim's epoch with a server rule, so PLANCK and
  HIGGS grab it on the grabbing PC. That is the "hand claim" route under "Interesting things to add".
  A detection with no Havok read, for the owner to decide on: a grip held (the controllers' state, as
  `VRHaptics::GripsHeld` reads it) with that hand within a few units of the copy's body (`NearestHandToBody`, nodes on
  the main thread). It reads the player's intent rather than HIGGS's hold: a copy without physics bodies here could
  never be held by HIGGS anyway. The rig's controllers can do both (`DO hold`, `DO grip both`).
- [ ] **Your body pushing loose items** (decided 2026-10-12: the walker's game owns a pushed item). Since 2026-10-12 the
      other game no longer sends a move that starts next to a player's copy, so a walked-into item stays put on both
      sides. To do: collision bodies on the local player's legs and torso (as the weapon bodies are made) so the
      walker's own game pushes the item and sends the move. Neither HIGGS nor PLANCK has a setting for it.
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

- [ ] **A mod list's server: a connect file inside a mod** (planned 2026-10-31, nothing written; starts once the current tree is committed).
      FUS's makers run public test servers and want the mod in their Wabbajack list with their server already set. `connect.txt` lives in
      `%LOCALAPPDATA%`, which neither a FOMOD nor Wabbajack can write to, and a list's players never see a FOMOD. So the game also reads a file
      from inside a mod: a list ships it as a small mod, and anyone else drags a zip into MO2. Client only, no protocol change, and our own
      release ships no such file, so a list's mod never has to beat ours.
      The file: `Data\SKSE\Plugins\urSovngarde\connect.txt`, in the format of `connect.txt`. `SKSE` because MO2 warns about a zip with no top
      folder it knows, and `urSovngarde` is not one.
      The rules:
      - The player's own `connect.txt` wins whenever its first line names a server. The launcher writes it for every join, a friend's code as a
        local tunnel address, so a mod's file winning would send that join to the wrong server. The cost: after any join or `setup-connect.bat`,
        a list's server is ignored until that file is deleted. The guides say so.
      - Address, password and `autoconnect` all come from the one file that gave the address. The two are never mixed.
      - No port means 10578. Today it means 25681, TiltedConnect's own default (`Client.cpp:120`): the launcher and `setup-connect.bat` both add
        the port, a hand-made file would not, and the game would retry a dead port for good.
      - Ticking another server mod takes a game restart, as MO2 builds its virtual folder at the start.
      The work, in this order:
      1. Unit tests, `Code/tests/connect_file.cpp`: a byte order mark, a setting on line 2, a password then a setting, no port, a port kept, an
         empty first line as no server, and which file wins. Reading a file moves to a small header with no game in it, like `PvpProjectiles.h`.
      2. `VRConnectService::LoadConfig` reads the player's file, then the mod's, by a relative path, as `LogEngineFixes7` reads through MO2 today.
         `Config` says which one, and the `connecting to` and `waiting for F6` log lines name it: that is the first question in any report.
      3. Rig, red on the current build first: `live-connect-default`. A `headless.ps1` switch sets the player's file aside and puts one naming the
         test server in MO2's Overwrite, so the mod list is not touched, and `down` removes it. Red: `no server in`. Green: `connecting to`, the
         source named, and the bot sees the player.
      4. Guides: a section on a list's server in `README-mod-manager.md` (the path, the format, the zip, that anyone can read a password in it,
         that the player's file wins); a line in `README-vortex.md`, `README-manual.md` and `VR_MULTIPLAYER_GUIDE.md`. The last line of
         `setup-connect.ps1` and the comment in `VRConnectService.h` still say the game connects on its own, which now takes `autoconnect=on`.
      5. The launcher needs nothing for this to work: it never writes a file with no server (`set_auto_connect`). For its own TODO when this
         ships: a way back to the list's server after a join, and showing that server.
      Only real games can show: MO2 taking the zip with no warning and the game connecting from it; a friend's code through the launcher, with
      a server mod ticked, reaching the friend.
      For FUS when it ships: a list pins one build and the server refuses another protocol, so list and servers update together; the source is a
      GitHub release (`directURL`), which must then never be replaced.
      Left out on purpose: a FOMOD (it would only help the Nexus copy), an MCM menu, per-player feature switches (the server-wide ones are in
      `STServer.ini`), several servers in one file, a tool that writes the zip.
- [ ] **Mounts** (parked 2026-10-12). The rider's `MountRequest` crosses and the other game gives the rider's copy the
      mount package, which it accepts, but the copy never gets on: copies run no AI of their own. Tried in the rig and
      reverted: `Actor::PutActorOnMountQuick` (vtable 0xAE on VR) returns false; letting the copy's AI run with its
      position free for 10 s left it beside the horse. Next if taken up: build the rider/mount `ExtraInteraction` on
      the copy, then `PutActorOnMountQuick` (real reverse work on VR); or ask cmpayc's fork whether it has mounts.
- [ ] **NPCs pulled back when a player joins** (2026-10-14): NPCs that walked away in the host's game, then were removed
      by the server for want of an owner, come back where the joiner's save has them. A fix that remembered the last
      released place was tried and removed the same evening: some release positions are bad ((0, 0, 0) for an unloaded
      actor, far under the floor), and Imperial guards ended up stacked. A redo must accept only sane positions.
      Still parked (2026-10-31): no report of it since 10-14, and no log line measures it. The removed attempt (d30525f0)
      took any release position and teleported on it. The server can refuse (0, 0, 0), but it holds no ground height, so it
      cannot tell "far under the floor". Bringing that teleport back blind risks the stacking again, for a case nobody has
      seen lately. What would move it: a play report of NPCs pulled back, with both players' logs. The (0, 0, 0) half has a
      clean test: a release whose actor is already gone carries cell 0 too (`Transferring ownership ... cell: 0`, seen in the
      rig on 2026-10-31), and `OnOwnershipTransferRequest` ignores such a place.
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
- [ ] **A small in-headset overlay for chat:** chat shows only on the dashboard tab.
- [ ] **A flat (non-VR) player with VR players** (asked on Reddit, 2026-10-09). Possible in principle: the repo still
      builds the flat client, and `Code/encoding` has no VR-only message, so both builds share the protocol and the
      server. Missing: (1) the VR player on the flat screen -- VR swings are not animations, and the poses we send
      (`VRPose`) are applied only in VR builds, so the flat side would pose arms and weapon from them, the bulk of the
      work; (2) form ids meaning the same thing on both sides (`SkyrimVR.esm` against the Creation Club files, VR-only
      plugins), with a rule for the expected differences; (3) a flat block counted in the defender's rule, which knows
      only VR blade contact; (4) the flat build untested since the fork, testing needs a Skyrim SE copy. Only once VR to
      VR is solid.
- [ ] **SkyrimNet with the mod** (low priority; an MGO player asked 2026-10-23 whether both work together). Not started.
      SkyrimNet (AI dialogue for NPCs; MGO's optional "AI and Speech" section, beta 24, off in the host's profile) runs
      on its own in each game. Its lines play through the game's audio manager, not the `SpeakSound` call we sync, so
      the other player never hears them. Its AI packages (follow, wait, walk up to talk) fight our position sync on
      copies, since copies run their own AI. It likely takes the other player's character for an NPC (UNVERIFIED).
      Never run together with the mod. Everything below runs only when `SkyrimNet.esp` is loaded.
      - Step 1, no protocol change: put the other player's character and every copy the other game owns in
        `SkyrimNet_ActorBlacklistFaction` (SkyrimNet's own filter: no talk, memories or actions), and take an NPC out
        when this game becomes its owner. Our faction sync must not send that faction, and `Actor::SetFactions` clears
        every faction before applying the owner's, so it is put back after. Each player then talks only to the NPCs
        their own game runs. Setup note for MGO players: only one of the games runs SkyrimNet's Game Master.
      - Step 2: `PublicRegisterEventCallback("dialogue", ...)` (SkyrimNet's `CppAPI/PublicAPI.h`; it runs on
        SkyrimNet's threads, so queue the work to the game thread) gives each line's text and speaker. Send it as a
        subtitle (`SubtitleRequest`) so the other player reads it on their copy, and hide it on a timer, since no voice
        line ends it there. Maybe also `RegisterDialogue` in the other game so both NPCs remember it (UNVERIFIED: it
        may make that NPC answer).
      - Not planned: the voice itself (SkyrimNet has no call to speak a given text, and capturing its audio is fragile
        and a protocol change), and actions on NPCs the other game runs (an ownership handover that fights the party
        leader rule).
      - Testing needs SkyrimNet set up on both PCs, with an OpenRouter key and credit; the rig has no SkyrimNet.
