# Skyrim Together VR: TODO

What is still to do, and nothing else. The full story -- session reports, investigations, the evidence for every fix
-- is in git and in `VR_HISTORY.md` (everything this file held up to 2026-10-07, word for word); `KNOWN_ISSUES.md` says
what is known to be broken and why. Cleaned on 2026-10-13: proven and closed entries removed (git keeps them), the
rig-tested work folded into the checklist below. Cleaned on 2026-10-31: finished work and the stories of open items moved to
`VR_HISTORY.md`, "Moved from the TODO, 2026-10-31", word for word.

`[ ]` open, `[!]` waiting on a test or action. An item play confirms moves to `VR_HISTORY.md`, word for word. Open bugs come first
under "Open work". Reordered on 2026-11-14: each section first holds what was reported or changed from
2026-11-02, then what has not been reported since 2026-11-01; owner actions, design questions and rig tools come last.

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
- **NPC sync, step 2: hand-offs follow what each game has loaded (server only, uncommitted).** Nothing to do in play: play near each other and apart as usual, NPC-heavy places best.
  The 11-08 session had 348 NPCs removed while the other game still had them, and most hand-offs "out of its range" (1,639 in the server logs since 11-02) were of an NPC exactly three cells from its owner, still loaded and run there.
  Cause: the server ranged each player from the cell they stood in. The game's loaded cells are centred where its last grid shift said, which trails the player by up to a cell, so the server's range was a cell off.
  The game also keeps running some actors a cell or two beyond its loaded cells (critters on the rig), which no grid rule can know.
  Now: walking into the next cell keeps the centre of the last grid shift; an owner whose presence report still has an NPC keeps it; a new owner is first a game that reports the NPC loaded, then one in range; a game's release drops its report of that NPC.
  A game just given an NPC keeps it 6 s, until its reports can say it runs it. Voice, doors and locks go from the cell the player stands in to the other game's loaded cells.
  Build: server v1.9.81-10-g09417770-dirty.e214e4b on FUS (server only, same protocol); also in 6abe2b0 below, which changes the protocol for the plugin warning.
  Rig, `live-range-centre` (the player steps three cells north, each time just past the edge, so the game's centre stays a cell behind): on the old server the bot's bear at the start was never sent and 19 NPCs the game still ran were handed to the bot.
  With the centre fix alone, 3 critters the game kept beyond its loaded cells were still handed away; with both, every check passes and the census counts 0 removals while another game had them.
  On e214e4b `live-range-centre`, `live-door-range` and `live-lock-range` pass, and the bot pairs `cellwalk` (now walking with the game's trailing centre, `gridshift lag`), `rangeback`, `range` and `ownerchurn`.
  Not seen in play. In the server log, `Handoff: ... out of its range and not running it` should be rare and `Presence: removing actor` far below the 11-08 count.
- **NPC sync: a warning when two players' NPC mods differ (a protocol change: every player and the server need the same new build, uncommitted).** Nothing to do but connect.
  Each game lists the plugins that add or edit NPCs or levelled NPC lists (38 on FUS) and sends it once connected; the server compares each pair of players.
  Both are told in chat and on a banner ("Not the same NPC mods as ..."), with the plugins only one of them has; a pair is told once per server run for the same difference.
  The owner's and player 2's games differed on 11-08 only by plugins that add no NPCs (water, collision, terrain), so they should see no warning.
  In both logs of the 11-08 session, all 644 NPCs one game ran and the other showed had the same name in both games.
  An NPC seen differently came after the server removed it while the other game still had it: that game's own reference took over with its own roll (`is a normal local creature again`, 34 times); step 2 above is the fix for that.
  A game's list goes once per connection; a list with more than 4096 plugins drops the message; names are read under a fault guard.
  Build v1.9.81-10-g09417770-dirty.6abe2b0, client and server, on FUS and MGO; TPTests 122 pass (the new messages' round trip).
  Rig on 6abe2b0, `live-npc-plugins` and `live-range-centre` green. `live-npc-plugins`: the game read its 38 plugins, the bot named one only it has, both were told, and a second report of the same lists told nobody. New behaviour, so no earlier build to fail on.
  Not seen in play. In the log: `NpcPlugins: N plugins add or change NPCs` at the first connection, `NpcPlugins: X has other NPC plugins` when told.
- **NPC sync, step 3: an NPC that changes hands keeps its creature, its fight and its weapon (a protocol change: every player and the server need the same new build, uncommitted).** Nothing to do in play: play near each other, fight together, and walk away from each other mid-fight.
  A game that shows an NPC on a stand-in (its own reference rolled another creature, or is not loaded there) now takes the NPC over on that copy when its owner walks away, instead of declining it.
  Before, the server then removed the NPC and that game's own, different creature came back (`is a normal local creature again`, 34 times on 11-08). A game with the real reference still goes first.
  The owner's game now tells the server whom each of its NPCs fights, in its presence report every 2 s; the next owner's game sets the NPC on the same target (`Combat: actor X ... fights Y here, as at its last owner`).
  Before, the new owner's AI started the NPC afresh: on 11-04 to 11-08, 26 of 37 NPCs gained with the weapon drawn put it away within 3 s (seven soldiers at a disconnect, a Blood Dragon mid-fight).
  A copy's weapon is no longer sheathed by force when it is already sheathed: that left it waiting for a sheathe that never played, and the game that took it over asked to sheathe over a hundred times a second, refused each time, for good (the `ActionFlood ... (Unequip)` after 500 of 2,287 gains in the logs).
  Health is right already: a bear hurt in one game was handed to the other with the health it had (210, told 210); no change made for "heals back up".
  A copy this game runs goes when the leader switches its reference off; it stayed standing before, unknown to the server, its own reference hidden.
  An NPC is not handed on fighting the character of a player who is leaving.
  Builds 70726c8 (stand-in), 33b5b65 (fight), bd757f8 (weapon), 746d82f, 10f31ee (after a review), ab8a07e (services), 3b220ce (after a full review), 51e8473 (3b220ce with the scene line fix merged in), 52d2ce2, a82e5cd, last b1908eb (committed as d0c73bc0 and bb428339, with the second scene line fix) (with the three Helgen fixes below, after a second review), client and server, on FUS and MGO; TPTests 122 pass (round trips of the two new fields).
  3b220ce: a walk two cells from the game's trailing centre no longer counts as an arrival that holds the leader's claims for 10 s; arrivals are measured from where the player stood, as before the range change (found by reading; the rig's teleports cannot make that case). The NPC plugin messages share one list format and one way to join names.
  ab8a07e is the same behaviour split into services: the server's presence and next-owner choice in `PresenceService`, the client's presence report in its own `PresenceService`, fights in `CombatService` (one rule for "set this NPC on that target"), copies and stand-ins in `CopyRegistry`, the gain stop and diagnostics in `Takeover`, and one helper each for weapon state, temporary ids and finding an entity by form id.
  Rig on ab8a07e: all eleven scripts below green again, and the pairs `range`, `rangeback`, `cellwalk`, `ownerchurn`. One run crashed in the renderer during `live-range-centre`'s cell jumps (`SkyrimVR.exe+0x1305447`, `BSShaderAccumulator::AccumulateObject` on a freed object, as the 10-31 fast-travel crash); none of our actors in it; the rerun on a fresh game passed.
  Rig on 10f31ee: `live-standin-switched-off` (RED on 746d82f: the copy stayed), `live-standin-return` (the copy a game ran is not announced again when the player comes back; the game had disposed of it), and the five below again, all green.
  Rig on 746d82f, all green: `live-standin-takeover` (taken on the copy, it follows the player with its own AI, it dies and the bot hears it), `live-handoff-fight` (a gained Anoriath fights the player), `live-handoff-fight-told` (a real game's bear set on the bot: the bot is told), `live-handoff-gear` (no stuck sheathe, no flood, no redress), `live-handoff-health`, and `live-equip`, `live-equip-twice`, `live-copy-weapon-swap`.
  Red before each fix: `live-standin-takeover` on 6abe2b0 (declined, the creature vanished), `live-handoff-fight` with no fight reported (not in combat), `live-handoff-fight-told` on 70726c8 (bot not told), `live-handoff-gear` on 33b5b65 (stuck wanting to sheathe, the flood).
  A follower such as Lydia cannot be set on the player at all: the game drops that fight at once, so the rig uses Anoriath.
  A copy taken over runs its base's AI, not its reference's: no patrol links or quest aliases; stand-ins are levelled rolls or references not loaded there, so mostly creatures and bandits.
  Not seen in play. In the log: `Hand-off of X (server id S) taken on the copy standing in for reference R`, `Combat: actor X ... fights`, `TakeoverDiag: ... weapon 0` rather than 4 at a gain, no `ActionFlood ... (Unequip)`; `GainDiag: NPC X gained N units from its last owner's newest report` measures the jump the other games see.
  Still open: "jumps to another spot" waits for `GainDiag` numbers from a session; "slides or skates" waits for evidence.
- **Party of three or four: enemy damage (client only).** Enemies now deal 120% to a player when two, three or four of the party are within 50 m; it was 135% for three and 150% for four, which with the
  server's difficulty took players from full to down in about 2 s on 2026-11-05. Their health bonus is unchanged. Say whether fights still feel fair. The line `Balance: now N of the party within 50 m` shows the numbers.
- **Party voice heard only nearby (client and menu only).** A new checkbox under "Voice chat" in the urSovngarde menu's settings: "Hear your party only within 30 m, like everyone else".
  It changes only what the player who ticks it hears; the others still hear that player's voice from any distance unless they tick it too.
  With it on, a party member is placed in the world and fades out at 30 m like any other player; one whose body is not loaded in this game is not heard.
  To see: in a party, voice on, one player ticks it and walks away. Past 30 m that player stops hearing the other; with it off, the other is heard centred, like a radio.
  In the log: `Voice: the party is heard only nearby` (or `at any distance`) when the box changes, and once at each start when it is on.
  Build v1.9.81-6-gd6c5b2c6-dirty.c96c491, client, its menu helper `TPProcess.exe` and the menu `UI` folder, on FUS only; server unchanged, same protocol. No rig test: the rig has no microphone or second speaker. Not yet seen in play.
- **An NPC talking to the other player (client only).** Player 1 talks to a quest NPC while player 2 stands beside them; player 2 says whether they hear the NPC and whether its mouth moves.
  Above all a quest scene, such as the College tour: player 2 should hear every line once. Read `this scene said nothing here` (the fix playing a line) and `this scene speaks here too` (the local scene voicing it).
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
- **An NPC handed over mid-walk (needs the build with the action trails).** Stay in a town with people walking about, and let the party
  leader go through a door or away and come back: about 10 s after the leader arrives, its game takes the NPCs over. Watch the walkers on the
  leader's screen. When one slides (body moving, legs still), note the time, the NPC's name and whose screen, and say so.
  Then read, in that player's log and the other's: `TakeoverDiag: NPC X gained, its last replays as a copy:`, `TakeoverDiag: NPC X, what this
  game's AI performed on it in the 6 s after the gain:`, `TakeoverDiag: copy X, its last replays 5 s after its first:` (each action `+` taken,
  `-` refused), and `Takeover: NPC X was moving as a copy here`.
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
- **The map, player 2 and together:** 60 closes online, no freeze. Read `Map menu: hide handled on thread N (the main thread), the map unpaused`
  at each close, and no `put on a list again` or `led back`.
- **Followers.** Each player brings a different follower, and each sees both, following their own player. Then both have the same one: only the party leader's exists, the other player has none while connected, and theirs is back with their items after they leave the session.
  Save and reload once in each case, connected and after leaving. Read `Follower:`, `Ghost: reference`, `SaveGuard:`, and that nobody's follower is missing after leaving. A granted sleep's OK button and a door with a follower are worth a look too.
- **Look at the other player:** stand in front of them while they hold their hands out, palms up, then have them run past you with
  smooth locomotion. Hands and palms are as they hold them, the legs run, and the face looks like theirs. Only a headset can tell.

After the session the logs answer the rest: `Stall:`, `Silence:`, `ThreadOwners:`, `Connection:`, `VRBodySync: actor X body --`, and
`logs/process-end.log` if a game vanishes.

### Reported or changed from 2026-11-02

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
- [!] **Join a friend from the headset** (asked for on 2026-11-13). The menu on the SteamVR dashboard has "Join a friend":
      the Steam friends playing urSovngarde, each with Join, a code field when the launcher is open, and the player's own
      code while they host with it. How a join ends also shows on the HUD, for a player who closed the dashboard.
      - Steam friends, no launcher needed: each game shows its Steam friends where it plays (rich presence `ursovngarde`):
        the server it is on, or, when it hosts, its home and virtual LAN networks and its public address (one STUN question,
        only while sharing is on). A friend's game picks the way it can reach (same house, a virtual LAN both are on, the
        public address), up to three tried in turn, without touching `connect.txt`. A menu setting turns sharing off.
        A host behind the launcher's relay shows only its code, joinable with the launcher open.
      - Codes, through the launcher: the game passes it `launcher-request.json` and reads `launcher.json`, both beside
        `connect.txt`, then connects to what the launcher wrote. The launcher's side is built (2026-11-14, not committed,
        for the launcher release after 0.4.86 if that is out): tested in the launcher alone, not yet with the game or a real code.
      Build ba120a7 (client, TPProcess.exe, menu page by hand), FUS only, not MGO. TPTests 122 (8 `[steamjoin]` cases).
      Rig: `live-launcher-join.py` (a stand-in launcher) red on 4d40ba5's old page, 8 of 8 on ba120a7;
      `live-steam-join.py` 6 of 6 on ba120a7, with no red run (no build without the feature is kept). The rig has one Steam
      account, so it joins itself: the same-house route only. Not seen in play: a friends list between two accounts, a
      public-address or virtual LAN join, and the Windows firewall for a host's server all need two PCs.
      Also in it: the menu's Disconnect and Cancel now stop the reconnect attempts too, and the client log no longer
      holds a menu event's arguments (Connect's password was in it).

### Owner actions and decisions

- [!] **The website's "What a report holds":** the launcher's SteamVR logs (done in the launcher, not committed) need the same addition once a
      launcher release carries them.
- [!] **VirusTotal key** in `%USERPROFILE%\.str-vt-key`, for `Tools\VR\scan-release.ps1` (scans a release before it is
      published). If a helper `.bat` is ever flagged: move the helpers to a separate manual-tools zip.
- [!] **Parties on a public server:** with `bAnnounceServer` off, every stranger lands in one party (shared XP, quest
      sync, teleport to party members). Decide what a public server does with parties.

### Not reported since 2026-11-01

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
      - 2026-11-21, build 1bd985f (client and server on FUS and MGO; same protocol; rig only, not seen in play): a member follows the leader's war forward only.
        Before, a member copied the leader's war exactly, so a leader whose war was behind gave back the member's won holds and un-completed and hid its finished objectives, and the member's save kept that; the lead passes to the next member whenever the leader leaves, so the player who is behind could lead after any reconnect.
        Now a member's objectives only gain flags (shown, completed, failed) and an objective already completed or failed in this game is left alone; when an objective is completed or failed in this game but not in the leader's, the leader's holds are not taken (`WarState: the leader's war is behind this game's`, once per state).
        Left as it was: a member that loads an older save while connected does not catch up until it reconnects, and a member runs a repeated stage sent by anyone.
        A finished objective can still be shown when the leader shows it; nothing else changes on it.
        Rig: `live-war-behind` (the bot leads and sends a war that is behind, then one that caught up): RED on 979d41c (hold 16770 went back to the other side, objective 101 un-completed and hidden), 9 of 9 on 92e590f, 8 of 8 on 1bd985f.
        Coarse on purpose: one objective finished here and not by the leader stops every hold from following until the leader finishes it too; a member's holds then stay its own, the safe side.
        A finer rule, per hold, needs this player's side from the war's own script data; and an objective the leader's war hides or opens again is not followed (whether the vanilla war does either is not read).
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
- [!] **An NPC taken over while it walks** (play session 2026-10-31 07:55 on c382599; again 2026-11-15 15:55, Ahlam 1A6A5 on player 1)
      - 2026-11-15/16, two real games on player 2's PC (toolbox `tools/two-games-takeover.py`, measured by legs against travel): about 1 in 5 NPCs
        gained mid-walk whose new AI walks them on slides on the new owner's screen; NPCs a game ran from the start never slid (about 25).
        The engine sends the walk start only once (`ActionMoveStart` from the movement message handler at SkyrimVR.exe+0x60B7C0), so a start
        refused while the NPC was a copy never comes again. Kinds seen in the action trails: the copy's last replay a stop; a frozen turn-to-walk
        blend (`CyclicFreeze`); a motion-driven idle; and graphs that took `moveStart` as copies yet refuse every turn and start after the gain.
        Nothing sent to the graph fixed them (`moveStart` at once or retried for 6 s, `IdleForceDefaultState` then `moveStart`, which turned a
        walker into a slider, `CyclicCrossBlend`). Cause not known. New copies also slid about 1 in 10, refusing their first replays.
        A copy-side `moveStart` while the owner walks seemed to help (0 of 5) but the rig game crashed twice in the AI movement code with it
        (SkyrimVR.exe+0x115bc08 and +0x115dc27, `MovementControllerAI`), so it is not used. The next build carries only the TEMPORARY action
        trails, for a play session (see the next-session item "An NPC handed over mid-walk").
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
- [!] **Crash at a fast travel from the map** (`SkyrimVR.exe+0x13085EE`, player 1 on d40cc8b, 2026-10-31 15:05; cause and fix in KNOWN_ISSUES,
      "Crash after closing the unpaused map"). Build a114d97 runs the unpaused map's close on the main thread at the start of a frame.
      Rig: `live-map-close` red on d40cc8b (the hide on a worker), green on a114d97 (Cancel, message box, a hit, a load), and `live-map-pointer` ok on b493cbb.
      That shows where the hide runs. It does not show the crash gone: it came once in hundreds of closes, never in the rig. Not seen in play.
      A real fast travel from the map cannot run in the rig. To see: "Fast travel from the map".
- [!] **Fingers not synced on MGO's skeleton** (since build f997b6a the check takes up to three hops). 536 flattened bones, and `no usable flattened
      bone array` up to 2026-10-23; no MGO session since. FUS has no such skeleton, so no rig run. To see on MGO: no such line, and fingers
      move on the copy.
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

### Test tools

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

## Open work

### Reported or changed from 2026-11-02
- [ ] **OpenComposite through our launcher: players with it ticked now run on it, with the panel inside the game as their menu** (found 2026-11-17 never used; made to work 2026-11-18, build afb0604, rig only, not seen in play).
      OpenComposite replaces SteamVR's `openvr_api.dll` in the game folder; FUS ships OpenComposite Unleashed 5.0.0 as an option (off, its DLL shipped hidden), MGO 4.0 ships 4.3.0 ticked.
      Rig, build d3591f6, FUS with Unleashed ticked once: Root Builder put Unleashed's DLL in the game folder, yet the game loaded `Steam\openvr_api.dll` and ran on SteamVR, no crash, the dashboard made.
      Cause: the launcher limits DLL lookups to listed folders (`loader/PathRerouting.cpp`, `SetDefaultDllDirectories`), and `steam::Load` adds Steam's own folder, which holds an `openvr_api.dll`; the game folder's copy loses.
      A player without SteamVR installed cannot start the game through our launcher.
      If our launcher loaded the game folder's DLL, the game would close at the main menu: OpenComposite's `CreateDashboardOverlay` and `SetOverlayRaw` are stubs that abort (plain OpenComposite at `VRDashboard.cpp:151`; Unleashed gives a hidden overlay, then aborts at the tab icon, `:201`; VRBanner's `SetOverlayRaw` likewise). Read from their source; never run that way.
      Decided 2026-11-18: OpenComposite Unleashed works in the next release (the in-game menu's step S9: the game folder's DLL, the guard, OpenComposite's keyboard characters, and the loaded DLL's path in the `VRRuntime:` line); the launcher says nothing about it until then; crash reports carry `OCUnleashedSKSE.log` and `OpenCompositeInput.log`, with a line on the privacy page before release. Its rig run needs Unleashed ticked in FUS once, put back after.
      Decided 2026-11-17: SteamVR stays forced until the in-game menu exists; then the game folder's DLL, and a guard: OpenComposite found by its name in the loaded DLL's data sections (present in FUS's and MGO's Unleashed, absent from SteamVR's; a missing `vrclient_x64.dll` is no test), no dashboard or banner overlays, one log line, all OpenVR interfaces taken in one place.
      2026-11-18, build afb0604 (client and launcher on FUS; no protocol change; rig only, not seen in play):
        - the launcher loads the game folder's `openvr_api.dll` by its path before Steam's folder joins the lookups (`loader/OpenVRLoader.cpp`), so the game gets a player's OpenComposite;
        - with OpenComposite the dashboard and the banners are off and the panel inside the game is the menu;
        - the `VRRuntime:` line names the runtime and whether the file came from the game folder, never its path, which can hold the Windows user name;
        - rig, FUS with Unleashed 5.0.0 ticked once, put back after (the mod list identical byte for byte): red on the build before (the game loaded Steam's copy and ran on SteamVR);
        - on afb0604 the game folder's Unleashed file loaded, the line said OpenComposite, no dashboard, `live-menu-chord.py` 5/5, `live-menu-open.py` 10/10 (the panel drawn into OpenComposite's eye images), `live-menu-laser.py` 5/5;
        - the same build with Unleashed off: the game folder's SteamVR file, the dashboard made, chord 5/5, menu 10/10, laser 5/5, `live-menu-keyboard.py` 7/7;
        - not tested: MGO's Unleashed 4.3.0, which MGO ships ticked, so its players get the panel instead of the SteamVR tab with this build;
        - not done: OpenComposite's keyboard characters (`WM_OC_CHAR`); the page's keyboard covers typing.
        - to see in a headset: the panel, its laser and the chord on OpenComposite; the SteamVR tab as before.
      2026-11-18, build e850fea (client, server and page on FUS and MGO; same protocol; rig only, not seen in play):
        - OpenComposite installed for the whole PC is found too: it replaces SteamVR's runtime file while the game keeps SteamVR's `openvr_api.dll`, so it shows only once VR is up; the line then adds ", installed for the whole PC";
        - that case is untested: it needs the PC's SteamVR setting changed; SteamVR's own runtime file does not hold OpenComposite's name, so SteamVR players are not taken for it;
        - the `VRRuntime:` line is now written when the game takes its compositor, so crash reports from then on carry it: on the rig 39 s before the SteamVR tab was made; on afb0604 it came after the tab.
      2026-11-18, build fdb6f08 (client and server on FUS and MGO; same protocol; rig only): VR up with neither SteamVR's runtime file nor OpenComposite's name now leaves the SteamVR tab off instead of using its overlays; SteamVR's runtime file is held while it is read; rig: SteamVR found at VR start, the tab made, `live-menu-open.py` 10/10.
      Seen in play 2026-11-18 21:07, build fdb6f08, MGO with Unleashed ticked, Virtual Desktop's OpenXR runtime active, two players:
        - the line said OpenComposite from the game folder's file; SteamVR was not running; the owner found it much smoother than SteamVR;
        - the panel, first seen in a headset: opened and closed by both sticks four times (21:14 to 21:23), all worked; the page looks exactly like the SteamVR tab;
        - the owner: the pointer is rough, as nothing shows where it aims until the dot lands on the page; a hand raised in front of the panel disappears behind it (the panel is drawn over the finished image, without depth); the hand has to stay below the panel to point;
        - after 22 minutes the game closed in Community Shaders 3.18 (TruePBR's landscape material lookup read a null, on a render job thread) during a burst of NPC and 3D loading; none of our code on the stack; the game held 30 GB of committed memory.
- [!] **A start-up crash in Mod Organizer's file system, through our `GetModuleFileNameA` hook** (`usvfs_x64.dll+87CFA`, `+6CC7C` on usvfs 0.5.5.1; public machines 11-08 to 11-14, one twice, at start-up or at the main menu). Fixed in 7fcedea1; build v1.9.81-17-g1eec55cc-dirty.f071edf, client and server on FUS and MGO, not seen in play.
      The launcher's ANSI hook called usvfs's internal `GetModuleFileNameW` handler itself, for any caller (the Steam overlay loading a DLL, a mod during a loader call). That handler takes usvfs's global lock, and usvfs's context was null: a write to 0xE0.
      Now the ANSI hook answers for our own module as before, and otherwise calls the system's `GetModuleFileNameW`, which reaches usvfs's handler only through usvfs's own detour. Its size is now in characters; it was twice the buffer.
      Not covered: three reports reach the same null lock through usvfs's own paths with none of our code on the stack, two of them after a heap corruption. Why usvfs's context goes null is not known.
      Rig on 56ac0c2: the game started under Mod Organizer, SKSE found its 94 plugins, a save loaded and the game connected. The crash itself cannot be made on demand, so there is no red run.
      Uncommitted on top, in f071edf: our own module's path given to a zero-size buffer returns 0 instead of reading the caller's buffer, and a path that does not fit returns the buffer's size with `ERROR_INSUFFICIENT_BUFFER`, as the system does; a buffer over 65535 characters no longer comes back empty. No caller of ours hits either. Rig on f071edf: started, SKSE found its 94 plugins, connected.
      To see: no `usvfs_x64.dll` crash at start-up in the reports of players on the next release.
- [ ] **The rig's game freezes at a load while connected** (`live-load`, 2026-11-15, on 56ac0c2 and on 0c05e7ce, the last commit before the NPC sync overhaul). Not fixed, cause unknown, not reported in play.
      Both builds: the save's load reverts each actor, and the game's own inventory code (`Actor::RemoveOutfitItems`, an unequip, extra data reads) never finished, with no frame of ours for over 3 minutes.
      Our hooks were on that stack each time, with an owner lookup per call: 56ac0c2 in `HookRemoveInventoryItem` and `Utils::GetLocalOwnershipToken`, 0c05e7ce in `UnEquipHook`, `GetLocalOwnershipToken` and `TESObjectREFR::GetItemFromExtraData`.
      So the NPC sync overhaul did not bring it. Since `live-load` last passed (11-05, 070635b) both the code (the copy items and hand changes) and the rig's mod list (FUS 5.1.0 since 11-12) changed; which one brought it is not known.
      The bot's player copy was in the cell, its items just set (168 worn entries). In play, the host loaded a save while connected on 11-14 at 15:37 and it went through.
      Next: `live-load` on the 11-05 build against FUS 5.1.0 tells code from mod list; a full dump of the freeze (`freeze.small.dmp` holds stacks only) would show which actor's inventory the game walks.
- [!] **A frozen copy of the other player in a building, and a nameless Imperial soldier in the other game** (2026-11-15 09:08 and 09:13, each time the players went into a building one after the other). Fixed in build 89a8563, rig could not reproduce it, not seen in play.
      Player 2's game had player 1's copy in the shop (09:08, Riverwood Trader) and in Gerdur's house (09:13), a cell it had not loaded, so the copy had no 3D. The server then removed it there.
      The game still held the copy, so it waited to be freed for up to a minute, and stayed switched on: since build d6c5b2c6 a removed copy with no 3D is not switched off, as that can crash.
      When player 2 walked in, the cell loaded, the game built the copy again and player 2's game announced it as its own NPC (`Entity: local actor FF000D4B has server id 200023`).
      Player 1's game then made a nameless copy of it (`local base FF000D9F ()`), drawn as a default Imperial soldier, once with a white face on a dark body. Both went when the minute ran out.
      How the copy got into the unloaded cell is not settled: it used the load door replayed from player 1's game 40 ms before the server removed it, and the guard that keeps copies off load doors logged nothing.
      Fix: a removed copy still waiting to be freed that comes back with its cell is never announced, is switched off now that it has its 3D, and waits its minute again (`OwnCopy: ... came back with its cell after its removal; not announced, to be switched off`).
      The waiting list's own check (every 250 ms) also switches off a waiting copy once it has 3D, so a skipped switch-off is tried again.
      Rig: five tries on b1908eb (the bot's player through Mistwatch's door, 2 s and 40 ms before its removal; the player leaving and coming back; the copy moved inside by script) never left a removed copy without 3D: each time it was removed with its 3D and switched off.
      In play, check: go into buildings one after the other; no frozen copy of the other player inside, no Imperial soldier, and the `OwnCopy:` line in the log of the one who entered second.
- [ ] **Brawl session, 2026-11-15 20:37-20:49** (build 9ab38c9, both logs read).
      Quests: the main quest's stages reached player 2 within seconds (Before the Storm to 200, Bleak Falls Barrow 10 to 90). The brawl is the game's DGIntimidateQuest, a miscellaneous quest, and the server syncs none of those (`bEnableMiscQuestSync`, off since upstream; the owner keeps it off); player 2's game rolled its own brawl with another target.
      Player 1 died while player 2 revived her (20:45): losing a brawl ends the bleedout and the game stands the player up, and the downed system took that for a bleedout that ran out and respawned her.
        Fixed, not seen in play: a down in a brawl (DGIntimidateQuest running and a fist's blow) is left to the game, which stands the player up, so the server never declares a death, alone or with the partner far; the setting that keeps a player down for a revive is put back after it, which the brawl's script clears; a down that outlasts the brawl quest becomes a normal down (`PlayerService.cpp`). Build d3591f6; no rig test (the brawl quest needs its aliases filled by its dialogue); `live-revive-giveup` and `live-death` ok.
      Amren stuck for player 2 during the brawl: player 2's copy fell into its own ragdoll (20:44:13), its get-up was refused three times, and it lay there while player 1's game had him up and chasing; player 2 could neither find nor hit him. Again in the Drunken Huntsman.
        Fixed, not seen in play: once every get-up is refused, after 6 s, with its owner's NPC standing over 100 units away, the copy is put on its feet there with the game's own move, noted to discovery so its 3D dropping for a frame does not remove it, once in 30 s at most (`InterpolationSystem.cpp`, KnockDown). Build d3591f6. The rig never reproduced the refused get-ups (`live-copy-knockdown` 5 runs: the copy got up each time); it fails now and then on the jump to its owner's place after getting up (up to 802 units), on the old build too.
      Player 1 heard the low-health heartbeat afterwards; the log shows it started and stopped at each of her three downs. When and how long is still to ask.
- [ ] **Companions session, 2026-11-15 15:42-16:17** (build 6271913, both logs and the server's read).
      Vilkas without his shield in player 2's game (16:01): player 1's game ran him and sent, at 16:01:08, the unequip of his shield (a light-plugin item) its hook saw; the game then wore it again with no change a hook sees, and player 2's copy kept the unequip. Player 1 saw the shield on after he handed over his sword.
        The hook queues a change before the game makes it, and the equipment sent with it is read a frame later, so it can already show the shield back on; only the player had a periodic equipment snapshot, and a snapshot carried only weapons and torches.
        Fix written, not built: after each equipment change of an NPC the owner's game reads its gear again 1 s and 3 s later and sends a snapshot when it differs, or at once when the change was undone by the time it was sent (`Services/Generic/NpcEquipmentCheck.cpp`); a snapshot now puts on the owner's shield; a copy given new armor no longer puts back the old piece of the same slot. Rig test to make: a re-equip no hook sees.
      Eorlund walking off while player 2 traded with him (16:05): player 1's game ran him (server: until 16:07:46) and his routine went on; in single player the trade menu pauses everything.
        Owner's decision (2026-11-15): no hand-over of an NPC to the game of the player talking or trading with it, too much risk.
      A sword drawn while the other game is paused does not show: the weapon-drawn message only updates the server's record for later arrivals, so a copy draws only by replaying the owner's draw, and a paused game refuses replays (player 2's journal pause 16:05:31-16:07:19 refused them throughout); nothing corrects the copy afterwards. Player 1 confirmed: player 2 was paused, player 1 drew, player 2 never saw it.
        Fix written, not built: the server relays a player's weapon-drawn state to the others (`NotifyDrawWeapon`, which no client handled), and a game running unpaused draws or sheathes a player's copy that has shown the other state for 1.5 s (`Services/Generic/PlayerCopyWeapon.cpp`, through CharacterService's two-pass draw). Server change; same protocol.
        A draw the owner made while this game was paused is drawn a second after it runs again, whatever the copy's state says (its owner's later actions can set it to drawn with the sword left sheathed); other mismatches after 1.5 s; two tries per state; none for a copy down or dying. Build 9ab38c9 (after a review: a pause in the 2 s before the state counts, as the state follows the draw by up to 1.5 s; a forced draw that started is not played again; the flag is dropped for a copy down or with a draw queued), client and server on FUS; `live-sword-pause` and `live-equip` ok on it.
        Rig (`live-sword-pause`, a bot, one game): red on fd3f66c (the copy never drew); on fcaa5d2 the draw was asked for 1 s after the journal closed and started (sheathed to drawing), then the copy sheathed again, as a bot sends no animation data to hold it drawn. A copy arriving goes through the same two passes and stays drawn in play. Not seen in play.
        The two-game rig cannot check it on FUS 5.1.0: the second game recompiles Community Shaders' cache (1 h 48 min) and never reaches the menu.
      Jorrvaskr NPCs flickering for player 2 after player 1 walked out (16:15:54): the server handed five of them to player 2 at 16:15:59, taken over at 16:16:01 where player 1's game last had them (0 units off); the repeated refused actions seen afterwards also happen in normal ownership, so they are not the cause. Player 2's game paused at 16:16:13 (journal) until the disconnect at 16:16:45. Cause not found; player 2's description of what flickered is needed.
      Player 2's copies were fed on time in Jorrvaskr (1-30 ms stale); both games ran about 35 fps.
- [ ] **Stuck at Gerdur in Before the Storm** (2026-11-15, the players left the session there). Not fixed; what closed the conversation is not known.
      Stage 50 comes from Gerdur's line "The Jarl needs to know if there's a dragon on the loose..." (DF01D), reached by itself after "There's something you could do for me" (DF023).
      That chain starts from her forcegreet (package 9A053, after stage 42) or from the player's topic "Do you have any supplies I could take?" (DF022, while stage 50 is not done and MQ00's DragonFlag is set), each through her supplies (GiftMenu).
      Player 1's game, 09:11: forcegreet, stage 42 (her key), supplies, then DF023 at 09:11:21.147; the Dialogue Menu closed at 09:11:23.303, during that line, so DF01D never played. Player 1 asked again and chose the Whiterun topic, not the supplies one.
      The close came 2 ms after a fox copy was spawned (re-sent every 3 s); no other close of the session came near a spawn, and a Whiterun line playing during a later spawn was not cut. Player input (closing the supplies, then the dialogue) is the other explanation.
      In player 2's game Gerdur was a copy, which runs no package, so player 2's two conversations with her (09:13:28, :34) had no forcegreet line.
      09:15:52: player 2 walked out of Riverwood and the quest's Player alias script (MQ102PlayerScript) set stage 55, its skip once the quest is past stage 50 (stage 52 ended the mill scene); player 1's game took it. Stage 50 never ran, so MQ102 stage 30 was never set.
      Rig: the 09:14 autosave loads with Gerdur beside the player, stage 52, DragonFlag true, her forcegreet spent (current package GerdurSleepHome22x9). A conversation started by script crashes the plain game (`SkyrimVR.exe+0x6E5BF9`), no rig tool picks a topic, and the bot cannot start indoors, so the cut cannot be replayed on the rig.
      To go on in that save: ask Gerdur for supplies again; entering Dragonsreach also moves MQ102 on (MQ102PlayerScript sets its stage 75).
- [!] **Naming the character at creation reconnected the game at the Helgen Keep door, and Ralof's keep scene stopped** (play 2026-11-14 15:29:42, both players in the vanilla intro). Fixed in build a82e5cd (client and server, a protocol change), not seen in play.
      The game connected as "Prisoner"; character creation renamed it, and at the next loading screen, the keep door, it reconnected (`the character is now '<the new name>', was 'Prisoner' when connecting; connecting again`).
      A reconnect hands away everything the game runs: player 2 became leader and took Ralof at 15:29:55, in the middle of his keep scene, which player 1's relayed stage 250 had started there 10 s earlier while he was still a copy.
      In player 2's game Ralof then said no scene line at all until he went down at 15:34:48; in player 1's solo replay, one game running him throughout, the same scene ran phases 3 to 16 in 45 s.
      The players hit him because he was stuck: both their blades and Flames, until he lay in bleedout (`DownedTravel: own NPC 2BF9E ... life state 7`).
      Fix: a name that changes with no save loaded since the last check (no `TESLoadGameEvent`, no trip through the main menu) is the same character renamed in play. Its look, face tints and name go to the server in place (`RequestRespawn`, two new fields), the server renames the player (`Rename: player N 'old' is now 'new'`, `NotifyPlayerName`), and the other games rebuild the copy. A save loaded still reconnects. A protocol change.
      Character creation took 7 minutes to show: it closed at 15:22:25, and the name check waits for a loading screen. Now the look and name also go the moment the race menu closes (`the race menu closed; the character's look and name '...' sent`).
      Not covered: the menu page's player list keeps the old name until the next connect. The race menu path has no rig test: the rig cannot close the menu by hand.
      Rig: `live-rename-load` (a rename by script, then a loading screen): red on 51e8473 (4 of 6 checks: it connected again), green on cd7ec0a and a82e5cd. A second review: a race menu closing after a save load not yet checked leaves the decision to the name check, and a send that fails is tried again.
      `live-load-other-character` (a save load must still connect again) could not run: every save loaded from inside the rig's game froze it, 3 of 3, connected or not (see Rig and test tools).
      In play, check: create the character together and walk into Helgen Keep: no `connecting again` line, and the other player reads the new name over the copy.
- [!] **A quest companion turned on the other player at their hits** (play 2026-11-14 15:32 to 15:34 and 15:48, Helgen Keep, Ralof). Fixed in build a82e5cd, not seen in play.
      MQ101 stage 240 calls `IgnoreFriendlyHits` on Ralof and Hadvar. Another player's hit arrives as a health message, so the game's friendly-hit rule never ran, and `FightBack` set Ralof on that player's copy at every hit, 13 times in player 2's game and twice in player 1's, hits of 0 included.
      Fix: for a hit from a party member, `FightBack` leaves alone a follower, and an NPC told to ignore friendly hits that is no enemy of this game's player, as the game's own rule does (`Combat: actor X lets player N's hit go, as the game does for a friendly hit`). The flag alone is not enough: the siege mod sets it on both sides. Party, not the PvP setting: both the players' server and the rig run with PvP on.
      Rig: `live-friendly-hit` (a friendly NPC with the flag: no fight; the flag cleared: the fight starts; a bear with the flag: the fight starts): red on 51e8473 (3 of 7), green on 52d2ce2. A fresh review caught the siege case on the first version, and the rig the PvP gate on the second.
- [!] **Helgen Keep's collapse never played in the member's game** (play 2026-11-14 15:42:41, player 2). Fixed in build cd7ec0a, not seen in play.
      The collapse is played by trigger box BACC8 in the game whose player walks in, and the trigger then switches itself off. The leader's relay sent that switch-off, and player 2's game applied it 55 ms after stage 310, before player 2 got there (`ReferenceEnable: BACC8 (base BACC7, form type 24) switched off here, as the leader's game has it`).
      Fix: a trigger box (an activator with a primitive shape) is neither sent nor applied, live or at a join, so each game's trigger fires for its own player (`ReferenceEnable: trigger X (base Y) left off|on here`). Its stage goes once per party, as before.
      Risk: a trigger whose script spawns things now spawns in each game, as "Scripted scenes run in each game" below.
      Rig: `live-trigger-stays` (the leader switches the trigger and a marker off: the marker follows, the trigger stays; the same after a rejoin): red on 51e8473 (3 of 5), green on cd7ec0a.
      In play, check: the member reaches the hallway after the host, and the collapse plays for them too (`ReferenceEnable: trigger BACC8 (base BACC7) left on here`).
- [ ] **The same session, read and left as they are** (2026-11-14). Ralof's jump across the cave at 15:50:44 was the quest's own `MoveTo` (MQ102B stage 1). The bear woke at player 1's first arrow (combat stage 730, 82 ms later) and was not held by our code.
      Small: player 2's copy of Ralof ran on the spot for up to 11 s with no reports before `QuietStop` (15:52:40), not after its 1.5 s; `JumpDiag` lines name no actor.
- [!] **An NPC's lines not heard by the other player** (10-31 14:25 and 11-01 12:38: Ulfric talking to player 1, silent for player 2; 2026-11-11 19:43: Faralda at the College bridge, silent for player 2). Cause found 2026-11-13, fix not yet seen in play.
      Cause: scene lines. In the 11-11 College tour (scene D67DC, Mirabelle and Ancano) player 2's game dropped 16 of the host's 21 lines as `which plays here too; the local line is kept`, and its own copies never said them: a copy cannot complete its owner's packages.
      Greetings and menu dialogue play: on the two-games rig (2026-11-13) player 1 talked to Faralda by hand and player 2 heard every line, confirmed by ear and by recording.
      Fix, no rig test (the tour scene is not set up on the rig): a scene line left to the local scene plays after all when no copy says a line of that scene here (within 3 s for the first line, 800 ms once that scene was silent).
      Lines: `this scene said nothing here for N ms, so the sender's line plays`, or `this scene speaks here too, so it voices the line`.
      2026-11-14, Helgen (scene AC133, Ralof): Ralof spoke twice for player 2; the copy was silent at the 800 ms check and its own scene said the line anyway. Now the scene here keeps the line whenever any copy speaks in that scene (whichever variant or moment); no play test yet.
      Rig: talk to an NPC by pointing the virtual hand at it and pressing E; a dialogue opened with the hand pointing at nothing crashes the plain game (`SkyrimVR.exe+0x6E5BF9`, also without our client).
      2026-11-11 on c57f1e6a: all of Faralda's lines the host's game spoke reached player 2's game and logged `started here`, 92 to 224 units away; player 2 heard none, and her lips did not seem to move.
      Player 2 did hear her when player 2's own game spoke her greeting, in its own dialogue menu with her.
      Rig, 2026-11-11, recording this PC's sound output: a line relayed to a copy (a temporary Lydia, and Faralda owned by a bot that leads the party) is heard, about as loud as the player's own line.
      The line's waveform was found in the recording (match 0.20 and 0.42, against about 0.1 with no line), so the relay works when nothing else happens to the copy; what silences it in play is not known.
      From the game's code: the console's `SpeakSound` and the game's dialogue mark the speaker as talking for the line's length (high process data +0x454 and +0x2F8), and our relay does not; the rig hears the line without it, so that is not shown to matter.
      Earlier cause, fixed in build v1.9.7-15-g6826e679-dirty.355ef2d:
      Cause: the game's first speak call for a voice line only starts loading its lip file, and returns with no sound. We asked once, so a line played only when its lip file was already loaded: about 1 line in 9 in both sessions.
      Fix: a line that made no sound waits for its lip file, read as the game reads it, then is asked for again, for up to 3 s. A line drops if its speaker dies or goes, or a local scene voices it. An actor past 4096 units is not asked again.
      Rig: `live-npc-line`, measured on 23ad300, gave sound id -1 and duration 0 for real lines, and a real sound of about 8 s once asked again. On 355ef2d they wait 15 to 16 ms for the lip file, then start. `live-quest-hold` ok.
      Nothing on the rig shows whether the mouth moves. In play, check: player 2 hears a conversation at normal distance, and the NPC's mouth moves. Player 2 should hear many more lines overall, as most were silent; if barks overlap, say so. `the game had not started it` in the log is a line lost.
- [ ] **A copy switched off while a game job still ran it** (2026-11-12 23:08:14, a public pair, report 20261112-220845; also 2026-10-18 on v1.9.4). Not fixed: the change below is not shown to touch it.
      The server removed a dead Skeever copy (FF0008BE) the game still held. The client's update switched it off at once (`Temporary Remote Held ... disabled and queued`), and 11 ms later a movement job of the game read it, deleted by then (`SkyrimVR.exe+0x714EB2`, MovementControllerNPC, the copy in R15 with form id 0).
      The client's update runs on the game's job threads, beside the jobs that run actors. The October fix kept such a switch-off away from the game's own temporary actors; our own copies were still switched off that way.
      Now the three places that switch off a removed copy queue it, and the main loop switches it off at the start of the next frame, on the main thread (`CopyOff: X switched off at the start of a frame, on the main thread`).
      Rig: `live-copy-removed` on a0f2f73 and 74a329c, each time 6 of 6 copies switched off on the main thread about a frame after their removal, the game alive. No red run: the race cannot be made on demand.
      But the game's switch-off is only a request: on 57331c6 every copy logged `the disable not landed yet` right after it, and lost its 3D 13 to 40 ms later. The game carries it out on its own schedule, wherever it was asked from, so moving the request moves nothing that matters.
      Since 57331c6 a copy with no 3D is not switched off at all (`CopyOff: X left as it is: it has no 3D`), the rule the October fix applies to the game's own actors, and the queue drops a copy queued twice.
      The skeever was dead, and 11 ms after the request the game had deleted it (form id 0) while 2 handles were still on it. Next: whether the game deletes a dead temporary actor at once when it is switched off, a variant of `live-copy-removed` with the bears killed first.
      If it does, a dead copy must not be switched off while held; it waits as it is until the game lets go. To see: no more `+0x714EB2` crashes in the hub.
- [ ] **A null call on an AI thread updating an actor's face** (2026-11-08 21:36, player 2, Fort Greymoor, report 20261108-203703). The same crash as the eye tracking entry below, followed there.
      `SkyrimVR.exe+0x5EB6C9` under `Actor::IsMoving` / `UpdateActor3DPosition` / `ProcessLists::UpdateHighActors`, `BSFaceGenAnimationData` and `"NPCEyeBone"` in the registers; no code of ours on the stack. 3 s before, Stormcloak soldier 8C00A309 changed hands three times.
- [ ] **Crashes in character creation, connected, with no other player nearby** (2026-11-08 19:46:49 and 20:01:00, two new games at Helgen, the RaceSex Menu open for 17 s and 35 s; 20:05:00, Realm of Lorkhan, connected by the auto-connect 3 s after the menu opened, crash 8 s later). Cause unknown.
      2026-11-16: two of these crashed at SkyrimVR.exe+3E95D0, in the game's face morph and head part update (function 26406). Five crash logs from another PC (2026-11-11 to 11-15, plain SkyrimVR.exe without this mod, RaceMenu VR loaded) crash in that same function at +3E85D7, on female head parts with male or Khajiit parts in the same face: the same game code failing without this mod, so likely a face mod's doing here too; not proven.
      Three of three were connected; player 2's character creation with the mod but not connected went fine. That points at something that runs only while connected, but no such code is found yet.
      Each time a part of the player's head read a pointer from freed memory, after a race or sex change. 20:05 is the same crash as 19:46 (`FemaleHeadHighElf`).
      Not connected (20:09, server stopped), the menu stayed open 14 minutes with no crash. The same game then crashed while saving, still not connected (20:15:52): the script engine's save read a garbage pointer from its own data (`SkyrimVR.exe+0xc6dbb0` under `+0x12ac40c`, `mov r8, [rdx]`).
      All four crashes were on v1.9.8-16-geed70ddc and in brand-new games, so the build and the new game are not told apart yet. The builds before it that day had no crash of this kind, but they only loaded existing saves.
      Not reproduced on the rig: console race and sex changes, Papyrus face, tint and weight changes (over 1100 steps, connected, also connecting mid-way), on a finished and on a new character; the rig cannot drive the VR menu itself.
      All three character creation crashes are one fault: a head part's shader property (`[geometry+0x168]`) freed while the part still held it.
      Diagnostic `HeadPartWatch` (TEMPORARY): logs a head part holding a freed property, and the stack of the release that destroyed one while a part held it. Deployed v1.9.8-17-gfab767c9-dirty.52e2ab5, FUS and MGO, client and server; it runs from the main loop on the main thread, its guarded reads tell the crash handler to leave their faults alone, and an address a new property took is not reported. Rig: 37 head rebuilds in character creation, no false line.
      Seen in play once on it (22:00, the owner): a new game in Realm of Lorkhan, connected 4 s after the menu opened as at 20:05, 2 min 46 s in the menu, 29 head rebuilds, a save after; no crash and no `HeadPartWatch:` fault line.
      So it does not happen every time; what differed from the three crashes is not known (that build, a freshly started server, nobody else on it).
      `HeadPartWatch` removed 2026-11-20: never a fault line in 35 character creations across 135 client logs, and the crashes it was for were the WEAPON and SHIELD bone fault found below, fixed on 11-13; every later crash at `+0x1354C40` was on a build from before that fix.
      19:46: the menu updated the head (`MaleHeadImperial` under its `BSFaceGenNiNode`) and read its shader property's fade node as garbage (`SkyrimVR.exe+0x3e95d0`, `mov eax, [rcx+0x10C]`).
      20:01: 4 s after the race change (`Animation graph project of actor 14 changed`), the shadow scene's update wrote through a garbage pointer from the brows (`FemaleBrowsHuman06`; `SkyrimVR.exe+0x1354c40`, `mov dword ptr [rcx+0x18], 0`).
      No code of ours is on either stack. Our only code touching a head's shader property tints other players' copies, and there was none; the VR body code writes only to copies' skeletons. The menu paused the game as it does without the mod.
      To tell: character creation on the same PC without connecting (the server stopped until it is done; the game retries and joins within a minute of it starting again). A crash there too is not the mod's; none there makes it ours.
      2026-11-13, FUS 5.1.0 (CSX 3.19.2-VR, Engine Fixes 7.9.0, address library 0.275.0), build 57331c6: player 1 crashed in character creation at Helgen, connected, at `+0x1354C40` again (19:08:02), on the player's own `clothes_underwear` mesh this time, not a head part.
      Player 2 crashed a minute later in the same lighting code (`+0x1355720`, his body mesh), 9 minutes into play and NOT connected. Neither session had a 3D change of ours (`RecentModelChanges: this client changed no actor's 3D`).
      Read from the game code: the shadow pass takes the mesh's shader property (geometry +0x168), passes its light data (`BSShaderProperty::lightData`, +0x70) on, and writes through it. It held two floats, not a pointer.
      The calls are inside address-library id 100997 (0x1354D20), the function Community Shaders' Light Limit Fix patches (`LightLimitFix.h:164`, see KNOWN_ISSUES on the combined library). The hub has it on two more machines (10-18, 10-20).
      The full dump of the 19:08 crash says otherwise: the shader property was overwritten from +0x48 to +0x77 with twelve floats, a clean 90-degree axis-swap rotation and the position (-0.23, -3.14, 7.16).
      +0x48 to +0x77 is where a node keeps its local rotation and position, so something wrote a bone's local transform through a pointer to memory that is now this shader property: a write to a freed bone.
      The dump shows no plugin hook on the crashing call path, and the shader mod moves no bones. So the lead is now whatever drives the player's own skeleton each frame and keeps bone pointers across a rebuild of it (VRIK, HIGGS, PLANCK; FBT is new in 5.1.0 but 11-08 predates it); character creation rebuilds that skeleton at every race change.
      Ours: `WriteLocal` in VRBodySync writes bone transforms only on other players' copies and checks the node's vtable first; player 2 had no copies (not connected). Not proven for any mod.
      Cause found on the rig, 2026-11-13: opening the character creation menu (or changing race) rebuilds the player's skeleton, but the player's animation graph 0 keeps two entries of its bone table (`WEAPON`, entry 43, and `SHIELD`, entry 42) on the old, freed nodes.
      Each frame the animation's pose-to-node loop (SkyrimVR.exe+0xB3C260, the rotation through +0x1E0510) writes those bones' pose into the freed memory; whatever takes that memory next is corrupted. The crash's twelve floats are `WEAPON`'s pose: (-0.225, -3.141, 7.159).
      Shown by: freed named nodes held 1.5 s (the player's 120 s) with their transforms compared (`NodeQuarantine: freed node 'WEAPON' ... was written`); a hardware write watch on the held `SHIELD` caught three writes from +0x1E05DF under +0xB3C383, on the main thread.
      Not a plugin hook and not our code: the bone table is the game's own (`BShkbAnimationGraph::boneNodes`, +0x160); our graph code only reads and writes behaviour variables. Why the game does not rebind those two entries is not known (a mod moving the weapon nodes would fit).
      Fix in build v1.9.81-6-gd6c5b2c6-dirty.a24c019: at the start of every frame the player's graphs, and every tenth frame the loaded actors' graphs, are checked; an entry on a held freed node is moved to the node of the same name in the graph's 3D, or emptied when there is none (the pose loop skips an empty one).
      It relies on the hold: freed named nodes keep their memory for a moment, so nothing is corrupted before the repair (`GraphRebind: actor 14's animation graph 0 entry 43 ('WEAPON') pointed at a freed node; moved to ...`).
      Rig on a24c019: the menu's first opening left both entries stale, both were moved within 70 ms, and nothing reached a freed node for the rest of the run (a second opening, race and sex changes, two loads). Not seen in play.
      To see: a character created in VR without a crash, and `GraphRebind:` lines in players' logs; a `NodeQuarantine: ... was written` line after a repair would mean another writer.
      Since b81ae54 the hold, the repair and the write watch live in `BoneHold.cpp`, to stay; the temporary eye bone check (`EyeBoneWatch.cpp`) only looks at the nodes its destructor hook destroys. Rig on b81ae54: both entries moved 34 ms after the menu opened, nothing written after.
- [ ] **An NPC announced again every 4 to 5 s when the game it is handed to declines it** (2026-11-08 14:46:09 to :25, rabbits 107D04 and 107D15; 12:04 to 12:06, Stormcloak Archer 8C00A316, 18 times). Not fixed.
      The owner's player walked out of range, so the server handed the NPC to player 2. Player 2's game declined (`Hand-off of ... declined: a copy of ours stands in`), the server removed the character, and the owner's game, which still had it loaded, announced it again (`is gone from the server while ... stands here; announced again`).
      Each round, player 2's copy is deleted and made again. Which side should break the loop is open. The leader's claim fix below does not touch this loop.
- [ ] **A crash in the game's update of a Sparks beam, on a cow the leader's game had just taken over** (2026-11-14 01:46:49, a public machine on v1.9.81, Falkreath, `SkyrimVR.exe+0x28DEC3`). Cause unknown, not reproduced.
      The leader's game claimed three cows it showed as copies of the other player's (01:46:35 to :40); 9 to 14 s later the other player's Thalmor Justiciar cast Sparks at them, replayed here.
      The crash read the child list of a cow's head bone ("Scull") while the bone was intact but its child list had lost its storage, on a game worker thread running the beam's update; Locational Damage was looking a bone up by name.
      Our crash block: none of our deletions in a register; our last changes to that cow were the forced weapon toggles at its spawn, 54 and 55 s before, and no 3D rebuilt.
      Rig on 10f31ee: `live-crash-cow-sparks` (three cows taken over, 45 replayed Sparks casts, with Locational Damage loaded) did not crash; the beams hit and killed the cows.
      To see: the game's crash dump of that crash (not sent with the report), or the same address in another report.
- [ ] **A dead horse that could still be ridden** (2026-11-08 14:22, the host's game). Cause unknown.
      The host killed horse 798EB at 14:20:58 (`BodyStand: dead 798EB had 289 health; taken` at its death). Its prompt then read "Steal", and at 14:22:08 the host's game reported riding it (`Mount: copy FF001386 of rider 12 told to ride copy 798EB` in player 2's log).
      It read "Search" after player 2 touched it; no log line says what changed. Build 1a16be1 adds the TEMPORARY `Mount: X starts to ride Y, whose life state here is ...`, logged when a ride starts on a horse that is not plainly alive.
- [ ] **A crash in the game's AI four seconds into a big battle** (play, 2026-11-04 21:25, the leader's game, 472df4d, confirmed by the player). `+0x743dc2`, a copy past a page end in an array erase.
      The chain is an AI job, then ActorKnowledge `0x7434f0` (SE 41632), erasing from its own list at `+0x28` through an iterator. That iterator pointed 4 MB from the list's current start, so the list had been reallocated while it was walked. Our detection hook (`0x743170`) is a different function, not on the stack, and changes no list.
      A second earlier, 63 soldiers had been switched on together, stacked on two spots, all entering combat. Our code had only claimed them, reported their equipment and relayed their dialogue. The second writer is not in the dumps.
      Read as the engine under load. A second sighting with the soldiers spread out and started in groups would call for a closer look.
- [ ] **A body that comes back into a game stands up until our net lays it down** (play, 2026-11-02 18:24 to 18:35 on d91bd5b, 50 to 100 soldiers, two players: "soldiers standing back up" at the end). Build 8543729, no test, not seen in play.
      18 of the 20 bodies that really stood (pelvis 57 or more; the first low readings of 50 to 56 are probably bodies on a slope) were already dead when a `CharacterSpawnRequest` brought them back, with `Disabled actor is being re-enabled`. The spawn's last step acts only when the reference's state differs from the owner's, so a body dead in both plays nothing, and its new 3D stands. `BodyStand:` caught them 5 to 55 s later: it needs two looks 2 s apart, and at most 24 bodies a pass.
      The owner saw them at the end of the last battle, Imperials only, and the logs agree: from 18:34 every body that stood through a spawn was an Imperial (host 7 back, player 2 6 stood, no Stormcloak). Why that side's bodies left the range is not read. `BodyStand: ... had 1 health; taken` (about 270 at player 2) is the copy's health floor, not the cause.
      The change: that step lays such a body down at once, logging `came back as a body`. It needs a red run: kill a soldier, take the other player out of range and back.
      Open: what disables these bodies (`Actor removed`, then a disabled reference at the next spawn). FE00E8F3 and FE00E8FB stood at the host with no spawn, on their owner's game, and three tries did not move them.
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
- [ ] **A siege side does not show on player 2's copy in the leader's game** (11-06 17:51 and 17:56). The copy took its owner's factions 2 and 5 s before the leader's check (20 factions, the side one in them by the 19 and 20 counts), yet `IsInFaction` read no side.
      Rig, two real games on v1.9.8-4-g2200fbc6 (`two-games-switch.py sides`, 11-06 21:12): the second player joined, left and joined `urSS_SideImperial`, and the copy followed each time within 3 to 4 s at rank 0, -2, 0.
      So the faction travels; the play warnings stay unexplained. If they come back, log the copy's rank for each side at the leader's check.
- [ ] **A crash in the game's own AI job in big siege battles** (`SkyrimVR.exe+0x743D74`, a job thread, `Job_Non_render_safe_AI` through ActorKnowledge; 11-05 18:27 and 11-06 21:16, the leader's game).
      A function that removes entries from a list of shared pointers was given a garbage index (0xFB69C82C), as when the list changes under it. No frame of ours on the stack.
      The 11-06 one came in the rig, two games on one PC at 2 fps (frames of 469 ms on average). Not known whether our copies play a part; a full dump from a crash in real play would show whose list it was.
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
- [!] **Soldiers revived before a player joins are killed by that player's save** (siege at Fort Sungard, 2026-11-08 12:04). Fixed in 870f1af0, built and deployed v1.9.8-16-geed70ddc (FUS and MGO, client and server), not seen in play.
      The host's game revived the fort's soldiers at 12:03:26 (`Death sync: local actor 526EB is now alive, telling the server`). Player 2 joined at 12:04:02 with them dead in its save:
      `DeathWins: 526F2 (server id 1A) is dead here but alive in its owner's game`, and the host's game took `player 2's blow of 100000` (`health broadcast killed actor 526F2`). Player 2 saw them all dead and stripped as it spawned.
      Fix (a protocol change, `6b296bbe`: every player and the server need this build): the owner's revive report marks the character on the server, a death clears the mark, and an announcement from a game that saw the revive carries it.
      A game joining later follows the owner's life for a marked character instead of keeping its save's body (`Death sync: X is dead here, but its owner's game revived it since its last death; it follows its owner`).
      `s_lifeFollowsOwner` keeps only the leader's switch-on case. A kill player 2 made where the owner did not see it, after the owner's revive, is lost: the body stands up again, as at the owner.
      Rig: `live-dead-revived-joins` RED on the build before (body kept dead, killing blow sent), ok after; `live-dead-stays-dead` ok after. TPTests 114 pass (a round trip of the mark).
      To see: soldiers alive at the host stay alive for a player who joins mid-siege.
      Follow-up in the tree, not built: a copy still waiting for its 3D now takes the mark from newer server state as it takes the death state, and every copy keeps the mark from its spawn or assignment, so announcing it again from that game keeps the mark (`CharacterService.cpp`, client only).
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
      Since 57331c6 it notes any plain node freed while a live parent lists it, not only the eye bone: a head bone released once too many frees the eye bone with it, as its child, and only the head bone's release shows the culprit.
      Its line is now `EyeBoneWatch: node X ('<name>') freed while its parent ...`; the first 20 are logged in full, then one in a hundred. Rig on 57331c6: silent through four trips that unloaded 142 actors, 60 fps.
      To see: an `EyeBoneWatch: node` line in any log; its stack names what freed the node.
      2026-11-20: no such crash in the hub's reports since 11-13 (17 reports after it), but all eight came from the release v1.9.81, which has neither the watch nor BoneHold, and no machine that crashed ever ran the watch. Kept until the next release has run a while with it.
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
- [!] **The script engine on a stale form, at more places than the guard covered** (2026-11-02, two players in one party on the release v1.9.7, 7 of their 9 crashes that night, and once on the rig). Build v1.9.7-18-g8d7f58c4-dirty.d91bd5b, rig: starts, plays and loads with no crash, no `FormGuard:` line in that run, so the guard itself was not exercised; not seen in play.
      `FormGuard` is in no release yet, so those players had none. It covered one check, `+0x93CE11`, and only a form's own handle, so `+0x93F0F9` (with its landing at `+0x15DDEC1`) and `+0x93DB7E` went through, the first on the rig with the guard on.
      `+0x93F0F9` and `+0x93DB7E` are the same read: a form looked up from a handle, then a call through its vtable. An alias's or active effect's handle names its form in the same low 32 bits.
      Change: every slot of the handle policy that looks a form up now gets the invalid handle in place of one whose form is stale, for all handle kinds.
      The stale form in one crash, `FF000E3E`, is a temporary base of a levelled NPC, the game's own, and `RecentDeletes: nothing has been deleted this session`. Three restarts in a row died within 49 s of connecting, so a stale handle may sit in that host's save. Not read. No way to make this crash on demand, so no red run.
      To see: `FormGuard:` lines with no crash at these addresses. The guard's slots run for every script, so a mistake there shows at once at start-up.
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
- [!] **8 GB more at every load, freezes and wrong colours: the siege plugin queued SKSE tasks 20 times a second through loads** (both players, 11-07; KNOWN_ISSUES has the details).
      Seen: textures in the wrong colours at load, loads up to 5 min 40 s, slow play, and freezes at the first load when the game passed the PC's 31 GB of RAM.
      Cause found by a rig A/B and the siege session's bisect: its glide thread (since 11-06 20:08) queued a task 20 times a second at all times; empty tasks did the same. This client plays no part.
      Fixed in the siege plugin (8eb27e9, deployed to FUS 15:19): the first `Health:` after a load on the owner's save is 8.7 GB, against 18.4 before, as on 11-05.
      To see in play: the colours right at load, `Health:` at the first load near 20 GB, no `FreezeWatchdog:` there. Seen needs the new siege DLL too.

### Not reported since 2026-11-01

- [ ] **Dead on one side, standing on the other, again** (14:29, 14:44, 14:52 on d40cc8b; see "Dead NPCs in two games"). Not fixed. At 14:52 it came after some time in place, not on a load.
      14:44, Death Hounds, dead for player 2 and standing for player 1, the case with a clear log: hound 1A002 (401A002 at its owner, player 2; 301A002 as player 1's copy). Player 1's blows from 14:44:13 to :17
      reach the owner as `health 121 to 104, already dead`, while the copy logs `kept at 1 health until its owner says it died`. The owner's log has no `is now dead, telling the server` for it. So no death word
      was sent, and those blows do not ask for one: the copy waits for a word that never comes.
      14:29, a guard dead for player 1 and standing for player 2: neither log has a death line from 14:28:00 to 14:29:40, and which guard it was is not known.
      14:52, Imperials getting up for player 1 while walking around, and an ice troll standing for player 2: no line says it, as nothing logs a body getting up.
      Next: an owner that gets a blow on an NPC already dead sends its death again. Find how the hound died with no line. Log a copy going from lying to standing, so the next session names the actor.
      Bandit CC591 of 10-26 (27 killing blows) may be the same thing; not checked.
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
- [ ] **A revived player's copy still lying down, and below the floor while down** (session 2026-10-30, once). Player 2's copy sat below the floor
      while down (18:26:44, 10 s, its hands at foot level). At 18:26:54 `bleedOutStop` was refused and the fix fired (`not taken; IdleForceDefaultState
      taken`), yet 5 s later it still lay: `Revive: player 2 ... 5 s after the revive`, copy head 27.5 (about 105 standing, under 30 lying).
      The other four revives logged read 101.5 to 114.7, and the owner has seen nothing wrong with revives, so this is the log's word alone.
      Its owner's knock state 5 was copied onto a copy with no ragdoll (`keptMask` in AnimationSystem). `live-revive-copy` does not reproduce it.
      Each 5 s a copy with a VR pose logs `VRBodySync: actor X body -- reference z, 3D root z, head above the root, knock state`.
- [ ] **Player 2's game piles up threads, and they stand in DynDOLOD.DLL** Highest counts: 4959 on 2026-10-24, 2987 on 10-25, 998 on 10-28, 802 on
      10-30, 1195 on 10-31 (205 at 08:06, 1195 at 08:16). `ThreadOwners:` at 08:14 read DynDOLOD.DLL in 23 of 24. Player 1's game stays near 170 to 270,
      on v3.0.43. Cause found 2026-11-01: player 2's FUS profile has Alpha-43 installed (`DynDOLOD DLL NG and Scripts`) but switched off since at least 10-17, and still runs Alpha-32 (`DynDOLOD DLL NG`), as every crash log to 10-31 00:27 shows (`DynDOLOD.DLL v3.0.32`).
      Same session, 10-31 14:10 to 15:06: player 2 went from 221 to 3195 threads, player 1 stayed at 157 to 170. `ThreadOwners:` read DynDOLOD.DLL in 22 or 23 of 24 each time. Private memory is no sign of it: 14.4 to 20.9 GB for player 2, 20.4 to 23.0 GB for player 1. To do: player 2 swaps the two in Mod Organizer; to see: `Health: ... threads` stays under about 300 next session.
- [ ] **A player seen twice at a load door, and gone late** (session 2026-10-31 08:10, player 1 through Windhelm's gate, seen by player 2). Not changed; the
      same removal shows on 10-30, so probably old. Player 2's log: `Door 55FCB: FF0012F7 activated it in another game ...; done here too`, 23 ms later
      `CopyGone: FF0012F7 removed as remote; the actor is still there, no 3D`, then a new copy FF000C25 892 units away (`CharacterSpawnRequest, server id: 1`),
      which stood there until player 1's load ended (`Temporary Remote Held FF000C25`, 10 s later). It should go when the load starts. Open: `live-door-copy-replay`
      says the guard stops a replayed load door, yet this log has no `DoorCopy:` line for that copy. Which is it?
- [ ] **A dead NPC doubled after a reconnect** (session 2026-10-31 08:15, the Old Orc). Not changed; the rule involved predates the commit of 2026-10-31. One of the two
      copies fell through the floor for player 2; player 1 sees both. Player 2's game made the temporary NPC (FF0012D4, server id 100073). At player 2's disconnect it
      went to player 1 (`Gained ownership of actor 100073`). On reconnect player 2's game announced its own again (`Temporary actor FF0012D4 was made by the game, not by
      this client; kept on connect`) as a new character, 200008, so each game shows a copy of the other's. A temporary form cannot be matched between games by id.
      Again 2026-11-14 15:39:58 at player 2's rejoin: each game had made its own Imperial Captain and soldier in Helgen Keep (FF ids), each announced its pair, so each game had four.
- [ ] **Body flickering and head missing for a few seconds after a respawn** (session 2026-10-31 08:00, player 2 respawning, an older bug by the players' account).
      Not looked at yet.
- [ ] **`SkyrimVR.exe+0x117B10`, probably moved to `+0x11E941`** A garbage pointer under Skjor's sword in Whiterun, 5 s after this game spawned him
      (1 report, v1.9.6, 2026-10-29). `+0x11E941` on v1.9.7 has the same machine, actor and sword on the stack (hub, 2026-10-31), so it
      probably moved rather than stopped. The two games had Skjor under two server ids, and his AI asked to unequip 150 times a second. Not
      proven; needs that player's `crash_UTC_2026-10-29_01-46-45.small.dmp`.
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
      Again 2026-11-14 15:40: player 2's copy of the keep scene (4E1E2) stayed at phase 15, waiting on Ralof, a copy there; the door the host's scene opened never opened for player 2. A relayed stage does not move a scene's phase.
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
- [ ] **What holds a removed copy with 4 handles** (a copy at 2 has been a normal end since build 4df623a: `the game let go of it`, 35 lines in play on
      2026-10-31). `CopyFreedHeld: X freed after 60 s with 4 handle(s)`: 55 of 161 lines before that build, 3 on 2026-10-31, any kind of actor,
      ghost or not. Not seen in the rig's runs.
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
- [!] **MGO crashed at the main menu from v1.9.6-23 on, fix waiting on a play session** (player 2's PC, profile `MGO testing`; rig and one real start of release v1.9.7).
      Cause: `cbp.dll` (CBPC) detours `Main::Update` when the main menu opens, follows a jump at the function's start, and copies the bytes it finds there without relocating them. Since `0dfd9747` our hook sat on that function, so the copy held `HookMainLoop`'s first call, which landed in the heap. In 12 of 12 crashes the slot at `rsp` minus `rip` is that call's displacement.
      Fix: `SkyrimVM64.cpp` swaps the main loop's only call to `Main::Update` (`+0x5B6E8F`) and leaves the function alone. Rig on v1.9.7-11-g83be966c-dirty.13cdf81: past the main menu, save loaded, connected, `live-load` 9 of 9; the live chain read from memory is `hdtsmp64.dll`, `HookMainLoop`, then `cbp.dll` on `Main::Update`.
      Not run: the rest of the MGO scripts (doors, gameplay), which the rig was stopped before, and any FUS start with this build. In play, check on FUS and MGO that a save loads and that a granted sleep's OK button works.
      Build v1.9.7-15-g6826e679-dirty.355ef2d, client and server, deployed to FUS and MGO on 2026-11-01. Unit tests pass, and the rig ran on it on FUS only. Its protocol changed, so both players and the server need it together.
- [!] **A follower left at a city gate** (the rules above, not seen in play). She no longer stands on the other player's own reference: her copy goes when she leaves their range. Moving a reference into the city's persistent cell stays out, as it stood her stuck in Windhelm's gate on 2026-10-31.
      To see: one player takes a follower through Windhelm's gate while the other stays outside. Nothing of her stays behind.
- [!] **Engine Fixes' form cache: build the guard?** `+0x93CE11` and its group, hub #2: 5 reports, last on 2026-10-30, v1.9.6. The cache is suspected, not
      proven. How it can keep a freed form, and a candidate guard for the game's own lookups, are in KNOWN_ISSUES section 6. Not built. A game
      without `FormCaching` is the test that would prove or clear the cache, and player 2's now has it off, so player 2's next crashes are that test.
- [!] **Freeze at a load: the game's allocator purging cells waits for good** (player 1, 2026-10-18 21:29; the same stack as a public player's freeze
      at 08:52 and player 2's of 2026-10-11 21:36). FUS then ran the game's own allocator. Since 2026-10-25 it runs Engine Fixes VR 7.7.1 with
      `bOverrideMemoryManager = true`, which replaces it; whether that ended the freeze is not known. Ralof's `Spawn Actor` in those loads is the
      game building his 3D, not our spawn.
- [!] **A dead copy near the player lost its 3D and was made again** (rig only: `live-death-copy` once in 3 runs on 10-26, `live-body-grab` twice on 10-30;
      none in play). Cause unknown. `CopyGone: dead copy X lost its 3D N units from the player; ...` says more when it comes again.

### Design questions and features

- [ ] **NPCs popping in: which world wins** The spawn path re-enables an NPC the other game has active and moves it here (`Disabled actor is being
      re-enabled`, up to 10-30). The two games' worlds disagree: one game's quest scripts put a group away while the other still had it. A game
      that refused would have NPCs it cannot see attacking its player. A design question, not a bug in that line.
- [ ] **A load door locked here stays shut when the other player opened theirs** (the owner's call, 2026-10-21). A door that does not lead elsewhere
      now opens and unlocks here.
- [ ] **A flying dragon whose owner goes silent or out of range is still handed over and falls** (older code). Freezing it instead needs a decision.
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

### Rig and test tools

- [ ] **A save loaded from inside the rig's game freezes it** (2026-11-14, 3 of 3, connected or not, the 2024 intro save and the most recent one). The same 2024 save loads from the main menu, and in play saves loaded mid-game fine that day. Suspect: DevBench's `load` action since the FUS 5.1.0 move; not looked at. Until then `live-load-other-character` and `DO load` scripts cannot run.
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
      2026-11-20, four scripts that cannot pass as they are (found on 979d41c, the same on c852168):
        - `live-arrow-nock` checks for `arrow put on the string`, a line the code no longer writes, and the copy's bow stays sheathed (`has its weapon sheathed; not attached`);
        - `live-blade` gets no VR pose from the bot (`no VR pose data for remote actor`), so no blade meets;
        - `live-corpse-dragged` can miss its whole-tree line: the line is written once in 5 s, and the bot's first drag pose can land before live-check takes the log mark (red once on 979d41c with the game's lines matching a green run, green on the rerun);
        - `live-scene-dialogue` has never passed, and leaves the player in Whiterun's own world, so the scripts after it find nobody (run it last, or with a fresh `headless.ps1 up` after it, as said above).

## Parked and later

- [ ] **A raised body turns to ash for its caster and walks on for the other player** (GitHub issue 7, 2026-11-19: the joining player cast Raise Dead on a corpse while the host's game ran it; Realm of Lorkhan's necromancer class; version not given). Parked 2026-11-20: not reproduced, and the reporter cannot be reached; no fix without a reproduction.
      From the code: a spell the local player casts on a copy is applied here and also sent to the other games (`ShouldSync` holds back summons, invisibility and wards, not reanimation), so the body's own game raises it too. That game then reports it alive, and this game resurrects its copy to match.
      Rig, 2026-11-20, `Tools/Flat/cross-raise.py` (flat game leads and runs a dead skeever, the VR player raises its copy with Raise Zombie by RemoteCast; flat client and FUS client on v1.9.81-27-gc64bd5dd-dirty.c852168): NOT reproduced. Both games showed the skeever raised for the 12 s watched; the flat game's `is now alive` came about a second after the cast, the copy was resurrected mid-raise (`set alive by its owner`) and stayed raised.
      Seen on the way: `BodyStand:` takes the health off a body being raised, as Papyrus counts it dead for its first second (a local raise went from 15 health to 0), so a fresh zombie is left at 0 health; it stayed up on the rig, and whether that plays any part is not shown.
      Not covered by the rig: two VR games over a real connection, a human corpse, Realm of Lorkhan's scripts (a summon limit of three) in both games, a fight around the raised body, the release v1.9.81. What would bring it back: both players' `tp_client.log` from such a session (`Death sync: local actor ... is now alive` on the host, the copy's lines on the caster).
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
- [ ] **A menu inside the game, for players without SteamVR** (asked 2026-11-17; decided the same day: build the full in-game menu). Players on OpenComposite have no SteamVR dashboard, so no join, "Join a friend", chat, settings, players or party page.
      Suggested: keep the dashboard for SteamVR and add an in-game panel showing the same page (M–L, a new feature: after the polishing phase unless decided otherwise).
      Where to draw it: an ordinary overlay moved every frame (needs the game's D3D11 device, which the VR client does not take today, `BSGraphicsRenderer.cpp:112-146`), or drawn into the eye images at the compositor's `Submit` (any runtime; upscalers and frame generation may run after it).
      Input: our own laser from the controller pose, events into the page as `VRDashboard.cpp:321-346` does; the hard part is keeping the game from swinging or casting on the trigger while the player aims at the panel.
      Text: a keyboard on the page itself, plus OpenComposite Unleashed's keyboard characters (`WM_OC_CHAR`) in `InputService::WndProc`.
      Cheaper steps: chat as HUD lines and a controller combination for connect, disconnect and rejoin (S); an MCM page for rejoin, Steam friends and settings (M, an ESP change, a second UI).
      Decided 2026-11-17: the full menu in the game, beside the SteamVR dashboard, which stays; it must work for flat players too; the game does not pause while it is open, and the game's input is held back instead; one shared logic with the dashboard, in services, no logic written twice, well tested.
      Still open: how it opens (to choose after looking at which controller combinations a big modlist such as MGO leaves free), where it sits.
      2026-11-17, build 391a228 (client and server on FUS, flat client on the flat rig; no protocol change; rig only, not seen in play):
        - the connect, launcher-link and Steam friends services now run on flat too, with the flat game's own folder (%LOCALAPPDATA%\SkyrimTogetherSE); flat F6 and the menu's own connect form go through the connect service, which then reconnects to that server after a drop;
        - one place opens and closes the menu on every surface (`MenuService`: SteamVR tab, flat F2/Esc, the in-game panel to come, other plugins through `urSovngarde_SetMenuOpen` / `urSovngarde_IsMenuOpen`); the page is told where it is shown;
        - rig: `live-launcher-join.py --flat` stopped at the code field on 506ac4c and passes 8/8 on 391a228; `live-menu-open.py --flat` had no menu calls on 506ac4c and passes 7/7; a real F2 and Esc close the open flat menu; VR `live-launcher-join.py` 8/8 and `live-steam-join.py` 6/6 unchanged; in VR the menu call is refused until the panel is drawn;
        - measured for the panel: each frame the game submits one double-wide eye texture (2224x1112 on the rig, RGBA8, render target, no depth) on its own D3D11 device, twice per frame, from the main thread in game.
      2026-11-17, build 01ee381 (with the flat/VR damage rule, a protocol change; client and server on FUS, flat client on the flat rig; rig only, not seen in play):
        - the game's own input is held while the menu is open, without a pause (`GameInputHold`, on the poll's call that hands the events to the game, VR +0x81, AE +0x7B): no press reaches the game, sticks reach it at rest, and a button pressed in the menu counts only once let go; HIGGS and PLANCK read the controllers themselves and are not held;
        - rig: `live-menu-hold.py --flat` red on ead49b5 (W walked the player 530 units with the menu open), green on 01ee381 (0 units open, 527 and 525 closed); in VR input passes through it (468 units), the menu itself waits for the panel;
        - the flat test rig no longer takes the mouse and keyboard of the person at the PC: it connects by itself, hands the focus back when its game opens, and a game it starts (`URSOVNGARDE_TEST_RIG`) takes DevBench's keys in the background while the real keyboard and mouse are not read; a gamepad still is.
      2026-11-17, build d984bba (client and server on FUS, same protocol as 01ee381; rig only, not seen in play):
        - the page is one picture source (`VRPage`) shared by its presenters, with one input path (`PageInput`) and one place for the VR runtime's interfaces (`VRRuntime`); the SteamVR dashboard is now only its presenter, its behaviour unchanged by reading (it pauses the game, so the input hold is off for it); the rig cannot open the dashboard, so the headset is its test: open the tab, click, type, close;
        - the panel inside the game (`VRPanel`, `VREyeDraw`): the page drawn into the game's own eye images just before they go to the VR runtime, on the game's device, both eyes at the left eye's Submit, mipmapped; fixed to the head for now (1.4 m wide, 1.25 m ahead, a little below the eyes); shown only once everything was made on the game's device;
        - rig: `live-menu-open.py` in VR had the panel call refused on 01ee381 and passes 10/10 on d984bba (opened on the panel, the page renders, the left eye's pixel under the panel centre went from the game's 39,35,34 to the page's 18,21,24); `live-menu-hold.py` in VR 4/4 (0 units with the panel open); `live-launcher-join.py` 8/8.
        - to see in a headset: both eyes show the panel at the same depth; text readable; the dashboard as before.
      2026-11-18, build 026adac (client on FUS, same protocol; rig only, not seen in play):
        - a laser from either controller points at the panel: a dot shows where, a fresh trigger pull clicks, the stick scrolls, the other hand's pull takes the pointer over (that pull only switches); the controllers come from the game's own nodes and devices (`VRControllers`), so any runtime and the rig's controllers work; the grip count for giving up now counts per hand;
        - rig: `live-menu-laser.py` red on 4932966 (no pointer, no click, no scroll), 5/5 on 026adac: aimed at the centre the page got the pointer within 10 page pixels, one pull is one click, the stick scrolls, a pull on "Join a friend" opens its panel;
        - measured: the game's wand node points along its +y; about 74.5 game units to the metre on the rig, while the laser assumes 70 times the headset node's scale (1.0), hence the small offset; not verified: the left-handed setting, a Vive wand's resting thumb on the touchpad scrolling, the hover left on a button when the laser leaves the panel.
      2026-11-18, page only (UI on FUS, client 026adac; rig only, not seen in play):
        - a keyboard on the page for the panel inside the game (`app-keyboard`): it comes up while a text field has the focus on the panel surface only, types where the caret is (letters, digits, a symbols page, shift for one capital, space, backspace, Enter as the real key, close), and its keys never take the focus;
        - rig: `live-menu-keyboard.py` red on the earlier page (the field took the focus, no keyboard came up), 7/7 on the new one through the laser: a pull on the chat field, pulls on h and i type "hi", the field keeps the focus, backspace, close; `live-menu-open.py` 10/10 and `live-menu-laser.py` 5/5 on it;
        - the page's own browser test (`playwright/specs/keyboard.e2e-spec.ts`) is written, but the page's test runner does not start Chrome 157 on this PC, for its older tests too.
      Decided 2026-11-18: the panel opens and closes by holding both thumbstick clicks for a second (free in MGO and FUS; on Vive wands both touchpad presses); the panel's own close button closes it too.
      2026-11-18 evening, client on FUS (sha256 EE41363C, its version string still reads b0e8a40) and the page in FUS's UI folder; rig only, not seen in play:
        - the chord: both thumbstick clicks held a second open the panel, and close it (`VRPanel`); a short press or one stick does nothing; `live-menu-chord.py` red on the build before it (both held opened nothing), 5/5 after;
        - the keyboard's review fixes: it comes up only for a field the player presses (the page focuses the chat by itself on opening), away from that field, with a line showing what is typed (dots for passwords); keys act on the press, show their real case, a double shift locks capitals, sizes no longer follow the text-size setting, backspace on an empty field changes nothing; `live-menu-keyboard.py` 7/7, `live-menu-laser.py` 5/5, `live-menu-open.py` 10/10 on it.
      2026-11-18, build afb0604 (client and launcher on FUS; rig only, not seen in play):
        - OpenComposite: the launcher gives the game the game folder's VR file, and on OpenComposite the panel is the only menu; rig results in the OpenComposite entry;
        - the menu does not open over a loading screen; held both sticks where it cannot open, a line on the HUD says so; a thumb lifted for a moment restarts the second, and one hold does one thing.
      2026-11-18, build e850fea (client, server and page on FUS and MGO; rig only, not seen in play):
        - the keyboard's review fixes reached the game's page only now (the page in FUS had been built before them): `live-menu-keyboard.py` 7/7, `live-menu-laser.py` 5/5, `live-menu-open.py` 10/10, `live-menu-chord.py` 5/5 on it;
        - MGO ships OpenComposite Unleashed 4.3.0 ticked, so its players now get the panel instead of the SteamVR tab; 4.3.0 is untested.
      Still open: the chord's stick presses reach the game during its second (a sprint on FUS's left stick); the laser's 70 units to the metre against the 74.5 measured; the left-handed setting; Vive touchpads, WMR sticks and Index buttons unverified.
      Decided 2026-11-18 after the first headset use: a visible beam from the pointing controller and a marker for the hand, drawn over the panel, as SteamVR draws its controllers over its menu; the panel stays in the room where it was opened instead of following the head.
      2026-11-18, build 1ce3164 (client on FUS and MGO; same protocol; rig only, not seen in play):
        - the panel opens in front of the headset, level, and stays in the game's world while the head moves; it comes back in front when the player ends up over 3 m from it (a door);
        - a gold beam from the pointing controller to where it lands (2 m ahead when it misses), a round dot there, and a pale ring at each hand, drawn over the panel; each checked once per game by reading the eye image's pixel back;
        - back on the panel, the page is told where the pointer is even for the pixel it last had;
        - rig: `live-menu-room.py` red on fdb6f08 (0/4: the panel turned with the head, no beam, no ring), 4/4 on 1ce3164 (head turned 40 degrees: aimed where it opened, the pointer at the page centre; aimed where a head-fixed panel would be, none; beam 200,169,110 and ring 235,235,235 in the eye image); the re-entry check red on 50ac9cf (0 moves), green on 1ce3164; `live-menu-laser.py` 6/6, `live-menu-keyboard.py` 7/7, `live-menu-open.py` 10/10, `live-menu-chord.py` 5/5;
        - the tests open the panel while DevBench holds the headset at 1.6 m, as the panel stays where it opened.
      2026-11-19, build daa699d (client on FUS and MGO; same protocol; rig only, not seen in play): the reviewer's fixes.
        - the panel can open only while the headset's node reads (otherwise the menu would hold the game's input with nothing shown; a guard, the rig cannot make the node fail);
        - a controller behind the panel points nowhere; opened looking straight up, the panel goes ahead, not behind;
        - each mark's pixel check tries again until its colour shows, 20 tries at most; the node checks are atomic;
        - rig: `live-menu-room.py` red on 1ce3164 (4/6: from behind the pointer reached the page; looking straight up the panel went behind), 6/6 on daa699d; `live-menu-laser.py` 6/6 (the re-entry check now needs both moves), `live-menu-keyboard.py` 7/7, `live-menu-open.py` 10/10, `live-menu-chord.py` 5/5.
      2026-11-19, build f9bcc1a (client and server on FUS and MGO; same protocol; rig only, not seen in play): the readability pass over this month's VR files, meant to change no behaviour.
        - every function documented in its header and .cpp, long functions cut into named steps (the SteamVR tab, the panel, the eye drawing), shared helpers for what several files did their own way (readable memory, finite numbers, game-to-glm, module sections, vectors in log lines, graphics object release, the panel colour over a pixel), the shaders' colours written from the C++ constants;
        - edges that do change: the tab's setup no longer leaks a device or picture when a later step fails and it tries again; the panel reads the headset once per frame; the view check for posing copies retries its headset lookup instead of giving up for the session when the first lookup came before the player's VR body; broken headset numbers count as out of view, as before the pass;
        - two reviewers read the whole set; their findings were applied before this build;
        - rig: `live-menu-open.py` 10/10, `live-menu-laser.py` 6/6, `live-menu-room.py` 6/6 (beam 200,169,110 and ring 235,235,235 drawn from the shader's written colours), `live-menu-keyboard.py` 7/7, `live-menu-chord.py` 5/5, `live-hands` ok (16 hands, median 0.0 units), on 39bfe66 and again on f9bcc1a.
      Next: the owner's headset check of the beam, the rings and the panel staying put; then the SteamVR tab (refactored).
- [ ] **VR FPS Stabilizer crash at loading screens** (seen 2026-11-15, on the rig). The game closes in VRFpsStabilizer.dll+0xFFF6, on that plugin's own thread, reading 0xFFFFFFFFFFFFFFFF, during a loading screen.
      Five times in the client logs we still have: 11-13 21:25, 11-14 03:53 and 10:07, 11-15 16:41 and 17:24, all on rig runs, with and without a connection, on several builds; not in our code.
      A sixth on 11-19 21:29, on the rig while the start save loaded (build f9bcc1a): the same address and read of 0xFFFFFFFFFFFFFFFF, only that plugin's thread on the stack; the next start worked.
      The plugin ("VR FPS Stabilizer" 1.4.13) has been in the FUS list since at least December 2024; our logs only reach back to 11-13. Its author's pinned note: the plugin reads SteamVR's frame timings all the time, and faults when they go away, so its fault is usually the result of another crash.
      Here it was the first fault (our handler caught nothing earlier), and SteamVR still had the game: at 17:24:58 the game faulted, SteamVR lost it at 17:25:04, after the dump.
      All five were rig runs, with the rig's null headset, whose SteamVR statistics count every frame as dropped (1525 of 1525 at 17:25); the plugin reads those timings. Probably rig-only; none seen in play.
      Not looked into: whether turning the plugin off in the rig's runs stops it.
- [ ] **A flat (non-VR) player with VR players** (asked on Reddit, 2026-10-09). Possible in principle: the repo still
      builds the flat client, and `Code/encoding` has no VR-only message, so both builds share the protocol and the
      server. Missing: (1) the VR player on the flat screen -- VR swings are not animations, and the poses we send
      (`VRPose`) are applied only in VR builds, so the flat side would pose arms and weapon from them, the bulk of the
      work; (2) form ids meaning the same thing on both sides (`SkyrimVR.esm` against the Creation Club files, VR-only
      plugins), with a rule for the expected differences; (3) a flat block counted in the defender's rule, which knows
      only VR blade contact; (4) the flat build untested since the fork, testing needs a Skyrim SE copy. Only once VR to
      VR is solid.
      2026-11-15: a flat rig, `Tools/Flat/flat-rig.ps1 up|down|status` and `Tools/Flat/deploy-flat-client.ps1`, a Mod Organizer instance of its own named by the `SKYRIM_FLAT_ROOT` user variable, Skyrim 1.7.104, DevBench on port 8922.
      The flat build starts, loads the world and connects to the test server, announcing its NPCs, at 60 fps (build 01b63b9). Its first connect crashed in `BSReadWriteLock` and its sound calls passed SE ids: both now take Anniversary Edition ids in the flat slot, the VR ids unchanged.
      Known on flat: the client reads keys only while its window is in front, and that now holds for DevBench's keys too.
      DevBench's keyboard was not ready because our focus hook took the input poll's first bytes before it; the hook now goes on at the first update, on top of DevBench's (build 6271913, rig: keys walk the flat player, refused on 01b63b9).
      Both rigs at once: `flat-rig.ps1 up` first, then `headless.ps1 up -AutoConnect`, which presses no F6 (the VR game takes F6 whatever window is in front).
      Mod Organizer runs once per PC unless given `--multiple`; both scripts add it only when another instance's runs, and `flat-rig.ps1 down` closes its own.
      A VR game beside the flat one wrote `tp_client_instance_2.log`, so the rig read a stale log; the log slot now belongs to one folder (6271913, rig: the VR game kept `tp_client.log`).
      2026-11-15 probe, one VR rig game and one flat rig game on one server (01b63b9, then 6271913), rig only, not seen in play:
        - both are let in; the server and both games warn that the plugins differ, and the VR game shows its banner;
        - each sees the other where it is, within a unit, 45 ms behind with 3 ms jitter; weapon drawn, equipping, running and the party leader's weather come through;
        - a flat sword hit landed on the VR player (`Defender's rule: a hit of 8 from player 1 lands`), with no blade check, as expected;
        - each game draws the other player's copy crouched, though neither is sneaking: the VR side's behaviour mods add variables to the human graph's list (`ConstructModdedDescriptor`), so the two games read each other's animation values in different slots; the same happens between two players with different behaviour mods; sending the variables by name would fix it;
        - the flat player's swing is not drawn in VR: the game refused `attackStart` on his copy (`ReplayDiag`), its target id unresolved there;
        - on flat, the VR player's appearance names a form from a VR-only plugin and 25 of her items are not found; 20 lock changes on VR-only references fail as errors;
        - NPCs: each side rolled other creatures for the same references (23 ghosts in VR, 36 on flat), carried by stand-ins; no crash, no error beyond those.
      Crouch fix written, not built (protocol change: both players update): a modded descriptor keeps the original graph's variables first, in the original order, with a placeholder where its graph lacks one, and its mod's own variables after them; a game whose list is not as long as the sender's applies only that shared part (`BehaviorVar.cpp`, `AnimationGraphDescriptor.h`, `TESObjectREFR.cpp`).
      Rig check to run: the same two games, the copies standing upright with a weapon drawn, and whether the flat swing then plays in VR (likely the same cause: its target arrives empty, but NPC attacks on player copies play with empty targets too).
      Open from the code: actions carry the owner's raw action, idle and target ids, not server ids, so they match only where both games load the same plugin at the same index; index 05 is `SkyrimVR.esm` in VR and the Fishing Creation Club file on flat.
      A behaviour mod's own variables keep one slot per name its replacer file lists (a placeholder where the graph lacks it), so their slots follow the file, not each game's graph; a short or empty list leaves the slots it lacks alone. Still open: two players whose replacer files differ but give lists of equal length would mix those extra slots.
      Not changed, to watch: in first person a modded human graph not yet patched gets the plain descriptor (`BehaviorVar::Patch`); in the probe the VR player was patched normally.
      The VR player's arms on flat, first version written, not built: the flat client stores the pose and poses the copy's arms, hands and weapon at the frame end (`VRBodySync::SetRemotePose`, `OnFrameEnd`, the three `ClearRemotePose`); on flat only remote players are posed (no NPC bodies, whose placement uses VR's physics); weapon physics, blade contact and nocked arrows stay VR only; the game's camera decides which copies are on screen (`Camera/CameraView.cpp`); the missing-pose warning now fires only for a player who had a pose.
      Unknowns for the rig: whether the game animates the copy again before it is drawn (a one-frame lag or flicker), the flattened bone array on Anniversary Edition (off on flat until measured, since a wrong offset would be written to; fingers keep the animation, and if the flat skin follows that array the arms may not move at all), skeleton mods that turn bone axes differently.
      2026-11-15 rig, builds 435c542 and fd3f66c (protocol change: client and server on FUS, flat rig; MGO not yet), not seen in play:
        - crouch: both copies stand normally with a weapon drawn (VR player's copy on flat in a sword-and-shield ready stance, flat player's copy in VR upright); crouched on 6271913;
        - flat swing in VR: 3 of 3 swings landed and none of the flat player's attacks was refused in VR; every one was refused on 6271913;
        - VR arms on flat: with the rig's headset held at standing height the flat copy's head stood 114 above its root and its hands matched the owner's to the decimal, but the body was not drawn at all; the flat log showed its spine with a rotation the renderer refuses (`rotation row 0.6752`); posing is off on flat again at its one entry (`InterpolationSystem.cpp`, fd3f66c), the rest of the code stays; next: find what corrupts that spine (instrumented build).
        - one rig start crashed in the third-party VRFpsStabilizer.dll while the save loaded (16:41:47); see "VR FPS Stabilizer crash at loading screens".
        - the default live suite on fd3f66c, for the slot order every VR player now uses: 26 of 39 ok in one pass; of the rest, re-run by name, knockdown, protected copy dies, body hold, world object, quest item copy, copy door race and NPC grace outdoors were ok (order-dependent the first time); one batch did not run (the stabilizer crash).
          Not ok, none traced to this change: PvP spell elements (the second spell hit for 40, the test wants 60: its last pass was 10-31, on the old install, so probably the new list's spell values; still red on 2026-11-20, on c8bc2c2 as on c852168), copy bleedout (ok, then no answer: the bot's recording lacked the get-up), load and door state (no answer both times: DevBench timed out while the game's main thread stalled; load did the same twice that morning on older builds), ref enable leader (no answer both times: the bot runs out of time in phase 3; last ok 11-01, old install).
          Not run on an older build, so those last ones are unexplained rather than cleared.
      Seen on the way, not cross-play: the VR game sent 509 lock changes in ten minutes, 66 of them for one reference (20177CA), and did the same with no other player at 12:31.
      2026-11-17 study (read-only) of the full experience, sizes S/M/L:
        - VR body on flat (measure S, fix M–L): both builds hook the renderer's end, but on flat the frame is already submitted there; the bad spine looks like our rotations blended with the animation's. Needs a later hook point or posing through the animation's pose track, and the Anniversary Edition bone array measured.
        - Actions, idles and targets as server ids, not raw ids (S–M, protocol change, helps VR to VR too).
        - Combat (M once designed): flat applies every hit at once; a VR player's held hit can never be dropped by a flat parry (no pose, no weapon body). Owner decides block, parry window, stagger, power attacks, killmoves, and whether mixed PvP starts off.
        - Held flames and beams aim along the body on flat (VR-only aim hook, M); nocked arrows VR-only.
        - Plugins: a by-name list of expected differences (`SkyrimVR.esm`, the Creation Club files, VR-only mods) and a wider difference log than NPCs (S); a flat modlist (L, outside code).
        - Shipping (M): CI and releases are VR-only; the flat client accepts only 1.7.104.0 (`Code/client/main.cpp:12`); `VRConnectService`, `LauncherLinkService` and `SteamFriendsService` are VR-only (`World.cpp:75-79`), so a flat game started by the launcher would not join by itself; flat F6 joins only 127.0.0.1 (`DebugService.cpp`), in every build of ours.
        - Launcher (L, its own repo): a flat/VR choice at first start and a switch in its settings, with each mode's setup kept; about 20 files assume VR. Hosting and relay are unchanged if both clients share the protocol.
        - Smaller: give-up when downed needs both grips (flat waits out the timer), carried bodies differ, plugin cards and the siege meter have no flat screen, a flat player's map pauses their game (untested).
        - Testing: `live-check.py` needs a port and game argument and a short cross check (about 10–15 minutes a run).
      Decided 2026-11-17: two packages, the launcher downloading the chosen one; plugin differences warn, with a list of expected ones left out of the warning; the launcher keeps its name, its texts worded per mode; flat and VR players cannot hurt each other until the combat rules are designed (fighting NPCs together unaffected).
      Still open: the combat rules; when (a new feature, after the polishing phase).
      2026-11-17, actions as server ids (step 1): parked, no red. Rig on v1.9.81-22-gc0257cb2 (flat rig and VR rig, one server), not seen in play:
        - The DLC order differs: FUS loads Dragonborn at 02 and Dawnguard at 04, flat 1.7.104 the other way round; HearthFires is 03 in both. So a Dawnguard or Dragonborn form crosses as the wrong one.
        - Each player played Dawnguard's `IdleDialogueWelcomeHandGesture` on itself; a bot saw all three actions go out each way, its id naming a Dragonborn landscape record in the other game.
        - Yet the flat player's copy in VR raised its hand 30 units for it (55 for Skyrim.esm's `IdleWave`): the replay carries the idle's event name, and the game plays that when the idle id names nothing usable.
        - In the owner's VR play logs every refused replay had its action and idle resolved; none was refused over an id.
        - Still wrong, not shown in play: an action from a plugin at another index comes back empty (Dawnguard's Vampire Lord actions between flat and VR, the likeliest real red if anyone transforms); a target on another player's copy, or on the player, names another actor or none (an NPC's target 14 resolved to the receiving game's own player); an idle id that happens to name another idle plays that one.
        - Rig check: `Tools/Flat/cross-idle.py`, the copy's hand rise against a wave from Skyrim.esm; only the flat-to-VR half runs, since DevBench's Papyrus on flat has no NetImmerse functions to read node positions.
      2026-11-17, no damage between a flat and a VR player (step 2, the owner's decision; a protocol change: every player and the server need the same new build). Build v1.9.81-22-gc0257cb2-dirty.01ee381 on FUS (client and server) and the flat rig; MGO not yet. Rig green, not seen in play:
        - `Tools/Flat/cross-pvp.py` 2 of 2: a Fireball each way took no health (the attacker logs `PvP: no hit between this player and copy ...: its player plays ...`).
        - `live-pvp-spell-perks` ok: two VR players still hurt each other.
        - A bot started with `--flat` hit the VR player with a spell and a weapon; the VR game took neither (`PvP: a hit of 30 from player 4 not taken`).
        - Each game says when connecting whether it is flat (`AuthenticationRequest.IsFlat`); the server's player list names the flat players (`NotifyPlayerList.FlatPlayers`), and a party kick now sends the full list, the kicked player still in it.
        - One rule, `PvpRules`: two players may hurt each other when the server's PvP is on, never when one plays flat and the other VR. Every check that read the PvP setting asks it: a hit sent by the attacker, another player's hit landing here (replayed arrows included), a spell's damage and a remote caster's effects. The defender also drops a relayed hit from a player it may not fight. Heals, buffs and fights against NPCs are unchanged.
        - Red on 391a228 (both rigs, the owner's server has PvP on): the flat player's Fireball took 65 health from the VR player, the VR player's 98 from the flat player (`Tools/Flat/cross-pvp.py`).
        - Not covered by the rig check: an arrow between the two (the defender's own game decides it), melee swings.
        - Seen on the way, not changed: a spell with any restore-health effect counts as healing, so its damage effects pass every PvP check.
      2026-11-17, the VR player's body on flat (step 3), measured on a flat build with a TEMPORARY probe (flat rig only, not seen in play):
        - With the VR rig's headset at standing height, the posed copy is drawn on flat; the spine stayed a true rotation in every sample, so the 11-15 "body not drawn" did not come back.
        - With the VR hands held above the head, the flat copy raises both arms when its bones' world transforms are written; written as locals alone the animation wins.
        - Each next frame the game puts the animation's transforms back and ours are written again at the frame end: a one-frame lag or flicker is possible and only play or a frame-timed capture can show it.
        - The flat bone array (needed for fingers) is at `BSFlattenedBoneTree`+0x130, its count at +0x128; still off on flat.
        - Why the 11-15 "body not drawn" did not come back is not known. Candidates: the animation-slot fix since then, or the headset's height in that run. If it comes back in play, the probe's numbers above are the reference (spine rows 1.000, world back to parent times local each frame).
      2026-11-17, the VR player's arms on flat, posing on for good (InterpolationSystem calls `SetRemotePose` on flat too; the probe removed). Build v1.9.81-22-gc0257cb2-dirty.fd913d1 on FUS (client and server, same protocol as 01ee381) and the flat rig; rig green, not seen in play:
        - `Tools/Flat/cross-pose.py`: the VR player's weapon sheathed and hands raised from the sides to above the head at 1 m/s; the flat copy's raised arms changed 19.9% of the picture above its head, against 3.8% in the following second. RED without the change: 8.1% against 9.4% (the copy kept the animation's arms).
        - On flat only another player's copy is posed; an NPC body's pose stays unused there. A knocked-down remote player in its ragdoll is still posed, as in VR.
        - Still open, each its own item: a one-frame lag or flicker (the game puts the animation back each frame and ours is written at the frame end; only play or a frame-timed capture shows it); fingers through the flat bone array; the copy keeps the VR player's body size (`HasScale`), as in VR.
      State on 2026-11-17 at the stop: steps 2 and 3 uncommitted, fd913d1 on FUS, MGO and the flat rig; next, the flicker check and fingers on flat, then step 4 (the plugin rule).
      2026-11-18, the flicker check of the VR player's pose on flat (rig only, flat fd913d1 and VR 4932966, not seen in play):
        - `Tools/Flat/cross-pose.py --frames 30`: 30 pictures of the flat copy at no fixed moment, the VR hands held above the head. None showed the game's own stance, and the flat game posed the copy at every frame end in that time.
        - The pictures differ among themselves: 19 with the elbows bent, 9 with the arms straight up, 2 bent forward at the waist. The VR rig's own sender logged 12 to 26 bone steps of 45 degrees or more per 30 s (`PoseFlipDiag: local player 14`), up to 180 degrees at the head and an upper arm: the rig's synthetic controllers flip its VR body, and the copy shows those poses as sent.
        - About twice in 10 s the flat game leaves the copy unposed for one frame end, its bone check refusing a bone (`has a bone the renderer cannot use`). The line now names that bone: on b0e8a40 the right upper arm (a rotation row 1.10) and the left hand (rows 1.05, 1.31, 0.61, scale 0.85), so a bone's world is sometimes a blend of two rotations, not a rotation. Same rate before the fingers change; cause not looked at.
        - Limits: 30 frames rule out a pose dropped every other or every third frame, not one dropped now and then. No picture of the copy unposed can be made beside them: DevBench's free camera also drives the game's view check. `--frames` now judges by the flat game's own count of unposed frame ends; not run in that form yet.
      2026-11-18, fingers on flat (build v1.9.81-22-gc0257cb2-dirty.b0e8a40, flat client on the flat rig, client on FUS, same protocol; MGO not updated; rig only, not seen in play):
        - The flat bone array is read at `BSFlattenedBoneTree`+0x130 with its count at +0x128, with VR's plausibility check; the copy's fingers and flattened bones are then posed as on VR.
        - `Tools/Flat/cross-pose.py --fingers`: RED on fd913d1 (`no usable flattened bone array`, `finger bones not found`), green on b0e8a40. A close picture of the copy's right hand shows the fingers changed from the animation's to the VR hand's.
        - The rig's controllers cannot curl the VR player's fingers (no button, touch or trigger axis moved them), so a closing hand on flat is not tested; only a real VR player can show it.
        - Reviewed 2026-11-18 evening: no fault in the C++. The checks could pass without measuring: `--frames` passed on a copy never posed (no unposed count is written then), `--fingers` passed on missing warnings alone. Now `--frames` also needs the flat game's posed-copy lines in that time, and `--fingers` needs a new one-time "fingers posed" line from the flat game and no "local finger bones not found" from the VR game. The bone warning says "rotation columns" (what it measures). Built in 1ce3164 (the menu lane's build from the shared tree): client on FUS and MGO, flat client on the flat rig. Both checks still to run once, when nobody plays.
      State on 2026-11-18 at the stop: b0e8a40 built and deployed (flat rig, FUS client), uncommitted; next, the reviewer on today's changes, then step 4 (the plugin rule).
      2026-11-18, flat packaging (step 6). Build v1.9.81-22-gc0257cb2-dirty.fabd6b3 on FUS (client and server) and the flat rig; MGO not yet; rig only, no real install from a zip yet:
        - The flat client is `urSovngardeSE.exe` (product name urSovngarde, described as the Skyrim Special Edition co-op); `SkyrimTogether.exe` is upstream's program. The rig scripts, `update.ps1`, `setup-connect.ps1` and `collect-logs.ps1` know the new name, and the old one where a folder may still hold it.
        - `make-release.ps1 -Flat -ClientFolder <flat client folder>` makes `urSovngarde-SE-<version>.zip` ("urSovngarde SE", "urSovngarde SE mod", "Server") and `-update.zip` (exe and pdb). The full SE zip has no pdb, so it stays smaller than VR's (about 170 MB against 175 MB compressed on fabd6b3; a first count of 186 MB for VR included old builds set aside in the FUS folder, which the script now leaves out), and launchers before this change still pick the VR zip as the biggest. The release workflow builds both and fails if the SE zip is not smaller.
        - Rig: the renamed client starts under Mod Organizer, loads the start save and connects; with the VR rig on the same server `Tools/Flat/cross-pvp.py` passed 2 of 2.
        - Seen once, not the rename: the first flat rig start timed out because loading the start save became a new game (the Helgen cart, the game's new-game message and never a save load); the next start loaded it normally.
        - The launcher's install of the local SE zip (its own repo, test folders only): into a Mod Organizer set up for Skyrim Special Edition (531 files, mod "urSovngarde SE" ticked, SkyrimTogether.esp enabled, executable "urSovngarde SE") and into a plain game folder; it reads the same build and protocol from urSovngardeSE.exe as from VR's exe; a VR zip into a flat setup and the reverse are refused before anything is written.
        - A join through the launcher in flat mode, rig only: the launcher (flat mode, connect automatically on) wrote `%LOCALAPPDATA%\SkyrimTogetherSE\connect.txt` as address, an empty password line and `autoconnect=on`. The flat game started with `flat-rig.ps1 up -KeepConnect` connected by itself from that file to the test server (e850fea, same protocol). The launcher's own reader then saw it connected and read the build from urSovngardeSE.exe.
        - Not done: the launcher starting the flat game itself (the rig's Mod Organizer is set up by hand); a join by a friend's code through the relay on flat; the READMEs in the SE zip still speak of VR; no tag has built the SE zips on GitHub yet.
      2026-11-18 evening: build v1.9.81-22-gc0257cb2-dirty.fdb6f08 (same protocol 2573329c, made from the shared tree) is on FUS and MGO, client and server, so MGO now carries steps 2 and 3, the fingers and the rest above; the flat rig keeps fabd6b3. Not seen in play.
      2026-11-18, each game's own plugins left out of the NPC-plugin warning (step 4, the owner's 11-17 decision; server only, no protocol change). Server v1.9.81-22-gc0257cb2-dirty.c6a0f4e on FUS (after the reviewer's fixes: the pair rule in `PluginRules::FilterForPair` with same-game tests, the server's "left out" line only beside a warning sent, the check reading only lines after each game's newest connection); MGO's server not yet; rig only, not seen in play:
        - Between a flat and a VR player the server leaves out SkyrimVR.esm on the VR side and flat Skyrim's free Creation Club files on the flat side (`_ResourcePack.esl`, Fishing, Saints & Seducers, Rare Curios, Survival Mode), and logs what it left out. Between two players of the same game every difference stays, so a VR player's ported Creation Club file still warns. The rule is its own file, `Code/server/PluginRules.*`, with unit tests (`Code/tests/plugin_rules.cpp`).
        - `Tools/Flat/cross-plugins.py`: RED on the server before (19:14 run: the warning named SkyrimVR.esm, Fishing and Saints & Seducers; the flat player was told "only you have" the two Creation Club files), green on 6d203b3 and again on c6a0f4e, 4 of 4 each (the flat player sees "31 only there, 0 only here", the VR player "0 only there, 31 only here", the server names what it left out).
        - The VR rig's FUS mods still warn, as they should: a flat player needs the same NPC mods. A flat modlist is still open (the owner's call).
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
