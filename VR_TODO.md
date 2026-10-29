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

## Waiting on a test or action

- [!] **Unpaused skills and map menus (contributed, merged as 779d7a82), hardened** (build 779d7a82 + the hardening,
      FUS and MGO folders, client and server; a protocol change: `ServerSettings` gained `UnpausedMenus`, so both
      players and the server need it). With `Gameplay:bUnpausedMenus=true` (off by default) the skills menu and the
      world map do not pause the game while connected. While connected, every menu that runs unpaused (inventory,
      favorites, container, message boxes, tutorials, console, and these two) blocks walking, turning, jumping,
      sneaking and melee swings, setting on or off: decided so, as a paused menu would.
      - Hardening: the map's four hooks and the laser update it calls are byte-checked first (prologues read from the
        running game); on other bytes none is installed and the map pauses as in the game (`Map menu: code found; ...`
        or `Map menu: ... is not the expected code`). The laser call and the frame-parity revert run only while the map
        is open unpaused, as the sky-job skip already did. Both menus clear their model's kIgnoreFade before the game
        handles the hide, which may free the model.
      - Rig: setting off: both menus open paused (no `runs unpaused` line). Setting on: 3 map and 3 skills openings,
        each closed with Escape or a bare kHide, all ran unpaused at 60 fps and the game kept running. Before the
        hardening, on the merged code: 10 of 10 the same, with the skill tree and the map drawn right in pictures.
        The 2026-10-18 map-close freeze (2 of 2, a bare kHide, Community Shaders 0.8.7) no longer reproduces on CSX.
        Not tested: the map closing when health drops, the controls block, the other player's view, fast travel from
        the map, a headset (the laser). To see in play, with the setting on: open both menus while the other player
        moves; neither freezes on close; the map closes when you are hit.

- [!] **Freeze 2026-10-25 14:51: the autosave entering the Palace of the Kings never finished** (player 1, a98ad845;
      fixed on a98ad845 + the ExtraDataList lock change, client only, FUS and MGO folders, client and server).
      Seen live, with the frozen game's memory read (no debugger): the main thread waited in the save
      (`BGSSaveLoadManager::ProcessEvents` > `Save`; `Autosave1_..._WindhelmPalaceoftheKings_...ess.tmp` stayed 0
      bytes); the save waited for the background loader, and the loader spun in `BSReadWriteLock::LockForWrite`
      (`+0xc4222b`) inside `TESObjectREFR::Load3D` of our player marker 8D005040, moved there 0.5 s before
      (`Player markers: 8D005040 follows player 2 ..., whose body is here`). That marker's ExtraDataList lock read
      writer 0, count 1, with no thread inside it: a count nobody would ever give back.
      - Cause: our `ExtraDataList::GetByType` and `Add` locked the list with `BSRecursiveLock` (a spin lock: count
        0 to 1), but the game's ExtraDataList lock is a `BSReadWriteLock` (read from the game's code: a writer sets
        0x80000001 and its thread id, a reader adds 1 whenever 0x80000000 is clear). So game readers came in while we
        held it, and when one came in between our unlock's `m_tid = 0` and its compare-exchange (1 to 0), the exchange
        failed and our count stayed. The player markers read their marker's list every 2 s from our update thread,
        while the game loads that marker's 3D on its loader thread. Upstream has the same code.
      - Fix: ExtraDataList takes the game's own lock functions, `LockForRead`/`UnlockForRead` in `GetByType`,
        `LockForWrite`/`UnlockForWrite` in `Add` (SE 66976, 66982, 66977, 66983; their VR addresses checked against
        the game's code). `BSRecursiveLock` stays for the spin locks it matches (animation graph manager, cell, actor).
      - Test: the race is microseconds wide, so no rig test makes it fail on demand; the evidence is the frozen game's
        state and the game's lock code. TPTests 94 cases. Rig: `live-map-marker` 1 of 1 (a regression check, not
        red/green): marker 8D005040 followed the bot, went on the map's list, came off and was put away, no freeze.
        To see in play: no freeze after a load with the other player's marker near (a rare race, so only many loads
        will tell).

- [!] **Party sleep (contributed, merged as cc8c5ccf), hardened** (build b335f22, FUS folder, client and server; a
      protocol change: both players and the server need it). On a private server with `bEnableSleep=true`, a party
      member's sleep or wait moves everyone's clock and date; the rest of the party watches with their controls locked.
      - Hardening: the setting is off by default (server code and `STServer.default.ini`); the FUS server's own
        `config\STServer.ini` has it on, to try. The VR Sleep/Wait menu runs unpaused only while connected to a server
        that allows it; otherwise it pauses as in the game, and the game's main-thread check in its ProcessMessage is
        left as the game has it (switched in one aligned 2-byte write, only while no Sleep/Wait menu exists:
        `Sleep/Wait: this server lets a sleep pass time, so the menu will not pause` / `... the menu pauses as in the
        game`). The menu's ProcessMessage address is now checked against the VR address library at start-up.
      - Why the switch: with the check removed for good, as merged, a paused menu's show and refresh messages ran
        on the menu thread instead of the main thread (2 of each opening, measured in the rig).
      - Rig (DevBench opens the menu): sleep off: pause counter 1, the server refuses; sleep on: pause counter 0,
        no refusal; after a disconnect the check comes back in 17 ms and the menu pauses again. TPTests 94 cases.
        Not seen yet: a real sleep (choosing hours and OK need the headset), the party watching, the clocks after.
        To see in play: sleep 8 hours with the other player near; both clocks and dates move, the other player's
        controls come back at the end, and nobody's game crashes or hangs in the menu.

- [!] **A revived player's copy still lying down on the other game** (a player's report on the v1.9.6 release,
      2026-10-24). The reviver's log shows it: after the second revive (21:45:16) `ReplayDiag: refused 'bleedOutStop'
      on FF000C97`, and the copy's hands stayed at floor height (z -5 to 12, the owner's 55 to 70) for 50 s, until the
      copy left. The fallback that sends a refused `bleedOutStop` to the copy's graph left player copies out, and its
      graph event is never taken in our logs (eight times since 2026-10-14, all NPC copies, all `not taken`). Now a
      player's copy whose graph refuses it too gets `IdleForceDefaultState`, which takes the graph back to standing
      from any state (AnimationSystem); the watching game also logs each revived party member's copy 5 s after the
      revive (TEMPORARY). Rig on cc8c5cc with the fix: `live-revive-copy-stuck` (new) 1 of 1: a copy that refused
      `bleedOutStop` logged `not taken; IdleForceDefaultState taken` and stood; a copy left down (pelvis 26 units over
      its feet, still so 8 s later) stood (64) after the event alone. `live-revive-copy` (new, the plain case) 2 of 2.
      Not reproduced in the rig: how the copy in play went down although it refused `bleedOutStart`. To see in play: a
      revived player's copy stands up on the reviver's screen; read there `ReplayDiag: 'bleedOutStop' refused as an
      action on X; sent to its graph instead (not taken; IdleForceDefaultState taken)` and `Revive: player N '...' 5 s
      after the revive, copy X here: ... head H ...` (H about 105 standing, under 30 lying).
- [!] **Party balance: enemies scale with the party's players nearby** (2026-10-25; client only, no protocol change;
      deployed to the FUS folder on player 2's PC). Counting this player and every party member within 50 m (3500
      units), enemies take 1/1.5, 1/1.9, 1/2.25 of this player's damage (as with 150%, 190%, 225% health) and deal
      120%, 135%, 150% to this player; more than four players count as four (`GameplayFormulas::PartyEnemyHealth`,
      `PartyEnemyDamage`, applied in `HookDamageActor`; counted once a second in `PartyService`). No actor value
      changes, so nothing of it reaches a save; hits between players and between NPCs, followers included, stay as they
      are. Each game scales only its own player's hits and the hits on its own player, so until every player has the
      build, only theirs are scaled. Rig: `live-party-balance` 1 of 1: with the bot near, the player's Fire Storm on an
      NPC did 62.3 against 93.4 alone (0.667), an NPC's on the player 448.3 against 373.3 (1.201). To see in play:
      `Balance: now 2 of the party within 50 m, ...` when the other player comes near and `now 1 ...` when they leave;
      fights tougher without enemies feeling like sponges; the numbers to tune, if any.
- [!] **The menu redesign** (the in-headset menu, UI only, no protocol change; 2026-10-24). Deployed to both tools
      folders on player 2's PC (`UI`; the old menu is in `UI.old-20261024-105922`) and runs with the v1.9.6 client.
      Framed buttons that light up under the laser and answer a press; a status chip (not connected, connecting, or
      connected with the server, the player count and the ping); a connecting card with a timer and a Cancel that
      works; errors replace the window behind them, and a different mod list lists what to install and what to remove;
      the player manager and settings keep Back inside the panel (the Party tab's Back was below the panel's edge and
      nothing scrolled); toasts at the top; Rejoin last server. Settings no longer says `Version is outdated. Please
      update.` (it compared our build with the original project's GitHub tags). Checked in the browser test mode at
      the panel's size and zoom, not yet in the headset. To see in play: open the tab, connect, cancel a connect, open
      Players and Party and press Back on each.
- [!] **Reveal Players** (client; waits for the next client build, not built). The press ran on CEF's thread (in the
      2026-10-18 log `revealPlayers` is on another thread than the game), and its 10 s ran while the headset's menu
      paused the game, so the glow had mostly run out by the time the menu was closed; on VR the menu also went blank.
      Now the effect starts on the game's thread, the 10 s start once no menu pauses the game, and on VR the menu
      stays open. To see in play: press Reveal Players with the other player near and close the menu; they glow for
      about 10 s. Read `Reveal: N of M other players near enough glow for 10 s`; with nobody near, the chat says
      `Reveal: no other player is near enough to glow.`
- [!] **Fixes from the second review of the last two commits** (build 67cefa2, FUS folder, client and server; a protocol
      change in the same build, so both players and the server need it).
      - Close the game in the "VR could not start" dialog ends the game at once with an `Exit:` line. Before, the
        game hung over 90 s with no `Exit:` line. Rig: `vr-start-check -Close` red on 875fd61, PASS 6/6 on 67cefa2.
      - A removed actor is no longer looked up by its id, which could find the freed actor or the id's next one
        (`Discovery: X came back at the same address with another handle` when one is reused). Rig:
        `live-copy-removed` ok.
      - A copy waiting for its 3D keeps a death and the owner's inventory changes that came meanwhile, and takes the
        server's newer state from a re-sent spawn (`Death sync: ... while its copy waits to be made here`,
        `CopyInventory: waiting copy X, ...`). Equipment changes made meanwhile are still not kept unless a spawn is
        re-sent. No rig script makes that wait.
      - An NPC copy dies only on its owner's word: a health value or this player's killing blow leaves it at 1
        health until then (`Death sync: NPC copy X kept at 1 health ...`). Before, a copy could lie dead while the
        owner's NPC fought on. Rig: `live-corpse-owner-alive` ok. To see in play: no NPC copy lying dead while it
        still fights on the other screen, and kills still look right on both.
      - A new copy under a reused id no longer gets up, slides or stays put on the old copy's account.
      - Held quest stages always end: the first done fragment or 2 s ends a stage's wait, 30 s without progress
        applies the rest, and a disconnect, leaving the party or losing the lead applies them instead of dropping
        them (`QuestHold: ...`). The server drops a duplicate stage only within 30 s of the first, so a reload no
        longer loses a stage. A remote quest start at stage 0 now starts the quest. Rig: `questdup` 1/1,
        `live-quest-item` ok; the hold itself needs a real quest scene. Still possible: a branch phase gated on a
        held stage is skipped on the leader.
      - Same round: `live-copy-knockdown` FAILED 2 of 2: the lying copy travels 105 and 163 units, against 0 and 4
        on the 10-17 builds. Not traced yet; nothing of these fixes touched the copy while it lay.
- [ ] **Session B review items left unchanged for lack of evidence** (since 840c92c the client sends movement in
      packet-sized messages through the shared packer, the player's first and the actions before them).
      - An NPC rider sent every 100 ms on a horse sent every 33 ms plays up to 100 ms behind it. To see: a remote
        NPC rider bobbing on its horse.
      - Late actions arriving in a burst under loss may be refused by the game. To see: `ReplayDiag: refused`
        counts in a lossy session.
      - One late point from before a jump empties a copy's buffer. To see: `InterpDiag` jump lines next to a
        snapped copy.
      - Each packet-sized chunk after a frame's first may still straddle two packets: it needs a TiltedConnect
        flag to send each chunk on its own.
- [ ] **Another player's follower left standing in your interior** (review C5/C6, designed, not built). When player 1
      takes Lydia outdoors while player 2 is indoors, player 2's Lydia stays frozen in the inn (an assignment, or a
      refused teleport) or becomes a live, unsynced NPC there (a server removal). Moving her out with LoadCell
      stalled the game 622 ms; disabling her would end up in player 2's autosave. Plan: a removal makes discovery
      announce her again, and the assignment moves a persistent actor into the worldspace's persistent cell at the
      owner's place, with no cell read. Needs a rig test first: player indoors, the bot owning Lydia outdoors.
      The refused teleport keeps today's behaviour: the game then takes her where she stands, which is consistent.
      Ready: `TESWorldSpace::GetPersistentCell` (+0x88, CommonLibVR) and live-check's `ref <hex> cell same|other`.
      To do: on a server removal of a persistent actor, `ProcessNewEntity` announces her again (as the HandedAway
      path does); the assignment branch `Assignment: X not moved ...` moves a non-temporary actor into
      `GetPersistentCell()` of the owner's world space (checked as a cell of it) with `MoveTo`. Rig test to write: the
      player and Lydia in Riverwood's inn (`coc RiverwoodSleepingGiantInn`), the bot follows in, claims her, `stay`,
      `cell out`, `npcmove 0 0 0`; CHECK `ref A2C94 cell other`. Expected red on today's build: she stays in the inn.
- [!] **Upstream's quest improvements, ported by hand** (build 3d08be4, in 67cefa2 with the fixes above; a protocol change:
      both players and the server need it). Ported: e9214a87, 5a72cdcd, 69fe8e67, parts of d2eaf9bf and a3f62ba7.
      - The server relays each quest stage once per party: a stage the other game reaches on its own is no longer sent
        back (server debug line `Dropped duplicate quest stage`). Before, it could push a scene past its own scripts.
      - The party leader's game holds a member's stage while one of its playing scenes still checks that stage, then
        applies it and waits for the stage's scripts (`Holding member quest stage`, `Releasing held member quest
        stages`, `Applying held member quest stage`).
      - Dialogue and subtitles carry the sender's scene phase: a game whose scene is stuck in an earlier phase plays
        the sender's line instead of waiting (`Dialogue: ... in scene X phase N; sent`, `... (sender's phase N) ...`).
      - A scripted slow time (a constant-effect slow spell) no longer reaches the other players.
      - Left out: upstream's "form a party" hint (the party is made at connect here, and its toast is not seen in a
        headset) and the lower level of the ownership lines (our log reading uses them).
      - Rig: pair `questdup` red on the build without the relay-once (2 updates came back), green on 3d08be4 (1);
        `run-all-pairs` 14/14, TPTests 64 cases, `live-quest-item` 3/3. `live-scene-dialogue` missed its 90 s window
        twice on a fresh rig, while the same build logged `in scene 5B1BA phase 0` and `phase 1` a few minutes later.
        Not seen in play. To see: a quest scene with both players near, nobody's scene stuck, no stage played twice.
- [!] **Three fixes from the review of the last 20 commits** (build 1b7b5b5, FUS folder; no protocol change).
      - A full inventory waiting for an NPC to have an AI process here is kept for the copy it came for, and dropped
        when that copy is gone, changes hands, or the game is offline. The owner's later inventory changes are applied
        to it while it waits: before, applying it could bring back looted items or put stale gear in a save.
        Lines: `CopyInventory: waiting inventory of X dropped: ...`, `... an inventory change from its owner applied to it too`.
      - An assignment no longer moves a copy out to an exterior beyond the loaded area, as for teleports
        (`Assignment: X not moved to its owner's place, beyond the loaded area here`).
      - A quiet copy's stop is forgotten when the copy is removed or this game takes the actor over
        (`QuietStop: copy X forgotten while stopped`); before, it could start or stop the next copy given that id.
      - No red runs: none of these states can be made on demand in the rig. Regression on 1b7b5b5:
        `live-temp-reconnect` 8/8, `live-body-take` 3/3, `live-teleport-far-loaded` 5/5, `live-copy-quiet` 4/4.
        Not seen in play.
- [!] **The game started before SteamVR or the headset was ready** (hub crash #1, `SkyrimVR.exe+0xC53E1B`, the most
      common crash: 13 by 2026-10-21, another on v1.9.6 on 2026-10-23). Always the first loading screen, often a few
      seconds after start. Crash Logger: `Unable to initialize VR runtime: VRInitError_Init_NotInitialized (109)`: the
      game's start of OpenVR (`VR_InitInternal2`) had failed, and the game went on without VR until that screen.
      - Now `Code/client/Games/Skyrim/VRStart.cpp` hooks that import. A failure logs `VRStart: OpenVR did not start
        (attempt N): error N, <OpenVR's words>` and shows a "VR could not start" dialog (Try again / Close the game);
        Try again starts OpenVR again in place. Every start logs `VRStart: OpenVR started (attempt N, N s)`.
      - OpenVR's `VR_IsHmdPresent` cannot do the job: it answered true here with no headset, and it started SteamVR.
      - Rig, `Tools/VR/vr-start-check.ps1` (started from Mod Organizer with SteamVR closed and no headset): RED on
        a547eb5 (the crash, reproduced on this PC), 7/7 on 1a569f4 (error 108 Hmd Not Found, the dialog, no crash
        while it waited, Try again with the null headset on started VR on attempt 2, main menu).
      - Build 1a569f4 (FUS folder). Not seen in play. To see: a player who starts too early gets the dialog instead of
        a crash, and the hub gets no `+C53E1B` from builds with it.
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

- [x] **The rig needs its null driver copied back after the SteamVR 2.18.4 update** (2026-10-21 20:42). Done: the
      Devkit's driver was put back on 2026-10-22 and the rig has run every day since. The update
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
      - Measured 2026-10-24 (`Tools/VR/light-compare.py`, build 4ef72be): the mod does not darken the game. Same place
        (the start cell), noon and clear weather (81A), five pictures each: without the mod 130.1 mean luminance,
        with it offline 131.5 (+1.1%), with it connected 130.6 (+0.4%). What players see darker is more likely the
        sync itself: a connected game keeps the server's clock, and a party member gets the leader's weather. The run
        without the mod needs two message boxes closed after the load (missing content; our plugin's "Skyrim
        Together is not running!"), then the move to the start cell; the tool does not do that yet.
      - Was waiting: `Tools/VR/light-compare.py`, for the players' report that the game is darker with the mod.
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
      - Rig, 2026-10-21: `live-door-echo` 6/6. The server logged `New player '<player 1's character>' ... connected with 300
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
      Open: whether a VR player who died ragdolled gets stuck now that the respawn does not knock it; a safe point in an
      interior (not tested in the rig).
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
      on 77e162e the same death-line run also gave "was killed by Netch using Shock Touch" (spell path). The game no
      longer writes the card to `logs\banner-death.png` (it cost a frame at every death). The new checks are new log lines, so the earlier build could not pass
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
      Twice more on 2026-10-26 and 2026-10-27 (v1.9.6, both at the start of a loading screen, KNOWN_ISSUES section 6):
      still waiting on that setting, or on word that it is already off.
- [!] **Player 2's update:** DynDOLOD DLL NG Alpha-43 (Nexus 97720, replacing his `DynDOLOD DLL NG` files) and the host's
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
- [!] **Our browser helper outlived a crashed game and froze Mod Organizer** (2026-10-20 evening). After the MGO
      game crashed at 21:18, one `TPProcess.exe` (Chromium's "on-device model" utility, started 21:04, parent the
      dead game) kept running. It still held MGO's usvfs (0.5.6.1) loaded. FUS's Mod Organizer, started at 22:21, then
      waited for good inside its own usvfs (0.5.5.1) at startup: no window, log stopped after "checking for blocked
      files". A PC restart cleared it. On 2026-10-18 a leftover GPU helper also locked the installer's files.
      - Likely what the Synergy players reported as Root Builder broken after crashes.
      - Now each helper watches the game's process (the `--pid=` the game gives every helper it starts) and ends when
        the game ends, however it ends (`Code/tp_process/main.cpp`; TiltedUI's own `ExitCheck` is commented out
        upstream). `Tools/VR/deploy-client.ps1` now copies `TPProcess.exe` too when it changed (it used to copy only
        the exe and pdb).
      - Rig, `Tools/VR/helper-exit-check.ps1 -StuckMain` (the game killed as a crash ends it, each helper's main
        thread suspended first, as a stuck helper's would be): RED with the old helper (5 of 6 still running 15 s
        later), 6 of 6 ended on 1a569f4. Without `-StuckMain` the old helpers quit by themselves (6 of 6), so a plain
        kill cannot show it. Build 1a569f4 (FUS folder; not MGO yet). Not seen in play: after a crash, no
        `TPProcess.exe` left in Task Manager and Mod Organizer starts.
- [ ] **Smaller items from the first MGO session** (2026-10-20 evening):
      - The Helgen prisoner carriage crashes HIGGS when grabbed (`SkyrimVR+0xADF694`, both games). HIGGS walks the
        carriage's links and reads its anti-tip action (`bhkTiltPreventAction`) as one. Not our code; don't grab it.
        Unproven without our mod.
      - A copy the server removes in a cell the receiving game can't reach left the loaded real reference
        unannounced (Ingrid on player 2's side: `OnRemoveCharacter` strips the components and nothing announces it
        again).
      - Lokir lay dead in player 2's copy while alive in player 1's game; it was corrected at 21:18:43 ("came back
        alive").
      - An ice wolf (33482) bounced owners and its body jumped about 8000 units into Helgen at 21:08; the cause is
        unknown.
      - VR players connect with F6 and never see the connect page's "don't connect during the Helgen intro" warning.

## Open work

- [!] **A protected NPC's copy that its owner says died, and the hand-over after it** (fixed by 10b98ee5, see the
      protected-NPC item: a copy of a protected or essential NPC now dies on its owner's word). On 206a1d36 the rig's
      `live-death-protected` (Lydia) failed 2 of 2: the copy stayed alive (`now dead: false`), and once this game took
      her over it sent `Death sync: local actor X is now alive`, so she lived again for everyone. That test also checks
      the hand-over, which `live-protected-copy-dies` does not. Ok on 317f7ce1 (2026-10-28), hand-over included. `live-death-copy` (an
      unprotected human) is ok on 206a1d36.
- [ ] **A dead copy near the player lost its 3D and was made again** (rig, `live-death-copy`, once in 3 runs,
      2026-10-26): 4 s after the copy died, `CopyGone: X removed as remote; the actor is still there, no 3D`, then
      `Announcing this cell again` and a new copy spawned dead: the body vanishes and reappears. Its pelvis was 15 units
      below its feet in the samples before. Cause unknown.
- [x] **A lock change reached only players in the door's very cell** (sync audit 2026-10-26; rig `live-lock-range`).
      Fixed (server only, no protocol change): `ObjectService::OnLockChange` relays a lock outdoors to every player near
      the sender, as a door's use (`OnActivate`). `live-lock-range` RED on 317f7ce1 (the bot never told of lock 3F558
      one cell over), green on 43e6a70 (dirty, on 317f7ce1; deployed to FUS and MGO, client and server). Not seen in
      play yet. To see: a lockpicked or script-locked door one cell away is locked or unlocked for the other player too.
- [ ] **A door a script opens (`SetOpen`) is never sent** (sync audit 2026-10-26; rig `live-door-script`, failed on
      206a1d36 2 of 2 and on 43e6a70: door 9B0A5 open here, closed for the other player, and no door line in the
      client log). In the civil war, quest scripts open and close most doors. Decision needed on the design (the sync
      audit's proposal: the party leader's game in charge of doors).
- [x] **"A lever pulled by the other player does not move its bridge"** was the test's timing, not a bug: the bridge
      jumps from one end to the other, and the recording started with the pull, so its first sample came after the
      jump. With each recording started 2 s before its pull, `live-lever-bridge` is ok on 43e6a70 (other player's
      pull 3 > 1 here, this player's 1 > 3 and told to the other player). Nothing changed in the lever's path since
      206a1d36.
- [ ] **A copy's body is shifted after it lands** (rig, `live-death-replay`, 3 of 4 runs, 2026-10-27): replayed beside
      its original in one game, the copy's ragdoll lay 69 to 108 units from where its owner's corpse lies and was
      moved there (`CorpseDiag`), the jump seen in play (68 to 220 units); again 76 units on 317f7ce1. It fell 0 to 1.4 s after its death, the
      original 0 to 0.1 s; resting poses differed by 18 to 33 units. Proposals: the sync audit (the corpse's pose sent
      once it rests; the fall streamed).
- [ ] **Public reports 2026-10-29 (v1.9.6, two players together in Whiterun, Quest 3 and Quest 2)**
      - `VCRUNTIME140.dll+0x5052` in our `AnimationSystem::Update` (a copy's action target looked up by id came back as
        freed memory): fixed for all our lookups, `TESForm::GetById` returns null for one that is not a live game object
        (KNOWN_ISSUES section 6). TPTests 95; rig smoke `live-death-copy`, `live-protected-kill` ok, no lookup refused
        there. Deployed to FUS and MGO, client. Reaches public players only with a release.
      - `SkyrimVR.exe+0xD8A96D` (SE 74377), Dragonsreach: a virtual call on freed memory 19 ms after Irileth's copy
        (owned by the other game) used the door 16707 by itself here and was unloaded (`Activation of 16707 by 1A67F,
        another game's actor, not sent`, then `Actor removed, form id: 1A67F`). Not ours by the crash handler (none of
        our deletions). The crashing call is the game's find-node-by-name (VR 0x140D8A900) on a freed 3D root, from a
        game job thread, with a `MovementControllerNPC` and a `MovementMessage` on the stack beside Irileth's copy and
        an Ash Spawn (FF00108F): an NPC's movement still pointed at the 3D the door had just unloaded. No mod's code on
        the stack. The trigger is already refused since build 1a569f4 (2026-10-24, `HookActivate`, `DoorCopy:` line):
        a copy never uses a load door on its own (`live-door-copy-load` red on a547eb5, green on 1a569f4), but v1.9.6
        does not have it, so it reaches these players only with a release. Their logs show how often it happened:
        153 copy activations "not sent" in this player's logs (16707 three times), 24 in the other's. The crash itself is not reproduced in
        the rig. What stays: the owner's own use of a load door is still replayed on the copy, which then unloads
        here the same way; nothing links that path to a crash yet.
      - `SkyrimVR.exe+0x117B10` (SE 11308), Whiterun, the other player: a garbage pointer (0x3F8CCCCD00000001) under
        Skjor's sword, 5 s after this game spawned Skjor (`Spawn Actor: 1A691` at 18:46:45). Before that, Skjor was
        removed here twice (18:40:03, 18:41:22) and in the other game within 2 s each time; the two games had Skjor
        under two server ids (20004D there, 20010A here); his copy activated something on its own 12 times in the
        other game (which were load doors the log does not say); and here his AI asked to unequip 150 times a second
        for three minutes, all refused by the game (`ActionFlood`, not sent). The door churn is what build 1a569f4
        stops; the rest is not linked to the crash. Not proven; needs that player's
        `crash_UTC_2026-10-29_01-46-45.small.dmp`.
      - `skyrimvrtools.dll+0x71B5`: the known load during our death sequence.
- [ ] **Play session 2026-10-28 18:01 (player 1 on 43e6a70, player 2 on 10b98ee5; about 30 min, a fight with archers)**
      - No sync bug seen. The only one: the revive banner ("<name> is reviving you") was cut off at both ends with a
        long name. Fixed: a banner line too long for its picture is written smaller until it fits (`BannerPicture::Line`).
        TPTests `[banner]` new case red before (1466 lit pixels in the outer 40 columns), green after; the revive line
        drawn in the game's font fits with margins. Build ff7f9c8 then c158a0b (dirty, on 87ea449f), deployed to
        FUS and MGO, client. Not seen in play yet.
      - Player 2 crashed at 18:29:41, 27 min in, at Lost Knife Hideout (report 20261028-172952-37821e78): a Havok island
        waking up (`FOIslandActivationListener`, a Steel Arrow on the stack) met a ragdoll body
        (`bhkBlendCollisionObject`) whose actor was freed (SE 36492 called on it, its vtable read -1). The family of
        KNOWN_ISSUES "Crash within a second of travelling away from copies this client made", without a loading screen:
        2 ms after this game had made five copies at once on a grid change. The freed actor (0x279507000) sits between two
        copies this game had deleted 6 to 7 minutes before; not in `RecentDeletes`' last 28. Our own risk beside it:
        `CopyFreedHeld` (a copy freed after 60 s with the game still holding it), twice 36 s before the crash, 72 times
        in his log, 10 times in player 1's session. His dump: the freed object is a Character the game destroyed (form
        type 62, form id zeroed, its first word a free-list link), last at (86484, -46113), where the 18:26 fight was, 4
        cells from the crash. At 18:28:57 this game had let four copies it made in that fight go as "removed as
        untracked; the actor is still there, no 3D" (FF000D7E, FF000D7C, FF00099F, FF000839): the game destroys those
        itself, so `RecentDeletes` never lists them. Which of the four is not in the dump. The log folder he sent was an
        old one (10-20 to 10-23, v1.9.6-3).
      - Player 1 froze twice after closing the unpaused map (19:47, 19:57), player 2 once (19:59): the game drew the VR
        pointer's pass forever, linked to itself (player 2's dump). Cause: something puts the pointer on the draw list
        twice; not known yet (KNOWN_ISSUES "Freeze after closing the unpaused map"). Holding the map's close for the main
        thread (a065abe1) is taken out again: it was built on a guess the dump does not support. Now: `RenderPassWatch`
        puts a self-linked pass's link back (no freeze) and logs both inserts' callers. Rig `live-map-pointer` does not
        reproduce it (NOT RED on the build without the guard; frame times unchanged with it). Build deployed to FUS and
        MGO, client. Next session: open and close the map and the skills menu, with message boxes and fast travel;
        then read `RenderPassWatch:` lines in both logs (each one names who put the pointer on twice).
        - 2026-10-29 08:04, player 1 alone, 5 closes: no self-link logged, no freeze. Seen: after each close the map's
          panels (legend, markers) stayed on screen for about 2 s, where the HUD's own panels show. Ours: while the map
          is open unpaused, `MapMenu.cpp` holds the panels' fade at 1; after the close the world runs on and they fade
          out from 1. Every freeze came within a second of a close, inside that window. Changed in build 6980a69
          (client, FUS folder, on ce0e10ea): an unpaused close hides the panels' model at once (`kHidden`), and the
          next show takes the flag off that model only. Not tested in the rig (it cannot see the panels). To see in
          play: the panels go with the map; the map opened again offline and online still shows them; still no
          freeze, and `RenderPassWatch:` lines read after the session.
        - 2026-10-29 17:30, rig, `live-map-pointer` three times (ok, no freeze, as always) with more of the watch:
          the laser is drawn only while the map is open, as three passes a frame on three lists (two put there from
          game worker threads, one from the main thread). The game takes them fresh from a pool every frame, link
          empty, and never put the same one on twice in a frame. So a pass linking to itself means one recycled
          while its list from an earlier frame was never drawn and emptied; we do not put the laser on twice.
          Build 2281e5e (client, FUS folder, on 66a82ad3) logs that directly: `RenderPassWatch: renderer R's list
          still holds pass P from frame N` (any pass, any list), with the stack that added to the stale list. Nothing
          in the rig (no false alarm either). Next session: open and close the map as before; any such line, or a
          self-link line (it now gives the frame of the pass's earlier insert), names the list the game left undrawn.
        - 2026-10-29 18:36, player 2 alone, on 9c536680, about 20 closes: 13 self-links of the pointer, each put back
          by the watch, then a freeze at 18:37:58. Every self-link came 1 to 3 ms after `Map menu: hide handled on
          thread X (not the main thread ...)`, in the close frame only (1393 frames with the pointer drawn once, 6
          twice, all at closes); both inserts from `SkyrimVR.exe+0x1308c9d`, one worker thread, one renderer, one
          frame; no `still holds` line. The freeze (full dump): the main thread in the same list draw
          (`+0x1349270`, at `+0x1349513`), the list a loop of two, `RIGHT_oculus_quest2_controller_right` and
          `LEFT_oculus_quest2_controller_left`, which the insert check (one pass on itself) cannot see. Scene graph
          sound (each with one parent, under `PlayerWorldNode`). So in the close frame the same passes go on one list
          twice; the pointer and both controllers, all under `PlayerWorldNode`; what accumulates them twice is not
          known yet. Changed in build 07bafd1 (client, FUS folder, on 9c536680; not run in the rig): the list draw itself (`0x141349270`, entry checked) finds
          the list head as the game does and cuts any loop before drawing (`RenderPassWatch: pass P ('name') led back
          to pass Q ... cut before the draw`). Every pass stays drawn once; the map's close is unchanged. To see in
          play: 30 to 40 closes, no freeze; read the `led back` and self-link lines. Also seen: a short flicker at
          each close, as if a menu opened for a moment; player 2 sees it offline and in co-op, so it is the game's.
        - 2026-10-29 19:06, player 2 alone, on 07bafd1, many closes: no freeze; 20 pointer self-links put back and 13
          loops of the two controllers cut before the draw (`led back`), each one a freeze before. Offline (19:13:05
          to 19:13:15) the map's show ran on the main thread but its hide still on a worker, so "a paused menu closes
          on the main thread" does not hold. Then a crash at 19:26:26 closing the unpaused skills menu: the main
          thread in the draw (`SkyrimVR+0x134a403` > `CommunityShaders.dll+0x388984`, a read at 0x10), 3 ms after
          `Skills menu: hide handled on thread X (not the main thread ...)`. The pass being drawn (a `BSEffectShader`
          one, as the tree's stars and lines) had a freed shader property (vtable 0). So the hide on the worker freed
          the tree while the main thread drew it. Changed in build 00060aa (client, FUS folder, on 9c536680; not run in the rig): an unpaused hide takes a
          reference of ours on the tree model (StatsMenu+0xC0), and the start of Main::Update lets it go once ours is
          the last for two frame starts, so the model is destroyed on the main thread (`Skills menu: its tree model
          ... held at the unpaused close` / `... let go on the main thread, N frames after the close`). To see in
          play: open and close the skills menu many times online, no crash; read those two lines.
        - 2026-10-29 19:46, player 2 alone, on 00060aa: 85 skills menu closes online, no crash; each tree held and
          let go on the main thread, 3 to 14 frames after the close, never still kept by the game. 65 map closes, no
          freeze; 20 pointer self-links put back, no controller loop this time. Seen fixed by player 2; not yet
          seen by player 1.
        - 2026-10-29 19:13:55, the same session: a confirmation box over the open map, then a 31 s loading screen
          (the watchdog's sample: the game's load, DynDOLOD and vrmapmarkers on the stack, none of ours), then the
          main menu. No `SaveLoad:` line for it, so which file is not known; the next load (Quicksave0, from the
          main menu) was fine. Cause unknown.
      - Player 1: `ActorValueService::OnHealthChange: failed to find server id` 1948 times, about 10 a second in the
        fight (18:10 to 18:12), against 7 in an older session: a health change (most likely regeneration) of an actor
        with no server id yet. Nothing seen on screen. Now named: `Health change of X ('name', base B) not sent: it has
        no server id[, its assignment is still pending]; N change(s) ... since the last such line`, once per actor every
        10 s (build c158a0b, deployed to FUS and MGO). Next session: read those lines.
- [ ] **Play session 2026-10-26 18:25 (both on aad1262c + the get-up and lock change; player 1 leads; PvP on)**
      - The unpaused skills and map menus paused: the FUS server's `config\STServer.ini` had `bUnpausedMenus=false`
        (the default). Set to true there on 2026-10-26; it takes effect at the server's next start.
      - Changed on 92ff338d (client only, FUS folder): a PvP hit is counted once. Arrows and spells: the defender's
        own replayed projectile decides; the attacker's report of it is dropped while a projectile of that player
        launched here less than 3 s ago may be in flight (`PvP: a hit of N from player P not taken: its projectile is
        replayed here, and that copy's hit decides`). Melee: the attacker's report, weighed by the defender's rule, as
        before; the attacker's copy's own hits here (its sword body touching this player, once a second while it
        rests there) no longer count (`PvP: player copy X hit this player here: ...; not taken, melee is its own
        game's report`). Those copy hits downed player 2 at 18:39:57 (31 of them, 11 to 19 each, from 18:38:59).
        Also logged now: each blow of another player on an actor here (`Death sync: player P's blow of N on this
        game's NPC X: health A to B[, in no cell here][, no 3D here]`), the weather a leader sends and a member is
        given (`Weather: ...`), and a copy's swimming starting or stopping (`SwimDiag: ...`). Rig, regression only:
        `live-defender` 24 of 24 (the bot's melee reports land or are blocked as before), `live-clash` 10 of 10; the
        weather line showed. The bot cannot launch a projectile, and no rig run makes a copy's sword body hit the
        player, so neither changed half has a red run. To see in play: an arrow that hits takes one hit's health, not
        two; a dodged arrow takes none; no `player copy ... hit this player here: ...; not taken` downs anyone.
      - Dead NPCs standing, different ones in each game (18:37). Player 1 landed 27 killing blows (18:36:08 to
        18:37:06) on FF00137D, the stand-in for player 2's bandit CC591 (server id 4C; it rolled another leveled pick
        in player 1's game, so player 1's own CC591 is disabled: `Ghost: reference CC591 rolled as ...`). Each blow
        reached player 2's game (`PvP: a hit of 16 from player 1 on server id 4C received; it is actor CC591 here`)
        and was applied, yet the bandit never died there: player 2's game reported no death all session. At 18:40:51
        it handed the bandit off at worldspace 0, cell 0, (0, 0, 0): not placed in its world. Player 1, the leader,
        cannot claim a ghost (its own reference is disabled), so the copy stood at 1 health. Not known yet: the bandit's
        health in player 2's game, which no line gave. In the whole session only 2 NPCs died in their owner's game
        (both in the fort, while player 2 was outside, so not applied there: `a death for server id ... arrived and
        was not applied: no copy of that character here`; the server then dropped them, so in player 2's world they
        live on). Not one `NPC X health corrected from ... to its owner's ...` line on either side, though an owner
        sends an NPC's health on every change and a copy is corrected when 25 or more apart: so the bandit's health
        in player 2's game never moved far from the copy's 1, or never reached player 1. No epoch drops anywhere.
        Rig (`live-owner-blow`, new): another player's killing blow on an NPC this game owns kills it here and the
        death goes back (`health 438 to -1562`, `health broadcast killed actor`, `local actor ... is now dead`), so
        the owner's path works. With the NPC protected (Lydia's base, as placed), the same blow leaves it alive,
        going down at -30 health. The engine's rule (KillImpl, SE 36872, VR 0x14060C340): it first recomputes the
        actor's essential and protected flags (flags2 bits 18 and 19) from its base (ACBS bits 1 and 11) and its quest
        aliases (VR 0x1405D9410), then asks SE 36247 (VR 0x1405D9520) whether the attacker may kill it: never when
        essential, and when protected only when the attacker is this game's player (a teammate's kill is counted as
        the player's first). Our Kill passes no attacker, so it is refused (rig: at full health too), and so is
        Papyrus Kill naming the other player's copy. Lifting flags2 around the call does nothing (recomputed first).
        Same cause, and a bug today: a copy of a protected (or essential) NPC does not die when its owner reports the
        death (rig: `set dead by its owner (now dead: false)`, the copy goes down instead), so an NPC protected by its
        base or a quest that its owner's player kills stays standing in the other games. Not seen in play logs yet
        (154 copy deaths, all generic).
        Fixed (PROTOCOL CHANGE: the ownership claim gains KillingBlow; both players update client and server): a copy
        dies on its owner's word even when protected or essential (a hook on SE 36247 says yes while our Kill runs on a
        copy); and this player's killing blow on another player's protected, not essential NPC makes this game claim it
        (the server grants it at once, leader or not, temporary or not; not another player's follower) and kill it with
        this player as the killer, so the bounty, crime and quest credit are the killer's, in the killer's game
        (`KillingBlow: X (server id S) killed here by this player`). Build 14b3d55 (dirty, on 206a1d36), deployed to FUS
        and MGO, client and server. Rig: `live-protected-copy-dies` RED on d6b97f6 (`now dead: false`), green on
        14b3d55; `live-protected-kill` (the player's Fireball as the killing blow on the bot's protected Lydia at 5
        health) RED on d6b97f6 (kept at 1 health), green on 14b3d55. TPTests 94/94. Not seen in play yet. To see: a
        protected NPC (a named townsperson) the other player kills dies in both games, and a bounty, if any, is theirs.
        Next session: the new `Death sync: player P's blow of N on this game's NPC X: health A to B[, in no cell
        here][, no 3D here]` lines on the owner's side.
        `live-hit-npc` casts 12FCD, which is Flames, not a Firebolt, and no longer lands in the rig (its bot check
        fails); Fireball 1C789 after `DO face npc` lands. Not changed yet.
        Fixed, death wins (both games' saves disagree on who is dead, the cause of the standing ones seen after
        reconnecting into cells cleared the day before): a generic (not unique) NPC dead here that its owner's game
        has alive stays a body here, is not replaced by a living stand-in, and its owner is sent a killing blow
        (`DeathWins: X (server id S) is dead here but alive in its owner's game`). Unique NPCs still follow their
        owner. Build d6b97f6 (dirty, on 206a1d36). Rig: `live-dead-stays-dead` (the Mistwatch bandit 45A3F, dead in
        the test save, registered alive by the bot while the game was offline): RED on the build before (the body
        stood up again), green on d6b97f6 (stays dead, the bot gets the -100000 blow); `live-owner-blow` ok.
        Not seen in play yet. To see: no NPC killed earlier gets up when the other player arrives or reconnects.
      - Arrows count twice when they hit: player 2's game replays player 1's arrow as a projectile, and its hit on
        player 2 there is applied (`PvP: this game hit us with player copy FF0012F8: 1 hit(s), 64 damage ... (not
        weighed by the defender's rule)`); player 1's own hit report then lands too (`Defender's rule: a hit of 20
        from player 1 lands`). The same holds for a copy's spells and sword body. Decision needed: which game's hit
        counts for each kind (the defender's own, or the attacker's report), so that one does. At 18:51 (a dodge on
        player 2's side, no damage), player 1's game registered no arrow hit at all: one hit of 11 from 18:50:40 to
        18:52:30.
      - Lydia (player 1's follower) stuck swimming on player 2's screen, and a second Lydia outside while player 1 was
        in the fort. Player 1 swam to the fort's secret entrance with her and went in at 18:47:05 (FortNeugrad01);
        the server took her out of player 2's view (`CellRange: character 10008E moved from cell 9884 to 15277`,
        18:47:08). Her copy there was player 2's own Lydia (a reference of the game's own), and removing a copy only
        hands such a reference back, where it stands: player 2's Lydia stayed in the lake by the door, in the swim it
        last had, unsynced (`OnSubtitleEvent: server id not found for form id A2C94`, 18:47:52), until her spawn
        took her back at 18:48:48, when player 1 came out underwater. After that her copy was not swimming (state
        0x81 at 18:51:39 and 18:52:53). Rig: a copy walked into the lake swims and stops on the shore, so the swim
        itself is followed. Fixed (server, FUS and MGO folders): when a character goes through a door out of a
        player's range, a reference of the game's own is first moved there in that player's game (NotifyActorTeleport,
        then the removal; `CellRange: ... removed from their view, its reference there moved through the door too`).
        Rig: `live-copy-through-door` (new; the bot leads, takes Ralof 2BF9E beside the player, then takes him alone
        into Helgen keep with the new bot option `npcmove ... cell <hex>`) RED on b7e842c2 (Ralof left in the player's
        cell), ok with the fix (`Successfully teleported actor, form id: 2BF9E, world space: 0, cell: 5DE24`).
      - Player 2's screen dark red (18:54, picture), gone after player 2 loaded elsewhere once player 1 had left.
        Nothing in either log explains it. The leader's weather is forced on members every frame and never logged;
        a weather from player 1's game is the lead. Next: log the weather a leader sends and a member is given.
      - Player 1's game ended on quitting (18:55:15, after the journal and the console): no crash block, dump, Crash
        Logger file or Windows error. Like 2026-10-25 17:21; no session has ever logged an `Exit:` line, so a quit
        and a crash on quitting look the same.

- [ ] **Play session 2026-10-25 19:15 (Fort Neugrad; both on 10749cfa + the marker fix; player 1 leads)**
      - Ralof lay on the floor for player 2 for 97 s while up in the leader's game (19:42:53 to 19:44:30). Player 2's
        log: knocked into his own ragdoll, `GetUpBegin` sent after 1.5 s queued (taken, knock state 6), then the
        owner's `bleedOutStop` came (`refused as an action ...; sent to its graph instead (not taken)`), the get-up was
        queued again (knock state 5), and the copy's recovery had already used its one get-up. Fixed (not yet seen in
        play): the get-up is sent again each time the copy waits 1.5 s queued once more, three times at most
        (`... sent it 'GetUpBegin' (taken, try N)`). Rig, regression checks only (the rig cannot ragdoll a copy just
        as its owner's bleedout ends, so no red run): `live-copy-knockdown`, `live-copy-bleedout` and
        `live-door-join-locked` ok on aad1262c + these changes (FUS and MGO folders, client and server); the new
        `Lock:` lines showed in the last one.
      - A door not unlocked for player 2 (19:49): every door player 1 picked and opened (5D57D, 5DA84, 5D394, 25274)
        was already unlocked on player 2's side when its opening arrived (no `and unlocked: it was locked here`), so the
        lock relay worked for those. Player 2 picked a lock at 19:49:49 for 21 s; no log names the object, and the lock
        relay logged nothing at all. Now it does: `Lock: X unlocked here (level N); sent to the players in cell ...`,
        `Lock: X unlocked in another game; done here` / `already so here`. Also fixed from the code: a lock applied from
        another game was sent back by the game's own lock change, and for a lock (not an unlock) the two games then
        passed it to and fro without end; it is no longer sent back (within 2 s), and one already so is left alone.
      - The prison's cell doors missing for player 2 (19:40): no reference switch reached player 2 from 19:39:16 to
        19:41:54; batches at 19:38:22 and 19:38:48 switched 5 and 1 references there, unnamed. Now a member's game
        logs each door, container, activator and furniture it switches as the leader's game has it
        (`ReferenceEnable: X (base Y, form type T) switched off here, ...`). Next time the line names them.

- [ ] **Play session 2026-10-25 17:12 (PvP; player 1 on 779d7a82 + the menu hardening)**
      - Player 1's game ended at 17:21:22 mid-fight (clashes with player 2's sword, player 2 at 22 health) with no
        trace: no crash block or dump of ours, no Crash Logger file, no Windows error event or report; frames ran
        until 17:21:21.93 (PhysicalCollisionVR's log), SteamVR saw the pipe break at 17:21:22.94, the server timed out
        at 17:21:32. Our log ends 0.6 s earlier, since it is flushed once a second. The launcher never ends the game.
        So the process ended past every handler: TerminateProcess or ExitProcess called inside it, a kill from
        outside, or a stack overflow too deep for any handler. No evidence for a cause, so no fix. To know next time:
        a line written straight to disk when the process ends itself (NtTerminateProcess on itself, with the caller),
        and the log flushed at once for warnings and errors. Not done yet.
      - Our player marker relayed to the party as a leader's switch: `Player markers: 8D005040 follows player 2 ...`
        at 17:16:22.738, then `ReferenceEnable: 21 reference(s) ... (first 129:5040 off)` 27 ms later (MoveTo
        switches the reference off and on). In the member's game the same form is its marker for the leader, so that
        one was switched. Fixed on 10749cfa + this change (client only, FUS and MGO folders): `IsSharedSwitchKind`
        leaves out SkyrimTogether.esp's references, on both the sending and the applying side. Rig:
        `live-marker-not-relayed` (new; this game leads, the bot walks out of sight and back so the marker moves, and
        must never get a switch of 5040; new bot check `refenabled <hex> none`) RED on 779d7a82 (5040 relayed off and
        on three times), ok on the fix; `live-ref-enable-leader` still ok. To see in play: no `first 129:5040` in the
        leader's `ReferenceEnable` lines, and the member's map shows the leader.
      - Player 2's collected logs miss this session's `tp_client.log` (the zip's newest log ends at 09:49), though
        `build.txt` was read from it. Not a sharing problem (spdlog opens with `_SH_DENYNO`); cause unknown.

- [ ] **Play session 2026-10-24 afternoon** (both on v1.9.6-9-g29aee76d; player 1 leader, the Jagged Crown):
      - Quest stages reach the leader 30 s late: nearly every stage player 2 reached was held by player 1's game
        (`Holding member quest stage N for 2610B until no playing scene of it checks it`) and applied only by the 30 s
        timeout (`QuestHold: no progress ... applying the N left in order`), 14:38-14:52. Galmar's scene A0E3D sat in
        phase 22 on player 1's side, repeating his lines every 14 s from 14:44 to 14:50, until stage 150 came. So the
        Draugr ruin's doors, opened by that scene's scripts, opened for player 2 at 14:49 and not for player 1. The
        hold's "a playing scene checks it" is true for almost every stage; the quest port's hold needs a narrower test.
        Fixed in 626a969 (FUS, client and server): a member stage is held only when it changes a check of a playing
        scene's running phase or the next one; before, any later phase checking it or a later stage held it. The
        hold line names the scene and phase (`... checks it: scene X phase N does`). Also fixed: the leader never
        knew a stage was done (`Papyrus: Quest.GetStageDone not found`, a wrong name since the quest port). Rig:
        `live-quest-hold` red on 29aee76d, ok on 626a969. To see in play: few holds, each naming a scene's next step.
      - Dead Imperial soldiers floating in the air with their hands still idling, for player 1 only (15:01; lootable,
        so dead). They were player 2's fight: player 1's game took them over as leader at 14:59:36, all at once,
        10 s after its arrival (the leader grace), while they were in bleedout (`DownedTravel: own NPC ... life
        state 8`), and they died in player 1's game seconds later (`Death sync: local actor 8A009C68 is now dead`).
        Lead: an NPC handed over in bleedout or mid-fight dies here without its ragdoll taking over. Not proven; a
        rig test first (the bot owns an NPC in bleedout, the game's leader arrives and takes it, it dies here).
        Rig experiment on 18fffe5 (the same soldier base, 8A009657, placed by the game, connected): killed outright, or
        taken to bleedout and then killed by the player, its body settles in about 2 s and lies still (pelvis 22-24
        units over the floor, hands not moving). Not reproduced, so no change. The play case (a copy of player 2's,
        taken over, then killed by player 2's hits) needs the bot to fight, which it cannot.
      - Player 1's arrow held in the hand sat a little ahead of the hand on player 2's screen (15:03, picture). Player 2's
        log: `arrow held in the right hand as its owner holds it, its head 68 units from the hand (hand scale 0.85)`.
        The hold offset needs measuring against the owner's own arrow node in the rig before changing it.
        Measured, no change: the archer's game sends the arrowhead at (21, -6, 77) in its body's hand-bone space in play
        ((21, -6, 79) in the rig), about 80 units, and the copy puts it there (68 = 80 x its hand scale 0.85).
        With a 64-unit arrow the nock sits about 6 units past the finger joints, as in the picture: the copy shows the
        archer's body hand as it is. Open question: does the arrow also float ahead of the fingers in the archer's own
        headset (VRIK body hands)? The rig cannot show it (no VRIK hand pose there; the first-person nodes are out of reach).
      - Both players dragging one body: the dragger's hand drawn well above the body, and the body sometimes flashing
        back to where it was picked up (15:00, picture). Logs, body A46FF: the dragging game stopped sending the body
        two seconds after it stopped moving, also while it hung still in the hand (player 2's log restarts sending five
        times in one drag), so the other game let it drop under the hand. 1.5 s later that game moved it to where the
        owner reported it, and the owner reported where it died: the reference stays there while a hand drags the body
        (`A46FF's ragdoll lay 166 units from where its owner's corpse is; moved there`, 14:59:56, player 1).
        Fixed in fa5e36c (client; FUS and MGO, server too): a body within 40 units of one of this player's hands keeps
        being sent while it hangs still; a dead body is reported where its 3D root is, not where its reference is;
        its 600-unit send range is measured from there too. Note: commit 0e8178e0 holds the second part switched off
        (`if (false && ...` in `AnimationSystem::SerializeMovement`, a rig red switch); the working tree has it on.
        Rig: `live-body-hold` (new; the player's hand drags the bot's body, then holds it still 14 s; the bot must
        still get its bones) RED on 5fee1f4 (bones stopped 7167 ms before the check), ok on fa5e36c (94 ms).
        `live-corpse-thrown` ok on both: in the rig a thrown body's reference follows it, so the reported-position
        part is backed by the play log only. Regression `live-corpse-place` ok. Not seen in play yet.
        The TEMPORARY `BodyDiag` lines answered in the evening session (22 lines over both games' drags): the 3D root
        stays 0-5 units below the pelvis and at most 7 beside it, so a lagging root is not a cause. Removed in c4caf8a.
      - Player 2 stuck in a menu after switching to Discord (15:07): nothing of ours in the log then; the log records
        menus opening but not closing, so it cannot say which one stuck. A `Menu closed` line would tell next time.
        Done in build 210e55b (client, FUS): `Menu closing: <name>` for each menu asked to close (WS* HUD widgets left out).
        Rig: `live-menu-hold` RED on 29aee76d (no such line), ok on 210e55b; one line per real close. Not seen in play yet.

- [ ] **Play session 2026-10-25 morning** (both on c4caf8a with the party-sleep merge; player 1 on CSX csx3.18-VR and
      Engine Fixes 7.7.1 for the first time; Whiterun during the battle, then sleeping).
      - Player 1's crash at 09:53:52, 15 s after arriving in Whiterun through Dragonsreach's door 16707: an access
        violation in the game's Havok world step (`SkyrimVR.exe+0x1e05db`, bhkWorldM and ahkpWorld in registers, a
        garbage pointer made of float data). No mod's code on the stack. Last night's freeze came right after the same
        door into the same battle, with the old shaders, so the shader switch is not the common part. 33 ms before,
        player 1's game had taken a spawn request for Ralof (sent back under a new server id 10 s after it was handed
        away) and moved its unloaded reference into the cell (`MoveTo` in `OnCharacterSpawn`). Not proven to be linked.
      - Player 1's stutters after the restart match the Sleep/Wait menu: 16 and 15 frames over 50 ms (p99 104 and
        122 ms) in the two 30 s windows with the menu open, 0 (p99 34 ms) in the one between. The menu no longer pauses
        while connected (party sleep), so the world runs the slept hours in a few seconds. Our update stayed at 0.6 ms
        on average (6 ms at most) in those windows; the slow frames are the game's, as every NPC replans each hour.
        Candidate change, not made: let the sleeper's menu pause as in the game (its world waits; our network keeps
        running in a paused menu) while the confirmation still waits for the server's grant. The rig can confirm a
        sleep (DevBench opens the menu, E confirms), but only for the menu's default hour: the arrow keys do not move
        its hour slider in VR, so a long sleep in a busy town, and the stutter, cannot be reproduced there yet.
      - The Civil War out of sync for player 2 (no camp, catapults or barricades; a bounty for fighting the guards;
        every NPC fighting after player 1's crash): CWAttackCity (4F8BF) never ran in player 2's game. Every stage from
        the leader arrived as `Remote quest updated: 4F8BF, stage: N; running here no` (35, 40, 50, 9000), and a stage
        set on a quest that does not run does nothing. Both battle quests start only from a story event that carries
        the city and the field commander (CWAttackCityStart 4F8C0, CWSiegeStart 7E32C); a plain start cannot fill their
        City alias. Running them on the member would also run its 24 reinforcement spawns there, so they stay leader-only.
        Fixed in f6e9063 (client and server, FUS and MGO; a PROTOCOL CHANGE, both players update): the party leader's game
        relays the references its scripts switch on or off (TESObjectREFR::Enable, SE 19373, and Disable, vtable slot
        0x8A on VR; statics and markers, activators, containers, doors, trees, flora, furniture; never actors, items,
        lights or temporary references). The server keeps the latest state per party and sends it to a player who joins;
        a member's game applies it, and retries references whose cell is not loaded yet every 2 s. Log lines:
        `ReferenceEnable: N reference(s) switched by this game's scripts sent to the party` (leader),
        `ReferenceEnable: N reference(s) the leader's scripts switched ..., M applied here now, K waiting for their cell`
        (member). Rig: `live-ref-enable-leader` (a script call switches the Stormcloak camp marker 8680C; the bot gets
        both switches) and `live-ref-enable-member` (the game drops its connection with F6 so the bot leads; the bot's
        switch reaches the game) RED on 98527fd (the client's sending and applying switched off), ok on f6e9063.
        Regressions `live-door-state` 17/17, `live-copy-removed` 4/4, TPTests 94/94. Not yet seen in play: a siege's
        camps, barricades and catapults for player 2. The hostile faction (CWSoldierPlayerEnemyFaction 74A4F) reaches
        NPCs the leader's game owns through the faction sync; NPCs player 2's game owns never get it (the bounty).
      - CSX logs `Force clearing cache due to bDepthBufferCulling` at every load: on VR, with the game's depth-buffer
        culling on, it empties the shaders in memory, which then load again from its disk cache in the background.
        MGO runs the same values; not a stutter cause.

- [ ] **Play session 2026-10-24 evening** (both on fa5e36c; player 1 leader and server; Windhelm, then the Battle for Whiterun).
      Build c4caf8a (client and server, FUS and MGO) carries the fixes below and the merged party-sleep PR, which changes
      the protocol: both players update before playing together.
      - Player 1's game froze at 20:03:30 on the loading screen into the Whiterun battle: the main thread waited inside
        the game's own code during its first draw. The freeze dump could not say on what: it held 1 stack of 151 threads.
        Past 3.5 MB a small dump is written again with only the writing thread's stack, and for a freeze that is the
        watchdog's. Fixed in c4caf8a: the freeze dump keeps every stack. Rig: the game's main thread suspended 35 s;
        on fa5e36c the dump held 1 of 254 stacks (RED), on ae27a6d 265 of 265, the main thread's readable. Cause of the
        freeze itself unknown: the next freeze dump answers it. Memory was not the limit (100 GB of commit free).
      - Player 2's game leaked threads: 220 at 19:13, 4915 at 20:05 (about 120 a minute; the afternoon too, 228 to 537).
        Player 1's stayed near 160. The newest of them, sampled at 19:53 and 20:03, wait inside DynDOLOD.DLL. Player 2
        runs DynDOLOD.DLL v3.0.32, player 1 v3.0.43. To try: player 2 on v3.0.43, then read `Health: ... threads`.
      - Player 2: heap corruption reported 2 s after a failed reconnect (20:05:48), then a Windows crash report at
        20:06:40. Player 1's rig games report the same code often and keep running. Open question: did the game crash,
        or was it closed after player 1's freeze?
      - Two Ice Wraiths (19:24): reference 7AAC1 rolled as an Ice Wraith in player 1's game and a Snowy Sabre Cat in
        player 2's (the owner's). Player 1's game made it a ghost and disabled it as its cell loaded (19:23:20.235); the
        game built its 3D again 0.27 s later, and it fought for 80 s unknown to the server. About 17 such ghosts in the
        day's logs lived on 9-148 s; every ghost not rebuilt was removed at 5 s. Fixed in c4caf8a: every 2 s a ghost
        with 3D that is enabled again is disabled again (`Ghost: reference X was enabled again ...; disabled again`).
        No rig test (the bot cannot own a world creature under another base); backed by the logs only. To see in play:
        that line, and no `OnSubtitleEvent: server id not found` for a creature. If ghosts still fight without the
        line, they are flagged disabled with their 3D, and disabling again does nothing.
      - The sliding at 19:27 on player 2's screen was the Ice Wraith copy (SlideDiag's frozen moves start and stop with
        it), not the refugee: player 1's game has no descriptor for its graph (494650d31516034), so it sends empty
        animation variables. Not a regression. A replacer from the capture in `logs\behaviours\IceWraith_...` is
        possible, but its graph has no Speed variable: waiting on a decision, and one live look afterwards.
      - Moving a lantern by hand showed nothing on the other side (19:28, outdoors east of Windhelm): neither game
        logged a world-object move. Which lantern, and what kind of object, is not known; no change.
      - Ulfric's talk not heard by player 2 (19:31): all eleven lines reached player 2's game and were started there;
        player 2 had the journal and console open (the game paused) from 19:31:15 to 19:31:36. Whether sound came out
        is not logged. Done in c4caf8a: `played here (game's answer A, D units from the player, 3D yes/no, pause counter N)`.
      - Catapults of the Whiterun siege missing for player 2 (19:54): vanilla (CWCatapult), initially disabled and enabled
        only by the CWSiege quest's scripts, which run in the leader's game alone. No message carries a script-enabled
        object's state: a missing feature (a protocol change), not a bug. Done in c4caf8a, to confirm: `Remote quest
        updated: X, stage: N; running here yes/no`. The siege camp and barricades are probably missing for player 2 too.

- [ ] **Review fixes, doors, ownership, transport** (from the reviews of the last two commits and of playback session B,
      2026-10-24; build b5963dd, client and server in the FUS folders, not MGO yet; the PickedUp flag and the decode guard
      change the protocol again, in the same release). Rig: red on 5b24e15 (these fixes switched off), green on b5963dd:
      `live-door-range`, `live-npc-grace-outdoors`, `live-quest-item-copy`, `live-door-join-locked`; regressions
      `live-door-join`, `live-npc-grace`, `live-door-state`, `live-npc-indoors`, `live-door-echo`, `live-door-copy-load`,
      `live-loss-playback`, TPTests 66/66, `run-all-pairs` 14/14. Not seen in play: both players need b5963dd or later.
      - Leader grace outdoors: a game's grid shift counts as an arrival (it comes before the cell entry), and the
        leader's claims wait like its assignments (`Ownership: LeaderClaim of actor X by player Y held back ...`).
        Test: `live-npc-grace-outdoors` (the player travels by `cow` and back; the bot keeps Lydia about 10 s;
        RED: she went to the player within 5 s of the arrival).
      - Doors: states kept per door, apart from the per-cell objects, so a door used from the next cell over or
        before a grid shift is kept; a lock forgets it; a door locked here is never opened on arrival (`left shut,
        it is locked here`); an activation of an object whose cell is not loaded here is not replayed (`its cell is
        not loaded here; not done`); a blocking script's own Activate(ref, true) is sent, the block read at the
        activation. Tests: `live-door-join-locked` (RED: the locked door was opened and unlocked on arrival),
        `live-door-range` (RED: a far persistent door in Riverwood's inn was opened), `live-door-state`.
      - Quest items: only the change that echoes a pick-up (`PickedUp`) is skipped on the picker's copy; a container
        take or a script's AddItem reaches it. Test: `live-quest-item-copy` (a second one from a container: the copy
        holds 2).
      - Server: movement older than the newest taken (within 1 s) is dropped, the tick reset on a hand-over (a late
        packet set actors back, and could clear a flying dragon's flag); a character's own cell is set at its cell
        change, before its first actions there; send rate 512 KiB/s to 2 MiB/s on server and client (128 KiB/s by
        default for any ping over 33 ms).
      - Encoding: a damaged or crafted message is dropped whole (`DecodeGuard`; caps on actors, actions, updates and
        animation variables, a repeated id rejected), TPTests `[encoding.damaged]`; one movement packer for client,
        relay and tests (`MovementPacker.h`, TPTests `[encoding.movementpacker]`).
      - Smaller: log prefixes (`DoorCopy:`, `CellRange:`, `Ownership:`), the blocked-door line once per door every
        5 s, map markers swept once per load or disconnect instead of every 5 s, PlaceCorpse's per-copy state
        forgotten with the copy, one door state enum.
      - Open: a flying dragon whose owner goes silent or out of range is still handed over and falls (older code).
        Freezing it instead needs a decision.

- [ ] **Playback session B: unreliable movement, actions on their own** (started 2026-10-24; a PROTOCOL CHANGE, to land
      with the door and quest ones). Session A (build 3d63d8e, in play since 2026-10-17 evening) shows the other player
      about 75 ms behind instead of 225, but movement and the VR pose still go reliably: one lost packet holds back
      every later update. Upstream is never fetched again; its logic (23901108, 0a6784b8, a4a33744) is rewritten here.
      - Encoding: `RequestActionEvents` / `NotifyActionEvents` (server id to actions in performed order, reliable);
        round trips in TPTests. Movement messages keep their shape; `ReferenceUpdate::ActionEvents` goes at the end.
      - Server: actions handled on their own (owner check, script cancel, current action, replay cache of the kept
        ones, reliable relay in range); movement relayed unreliably, split under 1100 bytes, the last message without
        Nagle's wait.
      - Client: movement and the VR pose sent unreliably, split by size, the last message of a frame without Nagle's
        wait; every local actor's actions in their own reliable message at each send; players, mounts and actors in
        combat every 33 ms, other NPCs every 100 ms. `MovementSend:` once a minute: about 1580 messages, the largest
        1078 bytes, none over 1100.
      - VR pose: weapons, fingers and scale resent every 250 ms as well as on change, so a lost message heals fast.
      - Bot: movement unreliable (`STBOT_RELIABLE_MOVEMENT=1` sends it reliably, as before, for a red); replays its
        actions through `RequestActionEvents`; captures them from `NotifyActionEvents`.
      - Build 875fd61 (client and server, FUS folders; not MGO yet). Rig: `live-loss-playback` (the bot with 40 ms lag
        and 2 % loss; no `InterpDiag players` window worse than -150 ms while it replays): RED with the bot sending
        reliably (worst -226, -142, -139, -132 ms), green on a fresh rig (worst -37 to -47). `run-all-pairs` 14/14,
        TPTests 64/64, `live-walk`, `live-creatures`, `live-blade`, `live-arrow-nock` pass. `live-clash` and
        `live-defender` failed at the rig's own step that brings the held sword onto the copy's still blade: the scripts
        moved the player onto the copy, which can stand off the player's ground (a lifted player falls back, and a copy
        under the ground gets no weapon body). They now stand the copy in front of the player (`infront 70`): on
        d78905d `live-clash` 11/11, `live-defender` 25/25. Rig-only; no client change.
        Session B review (bot and test): actions captured and replayed at their own time, movement NoNagle, a 1 ms timer
        in the bot; `live-loss-playback` also checks playback jitter. On d78905d: RED with the bot sending reliably (on
        jitter; worst window -103), green worst -21 ms, jitter 3-6 ms.
      - Not seen in play: both players and the server need 875fd61 or later. To see: the other player's hands and
        sword as smooth as before, no hitch when the connection drops a packet, `PlaybackDiag` delays as before (about
        75 ms), blocks still decided (`Defender's rule`).
      - Also open from session A's play test: for 40 s after a revive in a fight, player 2's delay for player 1 sat at
        its 250 ms cap, and two gaps of up to 2.6 s came with nothing ahead; causes not looked at.

- [ ] **The owner's decisions of 2026-10-24** (asked all at once). Status of each:
      - Doors at join: the server keeps a door's state after each activation it passes on, while someone is in
        the door's cell, and sends it with the cell (`ObjectData::DoorOpenState`, a PROTOCOL CHANGE: every player
        and the server update together). The entering game sets a non-load door to it (`Door X: open|closed in the
        other games when this game came in; set ... here from state N`); setting it sends nothing back. Only
        activations count, not a game's own snapshot, so a door a script opened is never shut by a stale state.
        Limit: a door that moves with no activation (a script's SetOpen) after one leaves the kept state behind; the
        server forgets it once nobody is in the door's cell.
        Build a7e1e02 (client and server, FUS folders). Rig: `live-door-join` (the bot opens Helgen keep's door while
        the player is at Riverwood's inn; back in the keep the door must be open) RED on the server-only red build
        03d9deb, 5/5 on a7e1e02; `live-door-state` 16/16, `live-door-echo` 6/6. Not seen in play: needs both players
        on a7e1e02 or later.
      - Door range: a door's use reaches every player who has the door's cell loaded, not only the exact cell
        (server `ObjectService::OnActivate`). Build a7e1e02. Rig: `live-door-range` (the bot uses Mistwatch's door
        in cell BAEA while the player stands in BAE9; the game must get it) RED on 03d9deb, 4/4 on a7e1e02. Not seen
        in play.
      - NPC claim burst after the leader's load: a party leader's assignment of an NPC another game runs waits
        until 10 s after the leader's arrival (a load, a door, a fast travel; not a walk into the next cell), then
        the rules are checked again (`Party leader assignment of actor X to player Y held back until 10 s after
        their arrival`, server log). Build a7e1e02. Rig: `live-npc-grace` (the bot is given Lydia in Helgen's keep
        while the player is away; the player comes back) RED on 03d9deb (she went to the player within 5 s), 10/10 on
        a7e1e02 (handed over 10.0 s after the arrival). Not seen in play: to see, after a load next to NPCs the other
        game runs, each NPC's `Gained ownership of actor X` once, not again and again for a minute.
      - PvP spells: the caster's perks apply, as on NPCs. Done by another session; its green `live-pvp-spell-perks`
        passed 3/3 on a7e1e02 (Firebolt with Augmented Flames arrived as -60, base 40), RED on e7530de (the same
        tree with the old PvP code: 40). Not seen in play (PvP on, both players on a7e1e02 or later).
      - PvP frost and shock spells: never sent before, since the test was the keyword MagicDamageFire (0x1CEAD).
        An effect now counts when it is a harmful value modifier (single or dual) on health. Build 67cefa2 and later.
        Rig: `live-pvp-spell-elements` RED on 875fd61 (Chain Lightning not sent), sent on 67cefa2; on d78905d ok 7/7
        with touch spells (frost -10 and shock -20 reached the bot; aimed casts miss under the rig's idle controllers).
        Not seen in play (PvP on, the caster on 67cefa2 or later).
      - Releases: a tag keeps publishing at once (no drafts); the gate runs on the local build before tagging.
        Nothing to build.
      - MGO: a fixed rig save, made at the rig's start cell in the MGO profile. Done: `urSovngardeRigStart` at
        MistwatchExterior03 in the MGO profile's `saves-rig`; an MGO `headless.ps1 up` loads it, `down` leaves the
        profile's own saves as they were.
      - SteamVR logs in launcher reports: done in the launcher (not committed): the last day's vrclient, vrserver,
        vrmonitor and vrcompositor logs, device ids and serials hidden, consent text updated in four languages.
        Still to do: the website's "What a report holds" needs the same addition once a launcher release carries
        it.
      - NPC left frozen outside when its owner goes indoors: an NPC that goes through a door or into another
        worldspace, out of a player's range, is removed for that player; a move that brings it back into range
        sends it to them again (`Character X moved from cell A to B, out of player Y's range; removed from their
        view`, server log). A walk along the edge of a player's loaded area removes nothing, so it cannot churn.
        Build a7e1e02. Rig: `live-npc-indoors` (the bot's bear goes into Helgen's keep: gone from the player's
        screen; back out: there again) RED on 03d9deb, 5/5 on a7e1e02; `live-npc-away` 5/5, `live-teleport-far-loaded`
        5/5. Not seen in play.
      - Silent startup deaths on player 2's PC: steps for Windows' crash dumps (WER LocalDumps) written, to send
        privately.
      - Map icons: if zooming in play shows the other player's icon, make the client force the map's regather.
        Waiting on that check in play (the checklist's "Map icons" line).

- [ ] **The rig suite on MGO, player 2's PC** (2026-10-23, 2½ hours, v1.9.6-3-g423f3502, about 200 checks in seven
      game starts). No crash in our code.
      - Passed fully: start-up health, live-copy, live-npc, live-look, live-walk, live-npc-away, live-away-seeker,
        live-temp-remove, live-copy-abandon, live-temp-reconnect, live-netch, live-game-drops, live-equip, batch 4 (six
        scripts), live-door-echo, live-door-state, live-body-owner, live-hands, live-death.
      - Rig only: two `skyrimvrtools.dll+0x71B5` crashes at a save load (2 of about 9 loads), the null driver's fake
        controllers. One start-up hang during Community Shaders/DLSS's start, before our code runs; the retry worked.
        live-load and live-load-dead not run (the game was gone).
      - Test problems, fixed in the tree (scripts only): `live-crime` waited for `Crime alarm ran`, a line the 10-13
        clean-up removed; it now checks the Eastmarch bounty (`CHECK bounty 267E3 mark|raised`). `live-door-state`
        leaves the player in Helgen's keep; it now runs just before live-load in the suite. `live-sender` (the bear's
        Kill takes the player as killer, `waitfor samecell 90` for MGO's long loads) was fixed on player 2's PC; it
        reaches this repo with their next push.
      - Test problems: `live-world-object` now carries a Mead bottle (92CFE) in Mistwatch's yard, since MGO sinks
        the plate (92D04) to z -30000; MGO 9/9 twice, FUS 9/9 (10-24). `live-dropmove` stays FUS only: MGO's physics
        drops, rolls and bounces the item, even off the yard's edge, and a short fall is not reported back to the bot.
        Fixed `live-crime` passes on FUS, 7/7 (bounty raised), build 1b7b5b5, 10-24.
      - Possibly ours, `live-copy-quiet` 2 of 2 on MGO: the quiet copy was stopped once after 3.4 s, once never (on
        FUS and in play, at 1.5 s). Likely cause: the stop needed the actor's speed above 1, and MGO's animation
        frameworks drive the legs through the graph's Speed with the actor's speed at 0.
        - Change in build 1b7b5b5 (FUS folders): either speed counts (`LegSpeed`); a quiet copy that moves but is not
          stopped says why once (`QuietStop: copy X had no report ... and moves, but was not stopped (on foot ...,
          speed ..., graph speed ...)`). FUS regression: live-copy-quiet 4/4.
        - Not certified on MGO: on this PC's MGO profile the rig's W key does not move the player (0 units, 0 actions
          recorded), so the test is INVALID here. Player 2's PC, where it ran, is the place to confirm (red on v1.9.6,
          green on 1b7b5b5 or later).
      - `live-creatures` on this PC's MGO (v1.9.6): seeker and lurker copies walk (lurker 8 of 12 samples with graph
        speed, top 190); the netch moved in 2 samples only, too little to judge, as on FUS on 10-21. Player 2's
        sliding lurker is not reproduced here.
      - `live-body-grab` on FUS (1b7b5b5): the body moved 365 units and ended 163 from the player, but the scripted
        HIGGS grab did not hold (`now holds None`). The 930 units on MGO is the rig's grab, not a sync bug.

- [ ] **Other players on the world map** (the owner's request, 2026-10-23). The markers (SkyrimTogether.esp,
      0x005040 on) followed the players (`Player markers: CE005040 follows player 2 ... in cell 984A`, MGO
      2026-10-21), yet nobody ever saw an icon. The world map draws only the markers on the player's list
      (`PlayerCharacter::currentMapMarkers`, VR +0xAE8, CommonLibVR "confirmed"), and moving a marker does not put it
      there.
      - Change (in the tree, not built yet): a shown marker is put on that list, a parked one taken off, and while not
        connected our markers are taken off any list a save left them on. Every two seconds (each whereabouts) a
        shown marker is put back, since the game empties the list at each load: its player reset (0x1406cb6b3) takes
        the lock, frees the array and zeroes its size. Shown markers stay "visible", never a fast travel destination.
        No plugin change.
      - The list's lock (SE 517728, AE 404255) has no VR id in any address library. It was read from the running
        game: 15 of the game's uses of +0xAE8 take a lock at 0x142FFFE50 (`lea rcx` then BSSpinLock::Lock, SE 12210 =
        0x140143380), and the player reset's own disassembly confirms it. Added as private id 9000013. Our
        `BSSpinLock` takes it the way the game does.
      - Build 1a569f4 (FUS tools folder). `live-map-marker`: red on a547eb5 (no `put on the world map's list`), 5/5
        on 1a569f4. That is the log only: **no icon on the map pictures**, before or after. Not done.
      - Rig, 2026-10-23 20:22-20:32 (memory read from the running game): our marker is on the list (390 entries),
        its handle resolves to 8D005040, enabled, flags 01 (visible), type 11. The map still does not draw it, even
        moved next to the player, and the view does not follow the player's heading.
        - The map caches its icons. With Mistwatch's own marker hidden (flags 03 to 00), its fort icon stayed on the
          next map. After Papyrus `AddToMap` on ours (flags 00 to 01), still no icon.
        - The map gathers markers in 0x1409184c0 → 0x14091a060. That code takes `player+0xAE8`, keeps a
          reference that resolves, is not disabled, has map marker data and the visible bit, and copies the type.
          The drawing loop adds no further check for us; the flying-mount gate applies only on a dragon.
        - The gathering is called from MapMenu's ProcessMessage (vtable 4) and AdvanceMovie (5), its
          MenuOpenCloseEvent sink, PlaceMarker and the fast-travel confirm. Each is guarded by state flags at
          MapMenu+0x305C4..6 and two float thresholds (camera?). So it runs at some moments of the map's opening,
          not at every open, and probably once after a load, before our marker is on the list again.
        - Found 10-24 (decrypted code dump): in MapMenu::ProcessMessage the camera height (0x1430A6F98) is read
          before and after its update (0x14091be70), and the markers are gathered (0x1409184c0) only when it crosses
          10000 (0x141EB5028) upward. The map opens with the camera at 40000, so opening alone never gathers. Forcing
          the crossing by writing the height put the camera somewhere broken, so the rig cannot judge the icon.
        - To check in play (cheap): open the map, zoom in close and back out, and look for the other player's icon. If
          it shows, the list change works and only the regathering is missing; the client could then trigger it when
          the map opens. Rig tools in the session scratchpad: mapprobe.py, mapresolve.py, mapflags.py, mapzoom.py.
      - Not yet checked: the icon type VR's map shows for type 11; whether the list goes into saves (a save while
        connected, then a solo load); fast travel between worldspaces (upstream noted trouble from Solstheim).
      - Not changed: a marker for a far player loads that exterior cell from disk (`ResolveCell` > `LoadCell`), the
        path of the 622 ms teleport stall. No session log shows the marker service slow (`slowest section
        PlayerMarkerService` never appears in the four MGO/FUS logs), so it is left as it is.

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
        - Fixed in build 1b7b5b5 (client only, no protocol change; FUS tools folder): the corpse is placed only once
          the owner's game has said the NPC is dead (`RemoteComponent::DeadAtOwner`, from the spawn data, the death
          message, ownership hand-offs and re-sent spawns). A health broadcast that kills the copy does not count,
          since the copy's health can be lower than the owner's. Without that word the copy lies where it fell:
          `CorpseDiag: X is dead here, but its owner's game has not said it died; left where it lies`. Bringing it
          back to life instead would be closer to the truth, but `Respawn` re-rolls a leveled NPC.
        - Rig, `live-corpse-owner-alive` (new): the game kills its copy of the bot's Lydia while the bot walks her
          1200 units on alive, then her owner's Lydia dies too; where the body lies is read from its ragdoll. Red on
          the v1.9.6 release client (13 drags while she lived, at most 588 units), green on 1b7b5b5, 5 checks (no
          drag while she lived; at her owner's death the ragdoll lay 1199 units back, then placed as before).
          Regressions on 1b7b5b5: `live-corpse-place` 5/5, `live-death` 9/9, `live-revive` 10/10. Not seen in play
          yet: the game where the copy dies needs this client.
        - Still not known: what moves a dead copy's reference with its living owner while the ragdoll stays (seen with
          a bear in the rig: +600, exactly the owner's moves; not the corpse placement, no re-sent spawn logged).
          The drawn body is the ragdoll, so players do not see it; it may matter to distance checks on the reference.
        - The first green try (build 4fccc91, 2026-10-24) crashed the rig's game in `PlaceCorpse`, once the owner's
          death had arrived and the corpse was placed. It passed Havok's `setPositionAndRotation` its position and
          rotation from stack arrays with no 16-byte alignment, and Havok reads them with aligned loads. That fault was
          in released builds too; the new timing lines only moved the stack. Fixed with `alignas(16)`, as the other
          two callers have. No crash at that address (`SkyrimVR+B5F09B`) in the hub's triaged reports. The rig's
          crash is in the host's client log, so the launcher may report it: it is not a player crash.
      - Ours, a body changing hands: 5CEEE (Imperial soldier) died in player 1's game. Player 2's copy was pulled 343,
        195, 343 and 175 units (`CorpseDiag`), then kept moving. Player 2's game then took it with a hand within
        40 units (`Body 5CEEE ... is being moved here; asking for it`). The server logged it as a `party leader
        claim` at 20:15:03. Player 1's copy was then pulled 300-564 units. Possible fix: ignore motion for 1.5 s
        after our own corpse placement, as is done for the owner's moves. Better: require a real HIGGS hold on the
        body (`HiggsBodyHolding`), but that reads Havok outside its step.
        - Checked 2026-10-23, not made: `IsRemoteBodyGrabbedHere` already ignores motion for 1.5 s after the owner's
          position moves, so these pulls came while the owner's corpse stood still and player 2's ragdoll drifted off
          by itself (four pulls back). The rig cannot make a ragdoll drift like that on demand (a bot `npcmove` moves
          the owner's position and is already ignored), so no red run is possible yet. The fix is three lines in
          `VRBodySync.cpp` (a time stamp in `PlaceCorpse`, checked beside `s_bodyPosedByOwnerAt`).
      - Ours, Ralof flickering in player 2's game. Player 2's game ran its own AI on its copy of Ralof (player 1's).
        That AI walked the copy through load doors: 909D2 into the keep (20:11:49-20:12:04) and 91608 out of the cave
        (20:25:53-20:26:22). Each time the copy went to the other side, was removed and was spawned back: 15 removals
        in 3 s cycles (`Activation of 909D2 by 2BF9E, another game's actor, not sent` / `Actor removed, form id:
        2BF9E`).
        - Fixed in build 1a569f4 (client only, no protocol change; FUS tools folder): a copy of another game's actor
          never goes through a load door on its own here (`HookActivate` refuses it); the owner's game reports where
          it went. The owner's own use of the door, replayed through `OnActivateNotify`, still goes through. Logged:
          `Load door X: not used by Y, a copy of another game's actor; its owner's game reports where it goes`, at
          most every 5 s.
        - Rig, `live-door-copy-load` (new): the game makes the bot's NPC copy use Mistwatch's load door. Red on
          a547eb5 (the copy went through and unloaded, then was removed and spawned again), green on 1a569f4, 10
          checks (the same copy stays loaded where it was, spawned once). Regressions `live-door-echo` 6/6,
          `live-copy-removed` 4/4. Seen in play: 16 refusals in player 1's log on 2026-10-24 and 10-25. Not covered: what the refused copy's AI does at the door (it tries again while its package
          lasts, so it may stand there until its owner's position moves it).
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
        - A door whose activation a script blocks (BlockActivation) is no longer sent; the receiver opened and
          unlocked it while the sender's stayed shut (review C12). Build 1b7b5b5 (FUS folders): `Door X activated by
          Y, but a script blocks its activation; not sent`. live-door-state's new blocked step: red on 1a569f4,
          green 16/16 on 1b7b5b5. Not seen in play yet.
        - Papyrus natives are now looked up without regard to case (review C18): the game's string pool keeps the
          first spelling it was given, so a native could be registered as "objectReference". A plausible cause of
          the public v1.9.4 setup that lost GetOpenState. Build 1b7b5b5; start-up health clean. Cannot be shown red
          here.
        - Open (review C23, cannot tell offline): Delete, SetMotionType and Enable still go through Papyrus natives on
          temporary refs. If those natives queue a task, a save could keep it for an FF id that is reused later.
          Proof would be a save holding such a functor; code left alone.
        - A quest object picked up from the world reached the picker's copy twice (review C24): the replayed
          activation gave it the item, then `AddOrRemoveItem` added another before giving the receiving player theirs.
          Fixed in 4ef72be (FUS folders): `QuestItem: X already reached copy Y through its owner's pick-up; not added
          again`. Test `live-quest-item-copy` (the bot picks up Pinewatch's 44E13): red on 1b7b5b5 (the copy held 2),
          green on 4ef72be; live-quest-item 3/3. Not seen in play yet.
      - Ours, found in the 10-24 round: `VRBodySync::PlaceCorpse` passed unaligned stack arrays to
        `hkpEntity::setPositionAndRotation`, which reads them with aligned loads (movaps). A stack shift from other
        changes made it crash at `SkyrimVR+0xB5F09B` (reading "-1") on the first corpse move. Fixed with
        `alignas(16)` in 1b7b5b5. No such crash in the hub's data, so released builds were aligned by luck.
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
- [!] **A freed form cast in `DiscoveryService::VisitForms`** (player 2, 2026-10-18 17:35 local, on the v1.9.4
      release; hub report 20261018-195126). `VCRUNTIME140+0x4AE1` < `+0x50E5` (`__RTDynamicCast` in 14.51)
      < `VisitForms+0x4c1` < `OnUpdate` < `World::Update` < `HookVMUpdate`, with a `BSMultiBoundNode*` where a form
      was expected and two `type_info*` in the registers. `TESForm::GetById(formId)` handed back a freed form, and the
      cast to `Actor` read it. Likely the same cause as player 2's 21:34 crash: Engine Fixes VR's form cache keeps
      freed forms (`FormCaching = true` on his side). Not reproduced in the rig.
      - Now each known actor keeps its handle, and both the missing-actor check and the removal loop look the
        reference up through the game's handle table, which stops resolving a freed reference; nothing casts what
        `GetById` returns there any more. Build 1a569f4. No red run: a freed form cannot be made on demand. Regression
        on 1a569f4: `live-copy-removed` 4/4, `live-door-echo` 6/6. To see: no more crashes in `VisitForms` in the hub.
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
      - Measured in the rig 2026-10-24 (1b7b5b5, a TEMPORARY timing in the "moved there" line): every move of a
        19-body ragdoll cost 31-33 ms, all of it in taking the world's write lock and moving the bodies; walking the
        skeleton cost under 0.02 ms (`Mod update took 32.2 ms, slowest section CharacterService::RunRemoteUpdates`).
        The next build splits that into the lock wait and the moves (`lock X ms, move Y ms`), to tell whether it
        waits for the physics step or the moves themselves are slow.
- [ ] **PvP spell damage carries none of the caster's perks** (rig 2026-10-18, PvP on): Firebolt on the bot's
      character was sent as a health change of 40, its base magnitude, without the caster's Augmented Flames, its burn
      or difficulty; the same spell on an NPC dealt 30 plus a burn of 7.4. `HookAddTarget` sends `-fMagnitude` before
      `AdjustForPerks` runs and does not apply the effect. Not known: whether the victim's game also applies the caster's
      replayed projectile (a second hit); that needs two games.
      - Decided 2026-10-24: as on NPCs. Fixed for the perks in build a7e1e02 (client only; FUS tools folder): the effect
        is made on the copy to take the caster's perk factor (`AdjustForPerks`) and left there at 0; the health change
        sent is base × factor (`PvP: spell X hit remote player Y for N (base B, the caster's perks F)`). Rig,
        `live-pvp-spell-perks` (new): Augmented Flames, Firebolt at the bot's character; red on a build without it (40),
        green on a7e1e02 (the bot got 60). Still not carried: the burn and the difficulty. Not seen in play yet: the
        caster's game needs this client.
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
      Review 2026-10-24, settled in the rig: the check was armed only when an update saw the player out of the game,
      and loading another character's save while connected did not (no `the character is now`; the server kept the
      old name). Now any loading screen queued since the last update arms it too (`UI::LoadingScreensQueued`).
      Test `live-load-other-character` (the profile's "Prisoner" save loaded while connected): red on 4ef72be, green
      on 5e1e1fd (`the character is now 'Prisoner', was '<player 1's character>' when connecting; connecting again`); live-rename
      4/4 and live-load 9/9 as regressions. Build 5e1e1fd (FUS folders). Not seen in play yet.
      In releases since v1.9.4; the hub has no crash of this kind from v1.9.4 or later (reports up to 2026-10-22).
      The `+24E6D0` crash in the same reports is the next item.
- [ ] **`+24E6D0`: the game's streaming thread on a cell that is gone, after our teleports** (ours; twice on v1.9.4,
      2026-10-18 15:15 and 15:22 UTC, reports 20261018-151548-79ee996d and -152224-4aefa2b9; once on v1.9.3 the
      evening before; once for player 1 on 2026-10-03). Not "the AI thread": the crashing thread runs a
      `QueuedPromoteLargeReferencesTask` (loading a cell's large distant references) for a static whose cell is null,
      in `TESObjectCELL::GetWorldSpace`. Each time it comes 3 to 47 ms after a burst of `Successfully teleported
      actor` lines (4 and 12 in one update, which took 335 and 210 ms), and R15 held the cell we had just moved Ralof
      into. The server sends `NotifyActorTeleport` to every player whenever an owner relinquishes an actor
      (server `CharacterService::OnOwnershipTransferRequest`), and `MoveActor` calls `TESWorldSpace::LoadCell` for an
      exterior cell outside the loaded area, queueing streaming work on cells this player never enters. Upstream
      has the same code. Since v1.9.5 a teleport is not made when the actor is not loaded here and the place it goes is
      not loaded here either (`Teleport of actor X not made`). Rig test
      `live-teleport-far` (new; bot `release ref:<hex> at <x> <y> <z>`): Lydia taken by the bot, the player travels
      away, the bot lets her go as last seen in a third place. RED with the fix switched off (the game loaded that far
      cell and moved her there), 5 of 5 on 4c41069. The hub has no `+24E6D0` crash from v1.9.5 or later (reports up
      to 2026-10-22; the last one was on v1.9.4, 2026-10-18). Not seen in play yet.
- [ ] **Copies the server removes were freed while the game held them** (ours; 2026-10-18 15:35 UTC, two players
      7 s apart: hub reports 20261018-153552-c5fa034b, `SkyrimVR.exe+714EB2`, labelled "fixed", and
      20261018-153541-63d12066, `VCRUNTIME140.dll+4AE1` from our code; player 2's logs of 2026-10-18 17:35 hold the second).
      Both right after the server removed dozens of copies at once: our crash handler found the copy deleted 270 ms
      earlier with 3 handles still in R15 (FF001213), and the other game crashed 27 ms after deleting two such copies.
      `DeleteTempActor` (the server's removal) freed a copy at once whatever its handles, and the wait list for
      unloaded copies (24 slots) overflowed in the same bursts (10 and 41 `waiting list was full` frees).
      Since v1.9.5 the server's removal goes through the same wait (disabled and queued until the game
      lets go), and the list holds 256 instead of 24 (TPTests `[copyremoval]`, a 130-copy burst). Rig test
      `live-copy-removed` (new; bot `npc temp <base> summon`): six summoned bears next to the player, let go, so the
      server removes them. RED with the fix switched off (all six freed with 3 handles held), 4 of 4 on 4c41069 (all
      six disabled and queued). Every rig script's end showed the same before: the bot's player copy freed with 3
      handles held when the bot left (216 times in the rig logs). The hub has no crash of this kind from v1.9.5 or
      later (reports up to 2026-10-22). Not seen in play yet.
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
      run in a history file with build, frame rate and network. Scripted swings now carry the
      controller's velocity. `live-world-object` samples the plate's 3D through the whole carry (`ref 92D04 3d steady
      3`) and requires `0 of N moves rebuilt its 3D`; `live-defender` needs one log line per hit. The rules:
      red before the fix, REQUIRE the preconditions, report rig results as rig results.
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
- [ ] **The other player's body looks off:** hands a little low with the palms together (`VRPose`, structural), legs
      not walking or running with smooth locomotion, the face "looks off" (FaceGen on VR).
- [ ] **Your own sword hitting where the blade is not** (2026-09-24 screenshots; VR_HISTORY "4. Position accuracy").
- [ ] **Less delay for hands and weapons:** after the playback delay is measured (checklist).
- [ ] **Movable objects too broad:** `IsMovableObject` takes form type 36, MovableStatic (waterfalls, banners, large
      animated scenery), not only loose objects.
- [ ] **Gaps from the review of the last 20 commits, not fixed for lack of evidence** (2026-10-24; the code allows
      them, no log shows them yet). What would show each:
      - A waiting copy's id reused: `RunWaitingCopies` knows a copy only by form id, so if the game frees it and gives
        the id to a new temporary actor, `FreeCopy` deletes the new one. Would show as `Temporary Remote Deleted X`
        right after a new `CharacterSpawnRequest ... form id: X` within its 60 s wait. None in 307 queued copies in
        the logs on this PC (FUS, MGO, player reports).
      - "Removed before loading" on the wrong actor: `OnRemoveCharacter` takes `CachedRefId` without the liveness check
        `RunSpawnUpdates` makes. Would show as `Removed before loading ... copy X` where X was by then another actor.
        All 10 such lines so far name the copy made for that very character.
      - Equipping an NPC with no AI process through other routes (`OnNotifyEquipmentChanges`, `AddOrRemoveItem` of
        worn items): only the full inventory waits for a process. Would show as a crash at `mov rax,[rcx+8]` with
        RCX 0 right after an equipment change, with no `kept until the actor is loaded` line. None in the hub since
        v1.9.4.
      - An actor with no cell at connect is skipped (`has no cell; not announced to the server`) and nothing asks
        again unless discovery drops it. Would show as that line with no later `Actor removed` or assignment for the
        id. The line has not appeared in any log yet.
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
