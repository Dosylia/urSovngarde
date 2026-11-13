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

All of this needs build dd9f756 or later, on both players and the server (a protocol change: an older client cannot join). Do what you can, in any order. Each line says what
to do, what you should see, and what to read in the log if you don't. "It looked fine" is a result too: say so.

Before you start: don't play the vanilla Helgen intro together. Player 2 stays tied, because Ralof is a copy in that game and
his scene never finishes. Use an alternate start, or connect after the intro.

- **NPC presence report, step 1 (a protocol change: every player and the server need the same new build).** Nothing to do in play: fight NPCs as usual, ideally three or four players at a bandit camp.
  Each game tells the server every 2 s which NPCs it has loaded, as its own, a copy or a stand-in; nothing about ownership changes yet. Afterwards read the server log:
  `Presence: removing actor X after ... while another game still has it loaded` is a drop that step 2 (server only: never remove an NPC a game still has, hand it only to a game that has it) would have prevented;
  every 30 s `Presence: N characters on the server, ... removed in the last 30 s, M of them while another game still had them loaded` gives the share, and the per-player lines count
  `it runs that the server gives to another game` and `it shows as a copy that the server gives to it`, two games disagreeing about the same NPC; `N of these two kinds already so at the last census` are the ones that lasted 30 s or more, the rest are hand-overs still in flight (a report can be 2 s old). Both logs are TEMPORARY and go once step 2 is decided.
  The 2026-11-05 four-player session had 242 removals at Halted Stream Camp and 222 at Silent Moons Camp, many within a second of a hand-over; how many were still seen elsewhere is what this measures.
  Where it stands (2026-11-06): built on top of 2200fbc6 (protocol `c5173cc4`), the message round trip is a unit test, the bot suite passes, not yet in a release, not yet seen in play.
  Build v1.9.8-7-ge2835e3d (2026-11-07), client and server, FUS and MGO; TPTests 114 pass. Not yet seen in play; every player needs this client.
  Rig, 2026-11-06, two real games (`two-games-bugs.py`, FUS testing profile, one save): the reports arrive and agree with the server (0 to 3 characters loaded in no game).
  Removals while the other game still had the NPC: brawl 4 of 35, Windhelm and reconnects 7 of 131, `walk-away` (new: the owner leaves four cells, the other stays) 0 of 71, where the staying game kept all 7 placed NPCs and took them over.
  No disagreement lasted to the next census. Four games with different mod lists, levelled stand-ins and a leader change, as on 2026-11-05, are not something the rig reproduces, so step 2 waits for a real session's numbers.
- **Party of three or four: enemy damage (client only).** Enemies now deal 120% to a player when two, three or four of the party are within 50 m; it was 135% for three and 150% for four, which with the
  server's difficulty took players from full to down in about 2 s on 2026-11-05. Their health bonus is unchanged. Say whether fights still feel fair. The line `Balance: now N of the party within 50 m` shows the numbers.
- **A copy's or a chest's items set without the game freezing (client only, uncommitted on 65b14ee7).** Nothing to do in play: siege battles, loot, trade and open chests as usual.
  In the 11-07 play the game's remove-everything call looped forever in player 2's game, on the copy of the siege's Stormcloak Archer FE09F882 as it spawned (at 15:51 and 19:48).
  The stuck thread, named with the build's symbols: `CharacterService::RunRemoteUpdates` → `Actor::SetActorInventory` → `TESObjectREFR::SetInventory` → the game's `InventoryChanges::RemoveAllItems` → `RemoveItem`, over and over, memory 38 to 71 GB.
  65b14ee7's count check caught about 25 other archers that session, not that one. The owner's game also held 15 chests whose items' extra data counted -1 to -5, which it then skipped, so their items were added again at each reapply.
  Now the game's call is gone: each list and each item is removed once per pass, by its own pointer, and one that does not go is logged and left; a list counting 0 or less is set to 1 first.
  Levelled lists an inventory carries with its items are no longer added to a copy or a chest; copies had held them as items (count 2 after two sets on the first try of this fix).
  Build v1.9.8-8-g65b14ee7-dirty.973eea0 (also the battle page's `urSS_Battle`), client on FUS and MGO, server unchanged (same protocol); TPTests 114 pass.
  Rig, two real games, a Sungard battle of 50 a side, twice: no freeze, no `kept item` line, about 115 copies' items set; 138 of 163 soldiers held the same item kinds in both games, against 97 of 167 on the first try.
  The freeze itself did not come on the rig, on the old build either (a fresh battle; in play it came at a second battle), so the fix is checked by its own bound, not by a reproduced freeze.
  Two copies still held 7 more item kinds than their owners (their own gear and the owner's); cause not known.
  To see: no freeze in a battle with player 2 near. In the log: `RemoveAllItems: X kept item Y` names an item a removal left; `counted 1 so that it can be removed` is a broken list mended.
- **Another player's copy flickering between their pose and the game's own stance (client only).** A bug report's video (2026-11-07) shows a copy, a mage holding a spell, jump between the player's real pose and the plain spell stance, standing farther back, several times a second.
  On the rig the sending game's body is now and then in another state for one update: an arm turned about 175 degrees, or the hips at the root instead of where VRIK stands them; the other game played those updates, and its copy jumped there and back.
  `PoseSpike: copy X: N poses in 30 s jumped away from the one before and straight back (shown as they came)` counts the one-update jumps.
  Say whether a copy flickers, and when. Read `PoseSpike:` and the temporary `PoseFlipDiag:` lines: `local player 14: ... steps ..., N came back` on the sender, `copy X: ...` where they arrive, `copy X posed at N of M frame ends`, and `copy X had no pose at N of M updates` (the arms back to the animation, the stance in the video).
  The step counts leave out the hands (a wrist flicks that fast in a swing) and poses that arrived late or twice (`N came late or twice and were left out`).
  `had no pose` also comes when the other player opens the map or skills menu, since no pose is sent then; it counts frames shown, not updates received.
  Both games need the build for the logs: each writes its own half of the lines. The change below is in the watching game only.
  Play logs of 2026-11-11 (both games, v1.9.8-12 then v1.9.81): the one-update jumps are real but rare, about one a minute (47 in 77,000 poses on one side, 20 in 43,000 on the other, 4 `PoseSpike` lines in an hour), too few for "several times a second". The copies were posed at only part of the frame ends, and almost every other one was "out of view". The filter for the jumps tried on the rig stays unshipped.
  Likely cause, UNVERIFIED: the view check tested the copy's own spine bone, which holds the game's animation or our last pose depending on the frame, 70 to 80 units apart. Near the cone's edge the copy was then in view and out in turn, posed and not posed in turn: the stance in the video.
  Change (uncommitted, built as v1.9.81-2-g5720bfc5-dirty.6848f6e, deployed to FUS): a live copy is checked where it is drawn (its root moved by the pose's hips, at chest height), and once in view it stays posed until 10 degrees past the cone. Bodies keep their check.
  To see: no flicker, with the other player at the side of your view too. If it flickers: `PoseFlipDiag: copy X posed at ...` now ends with `switched between posed and not N times` (a few per 10 s is looking around; dozens is the flicker) and `the view check at its bones would have decided otherwise at N` (how often the old check differed).
  Rig: `live-hands` passed on this build (copies still posed, hands 0.0 units off); the rig's headset does not turn, so it cannot show the flicker. Not seen in play yet.
- **Dead bodies, at connect and after (needs eed5ad8 on both games).** The first player plays alone a little, then the second connects near an old fight. Bodies lie down in both games, and stay down when you walk away and back.
  An older save may still hold bodies that stand: they should drop within a few seconds of you coming near. If one stands: its form id, and `BodyStand:` in the log. The dead creatures of "Dead on one side" are not all covered, see below.
- **A dragon fight with both players near (needs 8f303ba on both games).** The game must not stop. A `NonFinite:` line in either log is the guard catching what froze the game on 10-31, and it names
  the actor's place: say the time. If the game does stop: `FreezeWatchdog:` in the log and `logs\freeze.small.dmp`.
- **Fast travel from the map, a few times, and some plain closes (needs a114d97, player 1 is enough).** The rig cannot pick a marker, so this is the
  one way to see it. The map goes, the fast travel starts, the game does not crash. Plain closes bring the world back at once.
  If not: every close should read `Map menu: hide handled on thread N (the main thread), the map unpaused`. Say the time of anything odd.
- **PvP on: held spells go where they are aimed.** Hold Flames, then Sparks, on each other, and turn your hand away with your body still facing them. On the other screen the spell
  follows your hand, not your chest. A wide flame reaches them when it hits; a thin beam can pass a little to the side, as the copy's hand is not exactly yours.
  Say how far off it looked, and when. If not: both logs have `SpellAimDiag:` once a second, the caster's the real angle, the other's the same angle and how far that player is from it.
- **PvP on: one damage rule.** A hit by sword, arrow or spell on the other player now takes what an NPC's hit takes. So a sword hit of about 21 is about 105 at our difficulty, and a power
  attack can kill at once. Say whether that feels right or too hard: it is one number to soften. An arrow takes one hit's health, not two, and a dodged arrow takes nothing.
  If not: `PvP: hit remote player X for N` and `PvP: spell X hits on remote player` in the attacker's log, `PvP: spell hits from player` and `PvP: an arrow from copy X takes N` in the other's.
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
- **A follower out of an inn.** One player takes a follower out of an inn while the other stays inside. Her copy goes with her for the player left inside, and that player's own reference is as it was.
  If not: `Follower: <id> (server id <id>) is player <n>'s, far from here`, `Ghost: reference`.
- **An NPC handed over mid-walk (needs the next build).** Stay in a town with people walking about, and let one of you go through a
  door and come back: that hands NPCs over. No NPC stands with one leg forward, rocking, on either screen, and none walks on the spot
  or slides. Say which screen if one does. If not: `Takeover: NPC X was moving as a copy here`, `TakeoverDiag:`, `SwimDiag: own NPC`.
- **A weapon taken by hand off a body the other game runs (needs the next build).** Do it with the other player watching, holding the same
  kind of weapon if you can. They see the item once, in your hand, and it goes when you stow it or lies where you let it fall. It
  is gone from the body, and their own weapon stays in their hand.
  If not: `BodyTake: X xN taken off Y`, then `DroppedItem: dropped X`.

- **Win a hold with both players in the war.** The member's journal completes that hold and shows the next one, without any console command. Read
  `WarState: the war changed here` on the leader and `WarState: following the leader's war here` on the member. A player who rejoins catches up within 30 s.
- **Loot several bodies in a row while the other player watches from in front.** The looter's copy stands still in each menu, in the pose
  it had, with no snapping of arms or body at each opening and closing. If not: `VRBodySync: actor X hands --`, and whether `owner` and `copy` differ
  while `Menu queued: ContainerMenu` is open on the looter.
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
- **The map, player 2 and together:** 60 closes online, no freeze. Read `Map menu: hide handled on thread N (the main thread), the map unpaused`
  at each close, and no `put on a list again` or `led back`.
- **Followers.** Each player brings a different follower, and each sees both, following their own player. Then both have the same one: only the party leader's exists, the other player has none while connected, and theirs is back with their items after they leave the session.
  Save and reload once in each case, connected and after leaving. Read `Follower:`, `Ghost: reference`, `SaveGuard:`, and that nobody's follower is missing after leaving. A granted sleep's OK button and a door with a follower are worth a look too.
- **Look at the other player:** stand in front of them while they hold their hands out, palms up, then have them run past you with
  smooth locomotion. Hands and palms are as they hold them, the legs run, and the face looks like theirs. Only a headset can tell.

After the session the logs answer the rest: `Stall:`, `Silence:`, `ThreadOwners:`, `Connection:`, `VRBodySync: actor X body --`, and
`logs/process-end.log` if a game vanishes.

- [!] **The Civil War journal and holds stopped following the leader** (session 2026-10-31 14:26 and 20:34: player 2's quest did not move on to Liberate Hjaalmarch, and it had
      "never really synced since Liberate Falkreath"). Fix in the tree on 7eedbbe1, a protocol change: `RequestWarState` and `NotifyWarState`, so every player and the server need the new build. Not seen in play.
      The battle quests run on the leader only, so in a member's game `Remote quest updated: CWFortSiegeCapital, stage: 9000; running here no`: the scripts that give a hold to the winner and move the journal on never run there.
      Player 2's save read on the rig: `CW` active and on the right side (`WarIsActive` 1, `playerAllegiance` 2), `Liberate Falkreath` shown and not completed, Falkreath still owned by the other side.
      `Remote quest 19E53 already reached stage 4, current stage: 255` was a real part of it. `CW` allows repeated stages and reaches stage 4 once per hold, when the player reports in (player 1 at 14:25:52 and 20:34:20): it completes the report objectives, shows the camps and the next hold. A member refused every one after its first. A member now runs them again on a quest with that flag; a leader still takes a member's stage only once. `GetCurrentStageID` is a high mark: it stayed 255 after stage 4 ran.
      Player 2's saves, read on the rig: `CW` had stage 0 alone on 10-11; 0 and 255, war stopped, `MQ302` at 320 with stage 40 done on 10-16. That player did not play `MQ302` and used console commands in those weeks, and a member also applies every stage it is sent; the logs of those days are gone, so which of the two set it is unknown. Stage 4 came by 10-25 and set the war active again.
      Stage 255 cannot be taken back; no gameplay check of it found beyond one package on `GetStage >= 100`. `MQ302` at 320 and `MQ305` at 12 still run in that save, which is no fault in itself: `MQ302` also ran on at 320 in the 10-18 07:09 save, with the main quest finished and `CW` at 256. That later saves have lower stages (10-18 19:31: `MQ305` at 12, no `CW` 256) means an older save was loaded or a quest reset. Nothing to fix.
      The leader now sends who owns each hold (`CWOwner` on the hold's location) and each `CWObj` objective's state, within 2 s of a change and every 30 s for whoever fell behind. A member sets the same through the natives the war script uses. A member's own war is not sent, and a leader not in the war yet sends nothing.
      - Rig, two real games on a copy of player 2's save, local server: a hold won reached the member in under 2 s, with `Completed: Liberate Falkreath Hold` in its journal; an objective the leader does not have went at the next send; a member's change did not reach the leader; a member away while a hold fell caught up 27 s after coming back; one log line per change and no repeats.
      - Same rig, stages: the member's war stopped with stage 255 (`WarIsActive` -1), and the leader's stage 4 set it active again in 0.5 s (`Remote quest updated: 19E53, stage: 4; running here yes; run again`). A member's stage 4 was still refused on the leader. Play on d40cc8b and e6c7900c is the unfixed side: both refused it. 14 of 14 checks; bot pairs 14 of 14 on this protocol, before the repeated stages, which are client only.
      - Not carried: the rest of what winning a hold does in the member's world (the jarl, garrisons) unless a relayed `ReferenceEnable` does it, a `CW` script's own variables and globals (an objective's percentage text), and the leader's mission quests, which still run on the leader only. A member whose `CWObj` does not run gets the holds, and the objectives wait.
      - `Gameplay:bEnableMiscQuestSync` still decides whether `CW`, a quest of type None, syncs its stages at all. This new message does not depend on it.
      - To see: "Win a hold with both players in the war".
- [!] **A player's copy snapped between two poses at each menu** (session 2026-10-31 14:52, player 2 watching player 1 loot; fix in the tree on
      82bc61ca, client only, no protocol change; each player needs it for the others to see them right; not seen in play).
      In a full menu the game shows a second body. `CaptureLocalPose` read its bones and sent them, so each opening and closing snapped the copy,
      and looting 25 bodies in 3 minutes looked like the copy fighting an animation. In that session, with a loot menu open on the owner the copy's
      hands matched in 5% of samples (median gap 27.6 units) and with no menu near in 62% (median 0.1).
      While a menu that pauses by its own flag is open and the game shows another body than the last one read, the last pose is sent again, heading held.
      - Rig, two real games, local server, one corpse, the menu 21 s open and 21 s shut: unfixed 0 of 17 samples with the hands on target with a loot menu open, 0 of 5 in the
        journal, gap 28.5. Fixed: median gap 0.0 in both, and 8 of 8 in the looting rhythm. The read body changed `0x18c5cd380` to `0x18c54a000` at each opening and back at each closing.
        Hands were within 0.4 units of the held pose in the first half second after closing, so nothing waits for the body to settle. The rig has no real VR arms.
      - Left alone: the skills and map menus still send no pose, so the copy plays its animation there.
      - Open, cause unknown, not a menu: the copy's hand can jump 12 to 18 units at any time on the rig, menu shut or held (2 and 5 of about 22 samples shut on two runs of the fixed build, 1 of 22 unfixed).
        `ReachHands` corrects at most 15 units, so a body that sways across that limit may flip between corrected and not. Not seen in play as such.
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
- [!] **The published Linux server archive crashes when a client joins, on the newer systems it can run on** (fixed by the
      local `gamenetworkingsockets` recipe in `xmake-repo`, 34fe6f91; Linux server only, not released yet). CI on f0eeccfc: a client joins the
      staged server on amd64 and on arm64. To see: a game client joins the next release's archive on a Linux host. The public server is not
      affected; which build it runs is still not written down.
      FUS's fork fixed the same cause the other way (18126f99, 75ad014e): Linux moves to OpenSSL 3.5.7, where ours keeps 1.1.1w, which is end of
      life. Their CI checks for undefined OpenSSL symbols, and has no client joining. A merge must take the root `xmake.lua` and both recipe
      files from one side: a mix links two OpenSSLs into one server. Moving Linux to OpenSSL 3 is worth doing on its own, after the hotfix.
- [!] **A server that comes with a mod** (611853fb, no protocol change, not in a release; rig only). `Data\SKSE\Plugins\urSovngarde\connect.txt`, in the
      format of `connect.txt`, gives a player with no server of their own a list's server. A list ships it as a small mod, and anyone else drags a
      zip into MO2. The player's file wins whenever it names a server: the launcher writes it for every join, a friend's code as a local tunnel.
      No port means 10578. README-mod-manager has the steps.
      Rig: `live-connect-default` red on 4716f50 (`no server in`), 5 of 5 on d40cc8b. With a dead address in the mod's file, the game joined the
      server in the player's own (one run, 3ca977d). Unit tests: `Code/tests/connect_file.cpp`. The rig's file sits in MO2's Overwrite, so this
      shows the read through MO2, not a real mod.
      To see, with no `connect.txt` of your own: drag a zip into MO2, no warning, tick it, start the game. `VRConnectService: the server comes from
      a mod's connect file`, then connected. Then, with that mod ticked, a friend's code through the launcher reaches the friend.
      Known gap: the launcher reads only the player's file, so it shows no server here, and after a join the way back is deleting that file.
      Left out on purpose: a FOMOD (it would only help the Nexus copy), an MCM menu, per-player feature switches (the server-wide ones are in
      `STServer.ini`), several servers in one file, a tool that writes the zip.
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
- [!] **Followers: each player's is known to the server, and shown to the others on a copy** (uncommitted build on 26f2792c, protocol 7b2006a6, not seen in play; every player needs it). Replaces a follower left in your interior, one follower bouncing between two saves, and most of a follower's effect on the other player's save.
      A follower is the game's teammate flag plus `CurrentFollowerFaction`; the flag alone is a quest's and stays an ordinary NPC. A living one is run only by her player's game while they are connected. Other players show her on a temporary copy, and their own reference is not moved or rewritten, only disabled while loaded nearby, and written to a save as enabled.
      In both saves, only the party leader's exists. The other player has no follower while connected, cannot dismiss her or recruit another, and gets theirs back with their items.
      Rig on FUS: `live-follower-copy`, `-save`, `-teammate-only`, `-shared`, `-shared-leader`, `-far-hidden`, `live-follower`, `-left`, `-stays-in`, `live-gone-here`, `live-teleport-far`, `live-teleport-far-loaded`, `live-npc-grace-outdoors`, `live-copy-through-door` ok; see below for the rest. MGO: the game starts, loads and connects; `live-follower-copy` could not run there, as Lydia is disabled in that save (INVALID).
      Open, from the rig:
      - One crash loading a save while connected, in 5 runs of `live-follower-save` that got that far (`SkyrimVR.exe+0x93f0f9`, 2026-11-01 18:52, just after `FormGuard: script handle FFFFFF0011D6`). The handle is not the copy's and nothing had been deleted; it looks like KNOWN_ISSUES' script engine crash on a freed temporary form, but that is not proven. A sixth run ended 30 s after the load with status 0 and no crash block, cause unknown.
      - `live-npc-grace`: the player's console `moveto` to her landed outside Helgen, so the rest of the script did not apply. She is an ordinary NPC there, on no follower path; not run on an older build, so whether it is new is unknown.
      - `live-takeover-lost`: `ref A2C94 feet still 5 15` failed, a foot moving 79 units against the pelvis. She is an ordinary NPC there too. See "An NPC handed over mid-walk".
      Known limits: a quest disabling a reference while it is hidden here is undone when it comes back, and is written to a save as enabled. A quest NPC with the follower faction is hidden for the other player too, whose own quest may need her. Followers from a framework without that faction stay ordinary NPCs. `kHiddenHere` is not cleared by a load.
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
- [!] **Crash at a fast travel from the map** (`SkyrimVR.exe+0x13085EE`, player 1 on d40cc8b, 2026-10-31 15:05; cause and fix in KNOWN_ISSUES,
      "Crash after closing the unpaused map"). Build a114d97 runs the unpaused map's close on the main thread at the start of a frame.
      Rig: `live-map-close` red on d40cc8b (the hide on a worker), green on a114d97 (Cancel, message box, a hit, a load), and `live-map-pointer` ok on b493cbb.
      That shows where the hide runs. It does not show the crash gone: it came once in hundreds of closes, never in the rig. Not seen in play.
      A real fast travel from the map cannot run in the rig. To see: "Fast travel from the map".
- [!] **The map freeze, player 2 and together** (cause and fix in KNOWN_ISSUES, "Freeze after closing the unpaused map"). Seen fixed by player 1,
      30 closes online on 2026-10-30. To see: "The map, player 2 and together".
      Since build 82f3689 (with ec6f79e9) the loop guard on the list draw goes in at the first map open, not at start, so CSX 3.19.2 and later can
      start; a hook that never went live is now logged as one. No rig run yet. To see: `RenderPassWatch: each list is checked for a loop before it is drawn`
      after the first map open, and no freeze at a close.
      FUS 5.1.0 ships CSX 3.19.2 (2026-11-12). Its first rig start logged 20 `RenderPassWatch: renderer ... still holds pass ... from frame N in frame N+2` errors in the first minute, none on the old list; that check only logs and stops after 20.
      The map was not opened in that run.
- [!] **Crash when another game's voice line reached a copy with no AI process** (`AIProcess::GetLipAnim`, VR 0x659B00, from
      `OnNotifyDialogue`; a second player at a siege battle's start, 2026-11-05 18:09, v1.9.8). The game's speak call reads the speaker's
      process unchecked, and a soldier switched on a second before had none yet. A sent line now waits for the process, up to the
      3 s retry. Build 82f3689, no rig test: the rig cannot make a copy speak in the moment it has no process.
      To see: no crash at a battle's start, and `waiting for its AI process` lines now and then.
- [!] **Fingers not synced on MGO's skeleton** (since build f997b6a the check takes up to three hops). 536 flattened bones, and `no usable flattened
      bone array` up to 2026-10-23; no MGO session since. FUS has no such skeleton, so no rig run. To see on MGO: no such line, and fingers
      move on the copy.
- [!] **Our browser helper outlived a crashed game and froze Mod Organizer** (2026-10-20; each helper now ends with the game, build 1a569f4).
      `helper-exit-check -StuckMain` red, then ok; rig only, and not confirmed in play. To see: after a crash, no `TPProcess.exe` left in Task
      Manager and Mod Organizer starts.
      One did outlive its game on 2026-10-31: a `TPProcess.exe --type=gpu-process` started at 10:36 by the 43-second rig session on 8ad48b5 was still alive at 16:20, its
      parent gone, 2 MB. The crash at 15:05 that day left none. How that one survived is not known, so a GPU helper may not be covered by the fix.
      Two more on 2026-11-05, both started the second their game ended: a renderer at 00:56:11, at a rig game's crash, and a
      `gpu-process` at 13:15:52, the same second the rig quit its game. Their parents were gone, and they held `TPProcess.exe`,
      so the launcher's update to v1.9.8 stopped until its "Stop TPProcess.exe" button ended them.
      Lead, not confirmed: a helper started as the game ends may never reach `WinMain`, where it starts watching the game.
      One possible reason is Mod Organizer's file system creating each child suspended, to inject into it, and resuming it after.
      The 2 MB of the 10-31 one fits a process that never started.
      Next time one is left, read its threads (`Win32_Thread`, `ThreadWaitReason` 5 is suspended) and its `--pid=` switch before ending it.
      A fix that covers every case is a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` for the helpers: the system ends them when the game dies.
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

- [!] **A copy switched off while a game job still ran it** (2026-11-12 23:08:14, a public pair, report 20261112-220845; also 2026-10-18 on v1.9.4). Fixed in build v1.9.81-4-g003ad8cb-dirty.74a329c, rig only, not seen in play.
      The server removed a dead Skeever copy (FF0008BE) the game still held. The client's update switched it off at once (`Temporary Remote Held ... disabled and queued`), and 11 ms later a movement job of the game read it, deleted by then (`SkyrimVR.exe+0x714EB2`, MovementControllerNPC, the copy in R15 with form id 0).
      The client's update runs on the game's job threads, beside the jobs that run actors. The October fix kept such a switch-off away from the game's own temporary actors; our own copies were still switched off that way.
      Now the three places that switch off a removed copy queue it, and the main loop switches it off at the start of the next frame, on the main thread (`CopyOff: X switched off at the start of a frame, on the main thread`).
      Rig: `live-copy-removed` on a0f2f73 and 74a329c, each time 6 of 6 copies switched off on the main thread about a frame after their removal, the game alive. No red run: the race cannot be made on demand. To see: no more `+0x714EB2` crashes in the hub.
- [!] **Every address the client uses comes from its own table** (2026-11-11). Not in a release, not seen in play.
      104 ids the client uses on VR were answered only by the player's VR Address Library; releases older than 0.158 lack some of them (0.100 lacks 15, among them `Actor::DamageHealth`, `Actor::AddDeathItems` and the Havok contact listeners). They are now in `VRAddressOverrides.h`, copied from 0.275.0; no release from 0.100 on maps any of them elsewhere.
      The client also logs `VR Address Library <version>: <n> ids` at start and `SKSE plugins (<n>): <file> <version>, ...` at the first update, with the version Crash Logger shows, so a report without a Crash Logger log still names DynDOLOD's DLL version.
      The release workflow now refuses a tag not newer than every earlier one and runs the 14 bot pairs before it packages; neither has run on GitHub yet.
      Sleep/Wait no longer compares two addresses against its own constants, which could not fail once both were in the table; its byte and call checks stay.
      Rig: `live-clash` 11 of 11 on 75f7550 (sparks and contact listeners now from the table); build 3aef7f0 started, loaded and connected, both new lines as expected, Sleep/Wait hooks installed, no `did not resolve` line; deployed to FUS and MGO, client and server. To see: the next tag's workflow run, and no `did not resolve` line in players' logs.
      Why it matters (2026-11-12, one public machine on v1.9.81): `Address id 60493 did not resolve` in every session, and two crashes at `+1E05DB` (the weapon body left in physics). Two more crashes were in Havok with that line in the log (`+B3C2F1` with the same bhkWorldM and ahkpWorld registers, `+1114DA2` a second after a weapon body was placed), most likely the same.
- [!] **A crash as a jail sentence moves the player out, with another player's copy in the cell** (2026-11-08 23:17, a public v1.9.8 pair, Riften Jail, report 20261108-221734). Guarded, not seen in play.
      Player B accepted serving the sentence; 0.27 s later the game applied an effect (`PeakValueModifierEffect`) to player A's copy, and the effect's condition asked whether the player is in region C5853 (Skyrim.esm, edited by Azurite Weathers III).
      Player B had no cell at that moment, and `TESObjectCELL::GetRegionList` read a null cell (`SkyrimVR.exe+0x276DB9`, under `IsPlayerInRegion` and our pass-through `HookApplyActorEffect`).
      Guard: a copy's effect is not applied while the local player has no cell (`ApplyEffect: an effect on copy X not applied while this player has no cell`); a copy's values come from its owner.
      Which mod gave player A that effect is not known. Not tried on the rig: it cannot make that effect.
- [ ] **A null call on an AI thread updating an actor's face** (2026-11-08 21:36, player 2, Fort Greymoor, report 20261108-203703). The same crash as the eye tracking entry below, followed there.
      `SkyrimVR.exe+0x5EB6C9` under `Actor::IsMoving` / `UpdateActor3DPosition` / `ProcessLists::UpdateHighActors`, `BSFaceGenAnimationData` and `"NPCEyeBone"` in the registers; no code of ours on the stack. 3 s before, Stormcloak soldier 8C00A309 changed hands three times.
- [ ] **Crashes in character creation, connected, with no other player nearby** (2026-11-08 19:46:49 and 20:01:00, two new games at Helgen, the RaceSex Menu open for 17 s and 35 s; 20:05:00, Realm of Lorkhan, connected by the auto-connect 3 s after the menu opened, crash 8 s later). Cause unknown.
      Three of three were connected; player 2's character creation with the mod but not connected went fine. That points at something that runs only while connected, but no such code is found yet.
      Each time a part of the player's head read a pointer from freed memory, after a race or sex change. 20:05 is the same crash as 19:46 (`FemaleHeadHighElf`).
      Not connected (20:09, server stopped), the menu stayed open 14 minutes with no crash. The same game then crashed while saving, still not connected (20:15:52): the script engine's save read a garbage pointer from its own data (`SkyrimVR.exe+0xc6dbb0` under `+0x12ac40c`, `mov r8, [rdx]`).
      All four crashes were on v1.9.8-16-geed70ddc and in brand-new games, so the build and the new game are not told apart yet. The builds before it that day had no crash of this kind, but they only loaded existing saves.
      Not reproduced on the rig: console race and sex changes, Papyrus face, tint and weight changes (over 1100 steps, connected, also connecting mid-way), on a finished and on a new character; the rig cannot drive the VR menu itself.
      All three character creation crashes are one fault: a head part's shader property (`[geometry+0x168]`) freed while the part still held it.
      Diagnostic `HeadPartWatch` (TEMPORARY): logs a head part holding a freed property, and the stack of the release that destroyed one while a part held it. Deployed v1.9.8-17-gfab767c9-dirty.52e2ab5, FUS and MGO, client and server; it runs from the main loop on the main thread, its guarded reads tell the crash handler to leave their faults alone, and an address a new property took is not reported. Rig: 37 head rebuilds in character creation, no false line.
      Seen in play once on it (22:00, the owner): a new game in Realm of Lorkhan, connected 4 s after the menu opened as at 20:05, 2 min 46 s in the menu, 29 head rebuilds, a save after; no crash and no `HeadPartWatch:` fault line.
      So it does not happen every time; what differed from the three crashes is not known (that build, a freshly started server, nobody else on it).
      To see: a `HeadPartWatch: shader property ... was destroyed while` or `... holds no shader property now` line in any log, with the stack under it.
      19:46: the menu updated the head (`MaleHeadImperial` under its `BSFaceGenNiNode`) and read its shader property's fade node as garbage (`SkyrimVR.exe+0x3e95d0`, `mov eax, [rcx+0x10C]`).
      20:01: 4 s after the race change (`Animation graph project of actor 14 changed`), the shadow scene's update wrote through a garbage pointer from the brows (`FemaleBrowsHuman06`; `SkyrimVR.exe+0x1354c40`, `mov dword ptr [rcx+0x18], 0`).
      No code of ours is on either stack. Our only code touching a head's shader property tints other players' copies, and there was none; the VR body code writes only to copies' skeletons. The menu paused the game as it does without the mod.
      To tell: character creation on the same PC without connecting (the server stopped until it is done; the game retries and joins within a minute of it starting again). A crash there too is not the mod's; none there makes it ours.
- [!] **Soldiers revived before a player joins are killed by that player's save** (siege at Fort Sungard, 2026-11-08 12:04). Fixed in 870f1af0, built and deployed v1.9.8-16-geed70ddc (FUS and MGO, client and server), not seen in play.
      The host's game revived the fort's soldiers at 12:03:26 (`Death sync: local actor 526EB is now alive, telling the server`). Player 2 joined at 12:04:02 with them dead in its save:
      `DeathWins: 526F2 (server id 1A) is dead here but alive in its owner's game`, and the host's game took `player 2's blow of 100000` (`health broadcast killed actor 526F2`). Player 2 saw them all dead and stripped as it spawned.
      Fix (a protocol change, `6b296bbe`: every player and the server need this build): the owner's revive report marks the character on the server, a death clears the mark, and an announcement from a game that saw the revive carries it.
      A game joining later follows the owner's life for a marked character instead of keeping its save's body (`Death sync: X is dead here, but its owner's game revived it since its last death; it follows its owner`).
      `s_lifeFollowsOwner` keeps only the leader's switch-on case. A kill player 2 made where the owner did not see it, after the owner's revive, is lost: the body stands up again, as at the owner.
      Rig: `live-dead-revived-joins` RED on the build before (body kept dead, killing blow sent), ok after; `live-dead-stays-dead` ok after. TPTests 114 pass (a round trip of the mark).
      To see: soldiers alive at the host stay alive for a player who joins mid-siege.
      Follow-up in the tree, not built: a copy still waiting for its 3D now takes the mark from newer server state as it takes the death state, and every copy keeps the mark from its spawn or assignment, so announcing it again from that game keeps the mark (`CharacterService.cpp`, client only).
- [ ] **An NPC announced again every 4 to 5 s when the game it is handed to declines it** (2026-11-08 14:46:09 to :25, rabbits 107D04 and 107D15; 12:04 to 12:06, Stormcloak Archer 8C00A316, 18 times). Not fixed.
      The owner's player walked out of range, so the server handed the NPC to player 2. Player 2's game declined (`Hand-off of ... declined: a copy of ours stands in`), the server removed the character, and the owner's game, which still had it loaded, announced it again (`is gone from the server while ... stands here; announced again`).
      Each round, player 2's copy is deleted and made again. Which side should break the loop is open. The leader's claim fix below does not touch this loop.
- [!] **An NPC far from both players handed back and forth between their games** (2026-11-08 21:30 to 21:37, Stormcloak Soldier 8C00A309, about 10000 units from both, a new server id every 8 to 11 s). Fixed in build v1.9.8-16-geed70ddc-dirty.2e9c8bb, not seen in play.
      The leader's game claimed every copy it received, at any distance (`Sending ownership claim for actor 8A00A309`). Its discovery dropped the soldier as it left the leader's loaded area, the server found no taker, and player 2's game, which still had it loaded, announced it again.
      Now the leader's game claims a copy within 6000 units of its own player, a player's own follower at any distance, and any copy inside the player's interior cell, as before. A copy farther away stays with its owner and is claimed once it comes that near (`...; it came within 6000 units of this player`).
      Rig: `live-leader-claim-near` ok; on 1fcdc77 the copy was claimed at 7500 units (its far check failed; the run reads INVALID because the bot then owned no NPC to move).
      Regression run on 2e9c8bb: `live-npc`, `live-levelled`, `live-levelled-copy-rebuild`, `live-follower-stays-in` ok; `live-copy-through-door` ok on a fresh game (INVALID after other scripts had left Ralof the game's); `live-takeover-walk` INVALID twice, no baseline on this PC.
      Still open: the server's own `LeaderAssignment` hands the leader an NPC its discovery announces anywhere within cell range; last night's loop did not go that way.
- [!] **A crash in the game's eye tracking on a freed eye bone** (2026-11-08 21:36:54, player 2, Fort Greymoor, Irileth; 2026-11-12 19:49 to 22:15, five times on one public machine, each on a follower copy of Jenassa; the hub links two more machines, one on a Whiterun guard that was no copy). Cause unknown, diagnostic deployed.
      The game's per-actor job (0x7084b0, 0x7032f0, 0x5eb3c0) looks up `NPCEyeBone` by name in the actor's 3D, takes a reference, and crashes giving it back: the bone was freed already while still a child in the skeleton. `+5EB6C6`, `null call from +5EB6C9` and the jump to heap memory are this one call.
      The five Jenassa copies were made 3 s to 2 min before each crash (`Follower: E1BA9 ... is player N's; copy ... shows her`).
      `RecentModelChanges` ran on them for the first time: this client's last changes to the copy's 3D were 1 s to 2 min before, so no one change of ours lines up.
      Build v1.9.81-4-g003ad8cb-dirty.74a329c adds the TEMPORARY `EyeBoneWatch`: the plain NiNode destructor is hooked, and an eye bone destroyed while a live parent still lists it is logged with the stack of that release (`EyeBoneWatch: eye bone ... freed while its parent ...`) and listed again in the crash block.
      Rig (`live-eye-bone-watch`): installed on the first loaded actor (`.?AVNiNode@@`, SkyrimVR.exe+0x17EEC08), silent through four trips that unloaded 140 actors, 60 fps. It has never caught a real case.
      To see: an `EyeBoneWatch: eye bone` line in any log; its stack names what freed the bone.
- [ ] **A dead horse that could still be ridden** (2026-11-08 14:22, the host's game). Cause unknown.
      The host killed horse 798EB at 14:20:58 (`BodyStand: dead 798EB had 289 health; taken` at its death). Its prompt then read "Steal", and at 14:22:08 the host's game reported riding it (`Mount: copy FF001386 of rider 12 told to ride copy 798EB` in player 2's log).
      It read "Search" after player 2 touched it; no log line says what changed. Build 1a16be1 adds the TEMPORARY `Mount: X starts to ride Y, whose life state here is ...`, logged when a ride starts on a horse that is not plainly alive.
- [!] **Dead bodies standing: a body already dead here was given health** (session 2026-10-31 20:09, 20:12: a searchable guard standing, almost all NPCs standing for player 2 at connect, standing skeletons for player 1). Fixed in build eed5ad8, not seen in play.
      Cause: a copy that is to die gets 1 health so it lives until `Kill()` (`ValuesUntilKilled`). Three arrival paths gave it to a body already dead in this game, where no kill follows, so the body kept 1 health, got up and stood, still searchable. A save keeps that.
      Two real games on one save: the same corpse read -99877 health, lying, in its owner's game and 1 health, standing (pelvis 63 over the feet), in the second. Taking the health alone did not lay it down; `RagdollInstant` did. The first guess, the leveled rebuild, was wrong: with no rebuild they stood all the same.
      Fix: the arrival paths give that health only to a body alive here. A dead body is no longer rebuilt for its owner's leveled pick. Every 2 s, for bodies within 4096 units, a dead body's health goes and one that stands still gets `RagdollInstant` (`LayDownStandingBodies`, logs `BodyStand:`).
      Rig, two real games: 4 fresh games, every body with 3D lying with its owner's health, watched 45 s in 3. Before, 3 of 3 stood at 62 to 65 in the run with none rebuilt. One run on eed5ad8 read full health and standing 6 s after the bodies loaded; it did not come back and is not explained.
      `live-copy-came-dead` 3 ok, `live-death-copy` 6 ok, `live-body-hold` 4 ok, TPTests 111. The health line fired on real bodies; the lay-down branch has not, only the same event sent by hand. `Kill()` itself leaves 1 health too, and the line takes it.
      Not covered: the Death Hounds at 14:44 (a copy waiting for a death word, "Dead on one side"), creatures with no `NPC Pelvis` bone (their health goes, but they are not laid down).
- [!] **A freeze when a copy's weapon body is added** (rig 2026-10-31 from 21:36, three times in two-game runs, not reported in play). Fixed in build eed5ad8.
      `UpdateWeaponBodies` added the body at the frame end, under the world's write lock. `hkpWorld::addEntity` calls PLANCK, which took an SKSE lock, while a job running an SKSE task (Splashes of Storms' water check) held that lock and waited for the world's. Neither thread moved. Read live in the frozen game: who held what, by thread.
      Fix: the body is queued at the frame end and added at the start of the next frame, when no job runs (`AddPendingWeaponBodies`), holding its references and checked again. 12 two-game runs since, no freeze; at least 5 of them added bodies. `live-blade` ok alone, and failed its two blade-stop checks once right after another script, as it did on dd9f756.
      Still at the frame end: `ReleaseWeaponBody` takes the same lock. If PLANCK answers a removal too, the same freeze is possible there; it has not been seen.
- [!] **A full freeze when a dragon fight started** (session 2026-10-31 20:36 on e6c7900c, player 1's game). Fixed in build 8f303ba, not seen in play, not reproduced on the rig.
      Cause: the game's `NiPoint3::GetPolarAngle` (0x140C97030) wraps its angle in a loop a NaN never leaves (0x140C97095). The stall watcher caught one thread at that address at 20:36:45, and the freeze dump
      26 s later has it there, with the vector (-inf, +inf, +inf) on its stack. It comes from the movement planner (0x141189710): an infinite movement target minus a finite position.
      The actor stood at (135230, 9722, -11906), pitched 33 degrees up, so a flyer. The Elder Dragon's copy was made here at 20:36:22 and roared at 20:36:44.7, and the game stopped within a second.
      The dragon was player 2's, and that game went on. That the actor was the copy is inferred: no log gives the copy's place. Where the infinite target came from is not known. Our code only reads whether an actor flies.
      It fits copies running the game's movement step (`HookActorProcess`, in "Findings from reading the game's code").
      Fix: `GetPolarAngle` answers 0 for an infinite vector, and the planner treats an infinite target or position as no target, the game's own answer for one it cannot use. Each logs `NonFinite:` once a second.
      Rig: no red run. An infinite offset through `KeepOffsetFromActor` did not hang e6c7900c, twice, so the guard has never fired. `live-walk` 8 ok, both hooks installed, a real Elder Dragon beside the player ran at 100 fps. TPTests 111 ok.

- [!] **An NPC's lines not heard by the other player** (10-31 14:25 and 11-01 12:38: Ulfric talking to player 1, silent for player 2). Fixed in build v1.9.7-15-g6826e679-dirty.355ef2d, not seen in play.
      Cause: the game's first speak call for a voice line only starts loading its lip file, and returns with no sound. We asked once, so a line played only when its lip file was already loaded: about 1 line in 9 in both sessions.
      Fix: a line that made no sound waits for its lip file, read as the game reads it, then is asked for again, for up to 3 s. A line drops if its speaker dies or goes, or a local scene voices it. An actor past 4096 units is not asked again.
      Rig: `live-npc-line`, measured on 23ad300, gave sound id -1 and duration 0 for real lines, and a real sound of about 8 s once asked again. On 355ef2d they wait 15 to 16 ms for the lip file, then start. `live-quest-hold` ok.
      The rig cannot hear, and nothing here shows the mouth moves. In play, check: player 2 hears a conversation at normal distance, and the NPC's mouth moves. Player 2 should hear many more lines overall, as most were silent; if barks overlap, say so. `the game had not started it` in the log is a line lost.
- [ ] **Dead on one side, standing on the other, again** (14:29, 14:44, 14:52 on d40cc8b; see "Dead NPCs in two games"). Not fixed. At 14:52 it came after some time in place, not on a load.
      14:44, Death Hounds, dead for player 2 and standing for player 1, the case with a clear log: hound 1A002 (401A002 at its owner, player 2; 301A002 as player 1's copy). Player 1's blows from 14:44:13 to :17
      reach the owner as `health 121 to 104, already dead`, while the copy logs `kept at 1 health until its owner says it died`. The owner's log has no `is now dead, telling the server` for it. So no death word
      was sent, and those blows do not ask for one: the copy waits for a word that never comes.
      14:29, a guard dead for player 1 and standing for player 2: neither log has a death line from 14:28:00 to 14:29:40, and which guard it was is not known.
      14:52, Imperials getting up for player 1 while walking around, and an ice troll standing for player 2: no line says it, as nothing logs a body getting up.
      Next: an owner that gets a blow on an NPC already dead sends its death again. Find how the hound died with no line. Log a copy going from lying to standing, so the next session names the actor.
      Bandit CC591 of 10-26 (27 killing blows) may be the same thing; not checked.
- [!] **An NPC revived at its owner stays a body in the other game** (rig, two real games, 2026-11-01). Build v1.9.7-16-g2fb3bc58-dirty.84d6682, its last part never run, not seen in play.
      A script called Resurrect on an Imperial Soldier in its owner's game only. On 355ef2d it stayed dead in the other game, 0 of 3 rounds alive, health -99902, the owner's killing blow: `Reset()` unloaded the copy, and it was announced again with the server's stale health.
      On VR `Respawn` no longer calls `Reset()`. On 11f01df, 5 of 10 rounds then held, with equal health and about 1 unit apart. In the others the copy is dead again 50 to 100 ms after the revive, health full, with no call to KillImpl. Why is not known. A Papyrus Resurrect from our code did no better, 5 of 10 as well.
      84d6682 repeats the revive each second, 5 times at most, until it lasts: `Death sync: remote actor ... fell dead again after its revival`. Rig on d91bd5b, 2026-11-02: 10 of 10 rounds alive in both, health equal, under 5 units apart, one repeat in 8 of them. Why the first fails is still not known.
      `ReviveActor` on a body switched off does not take: dead and off, revived, then switched on stayed dead in both games, 5 of 5. Switched on first, then revived: alive in both, 5 of 5, under 10 units.
      Build v1.9.7-19-g9d1c6bfb-dirty.8543729, a protocol change, rig only, not seen in play. `ReviveActor` on a body switched off, or without its 3D, now waits until it has both. Every copy's revival is watched from `Respawn`, and the watch ends when the owner says the copy is dead.
      A copy its owner revives while it waits for its 3D now comes up alive, where it stayed a body before. That timing did not occur on the rig, so this has no test.
      `two-games-switch reuse` (dead and off, revived, switched on): 0 of 5 on d91bd5b, 5 of 5 on 6f71647, 4 of 5 on 8543729. The miss was a real death: the soldier came back alive in both games, took three blows 4 s later and died in both, and the check read it 6 s after the revive.
      New calls for plugins: `urSovngarde_IsOwnedHere`, and `urSovngarde_FindPartyMemberWithoutPlugin`, which asks the server whether every party member has a plugin. Two real games: first answer -1, then 0 for a file everyone has and 1 with a name for one nobody has, either case of file name.
      Build v1.9.7-20-g46bcb065-dirty.d629d90, not a protocol change: `urSovngarde_ShowCard` shows the death screen's card in the calling game, and a title too long for the card is written smaller. Rig, in a game not connected: both cards logged as shown, one with no subtitle, and a call with no title refused. Unit test for the title, red not run; a drawn picture of a long title looked right. Not seen in a headset.
      Play 2026-11-03 20:24, a battle started right after one ended within a second: the member's game sent the leader 19 killing blows of 100000 in 200 ms. Its copies had stayed bodies, as the revive and the switch off came 30 ms apart, so `DeathWins` kept them dead against their owner.
      A member's game also announced the leader's freshly switched-on soldiers before the leader's did, all 63 in three battles, so they stood where the member's game had them.
      Fix, client only: an actor the leader's game switched on here, or whose owner revived its copy, follows its owner's life (`s_lifeFollowsOwner`, not DeathWins). And a member's game waits up to 5 s after the leader's switch on before it announces that actor. A body here whose life follows its owner's is never announced from here.
      Rig, `two-games-switch.py quickoff` (killed, revived and off at once, then on): red 0 of 3 on 572b143 with a killing blow each round, green 3 of 3 on 4bef572. `claimfirst`: the member first 0 of 8 even on 572b143, so the rig does not reproduce that race (one PC); on 4bef572 the member waited for the leader 8 times. Not seen in play.
      A fresh review then found a gap: the life mark was set only when the switch took here, not for an actor already on or still loading. It is now set as the leader's switch arrives. Also from the review: the mark needs a real revival of a placed reference, and both lists are cleared on disconnect.
      Build v1.9.7-22-g05512d93-dirty.472df4d with those fixes, client only, no protocol change, on FUS and MGO: `quickoff` 3 of 3, `claimfirst` the member waited for the leader 8 of 8 times, none through a member. Not seen in play.
      The race of (b) does show on the rig in a real siege battle: on 572b143 at 22:00, the second player at the attackers' camp ran 23 of the leader's fresh soldiers for 10 s or more. The leader's battle logged `16 left where another game runs them`; those soldiers ignored its march orders, and the gate never fell. That battle, run again on the fix, is (b)'s green check.
      Which real revives reach this (quest scripts, mods, Reanimate) is not known.
- [ ] **A crash in the game's AI four seconds into a big battle** (play, 2026-11-04 21:25, the leader's game, 472df4d, confirmed by the player). `+0x743dc2`, a copy past a page end in an array erase.
      The chain is an AI job, then ActorKnowledge `0x7434f0` (SE 41632), erasing from its own list at `+0x28` through an iterator. That iterator pointed 4 MB from the list's current start, so the list had been reallocated while it was walked. Our detection hook (`0x743170`) is a different function, not on the stack, and changes no list.
      A second earlier, 63 soldiers had been switched on together, stacked on two spots, all entering combat. Our code had only claimed them, reported their equipment and relayed their dialogue. The second writer is not in the dumps.
      Read as the engine under load. A second sighting with the soldiers spread out and started in groups would call for a closer look.
- [!] **The script engine on a stale form, at more places than the guard covered** (2026-11-02, two players in one party on the release v1.9.7, 7 of their 9 crashes that night, and once on the rig). Build v1.9.7-18-g8d7f58c4-dirty.d91bd5b, rig: starts, plays and loads with no crash, no `FormGuard:` line in that run, so the guard itself was not exercised; not seen in play.
      `FormGuard` is in no release yet, so those players had none. It covered one check, `+0x93CE11`, and only a form's own handle, so `+0x93F0F9` (with its landing at `+0x15DDEC1`) and `+0x93DB7E` went through, the first on the rig with the guard on.
      `+0x93F0F9` and `+0x93DB7E` are the same read: a form looked up from a handle, then a call through its vtable. An alias's or active effect's handle names its form in the same low 32 bits.
      Change: every slot of the handle policy that looks a form up now gets the invalid handle in place of one whose form is stale, for all handle kinds.
      The stale form in one crash, `FF000E3E`, is a temporary base of a levelled NPC, the game's own, and `RecentDeletes: nothing has been deleted this session`. Three restarts in a row died within 49 s of connecting, so a stale handle may sit in that host's save. Not read. No way to make this crash on demand, so no red run.
      To see: `FormGuard:` lines with no crash at these addresses. The guard's slots run for every script, so a mistake there shows at once at start-up.
- [ ] **A body that comes back into a game stands up until our net lays it down** (play, 2026-11-02 18:24 to 18:35 on d91bd5b, 50 to 100 soldiers, two players: "soldiers standing back up" at the end). Build 8543729, no test, not seen in play.
      18 of the 20 bodies that really stood (pelvis 57 or more; the first low readings of 50 to 56 are probably bodies on a slope) were already dead when a `CharacterSpawnRequest` brought them back, with `Disabled actor is being re-enabled`. The spawn's last step acts only when the reference's state differs from the owner's, so a body dead in both plays nothing, and its new 3D stands. `BodyStand:` caught them 5 to 55 s later: it needs two looks 2 s apart, and at most 24 bodies a pass.
      The owner saw them at the end of the last battle, Imperials only, and the logs agree: from 18:34 every body that stood through a spawn was an Imperial (host 7 back, player 2 6 stood, no Stormcloak). Why that side's bodies left the range is not read. `BodyStand: ... had 1 health; taken` (about 270 at player 2) is the copy's health floor, not the cause.
      The change: that step lays such a body down at once, logging `came back as a body`. It needs a red run: kill a soldier, take the other player out of range and back.
      Open: what disables these bodies (`Actor removed`, then a disabled reference at the next spawn). FE00E8F3 and FE00E8FB stood at the host with no spawn, on their owner's game, and three tries did not move them.
- [!] **Three follower fixes after the overhaul** (review of 8d7f58c4). Build d91bd5b. `live-follower-save` ok, 7 checks, red not run; the rest below has no test.
      Release checks on d91bd5b, 2026-11-02: `live-load`, `live-death`, `live-teleport-far`, `live-copy-removed`, `live-revive` ok, 14 bot pairs passed. No `FormGuard:` line in any of them. Rig only: the overhaul has not been played.
      A hidden follower stays marked as hidden until her disable has landed, so a save in between writes her enabled, and the wait is 60 s, not 5. A load clears the mark, so a reference a loaded save has disabled is no longer enabled when its ghost is released.
      The runner takes tasks one at a time instead of `Drain`, which held its lock while they ran: Disconnect from the menu with a follower copy standing would have frozen the game, as a switch-on did on the leader. The rig's F6 takes another path, so only the menu shows it.
      To see: a follower missing after leaving, or a quest NPC appearing after a load while connected. Not fixed: `HideGhost` and the ghost checks have no loading-screen check, and a follower told to wait is only checked once for nobody near her.
- [!] **Calls for other plugins: leader check, switch an actor, revive an actor** (owner's yes on 2026-11-01, an exception to bugs only). Build 84d6682, no protocol change, client and server. Rig on 11f01df; 84d6682 changes nothing here but the revive.
      `urSovngarde_IsConnected`, `IsPartyLeader`, `SwitchActor`, `ReviveActor` in `Code/client/PluginCalls.cpp`, found with GetProcAddress on the main module: red on 355ef2d, found and right on 11f01df.
      Switch off, alive and dead: off in both games in 0.3 s, stayed off 45 s. Back on in 0.4 s, one soldier, 1 unit apart. The first on froze the leader's game, as draining the task queue held its lock: calls now run outside it.
      Revive of a body the member owns: alive in both, but 310 units apart 6 s later, and health 98 against 106. Not read further.
      A game that rejoins after a switch: not proven, the one run used a soldier already dead and off.
      `urSovngarde_ShowMeter` and `HideMeter`: a bar near the top of the view in an overlay of its own, the attackers' colour filling from the left, for a plugin's capture bar.
      `urSovngarde_SetPartyValue` (the leader only) and `GetPartyValue`: one number per key, kept by the server for the party and sent to its members, players who join later included. A protocol change.
      Build v1.9.7-21-g7e8251ef-dirty.572b143, a protocol change, client and server on FUS and MGO, not seen in a headset. Unit tests for the meter's picture and both messages pass; red not run, the calls did not exist before.
      Rig, two real games: the meter shown, changed in place and hidden in the first game, a fill that is not a number refused. The leader's value read by the member, then its change. A value set while the member was dropped reached it through the join.
      A meter not shown again for 10 s fades out. A fill update takes 0.07 ms to draw; a new label takes about 2.4 ms. The second game's meter cannot show on the rig (one SteamVR for both).
      `urSovngarde_GetBlowAttacker`: when another player's blow is applied as a change to an actor this game runs, the game's health damage call names no attacker; from inside that call, this answers that player's copy here.
      With it, a plugin hooking that call can give back a defender's blow on its own side's gate.
      Health given back after a blow is now sent at the next broadcast even when unchanged. Before, the server added every blow and was never corrected, so a hand-off or a joining player took a weakened actor. Copies still take a value only 25 or more away from theirs.
      Build v1.9.7-23-gd930658c-dirty.6bf0e6b, client only, FUS and MGO; unit tests pass; a reviewer's queued-small-blow finding fixed in it. No rig run of ours.
      Before it, on the rig: the owner held the hidden enemy at 1818.7 and a member's copy sat 59 lower. The check is a siege rig battle: a `sent unchanged` line, then the copy back at the owner's number.
      Left as they are: right after a hand-off, a late change from the previous owner's own NPC names that player as the striker. A blow given back still starts that actor's fight with the player who struck.
      `urSovngarde_SetPartyHour` (the leader, or a player in no party) and `GetPartyHourAnswer`: the shared clock moved forward to an hour, through the same server grant as a wait, so bEnableSleep on a private server and nobody of the party in combat.
      Build v1.9.7-23-gd930658c-dirty.070635b, client only, FUS and MGO; unit tests pass; a reviewer's three findings fixed in it.
      Rig, two real games: the leader moved 15:03 to 21:03, both games there within a second, a quarter day passed in both, the member's controls never locked; a member, an hour past 24 and the hour it is already: nothing moved. Not seen in play.
      The first try was refused: both players were in combat at the fort with no target. The server answers 2, 3, 6 were not run. MGO's server does not set bEnableSleep, so there it is refused.
      Left as it is: in the flat build only, a menu opened and closed within a round trip leaves a stale grant that a plugin's ask then takes.
      `urSovngarde_IsPlayerCopy`: true only for another player's live copy here; a copy left from that player's earlier connection, switched off until freed, answers false.
      The siege mod's own test (any temporary actor not owned here) also took that old copy and other games' humanoid NPC copies for players.
      Build v1.9.8-3-ge54d2693-dirty.e2e5284. Rig, two real games on v1.9.8-4-g2200fbc6 (`two-games-switch.py sides`): true for the second player's copy, false for an NPC and for the game's own player.
- [!] **A temporary actor its game deleted lived on in another game** (the siege's dragon, 11-06 17:55: deleted by the leader's plugin, given to player 2's game by the server, then back as a copy).
      The client let any local temporary actor go as if its cell had unloaded. Now one marked deleted while its cell is loaded is released as `Deleted`, and the server removes it from every game; a placed actor is still only let go.
      One the game has already freed is still handed on: the game also frees the temporary actors of a cell left by a door or a teleport, before the client notices, and a player still there should keep them.
      Protocol change: both players and the server need it. Build v1.9.8-3-ge54d2693-dirty.e2e5284 (client), server since 2128844, FUS and MGO.
      Not rig-tested: an actor nothing holds is freed the moment it is marked (Papyrus `Delete` and the console's `markfordelete` alike), so the rig cannot make the held, marked state the dragon was in.
      An interim build that took a freed actor as deleted was green in the rig, and was withdrawn after review for the door case.
      To see: a siege battle's end leaves no dragon in player 2's game, the leader's log has `Deleted: local actor ... released as deleted`, and the next battle has one dragon.
- [ ] **PLANCK crashes at the main menu since v1.9.8, for players on a Quest 3 or 3S through Steam Link** (hub `planck-right-hand-at-load`; two machines, 11-05 and 11-06).
      `activeragdoll.dll+0x24D04` reads through a null pointer next to the player's `RightWandNode`, called from HIGGS's hook of the player's update in the AI job, 0.3 to 1 s after the main menu's scene loads (the "VR Playroom", player "Prisoner").
      Same PCs and headsets on v1.9.7: 7 of 8 and 4 of 5 starts got into the game; on v1.9.8, 2 of 12 and 3 of 15. A Quest 3 through Steam Link on v1.9.6 and v1.9.7 never hit it (26 starts).
      Not found in v1.9.7..v1.9.8: nothing new touches hands, HIGGS or PLANCK at the main menu, and the client logs the same lines in the same order on both. The moved `Main::Update` hook (CBPC fix) is the only change there that runs every frame; HIGGS does not hook that call.
      Both crashing machines run PLANCK 0.8.2: the crashing read sits at +0x24D04 only in 0.8.2 (0.8.1 has it at +0x24C04, the Lifetime-and-Collision fix DLL at +0x25224).
      The read is the same in all three: the first-person right hand from the player's node table, unchecked.
      Machines seen on 0.8.1 or the fix DLL never crashed there. Both crashing machines start straight into the VR Playroom: a start-up console command, no Main Menu.
      Rig, v1.9.8-4 with PLANCK 0.8.2, started into that room the same way (`headless.ps1 up -NoConnect -MainMenuCommand 'cow VRPlayroom 29 -5'`): "Prisoner" spawned, no fault.
      So the missing piece is Steam Link with a Quest 3 or 3S; the rig's controllers are virtual and there from the start.
      Next: a Steam Link start on a Quest, straight into the playroom with PLANCK 0.8.2, on v1.9.8 and then on v1.9.7.
- [!] **8 GB more at every load, freezes and wrong colours: the siege plugin queued SKSE tasks 20 times a second through loads** (both players, 11-07; KNOWN_ISSUES has the details).
      Seen: textures in the wrong colours at load, loads up to 5 min 40 s, slow play, and freezes at the first load when the game passed the PC's 31 GB of RAM.
      Cause found by a rig A/B and the siege session's bisect: its glide thread (since 11-06 20:08) queued a task 20 times a second at all times; empty tasks did the same. This client plays no part.
      Fixed in the siege plugin (8eb27e9, deployed to FUS 15:19): the first `Health:` after a load on the owner's save is 8.7 GB, against 18.4 before, as on 11-05.
      To see in play: the colours right at load, `Health:` at the first load near 20 GB, no `FreezeWatchdog:` there. Seen needs the new siege DLL too.
- [ ] **A crash moving a dead follower's items to the dead-body cleanup coffin** (the owner's game, 11-07 19:53 and 11-08 12:14, each at the second siege battle).
      Both times Lydia died, and 9 to 12 s later `ReferenceEnable: A2C94 ... switched off here, as the leader's game has it`: a plugin in the leader's game switched her off with `urSovngarde_SwitchActor`, which is meant for placed actors.
      The party-hour jump then made the game's cleanup script empty the switched-off body into its coffin (reference 172B6, base F6891), and the game crashed on a freed extra-data list in the coffin's entry.
      Rig, the owner's 11-07 19:32 autosave (2026-11-08): kill Lydia, `Disable`, `RemoveAllItems` into 172B6 crashes with the play stack, with this client loaded and with the plain game; the same move without `Disable` works.
      So the crash is the game's own; the switch-off of a dead follower sets it up. A body without duplicate items was not tried: she holds 13 Sentinel cloaks and 6 Sentinel boots, how they came is not known.
      An earlier rig A/B passed F6891, the base, as the destination, which deletes the items instead of moving them; its "no crash" says nothing. With this client connected 5 kinds of item were back on her after it.
      The siege's call was its "set the fort's own people aside" step: its follower test is the game's teammate flag, which the game clears at death. The siege now skips the dead (deployed 2026-11-08 12:55, not seen in play).
      Guard here too (uncommitted, built v1.9.8-11-g4f4002ca-dirty.03a7b50, FUS and MGO): a leader's switch-off leaves on a dead actor whose reference a master file holds, logging `ReferenceEnable: dead X (base Y) left on here`; the leader's game does not send it either (`not switched off for the party`). A plugin's own placed or spawned dead still go, as the siege needs.
      Rig `live-dead-switch`: RED on the build before (Lydia switched off), GREEN on 03a7b50 (left on); her body then emptied into the coffin without a crash. Not seen in play.
      To see: no crash at a second battle after a follower died; no `ReferenceEnable: ... switched off` line for a follower.
- [ ] **A crash when a revived soldier's copy equips arrows** (player 2's game, 2026-11-08 14:29:45, first battle at Sungard, a Stormcloak reinforcement wave; both on 03a7b50).
      The copy FE09F8A8 came back as a body in that game and had its items set to its owner's; 70 ms later its owner revived it, and 3 s later its arrow equip crashed the game.
      The crash is in Enchantment Art Extender VR's equip handler (1.0.2.1, also in the owner's profile): on every equip of a living actor it reads the right hand's entry in the actor's AI process (+0x260) and that entry's extra data, and that extra data was freed.
      How that hand came to name freed extra data is not known. The copy had been switched on 106 ms before its items were set, and it was dead then; the arrows' extra data held 40 against a count of 26.
      Rig, the owner's 11-07 19:32 autosave, three ways (`live-dead-copy-hands`): Lydia dead in this game, made the bot's copy, her items set to the bot's (none; a worn bow and arrows; the same right after she was switched on), revived, then an arrow equip.
      Each time the game kept her hands right: it emptied them as her items went, filled them from the items set, and no hand named extra data her items did not hold. No crash, on 03a7b50 either.
      So the cause is open, and what is built is a guard at the place of the crash, not a fix of a reproduced fault.
      Guard (in v1.9.81): a hand whose extra data is not its own item's (freed, or since given to another item) is checked around each removal this client makes, before a revival, and before each equip or hand item from a copy's owner.
      Rig: `live-dead-copy-hands` passes (game alive, no `HandWatch:` line); it is a path check, not RED/GREEN, as the fault did not come.
      In play the guard crashed the game itself (2026-11-11, a public pair, SkyrimVR.exe+0x6896F7 twice in 4 minutes): Aela held a torch, passed to the other player at a join, her items were set to its items, and the guard let go of her left hand 16 and 45 ms before each crash.
      Letting go meant leaving the hand holding its item with no extra data list, and the game reads the list of a hand that holds a light with no null check. Across all reports, 26 let-gos of a right hand (weapons) crashed nothing; the only 2 of a left hand, both this torch, crashed.
      Change (uncommitted, v1.9.81-3-g546b5f93-dirty.75fadbc, client and server on FUS and MGO, client only in effect, same protocol): the hand is emptied, as an unequip leaves it.
      A player copy's hand emptied so is taken out of what this side put in its hands, and put there again at the next hand items from its owner while the owner still holds it; an item already in that hand is not equipped again.
      The guard logs `HandWatch: X (dead: ...) pointed its <hand> (item) at extra data ...; the hand is emptied`.
      Rig, with a temporary build that made the guard act on a copy's torch hand at an equip from its owner: RED with the old let-go (crash 1 ms after, at +0x1F0EEB, another game read of that list); GREEN with the emptied hand (game alive, the next equip went through).
      `live-torch-hand` (Lydia holding a torch at night becomes the bot's copy, her torch taken): in three runs the game emptied her hand itself; in one, on 75fadbc, it did not and the guard emptied it, the play fault, and the game lived on.
      `live-equip` and `live-equip-twice` pass on 75fadbc. The re-equip of an emptied player copy's hand has not run on the rig. How such a hand comes to name extra data its item no longer holds is still not known. Not seen in play.
      To see: no crash when NPCs holding torches pass to another player, at a join or as the party leader comes near. A `HandWatch:` line now says the hand was emptied; read which step logged it.
- [ ] **The siege's battle page keeps the battle going** (`urSS_Battle`, the siege plugin's page with the soldiers left and End battle as a 3 s hold).
      Opened unpaused by its plugin it took no laser clicks on the two-game rig (11-04); it opens pausing, as the setup page does, which takes them.
      Now on the unpaused menu list (VR only, connected only): the pause is cleared as it opens, and while it is open the player's controls and melee swings are held. Alone, it pauses.
      Build v1.9.8-8-g65b14ee7-dirty.da89bed (client, FUS and MGO), TPTests 114 pass; the siege session tests it on the two-game rig. Not yet seen in play.
      To see: in a battle with the page open, the fight goes on, the laser clicks the page, and a swing while pointing does not hit. In the log: `Menu opened while connected: urSS_Battle (runs unpaused)`.
- [ ] **An essential NPC copy stays down after its owner's NPC got up** (Lydia, player 2's game, 11-06 18:00-18:15).
      In the owner's game the battle kept dropping her into bleedout at -30 health. That health was copied onto her copy, and player 2's game put the copy into its own bleedout (life state 8, get-up queued, knock state 5).
      Copies run no AI, so the copy never gets up by itself. When the owner's Lydia got up, `bleedOutStop` was refused on the copy as an action and as a graph event. She lay or slid from 18:03.
      From 18:11 the copy counted as dead or dying, so `moveStart` and `TurnRight` were refused too and she stood still. Not fixed. The fallback (`IdleForceDefaultState`) runs for player copies only.
      Direction: an essential copy should not enter the game's own bleedout from a copied health, or should be taken out of it when its owner's life state is alive. `Tools/VR/bleedout-slide.py` holds an essential NPC in bleedout, a start for a red test.
- [x] **The two-game rig's second game closed itself at start** (11-05 09:09, 11-06 18:53): the `two-games.on` marker was missing, so the game's own check found the first game's window (`FindWindowA` in its start, VR 0x5B5969) and quit.
      Not a fault: the marker had been removed after the last run, as the guide says. The `setup` step now creates it when missing.
      With it, the second game starts, but its first load can stall: at 11-06 20:27 for over 15 min with no CPU (taken down), at 21:08 for 3 min before it went on; at 21:27 not at all.
      Both games write the same SKSE log files, so the first game's plugin logs are cut when the second starts.
- [ ] **A siege side does not show on player 2's copy in the leader's game** (11-06 17:51 and 17:56). The copy took its owner's factions 2 and 5 s before the leader's check (20 factions, the side one in them by the 19 and 20 counts), yet `IsInFaction` read no side.
      Rig, two real games on v1.9.8-4-g2200fbc6 (`two-games-switch.py sides`, 11-06 21:12): the second player joined, left and joined `urSS_SideImperial`, and the copy followed each time within 3 to 4 s at rank 0, -2, 0.
      So the faction travels; the play warnings stay unexplained. If they come back, log the copy's rank for each side at the leader's check.
- [ ] **A crash in the game's own AI job in big siege battles** (`SkyrimVR.exe+0x743D74`, a job thread, `Job_Non_render_safe_AI` through ActorKnowledge; 11-05 18:27 and 11-06 21:16, the leader's game).
      A function that removes entries from a list of shared pointers was given a garbage index (0xFB69C82C), as when the list changes under it. No frame of ours on the stack.
      The 11-06 one came in the rig, two games on one PC at 2 fps (frames of 469 ms on average). Not known whether our copies play a part; a full dump from a crash in real play would show whose list it was.
- [!] **A follower with no server id after a hand-over, so the other player loses her** (10-31 14:43, and 11-01 12:42: Lydia fighting for player 2 alone). Fixed in build v1.9.7-12-g629aaced-dirty.090ea03, protocol change, not seen in play.
      Cause, both times: her owner's game dropped her (`Transferring ownership of local actor`), nobody was in range of her last place, so the server removed her (`Removing actor ... no eligible owner remains`). The other game still ran her, with no server id: `server id not found for form id A2C94`.
      Fix: `NotifyRemoveCharacter` says when the server no longer has a character (`Gone`), and a game where that reference stands announces it again and runs it.
      Rig: `live-gone-here` red on 629aaced, ok on 090ea03. A fresh reviewer's five findings on it are in the same build.
      In play, check: leave a city through a gate with a follower behind you. Both players must see her, and no `server id not found` line. A `Character ... is gone from the server while` line is the fix acting.
- [!] **A levelled NPC of another race became a stand-in that lost its name** (11-01 13:01, Nilheim: Guard for player 2, Bandit Marauder for player 1). Fixed in build v1.9.7-12-g629aaced-dirty.090ea03, not seen in play.
      Nilheim's bandits take their name from their own base and their looks from a levelled list. An Orc roll against a Nord roll counted as another kind of creature, so a stand-in of the bare pick replaced the reference.
      Fix: two people are never foreign; the reference takes the owner's pick and keeps its name.
      Rig: `live-person-race` ok on 090ea03, the game's Orc conformed to the bot's Nord and reads Guard. No red run; the red is the session's log (`Ghost: reference 72A8D rolled as Bandit Plunderer`).
      In play, check: no `Ghost: reference` line for people, and both players read the same name at Nilheim.
- [!] **The bridge east of Nilheim differs in player 1's save** (11-01 13:06). Not a sync bug. See KNOWN_ISSUES "A save can give a reference its vanilla base at a plugin's position".
      Repaired on player 1's install with a one-line swap file. Rig on save 68: the right bridge, deck at z 10120. Not seen in play. In play, check: both players walk the same bridge there.
- [!] **A party member's waypoint could crash the other players** (from upstream cc4a108a). Fixed in build v1.9.7-14-g9f8803db-dirty.142d7a0, not seen in play.
      The game's `SetWaypoint` takes a world space or a cell and reads its form type with no null check (`+0x6C75B7`). Our receiver turned a cell, or a form this game lacks, into a null.
      Rig: `live-waypoint-cell` crashed 090ea03 at `+0x6C75B7` on a form the game lacks, and is ok on 142d7a0 with a form it lacks and an interior cell. That red run did not show the interior cell, as the bot may not yet have been in the party.
- [!] **A nearby player with a shout from a mod you lack could crash you in combat** (from upstream 43bfd173). Fixed in build 142d7a0, not seen in play. No rig test: it takes a player whose list holds a shout the other game lacks.
      Spell list entries that do not resolve are removed after a copy loads, and each logs `Failed to resolve form`. Upstream's crash was on SE; VR's combat code was not read.
- [!] **A quest that fails to start here ended it for the party** (from upstream 83d8080f). Fixed in build 142d7a0, not seen in play. `live-quest-hold` ok, so normal starts still go out; no test makes a start fail.
- [!] **A death at the Dimhollow Crypt dragon wall put the player in the locked entrance** (from upstream 3e68a605). Respawn override added in build 142d7a0, not tested.
- [!] **DynDOLOD's references were switched in the other player's game** Each player generates a different `DynDOLOD.esp`, so the same id can name another object. They stay each game's own, in build 142d7a0, not seen in play.
      Rig: the game's start sent 67 such switches at 14:15 on 090ea03, and none on 142d7a0. `live-ref-enable-leader` ok.
- [ ] **Findings from reading the game's code** (2026-10-31, checked against the game's instructions and public layouts). None of this ran on a real game, so every effect on play is inferred.
      Check each against our code before acting: two claims did not hold when that was done.
      - Check first: `BSAnimationGraphManager` has `lock` at 0xA0 and the graph index at 0xB0, AE offsets; a public VR layout gives 0x98 and 0xA8. If so, the client takes the wrong lock each time it touches a copy's graph.
        Reading one game function that takes the lock settles it.
      - Confirmed: `HookActorProcess` (id 37356, VR 0x1406226a0) sits on a small alarm check. It never sees an actor and skips nothing, so copies run their own AI, against "Remote AI suppression" in KNOWN_ISSUES.
        It may be what writes a copy's graph each frame (see "An NPC taken over while it walks"). Moving it to 0x1405e0e20 stops the movement step, not the AI, and needs a real game. A decision, not a quick fix.
        Seen in play 2026-11-08 21:35 to 21:37 at Fort Greymoor: Stormcloak copies near player 2, who is a Stormcloak in that save, went red on the compass, kept combat music on and flickered, without attacking, for about a minute.
        That fits copies running their own AI against the owner's replay. What started the fight was not logged; the only candidate is the host's PvP hit on player 2 at 21:33:34. Left as it is for now, by player 2's choice.
      - One check only: `ApplyAnimationVariables` (id 39004) reads a third argument in r8b, and the live `ForceAction` path passes none, so a copy may get the wrong variables after a forced action.
      - Confirmed, small: `InterruptCast` (SE 33630) is called without its bool, so a copy's magicka refund depends on a leftover register.
      - Suspect: `CalculateRealDamage` applies the multiplier the game skips for a fall, so the health change sent for fall damage is too high. A hit by a player never takes that path.
      - Suspect: `fYAngle` at launch data +0x50 is a pointer in a public layout, and we send its low half as an angle (`Projectile.cpp`, `CombatService.cpp`). It is on every arrow and spell replay.
      - Suspect, not read: `UnEquipShout` (a shout may stay on a copy), subtitles (text may be a `BSString`, so garbage or a crash), `StatsMenu` +0xC0 (skills menu unpaused), `AddSkillExperience`'s arguments, `MoveTo`'s handle.
      - Wrong, no effect today: `MenuControls` flags sit at +0x8B on VR; override 21600 is the `PlaySound` console handler; `DropObject` (id 40454) is PlayerCharacter's, which would corrupt memory on a copy,
        but the server never forwards a drop, so nothing reaches it.
      - Checked and right: `UI+0x1C9` and the cull's `+0x50`, as the map fix reads them. A replayed cast's slot reads only the six arguments we pass.

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
      Committed in f0eeccfc: a spell's or staff's damage runs on the copy in the caster's game as on an NPC, with the caster's perks, its burn and
      the multiplier for a hit on a player (see the entry below). Each hit goes as a health change with
      no tick, so the defender's rule does not weigh it: a blade cannot block a spell. The defender's replayed effect is only seen, its damage not
      taken, and it is not sent back. What a hand or a weapon delivers (a weapon's strike enchantment, a touch spell, a combat-hit spell) stays as on v1.9.7, weighed by the
      defender's rule, with no burn. A sword blow after a spell is no longer taken for the spell.
      Limits: a dodge on the defender's screen does not help, as with a sword. To look at in play: a ward in the defender's game does not stop a spell; only the copy's ward in
      the caster's game can, and whether it holds is not known. Absorb spells do nothing to a player, as on v1.9.7. A third player does not see the burn on the defender. A
      non-hostile damaging effect sends nothing. A burn on the defender from a replay that landed does its last ticks at the game's own multiplier if its caster's copy goes,
      and as well as the report if the copy is rebuilt mid-burn. A PROTOCOL CHANGE (a spell's hit has no tick): both players and the server update together.
      Scripts changed to this: `live-pvp-projectile`, `live-pvp-concentration`, `live-pvp-spell-perks`, `live-pvp-spell-elements`; they check one side only.
      Still open for play: a ward held against a spell, and Chain Lightning.
- [!] **PvP: where a held spell is drawn, and one damage rule** (session 2026-10-31 14:10 to 15:05: "a giant offset", "sometimes 20cm off, other time like whole meters"). Commit 4b2f8e0b, in build
      e6c7900c. Rig and two real games on one PC, not seen in play. A PROTOCOL CHANGE (`VRPose`): both players and the server update together.
      Why it was off: a held spell only sends "cast", and the other game casts it again on the copy. There the game aims a copy's flame or beam by its body, each frame. The caster's real
      one follows the hand, turned by the spell mods. In that session the caster's game landed 18 of 21 and 8 of 12 presses, and the replay touched the target on 6 and 2.
      The defender's rule was not the cause: spells have not gone through it since f0eeccfc.
      The fix: each frame the caster's game reads the angles the game has just given its spell from the hand (0x14076875B, 0x14075E987) and sends them with the pose (`VRPose::SpellAim`). The copy's
      flame and beam take them in the same two updates (four call swaps). Launch angles do not do: with a target near they gave its heading, 0.0 off, while the real beam was 112 degrees away.
      Damage: every hit of a player on a player, by sword, arrow or spell, uses the multiplier for a hit on a player, as an NPC's does (3 at our difficulty instead of 0.6, so 5 times what it was).
      An arrow: this game still decides that it hit, and its shooter's game now says how hard, as the copy has none of its owner's skill or perks. The highest report of one arrow counts, as an enchanted one reports twice.
      Two real games (helpers of `two-games-bugs.py`, the hand steered by `SpellAimDiag`): Flames on the target, sent 90, drawn 90, 7 of 7 seconds in both games, 24 a second off the target's real health.
      Hand turned away, body still facing: drawn with the hand, no touch, where a body-aimed copy would touch. Sparks in both hands: sent and drawn 89.8 and 13.4, real hits of 60 a second.
      On 3a8e1a1, with launch angles, the one flame that hit touched nothing in the other game in 6 s.
      Rig: `live-pvp-beam-aim` (a control without the aim, then Flames reaching the player and Sparks taking the angle; the bot's aim is level, so its Sparks passes over), `live-pvp-hand-aim` (Flames from the real hand, one step to 0.0 off, then real hits),
      `live-pvp-arrow-number` (150, 0 and 100 exactly), `live-pvp-spell-elements`, `live-pvp-spell-perks` (180 sent). TPTests 111 ok. A sword's own number was not swung in the rig: it is the same line as a spell's.
      Limits: one aim per player, so two hands aimed apart share it (6 of 85 casts in that session had two hands going). A thin beam is drawn from the copy's hand, 10 to 30 units from the real one, so it
      can pass beside a body at the right angle: Sparks touched nothing there, at 0.5 degrees off, while the real one hit. The first moment of a cast goes by the body, before the aim arrives; that effect is not taken.
      The second game of the two-game rig does not steer, so a real hit ran one way only; the other way, sent and drawn matched. An arrow's number has no shield in it (KNOWN_ISSUES, with three rarer arrow cases).
- [ ] **A revived player's copy still lying down, and below the floor while down** (session 2026-10-30, once). Player 2's copy sat below the floor
      while down (18:26:44, 10 s, its hands at foot level). At 18:26:54 `bleedOutStop` was refused and the fix fired (`not taken; IdleForceDefaultState
      taken`), yet 5 s later it still lay: `Revive: player 2 ... 5 s after the revive`, copy head 27.5 (about 105 standing, under 30 lying).
      The other four revives logged read 101.5 to 114.7, and the owner has seen nothing wrong with revives, so this is the log's word alone.
      Its owner's knock state 5 was copied onto a copy with no ragdoll (`keptMask` in AnimationSystem). `live-revive-copy` does not reproduce it.
      Each 5 s a copy with a VR pose logs `VRBodySync: actor X body -- reference z, 3D root z, head above the root, knock state`.
- [!] **MGO crashed at the main menu from v1.9.6-23 on, fix waiting on a play session** (player 2's PC, profile `MGO testing`; rig and one real start of release v1.9.7).
      Cause: `cbp.dll` (CBPC) detours `Main::Update` when the main menu opens, follows a jump at the function's start, and copies the bytes it finds there without relocating them. Since `0dfd9747` our hook sat on that function, so the copy held `HookMainLoop`'s first call, which landed in the heap. In 12 of 12 crashes the slot at `rsp` minus `rip` is that call's displacement.
      Fix: `SkyrimVM64.cpp` swaps the main loop's only call to `Main::Update` (`+0x5B6E8F`) and leaves the function alone. Rig on v1.9.7-11-g83be966c-dirty.13cdf81: past the main menu, save loaded, connected, `live-load` 9 of 9; the live chain read from memory is `hdtsmp64.dll`, `HookMainLoop`, then `cbp.dll` on `Main::Update`.
      Not run: the rest of the MGO scripts (doors, gameplay), which the rig was stopped before, and any FUS start with this build. In play, check on FUS and MGO that a save loads and that a granted sleep's OK button works.
      Build v1.9.7-15-g6826e679-dirty.355ef2d, client and server, deployed to FUS and MGO on 2026-11-01. Unit tests pass, and the rig ran on it on FUS only. Its protocol changed, so both players and the server need it together.
- [ ] **Player 2's game piles up threads, and they stand in DynDOLOD.DLL** Highest counts: 4959 on 2026-10-24, 2987 on 10-25, 998 on 10-28, 802 on
      10-30, 1195 on 10-31 (205 at 08:06, 1195 at 08:16). `ThreadOwners:` at 08:14 read DynDOLOD.DLL in 23 of 24. Player 1's game stays near 170 to 270,
      on v3.0.43. Cause found 2026-11-01: player 2's FUS profile has Alpha-43 installed (`DynDOLOD DLL NG and Scripts`) but switched off since at least 10-17, and still runs Alpha-32 (`DynDOLOD DLL NG`), as every crash log to 10-31 00:27 shows (`DynDOLOD.DLL v3.0.32`).
      Same session, 10-31 14:10 to 15:06: player 2 went from 221 to 3195 threads, player 1 stayed at 157 to 170. `ThreadOwners:` read DynDOLOD.DLL in 22 or 23 of 24 each time. Private memory is no sign of it: 14.4 to 20.9 GB for player 2, 20.4 to 23.0 GB for player 1. To do: player 2 swaps the two in Mod Organizer; to see: `Health: ... threads` stays under about 300 next session.
- [!] **A follower left at a city gate** (the rules above, not seen in play). She no longer stands on the other player's own reference: her copy goes when she leaves their range. Moving a reference into the city's persistent cell stays out, as it stood her stuck in Windhelm's gate on 2026-10-31.
      To see: one player takes a follower through Windhelm's gate while the other stays outside. Nothing of her stays behind.
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
- [ ] **`live-map-pointer`'s second half closes nothing** (found 2026-10-31). Its last five rounds press Escape while the box is up, the box takes it, and the map stays open to
      the end of the script, so `the map closed while the box is still up` has never been tested. The first five rounds are real closes. `live-map-close` covers those paths.
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

- [ ] **Mounts** (parked 2026-10-12). The rider's `MountRequest` crosses and the other game gives the rider's copy the
      mount package, which it accepts, but the copy never gets on: copies run no AI of their own. Tried in the rig and
      reverted: `Actor::PutActorOnMountQuick` (vtable 0xAE on VR) returns false; letting the copy's AI run with its
      position free for 10 s left it beside the horse. Next if taken up: build the rider/mount `ExtraInteraction` on
      the copy, then `PutActorOnMountQuick` (real reverse work on VR).
      Checked 2026-11-05: cmpayc's fork has upstream's mount code unchanged, the same as ours; nothing to borrow.
      The siege mod's cavalry needs it, and ships switched off until then (NPC riders the host seats with
      `InitiateMountPackage`). What it takes besides seating the copy:
      - A mount asked before rider and horse have server ids is dropped without a log line (`OnMountEvent`); hold it until both exist.
      - `NotifyMount` goes once, to the players in range: the server keeps no rider-to-horse state, so a game that loads the
        pair later, or joins, sees the rider on foot. The pair belongs in the actor's spawn data (protocol change).
      - No dismount message or hook exists (a knockdown's get-up, `Dismount`, the horse's death).
      - Hand-offs move each actor alone (`TransferToNextOwner`, the out-of-range sweep): with three players a rider and its
        horse can split. A mounted pair should move together.
      - A seated copy must not be placed at the owner's reported position, or interpolation fights the saddle.
      - Mounted combat on a copy has never been tested.
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
