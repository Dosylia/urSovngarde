# Skyrim Together VR: history of the TODO

Everything VR_TODO.md held up to 2026-10-07, word for word: session reports, investigations, the Physics and PvP
queues with their evidence, and the done items. VR_TODO.md now keeps only what is current and points here; the
sections below are not maintained any more. Newest first, as it was. Since 2026-10-31 finished work moves here from VR_TODO.md, word for word.

## Moved from the TODO, 2026-10-31

Finished work and the stories of open items, word for word, in the order they stood in `VR_TODO.md`. What is still open from them has a short
entry there. The first line is from the next-session list.

- **Sleep,** on a server with `bEnableSleep=true`: sleep 8 hours with the other player near. Both clocks and dates move, the other
  player's controls come back at the end, and nothing hangs in the menu.
- [!] **The published Linux server archive crashes when a client joins, on the newer systems it can run on** (fixed on
      b06cceb8 + the local `gamenetworkingsockets` recipe in `xmake-repo`; Linux server only, no client or protocol
      change; not released yet). One player's Linux host hit it. v1.9.6 and v1.9.7 amd64 were run; v1.9.2 to v1.9.5 and
      arm64 were only checked by their symbols.
      The package's recipe moved to `openssl3`, out of reach of our 1.1.1w pin, so its nested build used the host's
      OpenSSL 3 headers while the link took the static 1.1.1w. One function, `EVP_DigestSignUpdate`, then came from the
      host's library at run time. The new CI step `A client joins the staged server` fails such a build.
      - Test: an Ubuntu 24.04 container set up like the Linux CI job, amd64. On b06cceb8 that step failed 3 of 3 (server
        segfault on connect); with the recipe it passed 3 of 3 (bot accepted, server up, no OpenSSL symbol undefined, no
        libcrypto needed). The published archives were run with other systems' libraries through their own loader, not
        on an install: v1.9.6 and v1.9.7 died the same way with Ubuntu 24.04's, and v1.9.7 with the
        `crypto_openssl.cpp(451)` assertion with Ubuntu 26.04's and Debian 13's.
      - To see: the next push rebuilds every Linux package once (new cache key) and CI passes on amd64 and on arm64,
        which has not been built with the recipe yet. Then a game client joins the next release's archive on a Linux host.
      - The public server is not affected: on v1.9.7 it took its first client on 2026-10-30 and the process stayed up.
        Its host is Debian 12 (from its SSH greeting). The archives are built on Ubuntu 24.04 and need glibc 2.38 and
        GLIBCXX_3.4.32, so the loader refuses them on older systems (seen on glibc 2.35), and the recipe does not change
        that. So it does not run the archive directly; only a container with a newer system could. Which build it runs
        is still not written down.
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
- [!] **Reveal Players** (client; in 29aee76d, released in v1.9.7). The press ran on CEF's thread (in the
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
- [ ] **Session B review items left unchanged for lack of evidence** (since 840c92c the client sends movement in
      packet-sized messages through the shared packer, the player's first and the actions before them).
      - An NPC rider sent every 100 ms on a horse sent every 33 ms plays up to 100 ms behind it. To see: a remote
        NPC rider bobbing on its horse. Left with the parked Mounts item: a rider's copy does not ride yet.
      - Late actions in a burst: not shown. Of the 235 `ReplayDiag: refused` lines in the last four logs (one line per 10 s at
        most, so a sample), 164 had nothing else queued, and only 13 had ten or more, a copy's replay at its spawn. The line now
        says how many were refused since the last one (since bd2adc0).
      - One late point from before a jump emptied a copy's buffer and sent it back. Such a point is dropped since bd2adc0 (read
        from the code, no test: the server already drops older reports, so only the relay leg can reorder).
      - Each packet-sized chunk after a frame's first may still straddle two packets: it needs a TiltedConnect
        flag to send each chunk on its own.
- [!] **Another player's follower left standing in your interior** (fixed in build f997b6a, not seen in play). When player 1
      took Lydia outdoors while player 2 was indoors, player 2's Lydia stayed in the inn as a live, unsynced NPC or frozen.
      The teleport is refused, since moving her out with LoadCell stalled the game 622 ms. Now a teleport refused for a
      reference in an interior, with the server's removal right after it, announces her again, and her assignment moves
      her into the world space's persistent cell at the owner's place, with no cell read
      (`Assignment: X left this place with its owner`). Only that case, not at each grid change; a walled city's world space
      is in "Smaller items from the first MGO session". `live-follower-left` (new): red on d2fd00c (she stays in the inn), green
      on 6f521cc (in cell 99A2). To see in play: a follower taken out of an inn is gone from it for the player left inside.
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
      - Build 0567985 (client, FUS and MGO folders). Not reproduced in the rig, which was down then. To see:
        no more crashes in these two places in the hub.
      - Release gate for v1.9.6 on 0567985: bot pairs `run-all-pairs` 14/14; rig `live-door-echo` 6/6, `live-equip`
        9/9, `live-copy-removed` 4/4, `live-revive` 10/10, `live-death` 9/9, `live-load` 9/9, `live-teleport-far` 5/5,
        `live-teleport-far-loaded` 5/5. A first `live-load` froze in the rig's own VR plugins
        (`skyrimvrtools.dll`, PapyrusVR, with Crash Logger hung on it), nothing of ours on the stack; it passed on a
        fresh rig.
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
      `live-down-disconnect` 9/9. Open, for the author: the VR respawn knock skipped instead of fixed;
      `live-revive-hand` passes from the resting hand, never the held one.
      A loading screen during the respawn fade no longer calls the respawn off: only the game's own load event does
      (build f997b6a). `live-respawn-teleport` (new): red on d2fd00c, green on 6f521cc.
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
      it should hold a DisableFunctor for FF00126F. To see in play: no crash at a load right after a player left the area.
      The older saves hold more of these: on v1.9.6-9 player 2's game crashed twice a few seconds after a load, 3 minutes
      apart, with `DisableFunctor` on the stack and no frame of ours (hub #24, nobody has read the saves). One was Continue,
      form FF00126F, likely that autosave; the other `Autosave2_..._Korvanjund01_..._20261018194435`, form FF000B2E, the
      same session. So no autosave from the 2026-10-18 evening in Korvanjund should be loaded, not only that one.
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
- [!] **Smaller items from the first MGO session** (2026-10-20 evening):
      - The Helgen prisoner carriage crashes HIGGS when grabbed (`SkyrimVR+0xADF694`, both games). HIGGS walks the
        carriage's links and reads its anti-tip action (`bhkTiltPreventAction`) as one. Not our code; don't grab it.
        Unproven without our mod.
      - A copy the server removes in a cell the receiving game can't reach left the loaded real reference
        unannounced (Ingrid on player 2's side). An interior was fixed earlier. Taking the same move to a walled city's world
        space (build 4df623a) was a mistake and is taken out again. In the play test of 2026-10-31 08:10 it moved Lydia into
        Windhelm's persistent cell in the game of the player left outside, her 3D was rebuilt there, and she stood stuck in
        the gate with nothing moving her. `live-follower-gate` passed it 5 of 5 against a bot, and proved nothing about a second
        real game; the script is deleted. A gate is back to v1.9.7: her teleport is refused and she stays a live, unsynced
        NPC where she stands, which is the open problem this tried to solve. `Assignment: X left this place` now only follows
        an interior.
      - An ice wolf (33482) bounced owners and its body jumped about 8000 units into Helgen at 21:08; the cause is
        unknown. Only player 1's log is left, from before `CorpseDiag`. On 2026-10-30 no actor changed hands more than twice in
        either game. If it comes back, `Gained ownership`, `CorpseDiag` and `JumpDiag` name it.
      - VR players connect with F6 and never see the connect page's "don't connect during the Helgen intro" warning.
        What goes wrong is known: in the vanilla intro player 2 stays tied, since freeing the player is a scene in each game and
        Ralof there is player 1's copy (Fourth MGO session, "a scene limit"). No crash of ours in three MGO sessions through
        Helgen. A HUD line while `MQ101` is enabled and not completed (stage 900 completes it) is not in, as it could not be
        tested: the quest would not start in the rig (`Quest.Start` False, also after `Reset`). "Next play session" says it for now.
- [!] **A protected NPC's copy that its owner says died, and the hand-over after it** (fixed by 10b98ee5, see the
      protected-NPC item: a copy of a protected or essential NPC now dies on its owner's word). On 206a1d36 the rig's
      `live-death-protected` (Lydia) failed 2 of 2: the copy stayed alive (`now dead: false`), and once this game took
      her over it sent `Death sync: local actor X is now alive`, so she lived again for everyone. That test also checks
      the hand-over, which `live-protected-copy-dies` does not. Ok on 317f7ce1 (2026-10-28), hand-over included. `live-death-copy` (an
      unprotected human) is ok on 206a1d36.
- [!] **A dead copy near the player lost its 3D and was made again** (rig, `live-death-copy`, once in 3 runs,
      2026-10-26): 4 s after the copy died, `CopyGone: X removed as remote; the actor is still there, no 3D`, then
      `Announcing this cell again` and a new copy spawned dead: the body vanishes and reappears. Its pelvis was 15 units
      below its feet in the samples before. Cause unknown. `live-death-copy` ok in every run since, 6 on 10-30. Of the 381
      `CopyGone` lines of 10-25 to 10-31, 2 more are this, both `live-body-grab` on 10-30 (9.2 s after the death, as the grab
      starts, and the reason it then held nothing); the grab does not cause it, as a run on e20c11e kept its 3D and held. None in play.
      A TEMPORARY line now says more when it comes again (`CopyGone: dead copy X lost its 3D N units from the player; ...`).
- [!] **A door a script opens (`SetOpen`) is never sent** (sync audit 2026-10-26; rig `live-door-script`, failed on
      206a1d36 2 of 2 and on 43e6a70: door 9B0A5 open here, closed for the other player, and no door line in the
      client log). In the civil war, quest scripts open and close most doors. Decided: the party leader's game sends the
      doors its scripts move.
      Build 4df623a (client only, no protocol change). Scripts call `SetOpen` through the native's entry,
      and that entry now reports a door that moved; our own `SetOpen` calls the game's function, so a door set from
      another game is not sent back. The leader's game sends it as an activation, which sets the door over there and
      updates the state the server keeps. Load doors are not sent. A member's script stays its own, and that game no
      longer lets an older kept state shut the door (`left as this game's script left it`) until someone uses it.
      - Rig: `live-door-script` RED on e20c11e (`door F8239: closed for the other player`), ok on 4df623a (the bot told of
        both moves, nothing sent when a script asks for what the door already is). `live-door-script-kept` RED on e20c11e (a
        kept `closed` shut a door a script had opened), ok on 4df623a. Regressions ok on 474ec2b: `live-door-state` (17
        checks; its counts are for the fix), `live-door-join`, `live-door-motion`, `live-door-echo`.
      - Not seen in play. To see, with a quest that moves doors: `Door X (...) opened|closed by a script here; sent` on the
        leader's side, `Door X: opened|closed in another game ...; set open|closed here` on the member's.
      - Limits, from a read-only review; none shown in play. A door a member's script moved ignores the kept state until
        someone uses it in that game's sight, so a use made while that member was away is missed; telling which is newer
        needs a per-door sequence, a protocol change. It also lasts through a save loaded while connected. A script
        reaches every player in the door's cell, party or not, as an actor's use already does; this matters only for
        parties on a public server, still undecided. A leader's script that moves a door in the one round trip after
        arriving can be undone by the state coming back.
- [!] **A follower in both players' saves bounces between the two games** (KNOWN_ISSUES "Shared follower"). Each game
      asks for her back the moment she goes, and a follower's claim is granted at once. Rig, `live-follower-shared` on
      e20c11e: RED, the game took her back within 18 ms on each of five passes, epoch 3 to 11.
      Build 4df623a (client only): three passes, each within 2 s of this game getting her, leave her with the
      other game until she leaves this player's view or the connection ends. A hand-over at a loading screen does not count:
      she has been here longer than that. Refusing the claim on the server would not do: it never learns she was dismissed,
      so nobody else could hire her. `live-follower-shared` ok on 4df623a; `live-follower`, `live-follower-left` and
      `live-follower-stays-in` ok on 474ec2b. Not seen in play. To see: `Follower X ... was given to player Y; she is that
      player's follower too, so she stays there`, once, and no more `asked for back`.
- [!] **A dead copy standing, when its owner's health got there before its death** (rig, seen 2026-10-31 in `live-corpse-in-view`, the picture shows it
      upright; fixed in build 474ec2b, not seen in play). An owner sends an NPC's health and its death on two timers. `NPC X health corrected from 100 to its owner's 0`
      came 18 ms before the death, and that correction set the health straight to 0, the one path without the 1 health floor. The copy was dead with no death
      played, so the death found it `already dead here` and it stayed on its feet. A body in that state also pulls a moved ragdoll back to where it
      stands, 200 units a second: the first time a corpse move was seen undone in the rig. The correction now keeps the floor and the death kills it.
      `live-copy-health-first` (new; bot `npchealth`, the death 2 s after the health): red on 4df623a (pelvis 51 units above the foot, `already dead here`), green
      on 474ec2b and c382599. Regressions: `live-copy-came-dead`, `live-death-copy`, `live-protected-kill` ok; TPTests 105. It is not what moved the captain on 10-30:
      that session has no such correction, and the captain died normally in both games. How often it happens in play is not known: the logs mix rig and play.
- [!] **A copy's body is shifted after it lands** (rig, `live-death-replay`, 3 of 4 runs, 2026-10-27): each game drops a body with its own
      ragdoll, and the copy lay 69 to 108 units from where its owner's corpse lies and was moved there, a jump on screen (68 to 220 units in
      play; 40 first moves in the kept logs, median 122, 38 of them with no bones from the owner). Since build 4df623a a move waits until the
      player sees neither end of it (`CorpseDiag: X's ragdoll lies N units ..., in the player's view; it waits until the player looks away`).
      `live-corpse-in-view` (new): red on e20c11e (a body moved 357 units with the headset on it), green on the build with the rule (no move
      in view, moved at the look away; the picture shows the headset on the body), clean on 474ec2b. Not seen in play. To see: bodies stop
      jumping while you look at them.
      A body its owner moves is the exception. Its bones are only drawn over the copy, so its ragdoll stays behind and it would flash back
      to the old spot. Now the ragdoll is moved the moment the bones stop, seen or not, and a drag starts the move limit over. This is also
      the rest of "the body flashing back to where it was picked up" (10-24). `live-corpse-dragged` (new; bot `npcmove ... posed`): red on a
      build with the rule and without this exception, green on 4df623a. Regressions on 4df623a: `live-corpse-place`, `live-corpse-moving`,
      `live-corpse-owner-alive` ok; their head now turns away first.
      Also fixed there, an older bug: a body whose owner left in the middle of moving it stayed frozen in its last pose, drawn over whatever
      its ragdoll did. `live-corpse-owner-left` (new): red on e20c11e (posed every 5 s after the owner had gone), green on 4df623a.
      Left as it is: a body put on a table within 64 units falls back when the bones stop, since the distance is flat. Tried and taken
      out, no ground under a far target: a position beyond the loaded grid never reaches the copy (the server relays by range), only a
      dragon's can (10 cells), and `live-corpse-far` showed nothing to fix.
- [!] **Play session 2026-10-30 18:13 (both on v1.9.7, player 1's server, Fort Amol; player 2's clock runs 5.2 s ahead)**
      - Player 2 left at 18:29:19 and came back at 18:29:51 (`Disconnected from server 4`: the disconnect asked for by F6 or
        the menu, no automatic retry). All of its NPCs went to player 1, and most of the session's desync follows from that
        rejoin. Its game kept playing offline: the fox 107CC4 and the mages E68B3, E68B4 were dead there at the rejoin.
        The two mages were fixed by `DeathWins` at the rejoin, as designed (seen in play). A chosen disconnect now logs
        `VRConnectService: disconnecting, asked for here`.
      - Fixed in build aa045e7 (client and server, FUS and MGO, on v1.9.7, no protocol change), a dead body taken without a hand:
        `CanClaimOwnership` let any claim have a dead body, so the automatic `LeaderAssignment` of a joining game took bodies
        nobody grabbed (19 such transfers this session), and that game's corpse became the truth. Actor 15, the Imperial
        Captain, went from player 1 to player 2 at 18:30:01. Now only a claim takes a body, and a leader's automatic claim
        skips bodies too. Bot pair `bodyassign`: red on v1.9.7 (`taken from us` with no claim), green on aa045e7, 8 checks,
        a grab still takes it. `ownerchurn`, `deadfar` ok. The leader's claim has no red run: the bot cannot lead a body here.
      - Fixed in the same build, a copy dead and standing (the fox, 18:30): a creature that died while its game was offline is
        registered with no health, its copy died as those values were set, with nothing played, and the `came dead` kill then
        found it dead and left it on its feet. `ValuesUntilKilled` leaves it 1 health until that kill. `live-copy-came-dead`
        (new; bot option `npc temp <base> dead`): red on v1.9.7, pelvis 50 units above the foot, green on aa045e7, 6 units.
        Regressions on the fixed client: `live-death-copy`, `live-corpse-place`, `live-dead-stays-dead`, `live-body-owner`,
        `live-body-take`, `live-revive-copy`, `live-map-pointer` ok; TPTests 95. `live-body-grab` fails at HIGGS itself (`grab
        None`), as it did on 10-24, so it says nothing here. Neither fix seen in play yet.
      - The flickering corpse (18:33), limited in build d2fd00c, cause open: `PlaceCorpse` moved player 1's ragdoll of the captain
        134 times in 4 minutes, up to twice a second. Moves in a row now wait 1, 2, 4, 8, then 16 s, and 30 s without one start
        over. `live-corpse-moving` (new; the owner's body moves 40 units every 0.25 s, no bones): red on aa045e7, 21 moves, green
        on the fix, 4 moves, ending 4 units from where the owner left it. Replayed on the session's log, 134 moves become 15.
        Regressions on d2fd00c: `live-corpse-place`, `live-death-copy`, `live-dead-stays-dead`, `live-body-owner`, `live-body-take`,
        `live-copy-came-dead` ok; TPTests 95. Not seen in play. With the ownership fix above that corpse also stays with its
        killer's game.
        What moved the target: the big moves follow grid changes. Over all kept logs, 20 of 162 moves of 1000 units or more came
        within 1 s after that game's own grid change, where chance gives 0.5. The reported position ran 35,000 units along
        with the players, and three times it lay within 600 units of player 2 about 0.5 to 1.2 s after their grid change. A grid
        change alone does not do it in the rig: a body left behind is dropped cleanly, and one kept above the ground in step
        with the player stays where it is put. One thing did show in the rig: a body put where there is no floor falls, 2457
        units in 3 s, and the game puts it back on the ground about 1270 units from the player; its owner then reports that
        as a jump of 3163 units. Whether play did the same is not known. `CorpseDiag: own body X sent N units` now names each
        jump of 300 units or more, what it was read from, the reference and its cell, and each move of a copy gives its cell too.
        A dead body that changes hands performs `Unequip` every frame, refused (100 a second in the rig): noise, not a live body.
      - Not explained, player 2's copy below the floor while down (18:26:44, 10 s; its hands at foot level). `live-revive-copy`
        does not reproduce it: the pelvis stays above the reference. Its replay never reaches the state of that session, where
        the owner's knock state 5 was copied onto a copy with no ragdoll (`keptMask` in AnimationSystem) and `bleedOutStop` was
        refused. Not changed on that guess. Each 5 s a copy with a VR pose now logs `VRBodySync: actor X body -- reference z,
        3D root z, head above the root, knock state`. A rig run that throws the player while down gave no result: a push of
        20 carries the player out of the bot's reach, a push of 3 does not knock it down.
      - Not explained, player 1's slow frames with the rabbit 107C7E (18:37:01 to 18:37:31, 178 over 50 ms, our update 0.37 ms).
        The rabbit was alive, held in ragdoll, so no body pose goes out for it, which fits its delay on the other side. Now, under 15
        frames in a second, `Stutter:` logs where the main thread and the busiest thread are, at most 12 times a session. Forced
        in the rig it works; there it caught the main thread in `D3DCOMPILER_47.dll`, a shader being compiled, a lead for hitches
        in play. No rig run of a held rabbit: it runs from the hand, even with combat AI off, and the hand holds nothing.
      - Late updates, narrowed: player 1 sees player 2 stop for 0.3 to 2.2 s, never the other way. 21 of the 22 windows with a
        gap over 300 ms lie in the fight, up to 18:31:20, and they end with it. Voice plays no part (the heaviest minutes had
        none), nor do cell changes (27% of windows with one, 22% without). The two longest gaps have `Silence:` lines of the
        same length on player 2's side (1194 ms against 1099, 2310 against 2234), and that game's whole log stops with them: its
        game stood still, the connection did not. That proves those two; the shorter ones were under the 1 s line.
        What stops the game in a fight is open. Build d2fd00c: `Silence:` from 250 ms, `Perf` gives the longest frame, and `Stall:` reads
        the main thread, the update thread and the busiest thread at 200 ms into a frame, eight reads and one more every 30 s.
        Forced on the rig it caught a 500 ms stop. At the rig's grid crossings it shows the main thread waiting and the busiest
        thread reading files through `usvfs_x64.dll`. `Connection:` stays, every 30 s.
      - Lead, threads: player 2's game reached 802 threads tonight and 4731 on 10-24, nearly all started through ucrtbase; player 1's
        stays near 170. It is not tonight's performance: its frame rate rose while the threads did. The old sample read only the
        newest threads. `ThreadOwners:` now reads 24 across the group and counts them by module (forced in the rig, it reads).
      - Frame time: player 1 33.5 ms (29.0 on 10-28), player 2 26.2 ms (unchanged). Our update is 0.36 ms, render hooks not counted.
      - A script switching a reference off and on in one frame, over and over (an activator of a mod, 1190 lines on player 2): every
        other game redid each switch. Since build f997b6a only where it ended goes out. `live-ref-enable-leader` ok; no red run.
      - Reviews of these fixes found, and f997b6a fixes: a body with no health killed by raw values at two more sites, a kill before
        the 3D at a third, a leader's claim on a copy waiting for its kill, a claim held 10 s on an NPC that died meanwhile.
        Bot pairs `bodyassign`, `ownerchurn` ok; `live-copy-came-dead`, `live-death-copy`, `live-copy-removed` ok; TPTests 99.
      - To see: "Next play session", a fight, a ride away from bodies and a rejoin near them.
- [!] **Public reports 2026-10-29 (v1.9.6, two players together in Whiterun, Quest 3 and Quest 2)**
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
        the rig. The owner's own use of a load door, replayed on the copy, stops at the same refusal
        (`live-door-copy-replay`, 2026-10-31).
      - `SkyrimVR.exe+0x117B10` (SE 11308), Whiterun, the other player: a garbage pointer (0x3F8CCCCD00000001) under
        Skjor's sword, 5 s after this game spawned Skjor (`Spawn Actor: 1A691` at 18:46:45). Before that, Skjor was
        removed here twice (18:40:03, 18:41:22) and in the other game within 2 s each time; the two games had Skjor
        under two server ids (20004D there, 20010A here); his copy activated something on its own 12 times in the
        other game (which were load doors the log does not say); and here his AI asked to unequip 150 times a second
        for three minutes, all refused by the game (`ActionFlood`, not sent). The door churn is what build 1a569f4
        stops; the rest is not linked to the crash. Not proven; needs that player's
        `crash_UTC_2026-10-29_01-46-45.small.dmp`.
      - `skyrimvrtools.dll+0x71B5`: the known load during our death sequence.
      - 2026-10-30, the same two players, v1.9.6, both in Jorrvaskr: `20261030-022832-3012cf6f` is the script engine on a
        freed temporary form (open, not ours by its stack); `20261030-040047-015fab90` is our `AnimationSystem::Update`
        lookup again, in v1.9.7 by the `GetById` guard. How Engine Fixes' cache can keep a freed form, and a candidate
        guard for the game's own lookups, are in KNOWN_ISSUES section 6. Not built. Waiting on a test or action: whether
        to build that guard; a game without `FormCaching` is the test that would prove or clear the cache.
- [!] **Play session 2026-10-28 18:01 (player 1 on 43e6a70, player 2 on 10b98ee5; about 30 min, a fight with archers)**
      - The revive banner ("<name> is reviving you") was cut off at both ends with a long name. A line too long for its picture is now
        written smaller until it fits (`BannerPicture::Line`). TPTests `[banner]` red before, green after. Not seen in play yet.
      - Player 2's crash at 18:29:41 at Lost Knife Hideout: KNOWN_ISSUES, "Crash within a second of travelling away".
      - The freeze closing the unpaused map: cause and fix in KNOWN_ISSUES, "Freeze after closing the unpaused map". Seen fixed by
        player 1 (30 closes online, 2026-10-30). Still to see: player 2 on this build, and a session together.
      - The crash closing the unpaused skills menu: an unpaused hide on a worker thread freed the tree while the main thread drew
        it, so the tree is now held and let go on the main thread. Seen fixed by player 2 (85 closes online, 2026-10-29). Still
        to see: player 1, reading `Skills menu: its tree model ... held at the unpaused close` and `... let go on the main thread`.
      - 2026-10-29 19:13:55, player 2: a confirmation box over the open map, then a 31 s loading screen (the game's load, DynDOLOD and
        vrmapmarkers on the stack, none of ours), then the main menu. Cause unknown.
- [!] **Play session 2026-10-26 18:25 (both on aad1262c + the get-up and lock change; player 1 leads; PvP on)**
      - Changed on 92ff338d (client only, FUS folder): a PvP hit is counted once. Arrows and spells: the defender's
        own replayed projectile decides; the attacker's report of it is dropped while a projectile of that player
        launched here less than 3 s ago may be in flight (`PvP: a hit of N from player P not taken: its projectile is
        replayed here, and that copy's hit decides`). That window dropped melee too, 11 of 134 hits on player 2 on 10-30, so
        since build f997b6a it follows the distance: 0.3 s plus 500 units a second, at most 3 s, and only a launch the game
        made counts (`[pvpprojectiles]`). It is for arrows alone now: see PvP spells. Melee: the attacker's report, weighed by the defender's rule, as
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
        Fixed, death wins (both games' saves disagree on who is dead, the cause of the standing ones seen after
        reconnecting into cells cleared the day before): a generic (not unique) NPC dead here that its owner's game
        has alive stays a body here, is not replaced by a living stand-in, and its owner is sent a killing blow
        (`DeathWins: X (server id S) is dead here but alive in its owner's game`). Unique NPCs still follow their
        owner. Build d6b97f6 (dirty, on 206a1d36). Rig: `live-dead-stays-dead` (the Mistwatch bandit 45A3F, dead in
        the test save, registered alive by the bot while the game was offline): RED on the build before (the body
        stood up again), green on d6b97f6 (stays dead, the bot gets the -100000 blow); `live-owner-blow` ok.
        Not seen in play yet. To see: no NPC killed earlier gets up when the other player arrives or reconnects.
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
- [!] **Play session 2026-10-25 19:15 (Fort Neugrad; both on 10749cfa + the marker fix; player 1 leads)**
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
- [!] **Play session 2026-10-25 17:12 (PvP; player 1 on 779d7a82 + the menu hardening)**
      - Player 1's game ended at 17:21:22 mid-fight (clashes with player 2's sword, player 2 at 22 health) with no
        trace: no crash block or dump of ours, no Crash Logger file, no Windows error event or report; frames ran
        until 17:21:21.93 (PhysicalCollisionVR's log), SteamVR saw the pipe break at 17:21:22.94, the server timed out
        at 17:21:32. Our log ends 0.6 s earlier, since it is flushed once a second. The launcher never ends the game.
        So the process ended past every handler: TerminateProcess or ExitProcess called inside it, a kill from
        outside, or a stack overflow too deep for any handler. No evidence for a cause, so no fix. To know next time:
        a line written straight to disk when the process ends itself (NtTerminateProcess on itself, with the caller),
        and the log flushed at once for warnings and errors. Both done in build f997b6a: `logs/process-end.log`. A normal quit shows
        there through Engine Fixes' fast exit, or as `Exit:` in the client log. A game that vanishes with neither was killed from
        outside, failed fast, or overflowed its stack.
      - Our player marker relayed to the party as a leader's switch: `Player markers: 8D005040 follows player 2 ...`
        at 17:16:22.738, then `ReferenceEnable: 21 reference(s) ... (first 129:5040 off)` 27 ms later (MoveTo
        switches the reference off and on). In the member's game the same form is its marker for the leader, so that
        one was switched. Fixed on 10749cfa + this change (client only, FUS and MGO folders): `IsSharedSwitchKind`
        leaves out SkyrimTogether.esp's references, on both the sending and the applying side. Rig:
        `live-marker-not-relayed` (new; this game leads, the bot walks out of sight and back so the marker moves, and
        must never get a switch of 5040; new bot check `refenabled <hex> none`) RED on 779d7a82 (5040 relayed off and
        on three times), ok on the fix; `live-ref-enable-leader` still ok. To see in play: no `first 129:5040` in the
        leader's `ReferenceEnable` lines, and the member's map shows the leader.
- [!] **Play session 2026-10-24 afternoon** (both on v1.9.6-9-g29aee76d; player 1 leader, the Jagged Crown):
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
        its 600-unit send range is measured from there too.
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
- [!] **Play session 2026-10-25 morning** (both on c4caf8a with the party-sleep merge; player 1 on CSX csx3.18-VR and
      Engine Fixes 7.7.1 for the first time; Whiterun during the battle, then sleeping).
      - Player 1's crash at 09:53:52, 15 s after arriving in Whiterun through Dragonsreach's door 16707: an access
        violation in the game's Havok world step (`SkyrimVR.exe+0x1e05db`, bhkWorldM and ahkpWorld in registers, a
        garbage pointer made of float data). No mod's code on the stack. Last night's freeze came right after the same
        door into the same battle, with the old shaders, so the shader switch is not the common part. Not Ralof, whose spawn
        came 33 ms before: his reference had no 3D, and the crash report's stack names Irileth's animation graph
        (`GraphPhysicsStepListener`, `BShkbAnimationGraph`, spawned 9 s before). The game's quaternion-to-matrix helper was
        handed a destination from a table pointer in her graph's data that read as two floats; the first frame is that helper,
        not Havok. One sighting, and a report holds no dump, so what wrote over that pointer is unknown.
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
- [!] **Play session 2026-10-24 evening** (both on fa5e36c; player 1 leader and server; Windhelm, then the Battle for Whiterun).
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
        animation variables. Not a regression. Decided 2026-10-31: a replacer from the capture. Its graph has no Speed variable, so
        it sends Direction, the turn variables and the state ints (`GameFiles/.../SkyrimTogetherRebornBehaviors/IceWraith`, in FUS's
        mod folder; both players need it, it goes out with the release). `live-creature-icewraith`: 0 of 320 updates with variables
        without the folder, 324 of 324 with it; the bot reads what is sent, no copy's look. The look is checked in the next play session.
      - Moving a lantern by hand showed nothing on the other side (19:28, outdoors east of Windhelm): neither game
        logged a world-object move. Which lantern, and what kind of object, is not known; no change.
      - Ulfric's talk not heard by player 2 (19:31): all eleven lines reached player 2's game and were started there;
        player 2 had the journal and console open (the game paused) from 19:31:15 to 19:31:36. Whether sound came out
        is not logged. Done in c4caf8a: `played here (game's answer A, D units from the player, 3D yes/no, pause counter N)`.
- [!] **Review fixes, doors, ownership, transport** (from the reviews of the last two commits and of playback session B,
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
- [!] **Playback session B: unreliable movement, actions on their own** (started 2026-10-24; a PROTOCOL CHANGE, to land
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
      - In play on v1.9.7 (2026-10-30 18:13, both players and the server; player 1's log): `PlaybackDiag` median 77 ms
        (62 to 144), blocks still decided (`Defender's rule`, 16 blocked, 1 landed), no `MovementSend:` message over
        1100 bytes. Still to see: the other player's hands and sword as smooth as before, no hitch when the connection
        drops a packet.
      - Session A's delay at its 250 ms cap after a revive did not come back on v1.9.7: around the revive of 2026-10-30
        18:26:59 it read 65 to 84 ms. That session's one cap, 18:37:27 to 18:37:57 on player 2's side, follows player 1's
        game falling to 17 fps (snapshot interval 205 to 233 ms), so the delay followed what was sent.
- [!] **The owner's decisions of 2026-10-24** (asked all at once). Status of each:
      - Doors at join: the server keeps a door's state after each activation it passes on, while someone is in
        the door's cell, and sends it with the cell (`ObjectData::DoorOpenState`, a PROTOCOL CHANGE: every player
        and the server update together). The entering game sets a non-load door to it (`Door X: open|closed in the
        other games when this game came in; set ... here from state N`); setting it sends nothing back. Only
        activations count, not a game's own snapshot, so a door a script opened is never shut by a stale state.
        Limit: a door that moves with no activation (a script's SetOpen) after one leaves the kept state behind; the
        server forgets it once nobody is in the door's cell. (The leader's scripts now update it, and a member's are not
        undone by it: see "A door a script opens".)
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
      - Map icons: if zooming in play shows the other player's icon, make the client force the map's regather.
        Waiting on that check in play (the checklist's "Map icons" line).
- [!] **The rig suite on MGO, player 2's PC** (2026-10-23, 2½ hours, v1.9.6-3-g423f3502, about 200 checks in seven
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
- [!] **Other players on the world map** (the owner's request, 2026-10-23). The markers (SkyrimTogether.esp,
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
- [!] **Fourth MGO session, 2026-10-22 20:03-20:31** (both players on v1.9.6, host's server; player 1 hosted). Helgen
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
        - `IsRemoteBodyGrabbedHere` already ignored motion for 1.5 s after the owner's position moves, so these pulls came
          while the owner's corpse stood still and player 2's ragdoll drifted off by itself (four pulls back). Since build
          f997b6a it also ignores 1.5 s after our own placement. No red run: the rig cannot make a ragdoll drift on demand.
      - Ours, Ralof flickering in player 2's game. Player 2's game ran its own AI on its copy of Ralof (player 1's).
        That AI walked the copy through load doors: 909D2 into the keep (20:11:49-20:12:04) and 91608 out of the cave
        (20:25:53-20:26:22). Each time the copy went to the other side, was removed and was spawned back: 15 removals
        in 3 s cycles (`Activation of 909D2 by 2BF9E, another game's actor, not sent` / `Actor removed, form id:
        2BF9E`).
        - Fixed in build 1a569f4 (client only, no protocol change; FUS tools folder): a copy of another game's actor
          never goes through a load door on its own here (`HookActivate` refuses it); the owner's game reports where
          it went. The owner's own use of the door, replayed through `OnActivateNotify`, stops there too. Logged:
          `Load door X: not used by Y, a copy of another game's actor; its owner's game reports where it goes`, at
          most every 5 s.
        - Rig, `live-door-copy-load` (new): the game makes the bot's NPC copy use Mistwatch's load door. Red on
          a547eb5 (the copy went through and unloaded, then was removed and spawned again), green on 1a569f4, 10
          checks (the same copy stays loaded where it was, spawned once). Regressions `live-door-echo` 6/6,
          `live-copy-removed` 4/4. Seen in play: 16 refusals in player 1's log on 2026-10-24 and 10-25. The owner's own use, replayed on the copy, stops at the same refusal (`live-door-copy-replay`, 2026-10-31). Not covered: what the refused copy's AI does at the door (it tries again while its package
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
      - "NPCs not following" (about 20:21): nothing in the logs. No ownership changes then, no quiet stops. Quest NPCs
        follow the game that runs them (player 1's), not player 2.
      - Player 1 rejoined once (player 1, then player 2 on the server); player 2 was player 3. Player 2's clock runs
        about 6 s ahead of the server's.
- [!] **Third MGO session, 2026-10-21 22:01-22:21** (both players on d7e7105, after tweaks to the list): a lot more
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
        What was left of that line (2026-10-31, the last four logs): 3 lines, all a temporary reference
        (`FFFFFFFF:10D4`, `ED4`, `FC0`, form 0 here), so no door. Such an id exists only in the sender's game.
        Build 4df623a: a temporary reference's activation is no longer sent. `live-activate-temp` RED on e20c11e (the bot
        was told of `FFFFFFFF:12C7`), ok on 4df623a; it also checks that a bottle every game has is still sent.
      - Fixed in build ac5cace (client, FUS and MGO folders, no protocol change): the sender skipped every object
        whose lock had any flag (`flags & 0xFF`). A leveled lock keeps its flag once unlocked, so such a door's
        openings were never sent. Now only the locked bit counts.
      - Logged now: each door activation sent (`Door X ... sent to the players in cell`), each one received (done,
        or left as it is when the states differ, and `locked here` when the copy's activation cannot open it), and
        the id of an object not found. Not rig-tested: the rig was down then.
      - Still open, the owner's call: a load door locked here by this game's quest stays shut when the other player
        opened theirs. A door that does not lead elsewhere now opens and unlocks here.
- [!] **Second MGO session, 2026-10-21 20:20-20:41** (both players on cdec699, 1992 mods each, names right: the
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
          next to the player, let go far away): 5/5 on 0567985.
        - To see in play: no `Mod update took` over 100 ms when a lot of NPCs arrive or leave, and the
          `InventoryApply` counts.
      - Ours, scripted scenes: each game ran AP's Thalmor ambush, so each spawned its own Thalmor and also got the
        other's as copies (twice the enemies). Their bodies were pulled 1000-2400 units to where the other game had
        them (CorpseDiag). Player 2 found no loot on them; the log does not show why.
      - Ours, NPCs popping in: the spawn path re-enables an NPC the other game has active and moves it to the
        player's cell (`Disabled actor is being re-enabled`, five AP Helgen guards at 20:36:28).
        Looked at 2026-10-31 (54 such lines in the last four logs): the two games' worlds disagree, and the spawn does what
        it was written for. Of the 13 `Skyrim.esm` NPCs, 9 hang off a group marker (8 off `E8BDD`, 96 children; 1 off `2E975`),
        1 is authored disabled (`37F92`), 3 have no parent. The rest are a plugin's soldiers, not looked up. Bandit `C64AD` was
        loaded here at 19:32:47 on 10-25 and read disabled at 19:32:54, with `ReferenceEnable: 196 reference(s) switched by
        this game's scripts` in those seconds: this game's quest scripts put the group away while the other game still had it.
        Each spawn then enables one, and a leader's game claims and runs it. Not changed: a game that refused would have
        NPCs it cannot see attacking its player. Which world wins is a design question, not a bug in this line. No marker is
        logged, so the flip itself is inferred from the timing.
      - Alduin's refused `roarStart` was not why he roared without attacking. All six lines are type 6, a transition without
        animation, which plays nothing on the owner either. Once player 1's game ran him it refused exactly half of his 12 to
        24 tries a second itself (`ActionFlood`), so his AI spins on it in both games. In the 90 s his copy was here his owner
        sent no projectile at all; when he does attack, his breath is a spell made at run time (`spell FF0015CE`), which other
        games cannot name (see `No projectile found`). `ReplayDiag: refused` no longer counts type 6 (since bd2adc0).
      - Not synced: the Helgen keep door stayed locked for player 1 and opened for player 2. Each game runs its own
        scene, and neither log has a stage change for it.
- [!] **Freeze at a load: the game's allocator purging cells waits for good** (player 1, 2026-10-18 21:29, fast
      travel near Whiterun's battle; the same main-thread stack as a public player's freeze at 08:52 and player 2's of
      2026-10-11 21:36). From the bottom: a model copied for a load (`NiObject::Clone`), our pass-through
      `HookFormAllocate`, `MemoryManager::Allocate`, a memory-manager callback, `TES::PurgeBufferedCells`, then a wait on
      a semaphore that never comes (player 1's handle checked live: a Semaphore, not signalled). Private memory was
      13.3, 15.5 and 16.3 GB; threads 703, 2494 and 152, so the thread pile-up below is not what they share. FUS then
      had Engine Fixes VR's `MemoryManager = false`, so this was the game's own allocator. Since 2026-10-25 it runs
      Engine Fixes VR 7.7.1 with `bOverrideMemoryManager = true`, which replaces that allocator (our client chains to
      Engine Fixes' allocator). The small freeze dump keeps too little of the other threads' stacks to name the one
      that should release the semaphore. Ralof's `Spawn Actor` in the load of both the 08:52 and the 21:29 freezes is the game
      building his 3D, not our spawn: the client's update stops during a load (10-25 to 10-31: of 349 loading screens, none
      had one of the 588 spawn requests, 32 teleports or 136 stale-copy moves, and no spawn came in the 3 s before one), and the
      game's `MoveTo` (VR +0x9E90E0) has no call to `TES::PurgeBufferedCells`.
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
- [!] **A public group of four on a 1400-mod list** (logs of two of them, 2026-10-17, both on v1.9.3; the same group
      sent the Ralof and start-up crash reports). Fps fine (median 80-87); private memory 10 to 28 GB over 2h15.
      - **Freeze (player 2, 19:06, 44 s then killed):** the main thread waits in the game (`SkyrimVR+0x5b8d4c`, as
        player 1's Alduin freeze) under HDT-SMP; the other thread reported is in RaceMenu's skee64 and OBody. None of
        our code on either stack; it came 13 s after a burst of NPC copies we spawned (412 `already spawned`
        warnings in the minute before).
      - **Fingers not synced on their skeleton:** 536 flattened bones and the forearm-to-hand parent check fails
        (`no usable flattened bone array`), so no copy's fingers move and the local ones are not sent. The body,
        arms and head are posed from nodes. Since build f997b6a the check takes up to three hops, only when no field gives the
        hand's parent directly, so a skeleton that worked is unchanged. MGO has this skeleton (27 such lines), FUS does not, so
        no rig run. To see on MGO: no `no usable flattened bone array` line, and fingers move on the copy.
      - **Other players' projectiles refused by the game (`No projectile found`):** cause found. All 468 events in the crash
        reports, on four installs, are launches with every id empty. The caster's spell and projectile were forms a mod made
        at run time (on MGO, NPC Spell Variance's copies: `Projectile launch: ... base FF0015CC, ... spell FF0015CE`). Such an id
        means nothing in another game, and a fire-and-forget spell reaches another game only by its launch, so those spells
        stay unseen there. A real fix needs the other game to recognise the spell, which is not small. Since bd2adc0 the launch
        is not made and the raw ids are logged (`Remote projectile from X not launched: its projectile M:B of spell M:B does not
        exist here`; mod id FFFFFFFF is such a form). `live-pvp-projectile`: `No projectile found` on e20c11e, the new line on bd2adc0.
      - The reconnects were the name check during character creation (the intro's name, then the chosen one); v1.9.4 compares the
        name only after a loading screen. `OnNotifyActorTeleport: failed to retrieve actor` 61 and 50 times: the server tells every
        player of a teleport, for a reference that is not in memory here, its cell not being loaded, or of a plugin this game
        lacks. There is nothing to move and the reference is placed by its assignment when it loads, so it costs nothing.
- [!] **A removed copy "freed after its 60 s wait while the game still holds it" was a false alarm for most copies** (build
      4df623a; `TPTests [copyremoval]` pass, rig `live-copy-removed` 4/4, and on that build copies end with `the game let go of
      it`; not seen in play). A copy the server removes is disabled and waits. In the four FUS logs of
      10-25 to 10-31: 281 copies held, none ever let go, 161 `CopyFreedHeld` lines (105 with 2 handles, 55 with 4).
      Read in the rig's memory on e20c11e (`live-copy-removed`, read-only): the "handles" are the object's strong references,
      and a disabled copy reads 2, one of them the handle table's own entry, the other gone with it at the delete. `Delete()` drops
      both and the object is destroyed there, within 100 ms; its memory held other things 2 s later. So the wait for zero
      could never end. What the wait does protect from is what no count shows, raw pointers: the only two crashes ever with a
      deleted copy in a register came 206 and 270 ms after an immediate delete (v1.8.1, v1.9.4), and none of the 504 deletions
      60 s or older in the crash reports was. So the 60 s stay, and a copy at those two holds is now a normal end
      (`the game let go of it`), with no `CopyFreedHeld` or `DeleteClaim`.
      Open: what the other two holds of the 4-handle copies are (55 of 161; any kind of actor, ghost or not). They are counted, so
      that object outlives `Delete()`. Not seen in the rig's runs, which all read 2 or 3.
- [ ] **Console travel can crash the rig's game** (2026-10-30, 4 in about 9): `coc` or `cow` through DevBench queues the load on a
      worker thread, and the main thread then draws with the world torn down: `+0x1349238` (renderer+8 null), `+0x134c61a` (+0x180 null),
      `d3d11.dll+0x1808d1`, a bad jump under `CommunityShaders.dll+0x38b794`, 1 to 100 ms after `Menu queued: Loading Menu`. Seen on 10-26 and
      10-28 too, before the map work; none of our frames on the stack. In play every load is queued on one thread, and the hub has none of
      these addresses. Scripts that need to go indoors use Papyrus `MoveTo` (`live-follower-left`). `live-copies-left-3` and `live-spawn-burst`
      now travel that way too, to markers in Tamriel, as the freed body's crash is a contact in the same physics world.
      A soak on 2026-10-31: 13 `MoveTo` trips between `35E6E` at Windhelm's docks (cell 34 8) and `4AFD9` by Mistwatch, on a connected game
      that had already run 10 scripts, no crash. So the 22 scripts on those two legs now use it (`live-safepoint-travel` passes on them), and
      `headless.ps1 up` goes by `MoveTo` too, after its own `cow` crashed the rig on 10-31 (worked on the one `up` so far). Still on console
      travel, each saying it needs a fresh `headless.ps1 up`: cave-travel (console travel is its subject), cw-battle, door-join,
      door-join-locked, npc-grace, respawn-cell, scene-dialogue, teleport-far, whiterun, world-object-reload. `player.moveto` is console
      travel too (door-state and others), and is not counted here.
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
      Done 2026-10-31 (tools only, no client change): the default suite is rebuilt, 39 scripts, the tests shown red then
      ok and the basics (it held 8 of the 39 proven tests and 13 scripts unrun since 10-17); not run as a whole yet.
      `live-check.py` with no script name no longer starts it. `objectmoves <hex>` counts one object's moves (in
      `Code/bot`, waits for a build; `live-world-object` moves to it then). Rig said / play said now has a per-session
      record, with `live-map-pointer` as its first entry (rig ok, play failed). A grab that does not hold, a player
      who did not walk, and `slide` with no SlideDiag line now read INVALID or INCONCLUSIVE, not FAILED.
      Found: `live-hands` never runs `ReachHands`: it skips an arm more than 15 units from its target, and the script's
      targets are 56-80 away, so its 80-unit limit judges nothing. Targets within reach and a limit from the bug's size
      wait for a rig turn. `live-copy-knockdown`'s `npc slide below 100` failures of 10-24 were the ragdoll's pelvis
      rolling on a slope (202 then 0 units on one build); the check now reads the reference, but that form has not
      been shown red on a dragged copy. `live-body-grab`: the 10-30 `holds None` followed the copy losing its 3D (see
      "A dead copy near the player lost its 3D"); a hold was read once, 1 s after the grab, while the body was still
      being pulled; a bear was measured by its reference. Those are fixed; it has no clean run on a fresh `up` yet.
      Found the same day: only the headset turns a VR player (`setangle` and `SetAngle` are ignored), so `DO face` did
      nothing while it printed the turn; it now makes a run INVALID and no script uses it. The results of the scripts
      that used it stand: `live-blade` and `live-arrow-*` had the copy in view through `infront`, which reads the real
      heading, and the cast scripts read hits, not poses, so at worst they could miss. `live-death-replay` is what it did
      hurt: on the rig's save the copy starts 147 degrees off the view, so its `pose`, `fall` and `still` numbers read a
      body the client was not posing. It now turns the headset and has `REQUIRE view npc`, which applies the client's own
      view test (shown passing and failing on bd2adc0). In view, 3 runs: `fall` 63, -69 and -71 ms against 466 and 1517
      before, 15 to 50 from the original, `pose` 33, 19 and 33. The limits are now 400, 300 and 40; the first two would
      have caught the out-of-view readings, a bad build has not shown any of them red. `live-copy-knockdown`,
      `live-copy-bleedout` and `live-death-copy` read a fall the same way and got the same three lines, not run yet.
      `live-hands` now runs `ReachHands`: targets 10 units above where the pose rests the hands, median 0.0 against a
      limit of 5, and 17.1, RED, with a target just past its reach (a changed script, not a bad build). `live-arrow-hand`
      has its first pass, 10/10. `DO hold` and `DO reach` put the hand straight ahead of a heading that never turned, and
      now stand the player with the target straight ahead. On 474ec2b `live-revive-hand` passes twice right after
      `live-revive`: the hand on the body as it arrives, the revive 4 s into the hold (15 s from the resting hand
      before), so its wait is now 9 s, not shown red on a bad build. Behind scripts that had moved the player it found
      no hand, 3 times: the bot takes the player's height, a ledge left the player 697 units under the copy once it
      landed. It now starts at the rig's start, and passes from a raised spot too. Also on 474ec2b: `live-world-object`
      passes on `objectmoves 92CFE`, `live-walk` with its new REQUIRE (243 units walked; INVALID, 0 units, when a run
      before it leaves the player blocked), `live-death-copy` in view (down in 664 ms). `live-copy-knockdown` FAILED
      once, with no `moved at most` line, and the game was not at fault: `live-check.py` did not read the bot's output
      during `DO watch`, so a bot logging many object moves filled the pipe and froze mid-replay, and after 15 s of
      silence the server gave its NPC to this game. The output now has a thread of its own, shown off the rig; the same
      pair of scripts then passed, with fewer object moves. A pass in a script with a long DO line was probably sound,
      a failure in one may have been this. `live-body-grab` FAILED once: 1.7 s after this client took over a grabbed dead
      bear, the bear went about 38,000 units straight up. Each `DO walk` step is a `SetPosition`, a small teleport with
      the loading menus cycling. On the real W key, 2 runs, it did not fly, and from the rig's start the script has its
      first clean pass: walked 645 units, body 603, 69 from the player, HIGGS still holding. So the rig's doing, as far
      as shown. `corpse-jump` is still counted, but only `smooth corpse-jump` judges it: a ragdoll is now moved only out
      of view or as a drag ends.
      Next: the other scripts that leave the copy behind the headset (`live-revive-copy`, `live-creatures` and more),
      not judged one by one yet; the new suite as a whole; `live-copy-knockdown`'s `getup-jump 1 (up to 149 units)`,
      counted but not judged in its passing run; and a choice for the owner: let the rig set the monitors to 60 Hz
      while it runs.
- [!] **The other player's body looks off:** hands a little low with the palms together (`VRPose`, structural), legs
      not walking or running with smooth locomotion, the face "looks off" (FaceGen on VR). An old report, from before 2026-10-13.
      Measured 2026-10-31 on the session of 2026-10-30 18:13, both players' logs:
      - Hands: not low. In 451 `VRBodySync: actor X hands --` lines the copy's hands lie a median 1 to 5 units from where its
        owner's are, under 33 nine times in ten, and sit 4 to 9 units higher and 2 wider apart than the owner's. `live-hands`
        reads 0.0 with its targets in reach, and 17.1, RED, with one past it. That line compares the copy with what its owner
        sent, so a pose wrong at the sender would not show. Nothing reads a hand's rotation, so the palms are not measured.
      - Legs: no general sliding. `SlideDiag` counts 674 of 147,240 moves of remote bodies with frozen animation, 427 of them in
        one 10 s window on player 2's side, none on player 1's. It counts NPCs and players together. `live-walk` ok on 6f521cc,
        but that is a bot replaying a walk.
      - Face: nothing measures it.
      Only a headset can tell the palms, the legs at a run and the face: "Next play session", "Look at the other player".
- [!] **Gaps from the review of the last 20 commits: small guards** (build 4df623a; the code allowed them, no log showed them,
      and none can be made on demand in the rig, so no red runs and no sign yet that any has fired; not seen in play).
      - A waiting copy's id reused: `RunWaitingCopies` now finds a copy by its handle, so a later actor on that id is
        left alone, and a new copy of ours on the id takes the waiting entry off the list.
      - Equipping an NPC with no AI process (`OnNotifyEquipmentChanges`, `AddOrRemoveItem` of worn items): skipped, with
        `Equipment sync: remote actor X has no AI process here; change not applied`. It shows older gear until its next
        assignment, as a waiting inventory holds no equipment changes.
      - An actor with no cell at connect (`has no cell; not announced to the server`): asked about each second, and announced
        once it has one (`Actor X has a cell now; announced to the server`).
- [ ] **Map greyed out indoors** (player 2, 2026-10-11 and 2026-10-14): no code of ours touches the map indoors and it
      worked outside. To settle it: open the map inside any building with the mod not connected.

## PvP queue (2026-10-06 evening): the fixes from the first fight, then handling bodies

Emma, after the first fight between two headsets: "Loop for these fixes", then "not just dragging bodies working, but
actually interacting with an npc or maybe even the other player body could be seen". Worked top to bottom by the loop
like the Physics queue below: `[x]` with its evidence in one line, `[!]` with the question or action copied under
"Waiting on a test or action (PvP)", `[-]` with why. A design choice is never the loop's to make. What the fight showed is under "First
fight between two headsets" in P4 below; Seen's log clock runs about 2.5 s ahead of Emma's.

**Waiting on a test or action (PvP)**

- **Handling a living NPC the other player's game runs: what should happen?** Today your grab or shove on it is
  undone at once on your screen and he sees nothing; only your hits' damage reaches it. Options: (1) while your hand
  holds it, your game takes it over, as it now does for a body you drag -- you both see it pulled, but in a fight the
  NPC's AI switches between your games each time; (2) your game keeps out and sends your pull to his, which applies
  it -- smoother AI, a delay on the pull, a lot of new work; (3) leave it as it is (hits only). Which one?
- **Grabbing or pushing the other player's body: what should he get?** His copy in your game can be bent a little by
  your hand, and nothing reaches him. Possible: a buzz on the hand or arm you grab, a stagger or push of his body when
  you shove him, or nothing physical (only what you see). The game cannot move his real hands. Which ones?
- **A stiffer PLANCK, to make his body follow his pose more tightly?** His ragdoll hands sit 8-14 units from where
  his hands are drawn, because PLANCK's ragdolls follow their pose softly (your `activeragdoll.ini`: `positionGain =
  0.05`, `hierarchyGain = 0.6`, `poweredTau = 0.8`, PLANCK's defaults). Raising them would make his body (what your
  sword hits) follow tighter, but every NPC's body would also react more stiffly to hits and pushes. May I try e.g.
  `positionGain = 0.2` in the rig and measure, and would you want it in your game if it helps?
- **Next session, one drag each way:** Seen drags one of the bodies your game owns (one you killed), for a few
  seconds, then you drag one of his. Nothing to set up; both logs say whether the body changed hands ("asking for
  it", "Transferred ownership ... for party leader claim" on the server) and whether its bones arrived ("posing body").
- Carried from the Physics queue: the DevBench update (controllers in the rig: swings, PLANCK hits and the sword-hit
  fix testable without her) and how an equipped sword should meet his.

### Q1. Fixes from the first fight

- [x] Sparks at a clash (2026-10-06, `live-defender` 21:1x and 21:2x): "Clash: sparks at (...)" on both sides of a
      clash, the game's `FXMetalSparkImpactSlice.nif` for a second. On the way, from a failing run: a hand resting
      between the copy's two blades brushed each about once a second at 0-5 units a second, a new meeting each time
      (11 in a run, 21:28); a meeting now also needs 10 units a second (98 of the 103 meetings of the real fight were
      faster than 20, 6 at 10 or less); a slower contact is a touch (blocks, makes no sound). `live-defender` 21 of 21
      twice with it (21:3x, 21:4x). Was: `SparkClash` (the game's
      `Effects\ImpactEffects\FXMetalSparkImpactSlice.nif` through `BSTempEffectParticle::Spawn`, SE 29218, in the VR
      database), on both sides of a clash. Build, then `live-clash`/`live-defender` check "Clash: sparks at".
- [x] The defender's rule at its edges (2026-10-06, `live-defender` 21:1x and 21:2x, the bot's `hit other -30 atclash
      <ms>` against the first clash it was told of): stamped 300 ms before it, blocked (health 305 -> 305); 360 ms
      before it, landed (305 -> 275); at the clash, blocked; before any contact, landed. The late edge cannot be tested
      with a hand resting on the blade (every touch blocks), which is the rule working. Was: the bot stamps a hit at an exact offset from a clash it was told of, and a hit
      inside the window (clash from 300 ms before the hit to 100 ms after it is seen, `ActorValueService.cpp`) is
      blocked, one just outside lands. The player's hand must be off the blade by then: a hand resting on it touches
      it every 100 ms and blocks every hit (20:33).
- [x] A body somebody else owns, grabbed and dragged here, is sent from here (2026-10-06). The server refused every
      claim on a *temporary* actor before any other check, at debug level, and bodies of temporary actors are common
      (encounter spawns, leveled NPCs): red 21:54, the bot's dead Aspiring Mage asked for 17 times while dragged, never
      granted. Now a temporary actor is claimable once dead (living ones still refused), and every refusal is said at
      info. Green 22:10 and 22:14 (`live-body-drag-slow`, `live-body-grab-human`): "Transferred ownership of actor 15
      from player 2 to player 1 for party leader claim" while the bot was still there, then "VRBodySync: body FF... is
      being moved here, sending its bones". The bot now notes an actor taken from it. Was: today the player's game asks for it
      (`live-body-grab`: "asking for it" 15 times) and the server never grants it. Its refusals are at debug level
      (`CanClaimOwnership`); a dead body goes only to a party member, and the bot cannot join a party. Server refusals
      said at info, the bot in a party with the player, `live-body-grab` green end to end ("VRBodySync: body X is being
      moved here" on the player's side, the bot told the body is no longer its own).
- [!] Why Seen's game never noticed his drag (2026-10-06). Tried: a humanoid corpse (noticed), a slow drag of 3-unit
      steps (noticed once; HIGGS pulling the body after each scripted step still makes jumps, so the rig cannot drag
      smoothly). Found in the code: the watching side looked for 4 units of movement between two frames (240 units a
      second at 60 fps) while the owner's side looks every 100 ms (40 units a second); a hand drag is slower than the
      first. Now both every 100 ms -- on code evidence and his log's silence, not a failing run. Also fixed on the way:
      temporary bodies could never change hands (item above). Bodies did finish dying on his side within a minute
      (1018F9 "dying or downed" then posed). **Owed: a real drag** -- see "Waiting on a test or action (PvP)". Was: try what the rig has not --
      a humanoid corpse (a bandit, not the bear), a body still in its death animation, a body the other side killed
      with a hit rather than `npclife dead` -- and see which one `ObserveRemoteBodyMotion` misses.
- [!] Tighter ragdoll (2026-10-06, rig 22:25 and 22:30): ours installed around PLANCK's hook instead of inside it made
      it worse, 84-90 units from the drawn hands (8-14 inside) and 4-8 from the copy's own animation -- PLANCK's work
      before the drive copies the game's animation pose over the track, after ours. The same as the first try that
      morning; reverted. What is left is how softly PLANCK's active ragdoll follows any pose (its gains, Emma's
      activeragdoll.ini at the defaults): a setting of her install that changes every NPC's body, so a question under
      "Waiting on a test or action (PvP)". Was: the drawn pose written before PLANCK's own work, installed once the game
      runs (session scratchpad `p43_p44_patch.py`, its P4.4 half). Measured on the `VRRagdoll` hands lines against
      today's 8-14 units; reverted if worse.
- [x] The other player's delay alone (2026-10-06, both runs): "InterpDiag players: 601 updates, newest point ahead of
      playback: lowest 122, 1% 132, 5% 149, median 193 ms" every 10 s (the rig, one machine: the network takes ~30 ms
      of the 225 here). The next real session gives the real numbers; nothing to do but play. Was: an "InterpDiag players" line (the P4.3 half of `p43_p44_patch.py`), so the next
      real session says how much of the 225 ms playback delay the network needs.

### Q2. Next: handling bodies, and the other player, seen by both

- [!] A living NPC handled by the player (2026-10-06). Measured and read: HIGGS's script grab does not take a living
      actor (`live-npc-grab`: "now holds None"; a hand on a living NPC's limb needs PLANCK and real controllers), so the
      rig cannot pull one. What the code does today: a living NPC the other game owns is put where its owner has it at
      every update here (InterpolationSystem, ForcePosition), so a pull or a shove here is undone at once and its owner
      sees nothing; only a hit's damage is sent (and the owner's NPC fights back). Only dead bodies are watched for a
      hand moving them (`ObserveRemoteBodyMotion`). Design question under "Waiting on a test or action (PvP)". Was: what the NPC's owner gets today when this player grabs it (HIGGS), pushes
      it or knocks it over (PLANCK), measured in the rig with a bot-owned NPC -- what is sent, what the owner would
      apply. Then what would make it seen: the non-owner taking the NPC while handling it (as for bodies), or sending
      the hand's pull. Which of those is a design choice: under "Waiting on a test or action (PvP)" with the measured facts.
- [!] The other player's body handled by this player (2026-10-06, from the code): his copy is posed from his own VR
      pose every frame and its ragdoll is driven to that pose (P2), so a hand here can bend it a little (PLANCK's soft
      following, 8-14 units) and nothing is sent to him. His real hands cannot be moved by the game. Design question
      under "Waiting on a test or action (PvP)". Was: grabbing his copy's hand or arm, pushing his copy. What happens
      on his side today (nothing is sent, presumably: measure), and what could be: a buzz on the hand that was
      grabbed, his body pushed. Design choice to Emma with the facts.

## Physics queue (2026-10-06): swords and bodies as real physics, Blade & Sorcery as the aim

Emma: "No matter the cost, no matter the time." The aim is B&S: the other player's sword and body are real physics
objects in your game where you see them, so a blade is really stopped by his; your own sword has weight. The network
delay stays, so **the defender's screen decides whether a hit was blocked** (agreed 2026-10-06).

Worked top to bottom by the loop; each item ends `[x]` with its evidence in one line, `[!]` with the question or action
for Emma copied under "Waiting on a test or action (physics)", or `[-]` with why. A design choice is never the loop's to make.

Known going in (see "Swords between players" below): PLANCK 0.6.6 is installed (0.8.1 exists); its code for making
characters' equipped weapons physical is commented out in every version; it drives each ragdoll toward the behaviour
graph's pose at the call to `hkbRagdollDriver::driveToPose` (VR `0xB266AB`, after copying `hkbCharacter.poseLocal`
into the pose track when foot IK is on); our VR pose is only drawn, at the renderer's frame end
(`BSGraphicsRenderer.cpp:81`), so no physics ever sees it. The game keeps an equipped weapon's own rigid body out of
the world (PLANCK 0.7.0 notes). Sources: github.com/adamhynek/activeragdoll (PLANCK, VR offsets in
`src/RE/offsets.cpp`), github.com/adamhynek/higgs (HIGGS), github.com/ijwzac/WeaponCollisionVR (the parry mod).

**Waiting on a test or action (physics)**

- **DevBench update?** To swing a sword in the test rig (no headset) the rig needs controller poses, which newer
  DevBench releases have (`input vrTrackedSet`); the installed 1.22.0 has the keyboard only. Would you update DevBench
  (Nexus mod 181326) in MO2? It is a test tool only, and the combined VR Address Library note in KNOWN_ISSUES section
  7 still applies.
- **How should your *equipped* sword meet his?** A sword you grab with HIGGS is already stopped by his (it is a
  dynamic body on your setup). An equipped sword is not: HIGGS moves its body keyframed, straight to your hand, and two
  keyframed bodies pass through each other. The choices, each with a different feel:
  1. *B&S-like*: your equipped sword becomes a dynamic body held to your hand by a strong constraint, and what you see
     follows that body -- his blade stops yours, your hand keeps moving. The most work; it means taking over what you
     see of your own sword.
  2. *Feedback only*: your sword still passes through, but at the touch both of you get a clash (haptics, sound, sparks)
     and his attack is blocked by the defender's rule (P4). Least work, no physical stop.
  3. *Grab instead of equip*: fight with swords held by HIGGS (already blocked today); equipping stays as it is.
  Which one?
- **Next time you two fight (no rush, nothing to set up):** swords meeting should now buzz the hand and clang for both
  of you, and a hit he lands while your blade touches his on *your* screen should do no damage. Tell me if a block
  you saw still cost health, if a hit you did not block was dropped, or if damage now feels late (another player's
  hits wait about a third of a second to be judged). Your log has every case ("Defender's rule: ..."), and I read it
  myself; only Seen's log needs sending.

### P0. What the physics needs (reading, no game)

- [x] The Havok calls and their VR addresses (2026-10-06). From PLANCK's `offsets.cpp` (in use on Emma's game every
      session, PLANCK 0.6.6), checked where another source exists:
      - `bhkWorld::worldLock` at +0xC598 (PLANCK's static_assert, and CommonLibVR-NG `bhkWorld.h`); `hkpWorld::m_userData`
        (the bhkWorld) at +0x430; `bhkRigidBody::hkBody` at +0x10; `NiAVObject::collisionObject` at +0x40 (beside our
        measured parent 0x30 / local 0x48 / world 0x7C). Havok scale `0x15B78F4` (library id 231896, agrees).
      - `bhkWorld::AddEntity` 0xDFA520, `RemoveEntity` 0xDFAD80; `hkpWorld::AddEntity` 0xAB0CB0, `RemoveEntity` 0xAB0E50
        (library id 60493, agrees); `bhkRigidBody::setMotionType` 0xE08040 (beside the library's bhkRigidBody setters,
        SE 76259-76262 at 0xE08670-0xE088B0); `hkpRigidBody::setMotionType` 0xAA9530;
        `hkpKeyFrameUtility::applyHardKeyFrame` 0xAF6DD0; `bhkWorld::UpdateCollisionFilterOnWorldObject` 0xDFFE50;
        `bhkWorldObject::UpdateCollisionFilter` 0xDF88D0; `Actor::GetCollisionFilterInfo` 0x5F44A0 (library id 36559,
        agrees); `NiNode::SetMotionTypeDownwards` 0xDFD160.
      - From the library itself (CommonLibVR-NG names): `NiAVObject::SetCollisionLayerAndGroup` SE 76171 = 0xE03370,
        `SetCollisionLayer` 76170 = 0xE03350, `GetCollisionObject` 25482 = 0x3B5CB0.
      - In our client, an address the library does not have goes into `VRAddressOverrides.h` under an id; for the
        PLANCK-only ones there is no SE/AE id, so they need a private id range, documented as such.
      - Not yet confirmed by measurement: the hkpRigidBody field offsets (filter info at +0x4C per the Havok 2010
        layout). P0's dump reads a known body to confirm.
- [x] Which collision layers collide with what (rig, 2026-10-06, `PhysicsProbe: layers that collide`): biped 8 collides
      with 4 5 8 10 56 (clutter, weapon, biped, props, HIGGS) and not with 1 (static) or 30 (character capsule); HIGGS's
      hands and weapons are layer 56 in the player's group with bit 15 (its `hand.cpp`), and 56 collides with 4 5 8 10
      32 33 56 1. The copy's drawn weapon body is already layer 8 in the copy's own ragdoll group ("drawn weapon 'Weapon
      (00012EB7)': layer 8, group 1543, motion type 4, not in the world"), and a body in the same group without bit 15
      never touches that ragdoll. So the game's own layer and group meet the requirement as they are; nothing to choose.

### P1. His sword, a physics object where it is drawn

- [x] Red first (2026-10-06): the copy's drawn sword has a body, "not in the world" (`PhysicsProbe`, 09:24). On the way:
      a copy never drew or sheathed from `SetWeaponDrawnEx` on VR ("set drawn; the game now says sheathed", both passes)
      -- Skyrim VR's second extra Actor virtual sits before `DrawWeaponMagicHands` (VR slot 0xA8, CommonLibVR-NG), and
      `Actor.h` had it after, so `SetWeaponDrawn` called the wrong slot. Fixed; now "the game went from sheathed to
      drawing" (a copy that arrives with its weapon drawn really draws it). The plate on its own proved nothing: in the
      rig the sword is held 19 units above the copy's feet (no controllers, the recorded arms hang; `ReachHands` only
      fine-tunes 15 units), and a plate does not balance on a blade's edge anyway. Contacts are the evidence instead.
- [x] The copy's drawn weapon gets a rigid body in the world (`VRBodySync`, `UpdateWeaponBodies`, 2026-10-06): the game's
      own body of the weapon, put in the copy's ragdoll's world while the weapon is out (drawn, asked to sheathe -- a
      copy's own AI asks within 1.5 s and its graph never does it -- or sheathing) and its node hangs under the hand;
      node and body referenced while in; out again at the first frame end the copy is not posed, the weapon not out or
      not in the hand, or the copy's ragdoll in another world. Out-of-view copies keep theirs. Havok calls from PLANCK's
      and HIGGS's VR offsets (private ids 9000001+ in `VRAddressOverrides.h`), the world lock and `RemoveEntity` by SE
      id. Evidence, `live-sword-body` 11 of 11 in five runs (10:17, 10:28, 10:38, 10:47, 10:57): "is a body in the
      world now: layer 8, group 1541, motion type 4; placed, it reads 0.0 units from where it was put", "out of the
      world (the weapon is not out)" / "(its copy was not posed)". The rig survived every run. Drawn: the bot's
      `draw on` + `teleport away|back` (the copy respawns with it drawn) and `holdpose on` (the last replayed pose goes
      on being sent, or the copy is handed back to its animation when the replay ends).
- [x] The body follows the drawn sword exactly (2026-10-06): right after every physics step 0.00 units and 0.00 degrees
      from where the sword is drawn, in two green runs (15:0x, 15:1x) and the confirming run after the clean-up (15:3x),
      at 60 fps (16.7 ms frames, as with no body at all). What it took, each measured before and after:
      - driven right before Havok's step (a call patch at VR `0xDFB722`, the same call HIGGS hooks, chained with it),
        not at the renderer's frame end: from there the velocity given was not the one integrated;
      - the body's angular limit raised from the game's 31.6 to 500 rad/s while it is ours (HIGGS's value; restored on
        release): position went from 4-18 units off to 0.1;
      - velocities worked out from the rotation matrices Havok keeps, not `applyHardKeyFrame`'s quaternion (with that the
        body sat ~145 degrees off about one axis whatever was tried);
      - re-seated where the last step left it before each step: the game turns its weapon body to where its own
        animation holds the sword between steps, every frame at 60 fps (a write in `hkpRigidBody::setRotation` called
        from the `bhkRigidBody::MoveToPositionAndRotation` area, found with a hardware watchpoint on the rig); driven
        from there it swept up to 156 degrees through whatever was near in one step. "Between steps moved by something
        else up to 0.0 units and 156 degrees, put back 300 times" every 5 s.
      Ruled out on the way, by measurement: the body's motion (a proper keyframed motion, inverse mass and inertia 0, no
      constraints, no actions), its damping (0.05), its collision quality (already keyframed), and any other mod (the
      watchpoint found only Havok's own integration and the game's setter). The body's frame is the weapon node's: the
      visible blade (the node's world bound, `NiAVObject::worldBound` at 0xE4) and the body's centre of mass both lie
      along the node's +Y.
      Cost, found and fixed on the way: `IsReadable` (`VirtualQuery`) on these paths cost 26 ms before every step and
      the rig ran at 14 fps; lookups on live nodes no longer ask it. Now ~0.01 ms a step and ~0.16 ms a frame end.
- [x] Green twice and nothing broken (2026-10-06): the dropped plate meets the blade in every run since 10:17 ("touches a
      body on layer 4", Havok's own contact; it does not balance on an edge and the rig holds the sword low), `live-sword-
      body` 11 of 11 and `live-weapon-grip` 5 of 5 in the same sessions, `live-copy-abandon` 10 of 10 twice, and
      `live-copies-left-2`'s game checks (alive, copies gone) twice. Frames 16.7 ms with a sword body in the world, as
      without. Not ours, written down where they belong: the bot registers one of two bears asked for in the same
      instant (`owned == 2 -- 1 actor(s) owned`, every run today; "Sync: things to build"), and one run crashed during
      the travel tests (`SkyrimVR.exe+0xCBFD24`, 14:33, no sword body for six minutes before, not reproduced in the next
      run): the known PLANCK + copies-left crash (KNOWN_ISSUES).
- [x] Contact, measured (2026-10-06): a contact listener on each weapon body (Havok 2010 `hkpContactListener`, slots 0-2
      as PLANCK overrides them; `hkpEntity::AddContactListener` SE 60094), recorded under its own lock where Havok calls
      it and said at the frame end: "touches a body on layer 4 (group 1544, motion type 3) at (...)" for the plate in
      every run, layer 5 (a sword lying on the ground), and layer 56 (the player's HIGGS bodies, group 9, keyframed)
      when the copy's sword swept past the player. DevBench `input` has no controllers, so the player could not push a
      sword in on purpose; the HIGGS contacts came from the copy's side moving. Keyframed against keyframed gives
      contact points but no push: our sword stopping the other's needs a dynamic body on one side (P3).

### P2. His body, a physics object in his VR pose

- [x] Red first (2026-10-06, `VRRagdoll` lines, rig, `live-sword-body`'s replayed pose): the copy's ragdoll hand bodies --
      dynamic (motion type 2), in the world -- are 83-98 units from where its hands are drawn and 2.4-3.2 from where its
      own animation had them that frame, every 5 s. Hits and pushes on his body meet his animation, not his VR pose.
- [x] Put the VR pose where physics reads it (2026-10-06, `VRBodySync`: `RecordDrawnPose`, `PutDrawnPoseIntoTrack`,
      `HookDriveToPose`, `HookPostPhysicsOutside`). The frame end records, per remote player copy, the drawn world
      transform of each animation-skeleton bone with a node of the same name (44 of 99 on a humanoid). Right before
      `driveToPose` (call patch at VR `0xB266AB`, PLANCK's site, ours inside PLANCK's hook) the pose track gets them as
      parent-relative transforms, parents first, units measured ("game units"); the track's bones are saved before and
      put back right after, and after the step (`postPhysics`, VR `0xB268DC`, ours moved outside PLANCK's hook once the
      game runs) the game's and PLANCK's write-back of the ragdoll pose is undone for these copies. So the ragdoll
      follows the drawn pose and what is drawn is untouched. The first version, without the putting back, fed the drawn
      pose into itself: drawn hands 131 units off their owner's (median; normally 65) -- reverted, then this.
- [x] Green twice (2026-10-06, 16:59 and 17:0x): ragdoll hands 8-14 units from the drawn hands (83-98 before), steady
      over the run, and 78-90 from the copy's own animation; drawn hands unchanged ("hands within 80" median 65.8 and
      65.1); `live-sword-body` 11/11, `live-weapon-grip` 5/5, `live-copy-abandon` 10/10, `live-copies-left-2`'s game
      checks; 60 fps. Not "a few units" yet -- see "Tighter" under P4.

### P3. Your own sword with weight (local, HIGGS territory)

- [x] A sword grabbed with HIGGS is stopped by his (2026-10-06, `live-higgs-block`): the player's right HIGGS hand grabs a
      dropped iron sword by script (`HiggsVR.GrabObject`, only after the player's own weapon is sheathed -- a drawn
      weapon's hand cannot grab), and the player is moved so the hand goes to the centre of the copy's blade. The held
      sword stays 8 units out, in continuous contact (~600 Havok contacts in the window, "layer 5, motion type 3");
      with the copy's sword body gone (the same steps one run earlier), it went to 1 unit. Emma's HIGGS has
      `ForcePhysicsGrab = 1`, so a grabbed sword is a dynamic body pulled to the hand by a constraint. With it 0
      (HIGGS's own default for objects without constraints), HIGGS holds a sword keyframed and it passes through his,
      keyframed against keyframed (HIGGS `ShouldUsePhysicsBasedGrab`). No damage or hit on the copy from the push (its
      health 100 throughout). How it lags with the grab-constraint settings, and whether PLANCK counts a *swing*
      into him as a hit, need a moving hand: [!] below.
- [!] A swinging hand in the rig: DevBench 1.22.0 drives the keyboard only; its newer releases add `input vrTrackedSet`
      (HMD and both controllers' poses and buttons, frame by frame -- github.com/alandtse/devbench). With that, the rig
      can swing a held sword into his and measure the lag and PLANCK's hits. Question under "Waiting on a test or action (physics)".
- [!] How an equipped sword should meet his: design choice, question under "Waiting on a test or action (physics)".

### P4. Two screens, one fight

- [x] Clash events (2026-10-06, `live-clash`, `live-defender`): when the other player's weapon body (P1) touches one of
      the player's HIGGS bodies (layer 56: its hands, its body for the equipped weapon) or something a HIGGS hand holds
      (found through HIGGS's grab constraint: the held body's constraints, layouts from CommonLibVR-NG), the client
      feels it on that hand, plays the game's blade-block sound there (`WPNBlockBlade1HandVsOtherSD`, 0x3C73C, through
      `BSAudioManager`/`BSSoundHandle`, SE ids 66391/66404/66370/66355, all in the VR database) and sends a
      `ClashRequest` (which weapon, which hand, point, speed, server tick); the server passes it on as `NotifyClash`,
      and the weapon's owner feels it on the hand holding it and hears it. Evidence: "Clash: our right hand (7 units from
      its HIGGS body) met the right weapon of ... 233 units a second; felt and heard here, sent", the bot had it 16-19 ms
      later, the other way "Clash from server ...: heard, felt" 21-30 ms after; `live-clash`/`live-defender` clash
      checks green in four runs (18:09, 18:43, 18:58 and 18:19 for the sound). Fixed on the way, each from a failing
      run: the hand is the one nearer the HIGGS body with no distance limit (a scripted 84-unit move left it more than
      40 units from both), and a clash is a *meeting* -- the two start touching after a quarter second apart: a hand
      resting on the blade gave ~45 clashes a run, each a sound and a pulse (runs 3-4, 1-15 units a second); now "46
      touch(es) ... still touching, not meetings" every 5 s and one clash (18:58). Not measured in the rig: the pulse
      itself (no controllers).
- [ ] The defender's rule (2026-10-06): written and green once. A hit now carries its server tick
      (`RequestHealthChangeBroadcast`/`NotifyHealthChangeBroadcast.Tick`); this player's game holds another player's
      hit until it shows it (tick + 225 ms, the VR playback delay, + 100) and drops it if a clash or a touch with that
      player's weapon fell between 300 ms before and 100 ms after that moment ("Defender's rule: ... is blocked" /
      "... lands"). Red (18:43, before the rule): a hit stamped at the clash took 30. Green (18:58): "blocked: our
      weapon met theirs 0 ms before it was seen here", health 335 -> 335; the control hit "lands, 340 ms after it
      happened", 335 -> 305; 15 of 15. The 18:54 run never started (the test save timed out loading). **One more
      green run owed.** The 300/100 ms windows are first values: see "Waiting on a test or action (physics)".
- [ ] **First fight between two headsets (2026-10-06, 19:47-20:11), and what it changed.** Emma: the buzz matched
      where the sword was, a hand on his sword felt it too; no blocking, no block reaction or sparks; hitting his sword
      hurt him. Measured from both logs (Seen's clock runs 2.5 s ahead of Emma's):
      - **His sword counted as his body:** PLANCK takes a swing into any body of an actor as a hit on it, and the copy's
        weapon body (P1) belongs to the copy: 22 of Emma's 56 hits on Seen and 10 of his 29 on her came with the swords
        touching ("PvP: hit remote player FF001224 for 79" 7 ms before her sword's contact with his). Fixed where the hit
        is sent: PLANCK writes the hit point into the player (+0x6BC) before the game's hit code runs, and a hit at a
        point where the copy's weapon body was just touched by the player's side is not sent ("PvP: a hit of N on remote
        player X's weapon, not on X; not sent"). Cannot be tested in the rig (PLANCK hits need real controller speed);
        **owed: the next fight.**
      - **The defender's rule blocked 28 hits and let 109 through.** Where a landed hit had a clash on the defender's
        screen near it, the clash came 120-290 ms before the hit's own tick (6 of 8; the window started 75 ms before).
        The window now starts 300 ms before the hit. `live-defender` 15 of 15 (20:4x).
      - **A resting blade made clashes:** counted per pair of bodies, a hand lifting off while the held sword stayed on
        the blade was a new meeting each time (6 in a run, four at 1-4 units a second). Now per weapon; 15 of 15.
      - Sparks at a clash: the game's own `FXMetalSparkImpactSlice.nif` through `BSTempEffectParticle::Spawn` (SE
        29218, in the VR database), written, not built yet. A *block reaction* (the swords bouncing apart) is the
        equipped-sword design question above.
- [ ] **Dragging (same session, 20:03): Seen saw Emma's drag; Emma never saw Seen's.**
      - Emma's drags reached Seen, but his game skipped the pose "out of view" (1018F9: out of view 46, posed 5 in
        30 s), judging by where the body lay before the drag; when that spot left his view the body showed there ("it
        disappeared for a brief moment"). Now judged where its owner has it. Built, not yet seen in a session.
      - When she let go, the body went back to its own ragdoll on his side, 627 units from hers ("CorpseDiag"): the
        open "nothing we do moves a corpse" entry.
      - Seen's game never noticed a body moving under his hand: no hand-off line of any kind all evening. In the rig
        (`live-body-grab`, new: HIGGS grabs the bot's dead bear, the player walks off with it) the player's game does
        notice and asks for the body 15 times, but the server never grants it: dead bodies go only to a party member,
        and the bot cannot join a party (rejections are logged at debug level only). So the rig shows a second gap,
        not his. Next: the server's refusals at info level, and party support in the bot.
- [ ] Less delay for hands and weapons, measured: send rate and playback delay for them, from the `InterpDiag` numbers.
      Known (2026-10-06): the player is sent every 33 ms and played 225 ms late (VR). The real sessions kept on the
      desktop (2026-09-19..27, then 300 ms) say a median 150-175 ms ahead of playback, but `InterpDiag` mixes every NPC
      copy in, and NPCs that stop updating drown the low end. Next: a players-only line ("InterpDiag players",
      prepared), then a real session of Emma and Seen to read it -- the rig is one machine and shows no network.
- [ ] Tighter: the copy's ragdoll hands sit a steady 8-14 units from the drawn hands (2026-10-06). PLANCK's own work
      before the drive still sees the animation (moving ours before it made the ragdoll worse, when the drawn pose still
      fed back; worth trying again now that it does not), and its active ragdoll follows softly (12-27 units behind a
      moving pose).

## Goals, in the order they are worth fixing (set 2026-09-25)

1. **No more crashes.**
2. **The game completely in sync between all players.**
3. **VRIK interactions seen by everyone.**
4. **Dead bodies in sync at all times** -- dragging first, then handling another player's body and living NPCs.
5. **NPCs below floor level** -- they should always be in sync.
6. **VR weapon hits land where the weapon is.**

Everything below is ordered against these. An item that is shipped but has not been played is marked
`[x] [untested]` and is **not** finished; it is owed a session. Items that the people playing have confirmed are
deleted outright, not ticked -- the git history keeps them.

### Where the crash work stands (2026-09-25, end of night)

- [ ] **Correction (2026-09-26): there were no mid-session timeouts, and the churn was not a fault.**
      The reading of Seen's 21:26 bundle that said "his client dropped five times and that is what broke the
      world" was wrong, and the fixes that came out of that night stand on their own evidence rather than on it.
      What the reason codes actually mean, read from `TiltedConnect/Client.cpp`:
      - `0 kTimeout` is reported only when the **old state was `Connecting`** -- a connection attempt that never
        completed. His two at 21:16:01 and 21:16:16 are failed attempts to connect after loading a save, not a
        live connection dropping.
      - `4 kAborted` comes from `Client::Close()` -- **this client closing the connection itself**. All three of
        his were deliberate local closes.

      And the 45 actors handed back at 21:19:01 were not a fault either. His grid changes run
      (5,7) -> (6,7) -> (7,7) -> (8,6) -> (9,6) -> (10,7) -> (11,7) between 21:16 and 21:19: about 25,000 units
      across Solstheim in three minutes. The game unloaded the cells behind him and the client relinquished what
      was in them, which is what it is supposed to do. **Lydia was four cells behind him**, at x~29000 while he
      stood at x~47000, which is the whole of "Seen does not see Lydia at all" at 21:21.

      So: no timeout problem is established, and one reported symptom is explained as correct behaviour. What
      remains genuinely unexplained from that session is the bandit launched into the sky (21:19), the dead body
      in the wrong place (21:20) and the spriggan that never lands a hit (21:25) -- all of which happened while he
      was crossing cells fast, which is worth treating as the common thread instead.

      The `Silence:` line added on 2026-09-25 is kept: it costs nothing and a client that stops talking is still
      worth knowing about. Its justification is now curiosity rather than a diagnosis.

### Goal 1, continued: the diagnostics were a stall source of their own (2026-09-25)

Counted in Seen's 21:26 bundle. A client that stalls stops talking; a client that stops talking is timed out; a
timeout hands 45 actors away at once, and that churn is where the crashes and the nonsense live. So the cost of
watching had become part of what was being watched.

  What is deliberately kept: the `MessageBoxMenu` probe, because it only fires for a message box and the level-up
  and respawn boxes are still open questions; `AnimDiag` and `InterpDiag`, which are periodic summaries rather
  than per-event work; and the new `Silence`, `SinkDiag` and `BodyGrabDiag` lines, which are cheap and are the
  only evidence for three open goals.

### What is still listed here

Only this week's work is still ticked. 44 items shipped between 2026-09-14 and 2026-09-23 were deleted on
2026-09-26: they have survived many sessions without being re-reported, which is as close to "confirmed" as an
unplayed item gets, and the git history keeps every word of them. What is left ticked is from 2026-09-25 and
-26 and is genuinely unplayed -- that is the list to confirm on the next session.

## Session of 2026-09-30, 19:25-19:47 (both logs): what worked, and what it showed

- **Dropped items worked in play.** Seen dropped a butterfly wing (`727DE`); Emma's client placed it as `FF00108D` at
  19:26:07 and she picked it up at 19:26:41, which removed it for everyone. Earlier drops were **not** announced, and
  the code said nothing either way -- fixed to report what it finds on the player once per connection.
- [x] **[untested] Emma's crash at 19:47:30 was ours, guarded.** `RunSpawnUpdates` cast `TESForm::GetById(CachedRefId)`
  for a remote character waiting to reappear, and the lookup returned freed memory: vtable `0x3b33e809f967790a`, form type
  166, read from her dump. Our copy deletion is cleared (the pointer is none of the 19 recorded). A cached copy is now
  checked to be a live game object -- readable, vtable inside the process image -- before it is cast, and a dead one
  is treated as no copy. **Second crash the same day on a freed temporary form handed back by an id lookup**, the first
  being the game's own script engine; see `KNOWN_ISSUES.md`, where EngineFixesVR `FormCaching` is now the prime suspect.
- [x] **[untested] The hitch every 5 seconds was ours.** "Mod update took ~50 ms, slowest section RunLocalUpdates",
  300 times, on an exact 5.0 s period -- the "6 frames over 50 ms" in every 30 s window. `CaptureLocalPose` rebuilt its
  bone cache every 5 s as a safety net, and the search (`FindShallowest`: up to 8192 nodes, a string compare each, for
  19 bones) costs about that on a FUS body. The cache now checks each bone node's name as well as its vtable every
  frame -- the case the net was for -- and refreshes every 60 s. The search now logs its own duration once a minute, so
  the next session confirms it: `VRBodySync: local skeleton searched in N ms`.
- **The 19:40 stutter** (43 frames over 50 ms in one 30 s window, around killing the bear and the sabre cat) was not in
  our update: its average held at 0.54 ms. Game-side; not pursued.
- **Seen's stuck menu (19:46).** The Journal Menu opened at 19:43:54 with the game window unfocused -- the game opens it
  on focus loss unless `bAlwaysActive=1` -- and stayed on the stack through a save load and a death. No
  `bAlwaysActive` line exists in any of the profile inis. Setting it on both machines removes the trigger; why the menu
  could not then be closed is not established. His 19:45:33 disconnect was his own client closing (reason 4) around
  that load.
- [ ] **Corpse dragging: the bones arrive, and something on the receiving side skips or misplaces them.** Emma's troll:
  "body FF0010C1 is being moved here, sending its bones" at 19:31:47.144; Seen's side "posing body FF0010E1 from its
  owner's bones" 0.7 s later -- yet it did not visibly move for him, and a dragged bandit looked scrambled ("a pixel
  mix", and "not exactly on the same spot for him"). **First read as the copy's ragdoll overriding the pose, and that
  was wrong:** the pose is already written at the renderer's frame end, after the ragdoll has run, and the pelvis and
  legs already move with it (the 09-26 stretching fix). What the code does show is that "posing body" is logged
  *before* four checks that skip the pose silently -- skeleton not matched, out of view, an unusable bone, and a body
  more than 2048 units from the owner's, which is bent in place and never moved. The last fits a corpse the two
  ragdolls dropped in different places. **[untested] Each skip is now named per body** ("VRBodySync: body X not posed
  this time: ... (distance)"); the next session says which, and if it is the distance guard, a dragged body should be put
  where its owner has it rather than refused. Boneless creatures work both ways (Emma's bear sent; Seen's sabre cat
  moved "194.6 units to where its owner has it").
- **The Mist Watch bandit's death did sync -- its body did not.** Emma owned it (claimed 19:40:20) and sent its death at
  19:41:36.412; the server forwarded it (it reports every death it drops, none here); and Seen's side posed it as a dead
  body at 19:41:39, which that path does only for an actor already dead there. Seen had hit it for 81 moments before, so
  most likely his copy died from his own hit and ragdolled on his side -- to a different spot, which is what "not synced
  after it died" was. [untested] The death receiver's three silent exits now say which they took, including "already
  dead here when its owner's word arrived".
- Held and moved objects (Seen: "if Dosylia picks up the item with her hands I should see it in her hands"):
  **[untested in game] done for dropped items** -- see "Dropped items are moved for everyone" below. Objects placed by
  a plugin (a cup on a table) are still the open entry "Objects moved by hand are not seen moving".

## Autonomous queue (2026-10-02)

What can be worked on with nobody in the headset: the no-headset game (`Tools/VR/headless.ps1`), a bot as the other
player (`Tools/VR/live-check.py`, `Tools/VR/run-all-pairs.ps1`), and reading code and logs. Worked top to bottom, one
item at a time. Each item ends as one of:

- `[x]` done -- with the evidence in one line (red before, green after, or the log line that settles it);
- `[!]` waiting on a test or action -- with the exact question or the one action only she can take, copied to "Waiting on a test or action" below;
- `[-]` not worth doing -- with why.

A longer write-up goes in the section the item belongs to further down; this list stays one line per item.

**Waiting on a test or action** (questions and actions collected from the queue; newest last)

- Whether to show "X is down" when the other player dies (asked 2026-10-01, not answered).
- **Commit and push, and send Seen the new exe (2026-10-02).** The login message changed (it carries the protocol id
  now), so an older client or server is refused by a newer one, once. Seen's Linux build needs the two GCC fixes in
  the working tree. After that the exe in the tools folder and a server built from the commit accept each other even
  though their version strings differ: only `Code/encoding` has to be the same.
  **Seen has to rebuild his server from that commit**, not only take the exe: one of the two fixes of the evening
  is in the server (a player who left before having a character took the first player's character with them).
  Committed as `f3636117`. **A second commit is waiting** (2026-10-03): a creature this game made no longer comes
  back twice (`CharacterService.cpp`), the skeleton search no longer slows down through a session
  (`VRBodySync.cpp`), and the tests for both. It is not a fix for the crash on travelling away, which is open again.
- **EngineFixesVR, both machines (asked 2026-10-03):** in `mods\Engine Fixes VR\skse\plugins\EngineFixesVR.ini` set
  `FormCaching = false` and `TreeLODReferenceCaching = false`, then play as usual. It is the only test that can say
  whether form caching is behind the script-engine crash on freed forms (`SkyrimVR.exe+0x93CE17`); your say, since it
  is your modlist and Seen's.
- **PLANCK and the travel crash (2026-10-03):** the game crashes within a second of arriving through a loading
  screen when three or more of our copies (the other player's character, summons, spawned creatures) were left
  behind -- and only with PLANCK on: with PLANCK off the same test passes. Your choice: (1) keep PLANCK and live with
  the risk (fast travel or a door away from the other player's creatures); (2) switch PLANCK off for co-op sessions;
  (3) let me keep at it from our side -- the next step is reading how PLANCK tracks the actors it ragdolls, to make
  our copies leave in a way it notices.
- **How late remote movement is shown (2026-10-03):** everything the other player does is played back 300 ms after it
  happened, to always have a point to move towards. In your session with Seen of 2026-09-30 the newest point was
  always 206 to 313 ms ahead (worst 128 ms), so the delay could follow the connection instead: about 200 ms on a good
  link, back up to 300 when it gets worse. Less lag in fights, a little more risk of a stutter on a bad connection.
  Yes or no?
- **Seen's teleport spell (2026-10-03):** at 09:32 you were teleported back into the tower 0.95 s after your game
  replayed Seen's cast of spell 3100FB0C from Conduit.esp (teleport magic). His three other casts of it that session
  did not move you. **Done in build 22ae2af, undo if you prefer:** Conduit's spells are no longer replayed on the
  other screen, the way VRIK's, HIGGS's and SpellWheelVR's are not (`kLocalControlPlugins` in `MagicService.cpp`).
  You will not see the cast's effect on Seen's copy any more; where he ends up still comes through his movement.
- **The game grows with every loading screen (2026-10-03), your modlist, your call.** Measured in the rig, connected or
  not: about 0.4 GB and twelve threads more per round trip. The threads are DynDOLOD DLL NG's (Alpha-32 installed,
  Alpha-33 available in MO2). After enough loads the game froze (out of its own heap, its clean-up deadlocked) or
  crashed in the AMD driver; details in KNOWN_ISSUES.md section 6. Two things that may help a long session, neither
  tried yet: update DynDOLOD DLL NG to Alpha-33, and set `MemoryManager = true` in EngineFixesVR.ini (replaces the
  allocator that deadlocked). Both machines would need the same. Tell me which to try and I will measure it the same
  way.
- **Dragons taken over in flight (2026-10-03):** your game takes over every creature that reaches it (the party
  leader's claim), dragons in the air included; the Mistwatch dragon fell and died right after (09:06). Options:
  (1) never take a dragon over from the game that has it flying -- the other player's game keeps running it until
  it lands or is killed; (2) keep the claim. (1) is what I would do; your hits on it still count either way.
- **Who runs a follower when the two of you are in different places? (2026-10-03 15:09)** Lydia is your follower.
  During your 10-second loading screen the server gave her to Seen, who was still in the tower with her, while your
  game brought her out with you; for 15 seconds the two games pulled her back and forth (92 times), and Seen lost her
  afterwards. Options: (1) a player's follower always belongs to that player's game, whoever else is near (your game
  takes her back as soon as she is with you); (2) keep it as it is and have a loading screen not count as leaving.
  (1) is what I would do; it changes who controls followers in every fight.
- **Seen's EngineFixesVR (asked 2026-10-03):** his crash at 15:24 is the script-engine one on a freed form. Is
  `FormCaching = false` in his `EngineFixesVR.ini` too? Yours has been since 2026-09-30.
- **Did you close the game window at about 00:35 on 2026-10-03?** The game ended by itself thirty seconds after a
  green test run: no crash report, no Windows error event, and our log stops in mid-flow. Seen once; the next
  session ran twenty-two minutes of the same tests and closed normally.
- **A two-minute test in the headset (2026-10-02):** connected, with the bow in hand, let yourself be killed; then
  load a save from the menu **either while you are down or within ten seconds of standing up again**. Does the game
  crash? With nobody in the headset it does, every time (three of three), in `skyrimvrtools.dll`; a load forty
  seconds later is fine (two of two). Whether real controllers avoid it cannot be told without them. See "Seen, on
  the load after death".

### Tooling first, because every later item gets cheaper

- [x] One command that runs every real-game test: `Tools/VR/run-live.ps1` (up, all nine `live-*.txt`, one table, down). First full run 2026-10-02: 38 of 40, the two failures a drowned player (fixed in the scripts).
- [x] Screenshots: `log SHOT <name> [of copy|npc|drop|player]` in a live script (DevBench capture plus its free camera). The bot's copy and the player are both visibly drawn in `logs/shots/live-look-*.png`.
- [x] Linux build check: `Tools/VR/check-linux-build.ps1` (the repo's Dockerfile; build, unit tests under GCC, protocol id compared with Windows). First run found a second GCC-only error (`Inventory.h` used `std::optional` without including it); fixed, then clean, 3807 assertions passing on Linux.
- [x] Protocol id: a digest of `Code/encoding`, sent at login and compared by the server instead of the version. A bot from build `5f5d7f3` was let in by a server from build `eb60a4e` (same id `7016c98d`), refused after a message file changed (`957d2ba5`), and Linux computes the same id as Windows.
- [x] Bot replay of real movement: `capture` / `replay` in the bot, `live-walk` and `live-creatures`. A bot walking as before: its copy has no speed in its animation graph (0 of 16 samples). Replaying the real player's walk: 14 of 14. Seeker without its replacer 17%, with it 78%; Lurker 69-85%.
- [-] Our own questions in DevBench: not worth it. It means building DevBench's API source into the client everyone runs, for a tool only this machine has, and the tests have read what they need from the log in every run.
- [x] `session-report.py`: dropped items, game-made actors, respawns, captures, deaths not applied, slow updates by section, skeleton searches; and "is being moved here" no longer counts a dropped item as a dragged corpse. Run on 2026-10-02's sessions.

### Crashes and stability

- [x] Death while connected: `live-death`, killed twice by console, back in the world after 5 s both times, alive at 335, still connected, never at the main menu (7 of 7 checks).
- [!] Loading a save after dying: crashes in the no-headset rig when the load falls inside our death sequence (3 of 3), not 40 s later (2 of 2) and never without our respawn (4 of 4). Waiting on a test or action's headset test, above. Since then a load with no death at all crashed the same way (2026-10-03 01:41:59, `skyrimvrtools.dll+0x71B5`, the first load of `live-load`, 25 minutes into a session; the same script passed early in other sessions), so in the rig the trigger may be the fake controllers' state rather than our death sequence. The headset test is still the only way to know. One more of the family on 2026-10-03 07:47: a trip by cell (`cow`, a loading screen) half a minute after the mod had brought the player back from death crashed at `SkyrimVR.exe+0x6C689A` (`PlayerCharacter::UpdateAnimation`, a null pointer), one run of `live-sender` in two; the other run made the same trip cleanly.
- [-] Wrist menu crash: not reproducible here. Tween then inventory opened 20 times through DevBench, 170 ms and 20 ms apart, while connected: no crash, nothing left open. The crash had controller input reaching a menu mid-start and no frame of ours; DevBench opens menus without a controller.
- [-] Seen's stuck journal: not reproducible here. Journal opened and closed 25 times while connected (10 with a second open, 15 closed 50 ms after opening), game window unfocused throughout: it closed every time. If it happens to Seen again, his log's `Menu queued` and `Probe` lines around it are what to send.
- [x] Quit crash: twenty quits by `qqq` since 2026-10-01 18:00 (the last on 2026-10-03 01:08), no crash report and no dump from any of them. Every crash report in that time is a mid-session crash with its own entry.
- [x] The other places that delete temporary actors: none of the three crashes on a game-spawned creature by itself (`live-temp-remove` 3 of 3, `live-temp-reconnect` 5 of 5), but all three left the deleted copy's id in the client's "my copies" list and the game hands ids out again. `live-temp-reuse`: a Seeker given a deleted copy's id was taken for a copy and the game crashed at `SkyrimVR.exe+0x714EB2`, the crash of 2026-10-01 again; with the ids taken out, the same Seeker is left to the game (3 of 3).
- [!] **Crash within a second of travelling away: PLANCK and three or more of our copies.** `SkyrimVR.exe+0x3AC1A8`, `+0x2EEF6A` or a cast on garbage, 0.5 to 0.9 s after arriving through a loading screen (Emma's 2026-09-26 Apocrypha crash has the first address). It needs three or more copies this client made left behind, which the game destroys during the load (`live-copies-left-3` crashes in a fresh session; `live-copies-left-2` does not), **and PLANCK**: with PLANCK switched off the same red test passes (2026-10-03 03:00, one run, mod list restored afterwards). PLANCK gives creature bones physics bodies; it looks like it keeps those of copies the load destroyed. Three tries from our side did not fix it (disposing of copies we run at unload, which never ran; the identity fix, which only took the copies out of one test; disabling our copies as the loading screen starts, which crashed during the load instead). Question for Emma above.
- [x] A creature the game made and its copy side by side (one creature twice, for everybody): the client takes its own actor back instead of making a copy (`s_handedAway`). `live-copy-abandon` green three times; in the full suite every returning Seeker came back as the same actor.
- [!] The game ended by itself once, thirty seconds after a green run (2026-10-03 00:35): no crash report, no Windows error event, the log stops in mid-flow. Question for Emma above.
- [x] Found on the way, in the server: a player who left before having a character had the first player's character removed instead (`value_or(entity 0)` in the disconnect clean-up). The host became invisible to every newcomer and the server logged `Entity is invalid: 0` four times a second. `Tools/VR/leaver-check.py` (three bots, no game): red once, green twice.
- [!] Script-engine crash on freed forms: a soak here cannot settle it. The crash comes about once in months in single-player (ten times since 2024-12) and twice in one co-op evening; an hour each way with a bot would show nothing either way. The test is the one in KNOWN_ISSUES.md: both players switch the two settings off and play as usual. Action for Emma above.
- [x] Overlay calls that can run before the in-headset menu exists: every `OverlayService` handler, `PartyService`, `VRDashboard` and `OverlayClient` path checks for the overlay first. One hole, closed: the window-message hook (`InputService::WndProc` and the four `Process*` functions) used the service pointer without checking it, and the hook outlives the service at both ends.
- [-] The two havok crash guards from the other VR port: none of the 95 crash reports on this machine has either address (`+0AB1ABA`, `+03AD7B1`). Not ported; the pointer stays in "TiltedEvolutionVR `29f99ed`" below.
- [x] "Not a crime faction": already fixed (found by load index); five deaths on 2026-10-02 logged `Solstheim crime faction found at 2018279` and no error.
- [x] Crime alarm guard: fires and the game carries on. `live-crime` steals a horse at Windhelm's docks while connected (`SendStealAlarm`): `Crime alarm ran (1): offender 14, witness 9848C`, no null actor skipped, game alive after (4 of 4).

### Performance

- [-] Benchmark, same save and spot, mod off against on: not measurable in the rig. Frames there are held at 60 per second whatever runs, and with the mod off nothing reports frame times at all. What the rig does measure is the mod's own cost, in every session's `Perf last 30 s` line: 0.15 to 0.26 ms a frame on average in the sessions of 2026-10-03.
- [x] A freeze that grew through a session: the local skeleton search spent its time asking Windows whether the bone list was readable (`VirtualQuery`, which slows as the game's memory grows), a few hundred times per search. Comparable sessions on 2026-10-03, at the start / 10 / 20 minutes: 44 / 49 / 178 ms before, 7 / 15 / 31 ms after (answers reused within one search). The night before, the ten-minute re-search had cost 210 to 284 ms. Still growing a little; the remaining calls are a dozen per search.
- [x] Spawn bursts: the cost was not making the copies (first placement measured 0.0 ms) but asking Windows, once a frame for every copy still waiting for its 3D, whether it was a live object (`VirtualQuery` in `IsLiveGameObject`, about 10 ms a call in a big process). Now a guarded read. `live-spawn-burst`, nine copies arriving together: before, the spawn update was the slowest part of the frame at 14 ms with one copy waiting, 24 with two, 36 with four, and the frame peaked at 121 ms; after, it never shows in a slow-frame warning and the worst frame was 11.5 ms (the copy diagnostic).
- [!] Remote movement shown 300 ms late: a feel trade-off, so Emma's call (question above). The data: in the co-op session of 2026-09-30 the newest movement update was 206 to 313 ms ahead of playback (`InterpDiag`, Emma's side, Seen's updates), worst 128, so about 100 ms could go without ever running out of points; a bot on this machine cannot stand in for Seen's connection.
- [-] The once-a-second naked-NPC check: not worth changing. It was never the slowest part of a slow frame in any log kept (about 4,500 slow-frame warnings since 2026-09-26), and it already gives up on an actor after three tries.
- [-] The once-a-second equipment snapshot: not worth changing. Every 30-second report since it was measured says 0.00 ms per frame (1,043 reports, real sessions with Seen included).
- [x] Skeleton search on every menu: the player's 3D root changes when a menu opens and closes, and each change cost a search of 6 to 23 ms. Fifty menu opens: 68 searches and 51 slow updates before, 2 searches and 1 slow update with two roots remembered. The safety re-search is every ten minutes, not every minute.
- [x] Other per-frame costs: the mod costs 0.15 to 0.3 ms a frame on average (every `Perf last 30 s` line of 2026-10-03); linear searches and lookups do not show. The slow frames were three `VirtualQuery` callers: the skeleton search and the spawn update (both fixed above), and the copy diagnostic when a player copy appears, 10 to 18 ms before and 8 to 12 ms after sharing the answers within one description.
- [x] The dashboard overlay stopped the game right after the first load: 0.9 and 1.5 s in Emma's sessions of 2026-09-30 and 10-01, 1.3 to 1.5 s in every session with no headset. Its browser is now started when the main menu appears (the page paints two seconds later, while nobody plays); what is left after the load is the SteamVR overlay itself: 77 ms (2026-10-03 04:06), page ready and "enterGame" sent as before, `live-copy` 20 of 20.
- [x] Frame-rate drop after kills (reported 2026-09-30): the session's own reports explain it. From 19:30 on, 197 slow frames all name `RunLocalUpdates`, on an exact five-second period, 27 to 58 ms and growing as the session went on, and "frames over 50 ms" went from 0-1 to 5-10 per half minute: the local skeleton search with its old five-second safety net, which slowed as the game's memory grew. The net became 60 s on 2026-10-01 and ten minutes on 10-02, and the search itself 7 to 31 ms instead of 44 to 284 on 10-03. The kills only coincided with it.
- [x] The action flood (the "unequip loop" family): an `ActionFlood` log line now names any actor repeating one action more than ten times a second. It showed creatures retrying actions every frame that the game refused (one at Mistwatch: action `132AF` 53 to 62 times a second, all refused), all of it sent; and a Netch standing still performing `IdleSpecialStart` about 40 times a second, half accepted. Refused actions are no longer sent, nor an identical action repeated within a quarter second. The bot's capture of the Netch: 545 actions in 12 s (489 `IdleSpecialStart`) before, 59 (50) and 48 (47) after. Full live suite 87 of 87 after the first change; after the second, gait checks pass in two runs of three (the third: one Seeker copy at 42% against 50%, run-to-run variation). The re-equip check suspected in 2026-09 is capped at three tries since 09-24. Narrowed afterwards to idle actions only (movement and attacks are never held back); the Netch still sends 39 in 12 s.

### Session with Seen, 2026-10-03 09:00-09:37 (both on e766b05)

Read from both clients' logs and the server's. The first fixes are in build 59984a2, the rest in 22ae2af (the one to
send; it changes the protocol, so Seen needs the new exe **and** a server rebuilt from the same commit).

- [x] Seen invisible for Emma three times (09:19, 09:30, 09:35), each fixed only by his reconnect: the server re-sent him as he came through a door, the re-send landed on his old copy still in the previous cell, and that cell's teardown then held the copy back on the "held" branch, which did not ask the server again (the other branch did, since 2026-09-26). Now both branches re-announce the cell.
- [x] Emma invisible for Seen at 09:32, a Bandit Outlaw in her place: her new copy FF001231 was lost by the game within 29 ms and the id went to a Bandit Outlaw copy; her character kept pointing at FF001231 and moved the bandit every frame. A new copy now takes its id from any character still pointing at it, and a copy whose base no longer matches is treated as gone (RemoteComponent::CachedBaseId).
- [x] Both "sitting on the floor" after respawning: after every death the copy on the other screen replayed the owner's "Ragdoll" and lay down for one to two minutes (hands at floor height). A player copy now keeps its own life and knock state and does not replay "Ragdoll".
- [x] Seen took all of Emma's arrows at once on unpause (09:14): three were launched on his side while the journal and the console paused his game, hung there, and hit 0.13 s after he closed them. Remote projectiles are no longer launched into a paused game.
- [x] Emma's bowl moved "sluggish and stuttering" on Seen's side: positions were sent ten times a second; now thirty.
- [x] "could not spawn actor for remote server id" 78,769 times on Emma's side and 51,002 on Seen's (0.75 ms a frame instead of 0.14): retried every frame. Now every five seconds, logged once.
- [x] Stand-in copies (a copy of the owner's creature for a reference not loaded here) vanished: they were made at the owner's position, outside this game's loaded cells, got no 3D, and the next grid shift deleted them (five at 09:08:30.493, the moment of a grid change); those creatures were then invisible for Emma. Now a stand-in whose copy is gone is made again once the character is inside the loaded cells (22ae2af). The "frozen unkillable Mistwatch bandit" (09:09) was stand-in FF001177 that Emma's game accepted as its own when the server handed the creature over: the check for "a copy of ours stands in for this reference" looked the copy up by the wrong key. Fixed; such a hand-off is declined (22ae2af).
- [x] Emma teleported back into the tower at 09:32, 0.95 s after her game replayed Seen's cast of spell 3100FB0C (Conduit.esp, teleport magic) on his copy. Conduit's spells are no longer replayed (22ae2af); see the note for Emma above.
- [ ] The dragon of the "Dragon fly by" quest (35541, Seen's) fell out of the sky on Emma's side and was not there for Seen until 09:30: a flying creature's copy is not kept in the air.
- [x] A bandit (45AB9) killed by Emma was dead on Seen's side too, but Seen saw it standing: his game kept replaying the owner's actions on the corpse, and an attack or idle replayed on a dead actor stands it up. Nothing is replayed on a dead copy any more (22ae2af).
- [x] Bodies: Seen carrying one sent nothing (only the owner sends, and Emma owned it); now a client whose player moves a body it does not own for a third of a second asks for it, and the server gives a dead body to any member of the party, not only the leader (22ae2af). Emma carrying one arrived deformed: only the upper body's bones were sent and the other side kept its own legs; the legs are sent too now (22ae2af). Not yet seen in the game.
- [x] Objects in the world moved by hand (a bottle in hand, a cart) were not synced. Now each client watches the movable objects within 400 units of its player (items, alchemy apparatus, movable statics such as carts) and sends where one goes; the other client holds it and follows, and the server remembers where it was put down for whoever comes later (22ae2af, new messages). `live-world-object` green twice: the game's move reaches the bot, and the bot's move puts the game's plate within 20 units of where it was sent.
- [ ] Lydia did not fight back while Emma was down (09:36), and enemies ignored Seen while he was invisible to Emma (Emma's game had no copy of him to attack). Recheck after the invisibility fixes.
- [x] Seen's crash at 09:36:59 was the quit crash (journal open, EnchantmentEffectExtender.dll reading the UI singleton +0x160), the known family.

### Session with Seen, 2026-10-03 15:06-15:24 (both on 22ae2af)

Read from both clients' logs. Seen's clock runs about 3 s ahead of Emma's.

- [x] Seen "teleported inside constantly" (15:21-15:22): six times he came out of Mistwatch's upper door (`3F558`) and his own game made his player use it again 0.8-0.9 s later; each time Emma's game received his next activation just as his copy was removed. An activation that arrives in the player's own name sends the player through the door: reproduced with the bot (`live-door-echo`, red on 22ae2af, the player ended in Mistwatch01). Now a game refuses an activation by one of its own actors coming from elsewhere and logs `came back from another game; not done again`, and it never sends an activation by another game's actor (a copy) -- on Emma's side only Seen's copy could carry his id (73fbe77, green three times). Not proven: what made Emma's game send one in his name; our own replay does not pass through the hook, so it was something his copy did by itself there. If he bounces again, the log says which side it came from.
- [ ] Lydia popping in and out for Emma (15:09:39-55, 92 times) and gone for Seen afterwards: Emma's loading screen took 10 s with nothing sent, the server gave Lydia to Seen (still inside), while Emma's game had brought her outside as Emma's follower. Seen's updates put her back inside, Emma's follower AI pulled her out, every 130 ms, until Emma took her back. Seen's game then placed her 11,752 units lower than before (an outdoor height inside). Design question for Emma above.
- [ ] A book Emma touched could not be read by Seen (15:15-15:16): Emma took the skill book `108DE7` (SkillHeavyArmor1) and dropped it again; Seen's game never learned it was taken, still had it on the table, and took it for Emma's drop ("already lying here as 108DE7"), then held it while she moved it. Every hold was released 2-3 s later as designed, and in the rig a plate held and let go of can still be taken (`live-world-object`, `player has 31941`). Not reproduced; ask Seen what "cannot interact" looked like (no prompt, nothing happens, cannot grab).
- [ ] The bandit "ignoring us" (15:18): probably the unequip loop -- the Bandit Leader `430B5` refused 92 Unequip actions a second on Emma's side (15:19:58). Not looked into further.
- [x] Seen's crash at 15:24:28: `SkyrimVR.exe+0x93CE11`, the script engine on a freed temporary form (`FF001253`), the known family (KNOWN_ISSUES.md section 6). EngineFixesVR `FormCaching` is false on Emma's machine since 2026-09-30; on Seen's it is not known.

### Session with Seen, 2026-10-03 20:44-21:07 (both on 73fbe77)

- [ ] Emma's game froze at 21:06:48, one frame after loading into the Ragged Flagon: the log stops, no crash report. No stack, so the cause is not known; the rig's freeze at 12:07 with the same signature was the game's own allocator deadlocking (KNOWN_ISSUES.md section 6). Build 5d11dbd adds a watchdog: after 25 s without a frame it logs where the game's main thread and our update thread are (registers and stack scan), and every 30 s a "Health:" line with memory and thread count. The next freeze will say where.
- [x] The dead Blood Dragon "out of nowhere" (20:55): the Mistwatch dragon `35541` (DragonLair8Boss) has been a corpse since this morning. Emma's game took it over at 09:06:05 the moment it arrived, in flight, and it "fell from the sky next to Dosylia"; by 09:16 it had its death item and from 09:17 its corpse was being moved. At 20:54-20:56 the corpse moved on Seen's side by Emma's updates and by itself, and the dead-body hand-off (built in 22ae2af) made the two games ask for it in turn, four times in a minute. A body is now asked for only when one of this player's hands is on it, never when its owner's updates move it (5d11dbd; `live-body-owner` green, old exe green as well, so the test does not reproduce the dragon's case).
- [ ] Why dragons are "already dead": the morning dragon died after the leader's game took it over mid-flight. Question for Emma above.
- [ ] Emma's 19:52 crash (before this session, on the rolled-back save): the AI thread on an actor with no cell (`SkyrimVR.exe+0x24E6D0`, GetWorldSpace), 47 ms after the server teleported nine actors into cells outside Emma's loaded area (`MoveActor` loads such a cell and moves the actor into it). One occurrence; not changed yet.
- [ ] The bear "did a backflip on dying" and its death position was not synced, for Seen only (20:50). Low priority; not looked into.
- [ ] Hands where the owner's are. 02e6590 pulled the copy's hands onto the owner's reported positions (two-bone IK) and folded Seen's outstretched arms onto his copy's chest (Emma's screenshot, 2026-10-04 08:55): VRIK stands the owner's body some way behind its root (50 units in the rig) while the copy's stands over it, so positions measured from the root put the hands on the wrong body. fd631fb: the IK only fine-tunes, at most 15 units, and otherwise leaves the owner's pose; the owner's hip offset is now sent always and the receiver logs "body stands (x, y) from where its owner's does", not moving it. Next: from those numbers, decide whether to stand the copy's body where the owner's is (risk: the visible body away from its collision box), then the hands can meet.
- [ ] Dead bodies lie in different places on the two screens, and nothing we do moves a corpse (2026-10-04): the receiving game "moves" a corpse to its owner's with ForcePosition, which moves the reference and not the ragdoll the body is drawn from (`live-corpse-place`: the camera at the bear's new position found empty ground). MoveTo did not move it either, nor papyrus MoveTo/SetPosition, nor the console's moveto in the rig -- our HookSetPosition sends every move of a non-player actor through the game's virtual SetPosition without the character controller, and the original does not move a ragdoll either. Next: find what moves a ragdoll (the game's own corpse moves, the bhkRagdoll), then a corpse goes where its owner's lies when the owner stops sending it. Meanwhile "CorpseDiag: X lies N units from where its owner's corpse is" in the log measures it.
- [ ] Dragons taken over in flight fall: a better fix than not taking them over is to carry the flight with the hand-off. First reproduce it in the rig (a dragon flying, handed to the bot and back), then make the receiving game keep it in the air; if that cannot be done, hold the hand-off until it lands.
- [ ] World sync Emma ranks highest after crashes: a body being dragged, the swords in the hands of the other player's copy, items held in a hand -- each "in the right spot" on the other screen. To be measured in the rig first (positions on both sides), then fixed one by one.

### Session with Seen, 2026-10-04 09:27-10:00 (both on fd631fb)

- [x] Hands "still off, about 20 cm above and further away": the copy's body stood a median 22-28 units in front of the owner's VRIK body on both screens (327 samples of the new "body stands" line), and the hands were 20-25 units too far forward with it. The whole-body offset in PoseActor was added at every level of the bone chain (six times at the hand: the rig's 50-unit offset put the hands 269 units away), which is also why a dragged corpse came out stretched. Now applied once, and the copy's body stands where the owner's does, horizontally (e9d7d85). Rig: the copy's hands moved by exactly the body offset, arms natural. Needs a real session for the hands meeting; the height part ("20 cm above", median +3 to +7 units) is not addressed.
- [ ] PVP and spells missing: positions shown 300 ms late, and the visible body 22-28 units off its owner's (fixed above). The delay is the open question for Emma above.
- [ ] Both games slowed down over the session: 150 to 470 threads and 11 to 15.4 GB on Emma's side in 30 minutes, the same thread growth on Seen's -- the DynDOLOD thread leak per loading screen (KNOWN_ISSUES.md section 6). Question for Emma above.
- [x] Lydia struggling to appear outside and coming out in an old outfit plus her Orcish armour (2026-10-04 09:33, also 2026-10-03 15:09). The server handed all of a silent owner's actors to the other player after 3 s, and a loading screen is 8-10 s of silence: Lydia went to Seen, followed Emma out anyway, and the two games pulled her back and forth (85 appearances in 12 s); Seen's game, where she is nobody's follower, dressed her in her outfit from his save and sent it, and Emma's game applied it. Now (6f16f3f): the server waits 15 s of silence; the player's own follower keeps her gear (nothing from the server or the other game is applied to her), is asked for back the moment she is beside the player, and the server's copy of her inventory is put right when she is; the leader's claim is not refused as "out of range" over the stale position, and the new owner is always told it owns the actor (it was not, when the server had the actor out of its range). `live-follower` green: back 40 ms after catching up, no loop.
- [ ] Maven Black-Briar fought by Seen, invisible for Emma (Seen seemed to fight Lydia); the blacksmith dead on Seen's side, standing on Emma's; Brynjolf (essential) spinning on the floor for Seen after being "killed".
- [x] Seen's crash at 10:00:20: the quit crash (EnchantmentEffectExtender, journal open).

### Session with Seen, 2026-10-04 15:50-16:27 (both on 357bb590)

Hands "perfectly synced" (first time).
- [x] Emma's crash at 16:26:41, SkyrimVR.exe+0x714EB2 in the movement controller's destructor, a Troll in R15 with its form id already zeroed. 32 ms earlier the client had turned reference 85FAD into a ghost: rolled as a Troll here, a Bear on Seen's side, 15700 units away at the corner of Emma's grid. The troll was never loaded here (no "Spawn Actor: 85FAD" all session), so the client disabled a reference the game was disposing of. That is the same pattern as the documented crashes of 2026-10-01/02. Now a ghost is disabled at once only when it has 3D and is not deleted; otherwise it is disabled when it loads (ProcessNewEntity, which already did that safely three times that session).
- [x] "Belly position of both of us is buggy": a bug of the hip offset of 2026-10-04. The whole-body offset (30-40 units in this session) moved the posed bones and what hangs below them, but not NPC Spine [Spn0], between NPC COM and Spine1, which the waist is skinned to. It now moves the whole skeleton (Rig::Body: the root, every node, every flattened bone), which also covers dragged corpses.
- [ ] "Sword sync is still off": the copy hung its weapon off its third-person hand at the skeleton's angle, while the owner's VR weapon hangs off the first-person hand at the controller's angle. New: the sender reads the first-person attach node ("WEAPON"/"SHIELD") relative to its body's hand (VRPose::HasWeapons, a protocol change: client and server both), and the copy's weapon is put there (PlaceWeapons, skipped past 30 units). The first-person hands are read at PlayerCharacter+0x590/0x598 (CommonLibVR-NG), checked by name. Rig, `live-weapon-grip`: both hands found by name at those offsets; the rig's first-person sword (no real controllers) sat 7 degrees and 0.6 units from the body's WEAPON node, and the bot's copy had its sword turned 21 degrees and moved 1.3 units to the owner's grip; hands median 74 units (rig targets at the waist), game alive. Needs a real session for the look.
- [x] Boethiah Cultist on Emma's side, a Bandit Marauder on Seen's: a temporary actor's copy was made from the owner's levelled pick, and a named NPC that takes its stats from a levelled list (Boethiah Cultist B0E87, stats from Bandit Marauder 39D25) has that list's pick as its pick. Now a base with its own name, different from the pick's, is used (BaseForCopy, both spawn paths).
- [x] Flame Atronach dead for Seen, standing for Emma: Emma owned it, it died and burst, her client gave it up, and the server sent it straight back, dead. The fresh copy was killed before it had any 3D, and the 3D step then found it dead already and did nothing. Now the death is applied once the 3D is there. The blacksmith of the morning session (dead for Seen, standing for Emma) may be the same thing.
- [ ] Dinya Balu sliding on Emma's screen, not Seen's. Emma's client made her as a copy of Seen's at 15:55:41.889 and claimed her as party leader 60 ms later; Emma owned her until 15:57:18. So she slid on the screen of the game running her. The gained path clears the copy's components; the cause is not known yet.
- [ ] Seen's crash at 16:26:56: EnchantmentEffectExtender.dll, the signature of his quit crash, 15 s after Emma's crash. Ask whether he was quitting.

### Swords between players, Blade & Sorcery as the aim (2026-10-06)

Emma and Seen: "sword feeling still unsync"; the aim is B&S, feeling the weight and being blocked by the other's sword.
Agreed: the defender's screen decides whether a hit was blocked. Found before building anything:
- [x] PLANCK does not make characters' equipped weapons physical: that code is commented out ("TODO", `PostDriveToPoseHook`
  in its `main.cpp`). The parry mod (WeaponCollisionVR) parries only an enemy in an attack animation, and nullifies
  the game's melee hit on the player; a VR player swinging freely is neither. So sword-on-sword between players has to
  be ours. PLANCK does drive characters' ragdolls toward their animation pose (hook at the call to `driveToPose`,
  VR `0xB266AB`, after copying `hkbCharacter.poseLocal` when foot IK is on), which our pose never reaches: we draw it
  at the renderer's frame end. That matters for hits on the body (goal 6), not for blocking.
- [x] The touch check reads what is drawn: in the rig, 0.0 units from where the copy's hands and weapons were drawn 1-3 ms
  before, every 5 s while posed; 70-80 units (the copy's own animation) only once it is no longer posed (out of view or
  no pose arriving). A new log line says this each session ("VRWeaponTouch: X as this check reads it ...").
- [ ] What made it feel out of sync is not measured yet; no log of the session was found. Candidates: the other player's
  sword is shown 225 ms in the past (a swing at 1-3 m/s is 20-70 cm away by then); the check only buzzes; one-piece
  blades may not be measured as blades (no far node, see `ReadSide`); our own sword is measured from the VRIK body's
  node, not the first-person sword (7 degrees apart in the rig).

### Sync: verify in the real game, fix what fails

- [-] After a burst of copies, a returning player copy "bound to another actor": the test, not the mod. Standing at the Mistwatch cell centre (where the burst test leaves the player), the bot's "far away" spot is still in range, so its character keeps being sent; the first copy, placed at the edge of the loaded cells, is gone a tenth of a second later and the client makes another one for it (`Spawned character for entity`), which is right -- health 55 where it should be. The driver only read spawn lines and kept asking about the first. It now follows the binding (`New entity remotely managed`): the same pair 22 of 22.
- [x] A creature the game made (placed, a quest spawn) that was handed to the other player while its maker was away came back twice (three Seekers placed, six standing): fixed, see the identity line under "Crashes and stability"; `live-copy-abandon` finds no copy after the round trip.
- [x] The real game as sender: `live-sender` (2026-10-03), the player loses health, equips a sword, kills a bear of the cell, dies and is brought back, goes elsewhere and back; the bot sees each one (18 of 18: health 275, sword held, the bear dead 0.3 s after the kill, the player dead then alive, same cell both ways), and the game confirms the bear alive before and dead after. Two traps found on the way: the Mistwatch bandit 45A63 is already dead in Emma's save, and the VR console refuses `860F8.kill` (`DO papyrus Actor Kill 860F8` works).
- [x] Dropping and picking up from the game's side: an item dropped the ordinary way was never shared. Dropping goes through RemoveItem with the "dropping" reason (console drop and the game's own DropObject alike), and only Actor::DropObject announced drops -- the 47 shared on 2026-09-26 came that way, most likely items taken into the hand. RemoveItem now announces the player's drops too, guarded against announcing one twice. `live-game-drops`: before, the bot never saw the sword (6 of 9 failed); after, it sees it land and go when picked up, two runs of two.
- [x] Old dropped items announced on connect, for what was dropped while not connected in this game session: the client keeps that list itself and announces what still lies on the ground (`live-game-drops`, a sword dropped while the server was down: "1 of the 1 item(s) dropped while not connected announced", the bot sees it, two runs of two). Not covered: items dropped in an earlier game session. The game's own list of the player's drops was not found in 74 connections; it is probably kept on the player object, not in its extra data.
- [x] Equipment on a copy: the bot equips, the game's copy holds it, in both hands -- `live-equip` (2026-10-03): an iron sword in the right hand and out again, an iron dagger in the right and then in the left (`GetEquippedWeapon`), 10 checks. Two things learned: the copy has to own the item (a real client's inventory sync sees to that; the bot now has `additem` for it), and the game's `IsEquipped` does not count a left-hand item, which first read as "the left hand never takes".
- [x] Factions: corrected on return already worked (the spawn re-sent after a trip carries them), but a change made while both players were there never reached the other screen -- the client's handler only matched actors with a CacheComponent, which copies made from a server spawn never get. Now every remote actor. `live-factions` (the bot's bear joins the player's faction while the player is away, then BanditFaction in front of them): 5 of 7 before, 7 of 7 twice after.
- [x] Levelled creatures: a different animal at the same spawn point is conformed to the owner's. `live-levelled` (2026-10-03): the bot claims the bear's point near Mistwatch (860F8) as a goat (2EBE2) while the player is away; on return the client applies the pick and a goat stands there ("Applied leveled NPC pick ... pick: 2EBE2", "Spawn Actor: 860F8, and NPC Goat"), two runs of two. The game's `GetLeveledActorBase` answers with the temporary base it builds, so the check reads the client's own lines.
- [x] Whiterun gate: the copy goes when inside and comes back when out. `live-whiterun` (2026-10-03): with the bot outside the gate (Tamriel 5,-3), the player's copy is known; the player goes into WhiterunWorld and the server removes it from the bot's view ("other worldspace"); the player comes out and the bot has it again (8 of 8). The bot needed a `teleport <x> <y>`: a trip by cell coordinates sends no cell-change message, so it never followed.
- [x] Time applied on VR: `live-time` (2026-10-03), the hour set to 3 and then to 21 by console while connected; four seconds later the game clock is back on the server's, 17.6 both times.
- [ ] Weather applied on VR: only a party member receives the leader's weather, and with a bot the game is always the leader (the bot leaves a party it would lead, on purpose). Needs a bot that leads and sends `RequestWeatherChange`, with the game joining as a member; then compare the game's current weather with the one sent.
- [ ] Mounts, by script.
- [x] A player in a menu keeps sending, so its creatures are not handed away: `live-menu-hold` (2026-10-03), the inventory and then the journal held open twelve seconds each next to the bot. The inventory does not pause while connected; the journal does (pause counter 1) and the client still sent movement every frame. No "Silence" warning, nothing handed to the bot.
- [x] Hits on the other player's NPC reach the owner and come back: `live-hit-npc` (2026-10-03), the player casts a Firebolt at the bot's bear (`Spell.Cast`; no sword arm without a controller); the bot, as owner, sees its bear's health drop (to -109), reports it dead, and the game's copy dies (4 of 4 and the bot's 5). The copy's own health does not move when hit -- the owner decides, as designed.
- [x] NPCs below the floor: `SinkDiag` named 11 placements in all the logs kept (2026-09-26 to 10-03), one actor each time and never the same one twice. Nine came 0.3 to 10 s after that copy appeared -- its first placement, then settling onto the ground, 132 to 1,345 units; the largest, 4,112 units on Seen's copy right after it spawned, belongs to "Copies spawned on the ground when the incoming height is invalid" below. The other two were 22 and 141 units. Nothing sinks repeatedly, which was the bug watched for.
- [ ] The downed follower "lying sideways and running": bot-owned NPC in bleedout that keeps moving.
- [ ] Enemy health bar on the other player: is it drawn (needs screenshots).
- [ ] The invisible body, indoors and outdoors (needs screenshots).

### Sync: things to build

- [ ] The bot registers one of two NPCs asked for in the same instant: `live-copies-left-2` fails its own `owned == 2`
      every run on 2026-10-06 (two `npc temp 23A8A` 10 ms apart, one "NPC registered" back). The game's checks in that
      test pass; the test then only leaves one bear behind. Server or bot, not the client.

- [ ] An equip of a weapon the copy already holds may leave its hand empty: in `live-equip` run straight after `live-game-drops` (2026-10-03 12:54), the bot's copy arrived already holding the iron sword (from its snapshot), the bot equipped it again, and the copy then reported it equipped but nothing in the right hand. On a fresh server the same script is green. Reproduce on purpose (equip twice) before touching `InventoryService::OnNotifyEquipmentChanges`.
- [ ] The creature-gait check is noisy: since 2026-10-03 05:00 it failed in four runs of nine (a Seeker or a Lurker copy at 30 to 42% against 50%), after passing the five runs before. Not the action filters as far as the runs show: limited to idles, it still failed once in two. An A/B with the filters off, ten runs each way, would settle whether anything of ours moved it.
- [ ] The other order of "a creature this game made comes back": the server sends the creature before the game has its own actor loaded again. A copy is then made as before and the pair is back; the log says `HandedAway: ... a copy of it stands here too; not handled`. Never seen (the actor was loaded first in every run); build it the day that line appears.
- [ ] Objects already in the world moved by hand (a cup on a table): positions in `ObjectService`, as for drops.
- [ ] Removals for the viewer who walked away (the asymmetric range path).
- [ ] A newer ownership number accepted by the six receiving handlers instead of exact equality.
- [ ] Shared follower: the follower belongs to the player it follows.
- [ ] Dragon range check for remote dragons with no local copy.
- [ ] Dragged bodies: the bot sends a body's bones, to see why the receiving side skips or misplaces them.
- [ ] Nocked arrow on the other player's bow (build; seeing it needs a recording).
- [ ] Ownership warnings in the logs ("actor for ownership transfer not found"): where from.
- [ ] Copies spawned on the ground when the incoming height is invalid; fade in.
- [ ] Item and body drift fixes from the other VR port, if drift shows up.
- [ ] Wake a floating item when its carrier disconnects mid-carry.
- [ ] Re-send an NPC's health after a revive.

### Making it shareable

- [ ] Start-up checks with plain messages: address library present, fewer than 255 plugins, plugin header.
- [ ] Install script (finds MO2, copies the tool, adds the launch entry, creates the connect file).
- [ ] Guide for other modlists.
- [ ] Licence check before sharing builds.
- [ ] Server defaults for VR (difficulty, PvP, time scale).
- [ ] A test checklist per build.

### Housekeeping

- [ ] Bring this file in line with what was confirmed on 2026-10-01 (48 "untested" markers, superseded entries).
- [ ] Warn on screen, not only in the log, when the installed address library disagrees with our own table.

## Checked against the real game with nobody in the headset, 2026-10-01

The game now runs without a headset (`Tools/VR/headless.ps1 up|down`: SteamVR's null driver with two virtual
controllers, DevBench to load the save and ask the game questions, a bot as the other player), and
`Tools/VR/live-check.py <script>` runs a bot script and asks the game itself at every `CHECK` line. Everything below was
read out of the running game, not out of the server. What a headset is still needed for is how things look and feel.

**Fixed, found by these runs:**

- **A crash on walking through a load door with a game-spawned creature nearby.** `SkyrimVR.exe+0x714EB2`, twice out of
  twice before the fix, none in two runs and one more in passing after it. During this player's load screen the server
  hands their creatures to the other player, so on this side they become "remote"; the old cell unloads and the game
  disposes of any temporary actor it had spawned itself (a summon, a random encounter, anything a script placed).
  `CancelServerAssignment` took every temporary remote actor for a copy this client had made and disabled it in the
  middle of that disposal; 25 ms later the game's movement code ran on it. The client now keeps a list of the copies it
  really made (`s_ownCopies`) and leaves everything else to the game, saying so in the log (`was made by the game, not
  by this client; left to the game`). Test: `live-away-seeker`.
- **A diagnostic stalling the game every 30 s.** `RunRemotePlayerDiag`'s `CopyDiag` line walks a whole skeleton by name:
  17 to 34 ms, thirteen times in seventeen minutes with one other player near. Each copy is now measured once when
  first seen and then every five minutes.
- **"failure to find self in behaviorPool, Lurker / Netch"** at every start was a false alarm (a replacer for a creature
  the mod has no built-in behaviour for has no "self" to find) and is now one info line.

**Sliding, measured on 2026-10-02 with one game and a bot that plays back real movement:**

- `live-walk`: the bot walks as it always did and its copy slides (no speed in the copy's animation graph in any of
  16 moving samples; SlideDiag 196 of 196). The real player is then walked forward by a held key, the bot records the
  stream the server relays for them and plays it back as its own character: the copy has a speed in 14 of 14 samples
  (SlideDiag 0 of 810), and the screenshot shows it mid-stride. So a real player's copy does not slide here.
- `live-creatures`: the game spawns a creature, the bot records its stream and plays it back on a creature of its
  own. **Seeker**: with its replacer folder taken out, none of 102 updates carried animation variables and the copy
  had a speed in 17% of samples; with it, 107 of 107 and 78%. **Lurker**: 69-85%. **Netch**: hovers on the spot, so
  there is no gait to read; its updates do carry variables (102 of 102). That was the question left open on
  2026-10-01 as needing two real games.
- **SlideDiag was counting steady movement as sliding.** It called a move "sliding" when the animation variables had
  not changed between two snapshots, which is also true of anything running at a constant speed: the replayed walk
  was 122 of 122 "sliding" while the picture showed legs moving. It now also requires that the variables say nothing
  (no non-zero float), which is what the bug of 2026-09-26 actually was. Real sessions did not show it because an
  analog stick never holds still.
- **A Netch floods the stream.** Standing still, it sent `IdleSpecialStart` 282 times in 12 s (1521 actions in an
  earlier 12 s). Not looked into yet; see the unequip-loop item in the queue.

**Confirmed working:**

- Lurker, Netch and **Seeker** replacers match in the game (`found match ... has original behavior Seeker signature
  iState_HMDaedraDefault`). The Seeker was captured by spawning one; its folder is in `GameFiles` and in Emma's mod
  folder. **Seen needs the `Seeker` folder too.** Whether they still slide on the *other* screen needs two real games.
- The 5-second hitch is gone: `local skeleton searched in ~7 ms`, once a minute; `RunLocalUpdates` no worse than 10 ms.
- A remote player's copy: arrives with its 3D loaded; follows the owner's health (100, 60); is rebuilt on respawn at
  100; **is put right when the player comes back from five cells away** (health 55, the stale-copy repair).
- Damage from another player reaches this player exactly: a hit of 30 took 30, flames of 8 a second for 3 s took 24.
- The player's health is re-sent every 3 s whether or not it changed (36 snapshots in a row, all 335).
- An NPC owned by the other player: arrives, takes its owner's health (`NPC ... health corrected from 100 to its owner's
  40`), dies when its owner says so, comes back to life, and **is dead on this side when it died while this player was
  away** (through a load door and back) -- the oldest report on this list.
- Dropped items placed, followed while carried, and removed (see "Dropped items are moved for everyone").
- Copy removal: `DeleteClaim` / `Temporary Remote Held` fire and nothing crashed on our own copies.

**By design, and worth a decision:**

- **A dead player is a standing copy at 25 health on the other screen.** `OnDeathStateChange` never kills a player's
  copy ("Players should never be killed") and health is floored at 25 ("a copy never goes down"). That exit is the one
  silent one left in the death receiver. It is the open item about showing "X is down".
- **A revived NPC stands at its own full health, whatever its owner sent.** `Actor::Respawn` replaces the copy with a
  fresh one; a health value arriving in the same moment lands on the copy being replaced. In play a revive is to full
  health on the owner's side too, so both read "full"; it differs only for an NPC whose health differs between the two
  games (a levelled one). Nothing sends the owner's number again afterwards. Not changed.
- **A dropped item handed back to physics in mid-air stays there** until something touches it.

## Work done without a session, 2026-09-28: tests, tooling, and one thing the log was getting wrong

Five items agreed as safe to do alone, because each is either a test, a tool, or a log line -- nothing here
changes how the game plays. Two of them turned up real defects.

### The quantised rotation codec was losing a bucket on every hop, and never settled (2026-09-28)

`VRPose` had no test at all, which is how a field could ship to three of the four places it needs to be in and
go unnoticed for a day. Writing one immediately failed, and the cause was not in `VRPose`:
`Quaternion_NetQuantize::Pack()` was not idempotent. Packing a pose, unpacking it and packing it again gave
different bits for the same rotation, for ever, alternating between two encodings.

Two separate faults, both now fixed and both covered:

- [x] **[untested] The quantiser rounds to the nearest bucket instead of truncating.** `static_cast<uint32_t>`
      truncates toward zero, so a component decoded out of bucket 75 re-quantised to 74.9999847 in float and
      landed in bucket 74. Every pass through the codec walked the value down by a bucket, and the first pass
      biased every component low by up to a full one instead of half. A relayed pose drifted a little each hop.
- [x] **[untested] Packing settles on one encoding instead of oscillating between two.** "Smallest three" drops
      whichever component is largest, so when two are nearly equal -- `(-0.6034, 0.6034, 0.4022, 0.3318)` is a
      real example -- quantising moves them just enough that the decoded value has a *different* largest
      component, and the negation that follows produces a completely different set of bits. `Pack()` now walks
      the encodings to a fixed point, and where a near-tie gives a short cycle rather than a fixed point it takes
      the lowest, which every member of the cycle works out for itself.

This matters beyond neatness: `VRPose::operator==` compares packed forms and `VRBodySync` only sends a pose when
the quantised bones differ from the ones it last sent. A bone sitting near a tie was reported as changed on
every single frame while never moving, so it was re-sent for ever.

The wire layout is unchanged -- same bits, same meaning -- so a new client and an old one still understand each
other; the new one is simply half a bucket more accurate.

- [x] **[untested] `VRPose::operator==` compares `HasLegs` only when the pose has bones.** The serialiser skips
      that flag entirely for a boneless pose and the receiver reads back `false`, so comparing it unconditionally
      meant such a pose could never equal the pose rebuilt from it. Latent today, because every boneless sender
      path clears `HasLegs` explicitly -- but it is the same "a field in one place and not the others" shape that
      has now bitten this struct twice.

Tests: `Code/tests/vrpose.cpp` (every combination of the six optional flags), `Code/tests/quantize.cpp`
(idempotence and an eight-hop relay).

### The copy-removal log was reporting the give-up path as a success (2026-09-28)

Extending `session-report.py` to cover the waiting list meant reading what those lines actually say, and they
did not say what I had taken them to mean. From the session of 2026-09-27:

- The 16 copies reported as `(the game let go of it)` were **not**. Every one was freed exactly 60000 ms after
  being queued with **2 handles still outstanding** -- `kMaxWaitMs` giving up, printed in the words of the
  success path, because the reason string was chosen by `overBound ? "the waiting list was full" : "the game let
  go of it"` and had no third case.
- The bare `Temporary Remote Deleted` line was written **before** the branch was taken, so a copy that was safely
  freed, one forced out because the list was full, and one merely held back all logged identically.

So the earlier reading of that session -- "460 held back, 16 released, 0 force-freed" -- was not evidence that
the wait was working. The log could not have said either way.

- [x] **[untested] Each outcome logs its own line, after the decision.** `Temporary Remote Held` when a copy is
      queued (with how many of the 24 slots are in use), `Temporary Remote Deleted {form}: {reason}` with one
      reason per outcome, and a `CopyFreedHeld:` warning on both paths that free a copy something still holds.
      `CopyRemovalPolicy::ReleaseReason` returns which of the three it was, so the distinction is testable rather
      than a string built at the call site; `Code/tests/copyremoval.cpp` asserts the three descriptions cannot
      collapse back into one.
- [x] **[untested] `session-report.py` reads all of it**, and reports an older log as *unknown* rather than
      folding it into a total that looks clean.

**Open, and needing Emma's call rather than mine:** if the next session shows `CopyFreedHeld` firing often, then
`kMaxWaiting` (24) or `kMaxWaitMs` (60 s) is too small and the wait is not protecting anything. That is a change
to how the game behaves under load, and the last four attempts at this path each cost a play session, so it is
not one to make unasked.

### Walking into the next outdoor cell never gave you back what was around you (2026-09-28)

There are three ways a client says it changed cell, and only two of them sent the arriving player what is near
them. `HandleInteriorCellEnter` loops the characters in the new cell and sends them; `HandleGridCellShift` loops
the characters in range and sends them; `HandleExteriorCellEnter` sent nothing back. It announced the mover to
everyone else -- that half always worked, through `CharacterService::OnCharacterExteriorCellChange` -- and left
the mover holding whatever it happened to have.

Reachable on foot, not only through a door. `DiscoveryService::VisitExteriorCell` raises the exterior-enter
whenever the grid square the player *stands in* changes, and raises the grid shift only when the *centre* of the
loaded block moves, so walking from one outdoor cell into the next takes the exterior-enter path alone, several
times over, before a grid shift ever happens. A copy lost in that window had nothing to bring it back.

- [x] **[untested in play] `HandleExteriorCellEnter` sends the arriving player the characters in range**, with
      the same range test the movement broadcasts use. Re-sending a character that is already spawned is
      harmless: `OnCharacterSpawn` finds the existing copy, refreshes its ownership epoch, and leaves its
      position alone unless nothing has arrived for it in two seconds.

**Cost to weigh:** every exterior cell boundary now re-sends every in-range character. The cellwalk bot measured
19 spawn messages for one other player across a ten-cell walk. With two players that is nothing; in a crowded
exterior it is a burst per cell crossing, and that is the class of thing that has cost sessions before. If the
next session shows the stream struggling outdoors, this is the first thing to look at.

## Session of 2026-09-26, 10:43-11:30 (both logs plus the server's): five causes, all named

The longest session so far and the most productive one, because for the first time the server's own log settled a
question the two client logs could only argue about.

### Weapon touch, and why the first version felt random (2026-09-26)

Emma wanted to feel her blade against Lydia's axe. The probe shipped first, on its own, precisely so the feature
was not built on an assumption: it pulsed both controllers once a second for thirty seconds and she felt it, so
SkyrimVR is on OpenVR's legacy input and `TriggerHapticPulse` is a real path. The probe has been removed now that
it has answered.

The first working version fired, but only sometimes. Her log said why in one line:

    VRWeaponTouch: 'WEAPON' has nothing measurable below it; treating the weapon as a point at the grip

A sword was a **dot in her fist**. The blade search walked the attach node's child nodes and a weapon is usually
one geometry with no children at all -- the blade lives in the vertices. So contact only fired when her hilt came
within 14 cm of Lydia's, which is exactly "sometimes it works and sometimes it doesn't".

- [ ] (2026-10-06) `kWorldBoundOffset` 0xB0 is `previousWorld`; the world bound is at **0xE4** on VR as on SE (CommonLibVR-NG,
      and measured: the weapon node's bound read there has radius 31.9 for an iron sword, centred 21.9 units along the
      node's +Y). The touch check below reads 0xB0, which is why "nothing ... looks like a bounding sphere" every session.
- [x] **[untested] The blade is measured from the node's world bound** (`kWorldBoundOffset`, 0xB0, straight after
      the world transform). The centre of a sword's bound sits halfway down the blade, so the grip and the centre
      give the direction and, doubled, the tip. The read is validated -- a radius outside 4 to 300 units, or a
      centre more than 400 units from the grip, is not a bound -- and a rejection prints the raw values and the
      node's three rotation axes, so a wrong offset names itself instead of putting a blade through the floor.
- [x] **[untested] Each hand is its own segment now, alongside the weapon rather than instead of it.** The old
      code used the hand only when nothing was equipped, which is why "with my bare hand nothing" and why a
      sword hid the hand entirely.

### Player sliding: found, and it was never the merge (2026-09-26, 14:02)

Emma's live log, on the build with every exit named:

    AnimVarDiag (sending): 23 variables in range, 0 skipped by the bounds check (0.0%);
                           the set says size 100 and the descriptor wants up to index 75
    AnimVarDiag: animation variables were not read 5391 times (225 of them for the local player);
                 last was A2C94 -- the animation graph index is past the end of the graph list

So the bounds check the 2026-09-22 merge added is innocent -- nothing is skipped by it, anywhere. The cause is the
outer guard, one of the two that had no instrumentation on them at all until that morning, and it is absurd on
inspection: the player's branch reads graph **0** whatever the manager's index says, and the guard rejected the
player on that index anyway. Emma's own body was thrown out about twenty-three times a second -- every movement
snapshot -- before reaching the line that would have ignored the index entirely.

- [x] **[untested] Each side checks the index it is going to use.** `cGraphIndex = cIsPlayer ? 0 : animationGraphIndex`,
      and the guard tests that. For the player it is now unreachable whenever a graph exists at all.
- The lesson, for the third time this week: instrument the exits that log nothing before believing a diagnostic
  that reports zero. Two of the exits here were invisible, and the one that mattered was one of them.

### Weapon haptics: the probe before the feature (2026-09-26)

Emma: "when my hand goes against Lydia's axe I do not feel it, even when I have a sword." Checked her list --
PLANCK gives her an NPC's body, the parry mod only fires on an incoming attack and scales its pulse by the stamina
the parry costs, HIGGS is hands against grabbable objects. Weapon-touching-weapon is a real gap, and for another
*player's* weapon nothing else could fill it: only this mod knows where that weapon is.

Everything needed is already here -- `IVRSystem` from the game's own openvr_api.dll (as VRDashboard does), and
both `GetTrackedDeviceIndexForControllerRole` and `TriggerHapticPulse` in the header we compile against.

- [x] **[untested] `VRHaptics::RunProbe` pulses both controllers once a second for 30 s, ten seconds into a
      session, and says so.** SkyrimVR should be on OpenVR's legacy input, where that call works; if it has moved
      to the action system the call is accepted and silently ignored, and every line of a weapon-collision feature
      would have been built on sand. One session answers it for the cost of a file that is then deleted.

### The bot follows the host through load doors now (2026-09-26)

A headless bot cannot walk through a door, so when Emma entered Windhelm the bot stayed in the exterior cell and
the server correctly stopped sending it to her -- which made the one bug most worth testing, the copy that
vanishes at a cell change, untestable without a second real player.

It does not need a door. The server already broadcasts `NotifyPlayerCellChanged` to everyone else.

- [x] **[untested] The bot handles that message, and when it is the host's, announces the same cell and puts
      itself at the host's coordinates.** Interior or exterior, plus an immediate movement send so the copy the
      host sees is not left standing outdoors. Standalone bots ignore it, so the pair tests are unaffected.

### Three corrections from Emma, 2026-09-26 (all three were right)

**The crash count was inflated because I counted the ones they cause on purpose.** My own note says end-of-session
crashes are quit crashes; I then counted all 32 in the log history and read a trend off them. Split by what the
45 seconds before each one contains:

| | crashes while clearly playing | a menu was up first | nothing either way |
|---|---|---|---|
| 19 Sep - 26 Sep | **5** | 22 | 5 |

The five: 09-19 10:39, 09-21 19:55, 09-23 18:46, 09-26 09:06, 09-26 11:30. That is five real crashes in eight
days, not thirty-two, and the honest reading of the trend is that it is flat and very low rather than falling.
Caveat in the other direction: the 22 "a menu was up" include Loading and Fader menus, which are also a door
transition, so some of those may be load crashes rather than quits. Worth separating if one is ever reported.

**"NPCs below floor level" is not measuring what the goal says.** `SinkDiag` fires when an actor is more than 16
units (about 23 cm, so not a millimetre -- Emma asked) lower than where it was placed **on the previous frame**.
That is a fall, not a depth. It cannot see the thing the goal is about: a body that settles half inside the floor
and stays there produces no reports at all, because it is not moving.

What it actually caught in the whole 10:43-11:30 session, both clients:

    Emma:  1 placement, 2536 units, FF001131   |  1 placement, 1961 units, FF001191
    Seen:  654 / 1546 / 586 / 525 / 36 / 55 units, all single placements

Eight events in an hour, none repeating, and six of the eight are 0.5 m to 36 m in a single frame -- the size of a
door transition or a teleport, not a body sinking. Only the 36 and 55 (0.5-0.8 m) are even the right order of
magnitude, and they happened once each.

- Downgraded from a goal to a watch item. Nobody has reported *seeing* an NPC standing in the floor since the
  goal was written, and the number that justified it turns out to be measuring falls. If it is seen again, the
  measurement to build is the residual after ForcePosition -- the copy's z against the z the network asked for,
  held over time -- not a frame-to-frame delta.

**Sliding is a regression from the merge of 2026-09-22, and Emma knew when it started.** "Sliding was introduced
since the merge with main (for players sliding), it was not here before." The merge is a920247d, and of everything
it brought in, the only thing on the animation path is `SaveAnimationVariables` and `LoadAnimationVariables` --
where it wrapped every read and every write in a new bounds check:

    -   if (pVariableSet->data[idx] != 0)
    +   if (pVariableSet->size > idx && pVariableSet->data[idx] != 0)

    -   aVariables.Floats[i] = *reinterpret_cast<float*>(&pVariableSet->data[idx]);
    +   if (pVariableSet->size > idx)
    +       aVariables.Floats[i] = ...

If that bound is never satisfied on VR, the symptom is exactly what both logs show. The sender's arrays keep the
zeros they were pre-assigned, so consecutive snapshots are identical -- SlideDiag at 100% -- and the receiver
writes nothing into its graph, so the legs never move while the body does. No early return is taken, nothing is
null, nothing logs and nothing crashes, which is precisely why the instrumentation written that morning found
nothing: it was watching the exits, and this path does not take one.

`hkbVariableValueSet` is modelled from Skyrim SE (`data` at 0x10, `size` at 0x18) with no VR variant, so either
the offset or the meaning of that field may differ here.

- [x] **[untested] The bound itself is counted now, on both halves.** How many indices were in range, how many
      the check skipped, what the set reports as its size, and the largest index the descriptor asks for -- one
      line every 10 s for sending and one for receiving. **`skipped` far above zero with `size` far below the max
      index confirms it.** The fix then belongs on what that field means on VR, not on removing a check that was
      added to stop an out-of-bounds read.

### The invisible player is not a fade, not a misplacement. The copy is deleted and never asked for again.

Three logs, one minute, and the whole thing falls out:

    server  11:20:17  WorldDiag: player 'Queen Emma' entered interior cell 34FD2; removed for 'Seen the strong'
    server  11:20:21  WorldDiag: player 'Seen the strong' entered interior cell 34FD2; sent again for 'Queen Emma'
    Emma    11:20:21  Character with remote id 20 is already spawned.
    Emma    11:20:22  Temporary Remote Deleted FF00119B / Actor removed, form id: FF00119B
    Emma    11:20:58  MagicService::OnNotifyRemoveSpell: could not find actor server id 20
    Emma    11:21:01  ObjectService::OnActivateNotify: could not find actor server id 20
    Emma    11:21:31  (the copy is back, because a *second* load door fired the whole sequence again)

The server only re-sends a character when **that character** changes cell. It has no handler for the other case: a
player whose own cell unloads and takes every remote copy standing in it down with it. Seen's spawn arrived one
second before that teardown, hit "already spawned" and only nudged the old copy's position -- and then the old copy
was deleted. Seventy seconds of Seen being invisible, and only a second load door fixed it.

That also explains why this always reads as "the person going first cannot see the person following": the one who
goes through the door first is the one whose cell unloads.

- [x] **[untested] The client announces its cell again when it tears down a remote copy for a local reason.**
      `CharacterService::CancelServerAssignment` now calls `DiscoveryService::RequestCellReannounce`, which waits
      1.5 s (a load door deletes copies over several frames) and then re-dispatches `CellChangeEvent`. The server
      re-sends every character in the cell, onto an empty slot this time. The server's leave-cell cleanup bails as
      soon as it finds a player still in the cell, which is us, so nothing else is disturbed.

### Dwarven spheres are not fighting: the animation replay queue has no bound on the path that fills it

`ReplayDiag` prints how many actions are queued. On Seen's client a Dwarven Centurion Master reached **5,123** and
Neloth **17,411**; Emma's worst was 2,689. `AnimationSystem::Update` plays at most one action per frame, so 17,411
queued is six minutes of replay: the automaton was playing what it did before the fight started, all the way
through the fight. That is "not fighting at all".

The cap was written on 2026-09-24 and put in `AddAction` -- the single-action path. Actions also arrive in the
movement snapshot (`OnReferencesMoveRequest`) and in the spawn replay chain, and neither was bounded. The session
report's "Animation backlog trimmed 0" was true and meant nothing.

- [x] **[untested] The cap is `AnimationSystem::TrimBacklog` now and every push path calls it.** The server caps a
      replay chain at 32, well under the 96 here, so a fresh spawn is never trimmed.

### The health bar after a death: the copy is pinned at the floor and nothing ever lifts it

Seen died at 10:48:29. His copy on Emma's side was held at 25 health by the "a copy never goes down" rule while his
own went to zero, the copy was rebuilt at 10:48:43, a stale death delta drove it to -275 and it was floored to 25
again -- and then it **stayed at 25 for ninety seconds**: 25 at 10:48:46, 66 at 10:49:16, 66 at 10:49:46, 230 at
10:50:16. No correction line in any of it.

Because only *changes* go on the wire. Seen's health went -275 -> 315 while Emma had no copy of him to receive it,
and after that it did not change, so nothing was ever sent again. What Emma saw was the copy's own regeneration
crawling the bar back up, which reads exactly like watching somebody heal.

- [x] **[untested] Health, magicka and stamina are re-sent every 3 s for a player whether or not they moved.**
      Three floats per player per three seconds, and every copy becomes self-correcting: a missed change, a floored
      value, a copy that regenerated on its own, all repaired within one period instead of never.

### The friend's health bar "needs to be initialised once", measured

Emma's words, and the count behind them: the session started at 10:43:50 and `WSEnemyMeters` first appeared in the
menu list at **10:47:58**, four minutes later. It was missing from 205 of 575 probes -- a third of the session.
The crash fix of 2026-09-25 refuses to post to a menu that is not open, correctly, and says nothing; the meter is
not part of the HUD until something has shown it once, and until then there is no bar over anyone's head.

- [x] **[untested] When the meter is wanted and the menu is not there, the UI is asked to show it.** `kShow` with
      no data, at most once every 2 s, which allocates nothing from the pooled factory -- that allocation is what
      made the update dangerous. The next tick 250 ms later finds the menu open and posts the real target.

### Dragging a Dwemer automaton did nothing, and could not have

`CaptureBodyPose` starts with `FindBones`, which searches by human bone name ("NPC L Hand [LHnd]" and the rest). A
Dwarven sphere, a spider or a centurion fails it, the function returns false, and **nothing whatever is sent** --
not the bones, not even where the thing is.

- [x] **[untested] A body with no readable skeleton sends its root position alone (`VRPose::NoBones`).** The
      receiver shifts the whole node tree, and the flattened bone array with it, by the difference: rotations are
      left to the local animation, only where it is changes, which is all a drag is. Added in all four places this
      time -- message, sender, interpolation rebuild, receiver.
- Only for a **dead** automaton: the sender's gate is `IsDead()` and within 600 units, because a living NPC has no
      ragdoll to grab. If the spiders being dragged were alive, this changes nothing for them.

### Sliding: the descriptor lookup, not the bound (read from the log 2026-09-29, nothing changed yet)

The earlier standing theory was the 2026-09-22 merge's `pVariableSet->size > idx` bound. Emma's session of
2026-09-27 says otherwise. The exit that fires is a different one:

- [ ] **275,164 animation-variable reads skipped in one session**, 201 `AnimVarDiag` reports, all but four with
      the reason *"no descriptor for this behaviour, and the modded-behaviour patch did not make one"*
      (`TESObjectREFR.cpp`, the `TP_ANIMVARS_GAVE_UP` after `BehaviorVarPatch`). It hits the local player (`14`)
      and Lydia (`A2C94`). An unread set is an empty set, so the sender ships the zeros it pre-filled and the
      receiver's legs never move -- which is the 100.0% SlideDiag reading.
- The same log has, six times each: `BehaviorVar::Patch: multiple behavior replacers have the same signature,
      this must be corrected ... choosing the first one`, and `BehaviorVar::ConstructModdedDescriptor: Original
      game descriptor with hash 17103635255379484992 ...`. So the modded-behaviour patch does run and does build
      descriptors -- and FUS ships behaviour replacers whose signatures collide, and it picks one arbitrarily.
- The colliding replacers are **Cow, Deer and Goat** -- animals, so a side issue for them and not the player's
      sliding.
- [x] **[untested] Found (2026-09-30): `BehaviorVar::Patch` inspected a different graph from the one it hashed.**
      The player's own graph is fail-listed on *both* clients, same hash `bd3c34a36bb1b9bd` on Emma's and Seen's,
      and only from 2026-09-26 onward -- the 09-21 and 09-22 bundles have no such line. 09-26 is the day the
      reader was fixed to use graph 0 for the player (see "Player sliding: found" below): before it, the player's
      reads died at the outer index check and never reached `Patch`. The fix moved the failure one step along.
      `Patch` dumps the actor's variables to look for a replacer signature, and it asked `ResolveGraphIndex()`
      without forcing 0 -- while the hash and the reader both force 0. On VR the manager's index is garbage (the
      SE offset points at something else), and with more than one graph in the list the garbage comes back
      unchanged, so the dump is empty and no signature can match. `DumpAnimationVariables` now takes the same
      force index as `GetDescriptorKey`, and `Patch` forces 0 for the player.
      **Proven from Emma's log of 2026-09-27, the same day, by accident.** The fail list is keyed by graph hash, not
      by actor, so whichever actor reaches `Patch` first after it expires decides the next ten minutes for every
      actor sharing that hash:
      - 19:39:52 the local player tries `bd3c34a36bb1b9bd` first, dumps the wrong graph, fails; fail-listed.
      - 19:49:52, exactly ten minutes later, Seen's copy (`ff001191`, one graph, read correctly) gets there first
        and **matches `humanoid_Master`** -- same hash, same graph.
      - From 19:50:05 the player's failures go from ~240 per 10 s to **0**, and stay there for fourteen minutes
        through three loading screens.
      - 20:04:34, after another load, the player wins the race again and fails again: ~200-250 per 10 s.
      Seen's bundles show the same hash matching from his side's copies (`ff0010cc`, `ff001144`). So the graph was
      never the problem, only which graph was read for the local player -- and the local player, read every frame,
      nearly always wins the race. With the fix it reads the right graph itself.
      `Patch` now logs `formID 14 inspected graph 0 of N (manager index X); Y variables found`, which should be
      followed at once by `found match ... humanoid_Master`. What is still owed a session is the visible half:
      that the other player stops seeing a body slide.
- [ ] **Creatures with no descriptor at all slide, whatever else is fixed.** After the player recovered at 19:50,
      230-540 reads per 10 s still failed. Three graph hashes never match anything: `4d644aec1cadba38`,
      `569b7acef002ee5e`, `d85347c754a5ac18`, all on Dragonborn forms (plugin 02 in Emma's order), seen in
      Apocrypha -- most likely Seekers and Lurkers. They are **vanilla** graphs (only humanoid behaviours are
      regenerated as loose files in FUS; creatures come from the archives), and the mod has no descriptor for
      them: the 31 built in cover no Dragonborn creature but the Scrib, and no elk or fox either.
      **The fix can be data only.** `ConstructModdedDescriptor` does not need an original descriptor -- with no
      `__hash.txt` it builds the whole thing from the replacer's `__bool/__int/__float.txt` lists, and only
      `__sig.txt` is required. What it needs is the creature's variable names, which only the running game has.
      - [x] **[untested] `Patch` now captures them.** The first time a graph matches nothing in a session, its
        variables are written to `logs/behaviours/<Creature>_<hash>/` in the replacer folder format, with an
        `about.txt` naming the actor and listing every index. Under logs, which the loader never reads, so
        nothing changes in play. Types are guessed from names, safely: ints and floats travel as the raw 32-bit
        word, so filing one as the other round-trips exactly, and a name goes in the lossy bool list only when it
        says it is a bool -- which held for all 757 variables of the built-in descriptors.
      - [x] **[untested] Lurker and Netch replacers added (2026-09-30)**, from the first session's captures:
        `4d644aec1cadba38` is the **Lurker** (86 variables; signature `iState_BenthicLurkerDefault`),
        `d85347c754a5ac18` is the **Netch Calf** (45; `iState_NetchDefault`, which an adult netch should share). Both in
        `GameFiles/Skyrim/SkyrimTogetherRebornBehaviors/` (so they ship with releases) and installed in Emma's MO2 mod.
        The variables synced follow the built-in Wolf, SabreCat and Troll descriptors -- locomotion floats, state bools,
        the `iSync*` ints -- and leave out foot-IK gains, camera, blend weights, state constants and the `CPR_*`
        variables of an AI combat mod. Every name was checked against the capture; both signatures appear in no other
        known graph. **Seen needs the two folders too**, or his side has no descriptor and still shows them sliding.
      - [ ] **The Seeker** (`569b7acef002ee5e` most likely) was not met in that session; it will be captured the next
        time one is, the same way.
- The Cow/Deer/Goat tie is **minor**, not the cause of anything visible: six different graphs matched "Cow", and
      each found all 21 of the cow's variables, because borrowed descriptors are looked up by name. At most a
      species' own extra variables go unsynced.
      **Sender-side:** each client sends its own player's variables, so Seen needs this build before Emma sees
      *him* stop sliding.

### The script extender warning, finally read rather than guessed at

`Looked for 'sksevr_.dll', built from exe version ''`. Not a trimming bug this time: `VersionDb` has no loaded
version string at that point, so there was nothing to build a filename out of at all. Both failures share a cause
-- guessing at a filename the loader can simply be asked for.

- [x] **[untested] The loaded modules are enumerated and the one called `sksevr_*.dll` is found by prefix.** No
      version string involved. It logs which file it found, so the next log says so in one line either way.

### Emma's crash at 11:30:43: not named, and not guessed at

`c0000005, execute at 0x0` -- a call through a null function pointer, from `SkyrimVR.exe+0x3b2823`, with four
frames in the same `+0x3b0xxx` range (a recursive traversal) and **no mod frames on the stack at all**. It is on
thread 16928, the renderer frame-end thread where the body sync runs, 0.7 s after that thread posed a remote body.
That is suggestive and it is not evidence; the game exe is Steam-encrypted here so the caller cannot be named
offline. Two remote actors had been deleted in the seven seconds before it (FF001193 at 11:30:36, FF001191 at
11:30:39), which is the shape of a use-after-free but not proof of one.

- Left open deliberately. Nothing shipped for it this round.

### Goal 1: the "drives a game menu directly" class is closed (2026-09-26)

The enemy-meter crash was not a null pointer, it was **building a UI message by hand and posting it to a menu
that was not there**. Swept for others of the same shape: `SetEnemyMeterTarget` is the only place in the client
that constructs a `HUDData` and posts it. Everything else that talks to the HUD goes through
`Utils::ShowHudMessage`, which calls the game's own notification function -- the sanctioned path every mod on the
list uses, and not ours to second-guess. One member, fixed, class closed.

### Goal 2: a copy that goes out of range is never put right when you come back (2026-09-26)

Found by a new bot test rather than by a session, and confirmed in the server's own log.

**The chain.** The server range-filters updates by grid: `uGridsToLoad` is 5, so `IsCellInGridCell` allows two
cells. A bot walked five cells away and the server reported `0 character updates sent, 321 withheld` -- correct,
and the point of range filtering. Meanwhile the real actor carries on being moved by its owner. Walk back, and
the server re-sends the spawn to put the copy right. **The client threw that away**: the copy had never stopped
existing, so the spawn hit "Character with remote id N is already spawned" and nothing moved. Those warnings are
all over Seen's logs.

A copy left where it was last seen, while the real thing moved, is a body in the wrong place and a follower that
looks frozen -- which is two of the four reports from 21:19-21:25 on 2026-09-25.

- [ ] **The server's two range paths are asymmetric, and the other half is still open.**
      - When the **character** moves out of a viewer's grid, `OnCharacterExteriorCellChange` sends that viewer a
        removal. That half works.
      - When the **viewer** moves, `PlayerService::OnShiftGridCellRequest` re-sends what is now in range and
        simply `continue`s past what is now out of it -- **no removal is ever sent**.

      So a viewer keeps copies of everything it walked away from. For a player that self-corrects, because the
      other player moving fires the first path. For anything that stays put -- a corpse above all -- it never
      does. The client-side refresh above makes the stale copy correct itself on return, which is the safe half of
      the fix; sending removals is the other half and is **not** done yet, because removals are entity churn and
      churn is where the crashes have been.
      **Tested 2026-09-30, server side:** the `rangeback` bot pair walks one bot five cells away; the one who stayed
      loses health meanwhile. The walker still held the stale copy at 100 while away -- the asymmetry, confirmed --
      and was given it at 55 on the way back. Whether the game's copy then shows 55 is the client refresh, which
      needs a headset.

### Goal 2 / the flying bandit: two points that are not a journey (2026-09-26)

**Correction to the entry written earlier today.** That one said `delta` could go negative and walk an actor
backwards without bound. It cannot: `aTick - first.Tick` is **unsigned**, so a playback tick behind the oldest
point wraps to an enormous positive value and the existing upper clamp catches it. `Lerp` with delta in [0, 1]
can only ever place an actor *between* the two points it is given. The lower clamp was added anyway and is
harmless, but it fixed nothing, and the explanation that came with it was wrong.

Which leaves the real question: if the actor can only be placed between two points, then two points were
875,458 units apart.

They were. **The buffer records no worldspace and nothing clears it when one changes.** A point from before a
worldspace change and one from after sit side by side, and the actor is walked between them -- Solstheim to
somewhere else, two hundred cells, in a tenth of a second. 195 actors displaced at once, at 19:04:11 on
2026-09-23, is every actor in the world at the moment somebody crossed a boundary.

- [x] **[untested] `AddPoint` drops what is buffered when the next point is more than a cell away.** A cell is
      4096 units and points arrive about a tenth of a second apart, so nothing legitimate moves that far between
      two of them: a worldspace change, a fast travel, or bad data. The actor arrives at the new place instead of
      flying to it. One rule covers all three, and it needs no new field on the wire.
- **What was checked and did not hold up.** The save load at 19:04:20 on 2026-09-23 was suggested as the trigger;
      it came **nine seconds after** the report, which covers the preceding ten. So the load did not cause it.
      The window itself shows a flood of "Death sync: health broadcast killed actor 39FB7 (now dead: false)" and
      an ownership transfer carrying `worldspace: 0, cell: 0, position: (0, 0, 0)` -- a null position, which the
      new guard also catches, since Solstheim to the origin is about 47,000 units. Whether the 875,458 was a
      worldspace change, a null position or something else is **not established**, and the guard does not depend
      on knowing.
- **Not bot-testable.** The bug is in the client's interpolation, which the bot does not implement -- it reads
      positions, it does not smooth between them. The suite confirms the guard does not disturb ordinary movement
      (the range test walks 20,000 units in ~300-unit steps), and that is all it can say.

### The next session's logs read themselves now (2026-09-26)

Eight measurements were shipped this week and none of them was worth much if reading them meant grepping for an
hour. `Tools\VR\session-report.py` now knows all of them and, more to the point, says what each **number**
means rather than whether the line appeared.

Running it on the 21:26 bundle immediately paid for itself twice:

- [ ] **An actor was found 875,458 units below where it had last been placed** (2026-09-23, character 30195BD;
      2,348 units on Lydia in the same session). Goal 5 has had evidence sitting in the logs for three days.

### A new field in VRPose must be added in three places, not two (2026-09-26)

**The hip sync shipped on 2026-09-25 has never done anything, and I said it was working.**

`InterpolationSystem::Update` does not hand the received pose to the body sync. It **rebuilds** one from the two
buffered points, copying the fields it knows about: bones, fingers, root position, scale. `HasHips`/`HipOffset`
and `HasHandCheck`/`LeftHandOffset`/`RightHandOffset` were added to the message, to the sender and to the
receiver -- and not to that rebuild. So they were dropped between the wire and the body, silently, and the
receiver simply never saw them. Nothing logged, because the code that logs runs on the rebuilt pose.

That is why the session report reads `Hips (receiver) 0` and `Hands compared 0` while the sender is filling both.

- [x] **[untested] Both are carried through the rebuild now.** Hips are blended like the movement they are (the
      whole body is offset by it, so a step in it is a step in the body); the hand measurement takes the newest
      point that has one, because averaging two readings of where somebody's hand was would agree with neither.
- **The rule this leaves:** a new field in `VRPose` needs the message, the sender, **the interpolation rebuild**
      and the receiver. Three of the four are obvious and the third is not, and missing it fails silently in the
      most expensive way -- a feature that is shipped, believed, and does nothing.

### Goal 2: a corpse that still walks, and why (2026-09-26)

A death is broadcast to **every** player rather than only those in range, deliberately, so that nobody holding
the actor can miss it. The receiving client then applies it only when the copy's ownership epoch matches the
message's -- and `NotifyOwnershipTransfer` **is** range-filtered (`SendToPlayersInRange`). So:

1. A player walks out of range.
2. The actor changes hands. That player never learns the new epoch.
3. The actor dies. The death reaches them, and their own handler throws it away, because their copy's epoch is
   one behind.
4. They walk back. The epoch is refreshed by the re-sent spawn -- but the death is long gone.

They are left with a corpse still walking around, for the rest of the session.

- [x] **[untested] The stale-copy repair now applies death state.** The spawn is the last thing that still knows,
      and it carries `IsDead`. Position, health, weapon state, factions and death are all repaired now; inventory
      is still only compared.
- [ ] **The receiving-side epoch equality check is the underlying issue and is left alone for now.** Six handlers
      require the copy's epoch to equal the message's -- health, death, equipment, inventory. The server has
      already checked the sender's ownership before broadcasting, so re-checking on arrival mostly provides a way
      to drop valid state. A monotonic test (accept anything not *older*, and take the newer epoch) would be
      better than equality. Not changed without evidence: the equality check presumably exists to reject messages
      that overtake a transfer, and the spawn refresh does heal the gap on return.

### Goal 2: factions are the third thing repaired on a stale copy (2026-09-26)

- [x] **[untested] A copy that comes back into range now gets its factions put right.** Faction changes are
      range-filtered like health and equipment, so a character that turned hostile while this client was away is
      still friendly on the copy -- and a copy in the wrong faction is a creature that squares up to somebody and
      never swings. That is the shape of the Burned Spriggan of 2026-09-25, which spent a fight trying to attack
      Lydia and never landing one. The stale-copy gate now repairs position, health, weapon state and factions;
      inventory is still only compared.

### Swept and clean, so nobody looks again

- Every `Notify*` the server sends has a client sink, except `NotifySetTimeResult` (an admin command's reply,
  nobody listens, harmless).
- No `Request*` the client sends is unhandled by the server. `RequestObjectInventoryChanges` is vestigial: an
  `#include` in the client and a forward declaration on the server, never constructed.

### The same rule, turned on the real client -- and it found one (2026-09-26)

The epoch trap caught the bot four times, which made it worth asking whether the **client** has the same hole
anywhere. It does, or did: one of the seven request messages that carry an epoch.

- [x] **[untested] The periodic equipment snapshot has never reached the server.**
      `InventoryService` sends `RequestEquipmentChanges` from two places. The event-driven one, when the player
      equips something, sets the epoch. The **snapshot** -- the safety net that sends the whole worn inventory
      when it notices it has drifted -- did not, and `OnEquipmentChanges` refuses any equipment change whose epoch
      does not match the owner's. It logged "Equipment snapshot sent: N worn entries" every time and the server
      threw every one away.

      This is the mechanism that is supposed to repair equipment when a change event has been missed, and missing
      change events is exactly what happens while a player is out of range, because equipment travels in range
      only. So the repair for the "not swapping weapons" family existed, ran, logged that it had run, and did
      nothing at all.

      The other six are correct; each was checked individually rather than counted.

### The epoch rule, swept to the end (2026-09-26)

Every `Request*` message the bot sends was checked against the rule rather than waiting for the next one to bite.
Seven request messages carry an `OwnershipEpoch`; the bot sends four of them.

- `RequestHealthChangeBroadcast` carries no epoch by design: the whole point is hurting somebody else's actor.
- `RequestInventoryChanges`, `RequestActorMaxValueChanges` and `RequestOwnershipClaim` carry one, and the bot does
  not send them. Anything that starts sending one needs the epoch first.

### Goal 4: dead bodies. The cause is known and needs no further logs.

- [ ] **Dragging a body you do not own is invisible to everyone, by construction.**
      The send path is gated on ownership -- `AnimationSystem` reads a dead actor's bones only for "a body this
      machine owns". The thing that used to claim a body you had grabbed, `RunBodyGrabUpdates`, was **removed on
      2026-09-24** because its test ("a dead body more than 32 units from its network position") is true of
      corpses that have merely settled: it fired 207 times across 58 bodies in one session and crashed the
      receiver. Nothing replaced it. So a corpse owned by the other player can be dragged all night and nobody
      else will see a thing.
      This is the 20:45 Lurker of 2026-09-25, and it explains it without Emma's log: Seen joined first and owned
      most of the world, including, in all likelihood, that Lurker.

      **The fix is a claim, and the claim needs a test that cannot false-positive.** A settling ragdoll comes to
      rest in a second or two; a body being dragged keeps moving for as long as someone drags it. So the
      candidate is *sustained* motion within arm's reach, not distance from a network position.
      **Measurement shipped, claims nothing:** `VRBodySync::ObserveRemoteBodyMotion` watches dead bodies this
      client does not own, within 400 units, and logs
      `BodyGrabDiag: remote body N has been moving here for M ms, D units travelled, P from the player`
      after a full second of continuous movement. One session says how often that is true when nobody is
      grabbing anything -- which is the number the old version never had -- and the threshold follows from it.
      Only then does a claim go in, and it goes in rate limited.

### Goal 5: NPCs below floor level

- [ ] **Never measured until now.** `ForcePosition` puts a remote actor exactly where the network says on every
      frame, so an actor found *below* that target on the way in has sunk locally between frames -- which is the
      bug exactly: the owner sees an NPC on the floor, the other player sees it in the cellar. An owner genuinely
      falling drags the target down too, so a real fall shows no gap; only a local divergence does.
      **Measurement shipped:** `SinkDiag: N remote actors were below where the network put them; worst X (name)
      by D units, has controller yes/no`, every 10 s.
      **Second measurement, 2026-09-26.** `Actor::ForcePosition` gives up on moving the character controller when
      physics step timing is not ready yet, on the stated assumption that "interpolation will catch the controller
      up once physics timing becomes available". That is an assumption, and an actor below the floor is what it
      looks like when it is false: the reference sits where the network says while the controller, which is what
      keeps a body on the ground, is somewhere else. `ControllerDiag: N of M remote placements could not move the
      character controller with the reference`, every 10 s. Occasional is a controller waiting a frame for its
      first step, which is what the code expects. Consistently high is the assumption failing, and then SinkDiag's
      `has controller` field says for which actors.

      **The other suspect is already in the code.** `HookSetPosition` moves every non-player actor with
      `aUpdateCharController = false`, under a comment reading "It just works TM". The character controller is
      what keeps a body standing on the ground; moving the reference without it is a good way to end up under the
      floor. The `has controller` field in the line above is there to test that idea against a real session
      before anything is changed, because that flag is upstream code and flipping it blind is how a night gets
      lost.

Plan for getting from "co-op works" to "smooth, and easy for other people to set up".
Items are ordered by priority inside each section. Details on existing bugs are in
`KNOWN_ISSUES.md`.

Status tags: **[untested]** built but never checked in game · **[measure]** needs numbers
before any change · **[big]** several sessions of work.

---

## 0. Sessions of 2026-09-18 (three sessions, builds v1.8.0-35 to -58): results, and what the next session must answer

Deployed and untested since: `v1.8.0-58-g00cd9643-dirty.67dc30f` (same-named levelled variants synced, PvP sword
hits, shouts and powers, cast/projectile/drift logs). Every build goes into the tools folder; the friend gets the exe
and pdb from there. Both players run `collect-logs.bat` after each session and note the time of each problem.

**Confirmed working on 2026-09-18:** skills and level up screens while connected; the sword in the other player's
hand; spell casting and the held spell in the hands; arrows seen leaving the other player's bow; Lydia follows,
fights, and attacks the other player; sync of same-kind levelled bandits.

- [ ] **Different animals on each screen.** The owner's base form now travels on both spawn paths
      (`CharacterSpawnRequest.BaseId`, `AssignCharacterResponse.BaseId`). Same name or same race: synced normally.
      A different creature (fox at a rabbit's reference, spider at a bear's): adopted and positioned by its owner,
      but the owner's animation data is withheld (`ForeignGraph`), so it slides instead of freezing. Refusing it
      instead was tried on 2026-09-18 and gave each player private bandits; never again. What remains is
      ownership: the bear the friend was fighting vanished when the host walked out of range and dropped it
      (the ownership rework, merged 2026-09-22). Levels differ between variants; cosmetic.
- [ ] **NPC under the ground for one player only** (Durak, a rabbit; last seen 2026-09-19). Not seen since, and
      nothing was changed for it on purpose. Two changes since then could have taken it away by accident: the AI
      step fix (remote actors' animation graphs advance again, so their ground snap runs) and the health clamp on
      player copies. `SinkDiag` stays in; if it names nothing for another two sessions, close this. The havok
      capsule read (TiltedEvolutionVR `aaf5d83`: controller at `MiddleProcess+0x250`, `+0x360` bhkRigidBody,
      `+0x10` hkpRigidBody, position `+0x1A0`, filter `+0x4C`) is only worth doing if it comes back.
- [~] **Body there but not drawn (EMERGENCY; 2026-09-20 21:12/21:20/21:27, 2026-09-21 20:43/20:45 and many
      more, every reconnect that evening was a cure for it).** Three wrong guesses first: the health floor, the
      body scale, and the invisibility actor value. The copy probe killed all three. At every moment either
      player could not be seen, the copy read perfect: metres away, full health, not dead, not bleeding out, not
      disabled, 3D present with every child and a parent, both scales exactly 1.000, invisibility flat 0.00. The
      refusals added for invisibility never fired once, because there was nothing to refuse.
      What is left, and what the fix of 2026-09-21 21:xx acts on: only player copies vanish, and player copies are
      the only bodies we write bone transforms into, thirty times a second; it grew far worse the day the fingers
      added thirty more writes per frame. Each posed bone's local rotation was built by multiplying the previous
      frame's value by a correction, never re-derived, so error accumulated with nothing to shed it, and a bone
      matrix that stops being a rotation takes the skinned body out of the render while leaving the actor
      untouched. It is now derived fresh each frame from the parent's world rotation, which cannot drift and
      repairs a bone that already has. On top, a body whose bones the renderer cannot use (row length off 1, wild
      scale, anything not finite) is no longer posed at all until the game's animation puts it right, which brings
      it back without a reconnect and logs which bone and what value. `CopyDiag` also now says whether the body
      hangs from the same scene as the player, which is the one remaining alternative cause.

- [x] **[untested in game] Dropped items are remembered by the server now (2026-09-30), and everything below is the
      reason.** `DroppedItemService` on both sides:
      - **Server:** every drop is recorded with where it lies, sent to everyone in range, sent again to anyone entering
        its cell, and forgotten when picked up. The list is saved to `dropped_items.bin` next to the server, so it
        outlives a restart. The `bEnableItemDrops` setting is **gone** -- it cannot be left off again -- and the old
        path no longer forwards the drop flag, which would have made the dropper's copy drop a second item.
      - **Client:** reports the player's drop from the reference the game made (so the position is where the item
        lies), then for every item the server sends **adopts** one already on the floor (its own drop, or one its save
        kept) or **places** it (given to the player and dropped straight back out at the spot, under the inventory
        override, so nothing is sent twice). A pick-up of a remembered item is passed on, and everyone takes theirs off
        the floor through the game's own Delete.
      - **Earlier drops:** on connecting and on each cell change the client reads the game's own list of what this
        player dropped (`ExtraDroppedItemList`, found by RTTI -- never by a guessed type number -- and walked with every
        node checked readable) and announces those items. The server matches an announcement against what it already
        knows before adding it. This is what brings back Emma's items from before 2026-09-30, and it is the least proven
        part: it depends on that list surviving in the save and on its layout. It logs what it found either way.
      - **Tested:** the `drops` bot pair (dropped indoors while the other bot is outside -- not sent; the other bot walks
        in -- given to it within 46 ms; it picks up -- removed on the dropper's side), a server restart (item reloaded
        and handed to a newcomer), and a unit test for all four messages including the announcement flag.
      - **Owed a session:** that placing works in the headset (the item at the right spot, no stray "added" message),
        that adopting finds your own item instead of placing a copy, and whether your old items come back.
- ~~**Dropped items not visible**~~ to the other player -- **and the 2026-09-26 fix never ran.** It changed the
      default of `bEnableItemDrops` to true in code, but the server reads `STServer.ini`, and both inis still say
      `bEnableItemDrops=false` (the build folder's was rewritten on 2026-09-30 18:05, just before that session). A
      value in the ini beats a new default. Across every log we have: 53 drops sent (Emma 47, Seen 6), **0 ever
      received**. Two more gaps behind it, from the code:
      - **Live only.** A drop is replayed on the other side at the moment it happens. The server keeps no record, and
        each player loads their own save, so anything dropped before the other connected, out of range, or in another
        session exists in one world only -- Emma's report of 2026-09-30, items dropped sessions ago that Seen cannot see.
      - **Pickup is not synced.** Picking up is an activation, and `ObjectService::OnActivate` only syncs objects that
        exist in a plugin; a dropped item is a temporary reference, so `GetServerModId` fails and nothing is sent. With
        drops on, both players could pick up the same item -- likely why upstream shipped it off and "(Experimental)".
      Sender logs `drop: true`, receiver logs `Remote actor ... drops item`. Reported with it (2026-09-20): when he drops
      something, his copy's body stays in place but "all his bones try to violently leave it". That is the VR
      pose fighting an animation: the drop plays a throw or ragdoll-style animation on the copy while the
      pose keeps writing the sender's bone rotations over it, so every bone jerks between the two each frame.
      Worth checking against `VRBodySync` whether the copy is in a hit, ragdoll or "drop" animation state during
      those frames and skipping the pose then, as it already does for dead and bleeding-out bodies.
- [ ] **[untested] Whiterun gate: is the second vanish cause the worldspace change?** Logging in place on both
      sides (`WorldDiag` on the server for every remove and re-send of a player's copy on a cell or worldspace
      change, and for every grid shift; `WorldDiag: server removed player copy` on the client with our worldspace
      and cell). Test: run `STBot.exe scripts\stand.txt`, then walk through the Whiterun gate and back out. The
      copy must go when you are inside and come back when you are out. If it does not come back, the server
      log names the decision that withheld it.
- [ ] **Death ends at the main menu, connected only.** **Level-up is confirmed fixed** (Emma, 2026-09-30: gone for a
      while, works perfectly). Death went through the same routine below, so it is probably gone too -- confirm on
      the next death while connected, then delete this entry.
      Original report, level-up and death (07:58, 11:46, 11:55, 14:42 x2, 15:08). Solo
      the level-up is fine (15:35 test): the attribute box closes from the level-up code itself
      (`SkyrimVR.exe+0x8d93d2`) and play goes on. Connected, the box is closed by `SkyrimVR.exe+0xf207f7`
      ("type 3, data yes") and a save load starts 6 ms later (Loading Menu, Mist Menu), which ends at the Main
      Menu. The death case is the same: "respawning player" -> the box from `SkyrimVR.exe+0x168507` (the same
      helper that shows one after every save load) -> closed by `0xf207f7` -> load -> Main Menu. So one game
      routine at `0xf207f7` answers a message box with a save load whenever we are connected. Neither address is
      in the address library and the exe on disk is Steam-encrypted, so it cannot be read offline.
      `SaveLoad: load requested` (deployed 15:17, not yet hit) names the file and the caller at the next
      occurrence; that is the missing piece. Candidates: the box runs unpaused when connected (UnfreezeMenu)
      and gets `kUpdateUsesCursor`; both are ours. Since 16:17 every MessageBoxMenu message also logs the
      12 frames above the caller (`stack [...]`) and the first four words of the message data (`data words`):
      +0xf207f7 lies in the UIMessageQueue region (between ids 80061 and 80077) and +0x168507 in the TES helper
      `sub_1401575D0` (id 13213), both generic, so the frames above them name the feature. A one-off read of
      the two code regions from the running game (no debugger; scratch script `dump_live.py`) can be
      disassembled with capstone once the game sits at the main menu; the exe on disk is Steam-encrypted.
      Friend's reading (2026-09-20): the caller is a 0xC1-byte function, SE 0x140EC3C80 = VR 0xF20750 (RET at
      VR 0xF20810), whose last call is AddMessage at VR 0xF207F2. Too small to be the feature: a send-message
      wrapper; its callers (xrefs) are the next thing to read. Timing says the "load" is a quit to the main
      menu, not a save load: hide -> Mist Menu -> Main Menu took 1.2 s at 14:42 (twice) and 15:08, 31 s once
      at 11:46. Same shape as the game's own "return to main menu" (Loading, Mist, Fader, LoadWaitSpinner, HUD,
      the TES box, Main Menu).
      Seenfront's second reading (17:21): a queued request of type 0xD0000010, built by the constructor at
      SkyrimVR+0x594D80, can reach the hide through 5910D0 -> 5924A0 -> 584910 -> 591E80 -> 168160 -> F1BF10 ->
      F1D0E0 -> F20750 -> AddMessage (return RVAs to look for in the hide's stack, inner to outer: F207F7,
      F1D1D8, F1BF61, 168242, 591F69, 584A58, 592763, 591390). Two producers build it: the save-warning dialog
      callback at +0x595B60 (response 1 queues it, calling the constructor from +0x595BCF) and another result
      handler (from +0x591964). Also +0x168507 belongs to FUN_140168160, not to the function at +0x168020. The
      client now logs the constructor (`SaveLoad: request built`, with caller, stack, raw arguments and the
      object's first words) and the callback's response byte (`SaveLoad: warning callback`). A mechanism is
      verified, its involvement is not; the runtime stacks decide.
- [ ] **Distant dragon vanishes (15:03).** Read again with positions: the dragon hovered at (62387, 48916,
      z 7287) while the player stood at (55309, 56470), about two cells away diagonally, on the edge of the
      5x5 loaded grid. Both engines unloaded it at that edge (`Actor removed 2034EDC` / `3034EDC`), which is
      the game, not the sync: no ownership rule can draw an actor the engine has unloaded. What the sync adds
      is churn: each unload sends a transfer, the other side is told to take it while it is unloaded there too
      (`Actor for ownership transfer not found`), the server destroys the entity and the next load registers
      it under a new id (200087 -> 300087), and the party leader claims it back each time. A rule to keep the
      last owner would only cut the churn, not the vanish, so nothing was changed. Worth a solo check: does a
      dragon circling that far away pop out the same way without the mod.
- [ ] **Paused player goes silent (11:51).** Not confirmed. The world probe keeps running while the pause
      counter is 1 (02:05 and 15:35 logs), the local update sender has no pause gate, and connected menus run
      unpaused anyway, so a player in a menu should keep sending. Every hand-off seen on 2026-09-20 afternoon
      happened with the pause counter at 0 (range hand-offs), except one on Seen's side in the Journal Menu
      (14:58:45 -> 14:58:48). A loading screen is the one time nothing is sent. The probe line now ends with
      `last move sent N ms ago`; a value over 3000 on a paused player would confirm the silence. Also needed:
      the server log from the host (`Handoff: ... silent` vs `out of its range`), which lives in the host's
      Server folder.
- [ ] **Quest dialogue heard twice (15:05).** Upstream syncs the other player's dialogue lines and subtitles; with
      both in the same conversation each hears both. Decide: only the speaker's own conversation.
- [ ] **Grey hills (15:01).** Missing distant terrain textures; a screenshot exists. Likely the game's LOD stream
      under memory pressure, not sync. Check once with the game alone.
- [ ] **Nocked arrow not shown** on the other player's bow (the arrow leaves fine). The nocked arrow is an
      animation attachment and VR players never play the draw animation.
- [ ] **[untested] Crash when quitting.** Read from the DLL's disassembly (2026-09-19): Enchantment Art Extender's
      equip handler reads the `UI` singleton (`SkyrimVR+0x1F83200`, our id 400327) and tests `numPausesGame`
      (`+0x160`); at quit the singleton is already destroyed while our disconnect handler was still deleting remote
      players and flipping actors back to local, which fires equip events. `IsProcessExiting()` now gates the
      disconnect handler, temporary-actor deletion and ghost re-enables. The ENB light plugin's quit crash is the
      same class (shader art freed on our teardown). The "black screen then crash" after the friend's respawn was
      the same quit crash after giving up. The black screen itself: VR's `FadeOutGame` takes its first argument
      with inverted polarity and as a dword (TiltedEvolutionVR, checked against the VR prologue); ours passed bools
      through, so the death fade did nothing and the respawn fade-in faded to black ("bright light, then black
      screen", 10:39 on the 19th). Fixed 2026-09-19, untested.

## 0. Session of 2026-09-25: the bot was testing itself, and one real bug fell out of fixing that

No play session. Everything here was found and proved with two bots and no headset.

### The bot's own tests were not testing the server

Every change the bot asked the server for -- health, and therefore death -- was **silently discarded**. The server
checks `IsCurrentOwner(player, epoch)` on each request and rejects `epoch == 0`, and the bot never set one. The
value it then asserted on was its own bookkeeping, recorded locally when it sent the message. Nothing crossed.

That means `health-sign.txt`, `death-recovery.txt` and `auto-suite.txt` proved only that the bot could add and
subtract, from the day they were written until today. They pass now for a better reason, but `expect health me`
is still a local read: **the only script that tests the wire is `relay-watcher.txt`**, which asserts solely about
the other client.

### Session of 2026-09-25, 21:15-21:26 (Seen's logs): one cause behind most of it

Reported: a bandit launched into the sky (21:19), a dead body in the wrong place (21:20), Lydia not visible at
all and frozen (21:21), a Burned Spriggan trying to attack and never landing one (21:25), and the health bar
working perfectly. Almost all of it is downstream of one thing.

- [ ] **Why does a fresh connection time out at all?** This is now the top question. A client that has just
      joined takes the whole world at once -- 30 to 40 `Spawn Actor` lines land in a single millisecond -- and the
      one that joined first owns the most, so it has the most to hand back when it stalls. Nothing here measures
      how long that burst takes.
      **Plan:** time the spawn burst and the authentication-to-first-update window, and log the gap between
      updates on the client when it exceeds a second. Until that exists, every symptom above can recur and the
      logs will only show the aftermath.

### Session of 2026-09-25, 20:44-20:51 (Seen's logs)

- [ ] **The dragged Lurker was not seen (20:45).** Seen's log has **no `posing body` line anywhere on
      2026-09-25** -- the last are from the day before -- so no dead-body pose reached him at all this session.
      What his log cannot say is whether Emma sent any. It also shows both Lurkers 2.3 to 3.5 cells away and
      `InterpDiag starved` (stale 3 s, then 11 s, then 16 s), so the server may simply have been withholding
      their updates for range, in which case there was nothing to see and nothing is broken.
      **Next:** Emma's log for the same minute. The sender's line is the other half of `posing body`. If she sent
      and he did not receive, it is the range filter; if she never sent, it is the capture side.

- [ ] **Lydia, downed, lying sideways on the floor and running (20:50, Seen's side).** Lydia (`A2C94`) is Emma's
      actor, so Seen has a copy. Nothing in his log names her at 20:50, but the animation diagnostics for that
      minute are loud: `moveStart 16/27 failed`, `turnStop 13/23 failed`, `CyclicCrossBlend 8/15 failed`, and
      `(no event) 99/99 failed`. A copy playing a run while its body is prone is the shape of movement arriving
      for an actor whose own state says it is down.
      **Next:** log bleedout and get-up transitions for remote actors, with the state at the moment movement is
      applied. Nothing built yet -- the fix of 2026-09-24 made bleedout behave like dying in `HookActorProcess`
      and `InterpolationSystem`, and this may be that change's other side.

### VRIK and HIGGS interactions the other player cannot see (2026-09-25)

The general complaint, and it is one problem wearing several hats: **this mod replicates the body, and the things
the body does to the world travel only where something was built for them specifically.** Bones, fingers, scale
and now hips cross. A shot arrow crosses because a shot is its own projectile message. A dragged corpse crosses
because that was built by hand in September. Everything else a hand does is invisible.

- [ ] **Nocked arrow not shown on the other player's bow.** Cause found 2026-09-25, not a mystery any more.
      The arrow *leaving* works because `CombatService` sends a `ProjectileLaunchRequest` and the other side spawns
      a projectile directly -- the bow animation is not involved at any point. The arrow *on the string* is an
      animation attachment, and the remote copy's attack state never leaves 0 because nothing syncs it. The game's
      own value for this is `kBowAttached`, in bits 28-31 of `ActorState::flags1`, now readable as
      `ActorState::AttackState()`.

      The open question is whether VR archery moves that state at all, since a VR player never plays the draw
      animation. `RunLocalUpdates` logs `VRArchery: local attack state N` on every change.
      **Measured 2026-09-27, and not yet an answer.** States 9 (bow draw) and 10 (arrow attached) do appear, 55-57
      times -- but always as `8 -> 9 -> 10 -> 0` inside the same second, never 11-13 (drawn, releasing, released),
      and not one arrow was fired that session (no shooter-14 launch with ammo). So what was logged is a bow being
      raised and lowered, not a draw. The session report read it as "the state moves, so it can be synced", which
      was more than it showed; it now says so. **Needs a session with arrows actually shot**, and then the two
      branches below apply.
      - If the state does move through a draw and release, sync it and let the receiver's graph attach the arrow.
      - If it never leaves 0, there is nothing to replicate and the arrow has to be attached on the receiving
        side by hand, which is a much larger job -- and the state would then be wrong for bashing and blocking
        too, which is worth knowing either way.

- [x] **[untested in game] Dropped items are moved for everyone (2026-09-30).** A dropped item someone picks up in
      a hand, carries, throws or kicks is followed on the other side while it moves, and the place it comes to rest is
      kept by the server for whoever arrives later.
      - **Client:** ten times a second each dropped item the server knows is compared with where it last was (the 3D
        node's world position, which follows physics; 2 units or ~3 degrees). A move is sent while it lasts, and a final
        "at rest" once it has been still for 0.6 s. The first 2 s after a drop or placement are ignored, so each side
        lets its own item fall. The receiving side makes the item **keyframed** while it is carried (so its own physics
        does not pull it down between updates), moves it to each position, and gives it back to physics 2 s after the
        last word -- not at once on "at rest", because a hand kept still sends that too. An item moved while this
        player was away is put where it was left when the server sends it again on entering the cell. A cell loading
        is treated like a fresh drop (its items settle on both sides at once, which is not a move), and an id whose
        reference now has a different base form is left alone (the game reuses a deleted item's `FF` id).
      - **Server:** `RequestDroppedItemMove` updates the item's place and relays it to players in range; the list is
        written to disk only at rest. Drops now carry a rotation too (`dropped_items.bin` version 2; version 1 still
        loads).
      - **Tested:** the `dropmove` bot pair (carried in three steps while the other bot watches -- all three relayed;
        moved again while the other bot is outside -- not relayed; the other bot comes back -- given the item where it
        was left, 424 units from where it fell), red with the server not keeping the place, green with it. Unit test
        for the move message.
      - **Run against the real game on 2026-10-01, with nobody in the headset** (null driver + DevBench, a bot as the
        other player; script `live-dropmove.txt`, positions read from the game every half second):
        the dagger was placed; it rolled 800 units down the hillside at Mistwatch and the game **sent that roll** to the
        bot, about ten moves a second, then "at rest"; carried by the bot in twelve steps it sat at exactly the commanded
        position at every sample; a single far move landed exactly; the pick-up removed it. No `rebuilt its 3D` line:
        `MoveTo` does not rebuild an item. One finding: handed back to physics in mid-air it **stays there** until
        something touches it (the game does not wake a body whose motion type was changed). For an item put on a
        surface or held still in a hand that is the right picture; only a carrier who goes quiet mid-carry leaves it
        floating. Not changed.
      - **Still owed a headset:** whether it looks smooth, whether its turn follows while held, and whether a real
        grab is seen as a move on the holder's side (a roll is; a grab should be).
      - **What to watch for in a session:** the log says `DroppedItem: N (...) is being moved here` on the side
        that picks it up, `is being moved over there; holding it here and following` on the other. Three things I
        could not check without the headset: whether it follows smoothly or in visible steps (ten updates a second,
        each a `MoveTo`; if it steps, the engine's own `TranslateTo` is the next thing to try -- and if a move rebuilds
        the item's 3D the log says `moving N rebuilt its 3D` once, and it is re-held after each move); whether its turn
        follows while held (the turn is read from the reference, which may only catch up when it settles); and
        whether the grab itself counts as a move on the side holding it (it should: the node moves).
- [ ] **Objects moved by hand are not seen moving** (plugin-placed objects; dropped items are done, above).
      `ObjectService` syncs activation, locks and script
      animations, and **no positions at all**. Pick a cup up with HIGGS, throw it, and on the other screen it never
      left the table. The identity and ownership machinery is already there -- objects have server ids and an
      `ObjectComponent` -- so this is a missing message rather than a missing subsystem.

      **Measurement written, not yet in a build:** `ObjectService::OnUpdate` samples every tracked object every
      250 ms and logs `ObjectMove: <formid> moved N units in 250 ms, D from the player` for anything past 8 units,
      at most four per sample.
      It answers the three things a design needs: whether objects a player handles are even in the set the server
      assigned us, how often they move, and by how much. Deliberately measured first -- the corpse-drag detector
      of 2026-09-22 was written on an assumption instead and fired 207 times across 58 bodies that had merely
      settled, which ended in a crash.

- [~] **Dragged corpses** -- built 2026-09-22, offset fixed 2026-09-24, still unplayed. See the HIGGS grab section
      further down. This is the one member of the family that already has an implementation.

### Same shape, not touched, needs evidence first

- [x] **Answered 2026-09-30: an NPC brought back to life never reaches this path.** The client sends
      `RequestRespawn` as an owner only for the player's beast form (`OnBeastFormChange`); an NPC's revive goes out as
      a death state "alive" (quarter-second tick) and then its restored health (one-second value tick), and the server
      stores both. The `revive` bot pair sends exactly that: the other bot sees the NPC arrive at 100, die at 0 and come
      back alive at 100, and a copy rebuilt from storage after a reconnect is alive at 100. Control: reviving at 40
      makes the rebuilt copy read 40, so it is the stored value being read. The entry below stays as written for the
      record; nothing in it needs changing unless a caller other than beast form appears.
- [ ] **`CharacterService::OnRequestRespawn` does not restore stored health either.** When somebody who is *not*
      the owner asks, that handler serialises the character out of stored state and sends it as a fresh spawn --
      the same copy-from-storage path the player respawn bug travelled on. For players it is now safe, because
      `PlayerService` restores the health before the notify goes out. For an **NPC** resurrected by its owner
      nothing restores it, so the copy other clients build should arrive at the health it died on.
      Not changed: no session has shown it. What would show it -- a resurrected NPC (a follower getting back up,
      a scripted resurrect) appearing on the other screen as a corpse or at a wrong health. If that is seen, the
      fix is the same fifteen lines. The reason for holding off is that a respawn there is not always a death:
      forcing full health would be a guess about every other caller.
- [ ] **`NotifyRespawn` in `PlayerService` is sent inside the inventory lookup**, which reads as a bug and is not
      one: `CharacterService` emplaces an `InventoryComponent` on every character unconditionally, so the lookup
      never fails. Checked 2026-09-25, left alone deliberately. Noted so it does not get "fixed" twice.

### Harness

- **`STServer.dll` and `SkyrimTogetherServer.exe` are version-locked.** Rebuild the DLL alone and the server exits
      immediately, code 1, no log line, no message. Always build `SkyrimServerRunner` with it.
- **Any tracked source edit changes the version hash**, so `STBot` must be rebuilt too or it is refused at the
      door. The refusal is now loud, but the rebuild is still manual.

### Still blocked on a play session

Unchanged from 2026-09-24: the invisible-body render-word comparison, the hand-offset reach comparison, the PvP
health-bar test (now with a named suspect), and humanoid corpse dragging. Nothing below needed a headset today.

## 0. Everything from 2026-09-24, ordered. Nothing dropped.

Ordered by what costs a session first, then by how cheap the fix is. "Debug plan" means there is no fix yet and
the task is to find the cause rather than guess at one.

### 1. Costs a session, cause known, do next

- [ ] **The `Unequip` loop itself.** Capping the queue treats the symptom. Something makes one actor emit the same
      unequip action hundreds of times; the suspect is the once-a-second naked-NPC re-equip check fighting whatever
      unequips it, and `Actor::IsWearingBodyPiece` was rewritten during the merge to read container flags.
      **Plan:** on the owner's side, log each time the re-equip check acts on an actor, with what it saw and what
      it did. One session then says whether our check is the source. Cheap, and it is the last known cause of the
      stream starving.
- [ ] **Superseded: spell hits are not authoritative.** The caster sends the cast and the projectile; whether it *hit* is
      decided separately on each machine, so a spell can land on one screen and miss on the other. Sword hits
      already avoid this by sending the damage. **Plan:** do the same for spells against a remote player, reusing
      the health-change path. Medium, no new addresses, and the most likely explanation for "I damage him, he
      takes nothing".
- [x] **[untested] The receiver trusts the transmitted spell id first.** It used to use whatever was staged in
      that casting slot and fall back to the id only when the slot was empty, so a quick spell switch replayed the
      old spell. Already done in `MagicService` (found done on 2026-09-30 when this entry was read as open): the
      id is resolved first, the slot is the fallback, and a fallback is logged as `did not resolve here; falling
      back`. The voice slot is never a fallback on VR.

### 2. Crashes with no cause yet: debug plans, not fixes

- [ ] **Elbios, on opening the wrist menu.** `TweenMenu` at 21:27:52.184, `InventoryMenu` 174 ms later, dead 6 ms
      after that on a null byte read. No frame of ours anywhere in the stack; it holds `handleVRButtonUpdate`,
      `SkyUI-VR::DispatchControllerState`, HIGGS and FBT callbacks, which is controller input reaching a Scaleform
      menu mid-initialisation, and there is a matching firsthand report on FBT's support page. Our contribution is
      timing only: `TweenMenu` and `InventoryMenu` are kept unpaused.
      **Debug plan:** have him open and close the wrist menu a dozen times in one session. If it reproduces, drop
      those two from the unpause list (brief hub menus, so the cost is small) and see whether it stops. Do not
      remove them pre-emptively: a paused player stops sending updates and goes silent to everyone else, which
      trades a confirmed problem for an unconfirmed one.
- [ ] **Seen, on the load after death.** Save/load request built at 21:28:07.734, `Loading Menu` while still
      connected, access violation 68 ms later with a `MovementHandlerArbiter*` in the registers. Same connected-load
      path as the long-standing "level-up and death end at the main menu" item.
      **Debug plan:** the `SaveLoad` probes already installed name the caller and the stack; read the next
      occurrence from them rather than from the dump.

      **A load crash made on purpose, 2026-10-02, and probably not the same one.** With nobody in the headset
      (`live-load`, `live-load-dead`): the player is killed by console while connected and a save is loaded through
      DevBench. Loaded while the player is down (2 s after the kill), or about 10 s after standing up again (inside
      the knock-down and the ten seconds of protection), the game dies 14 ms after `Finished loading`, three times
      of three. Loaded 40 s after the respawn it is fine, twice; two loads in a row with no death are fine; and with
      the mod not connected -- so the game's own death and reload, with no respawn of ours -- death, reload and two
      further loads are all fine. So it is our death sequence, interrupted by a load.
      The crash itself is `skyrimvrtools.dll+0x71B5` reading a device pose at index 0xFFFFFFFF, called from a
      Papyrus native, `GetSteamVRDevicePosition`, and only one mod on the list calls that: Simple Realistic Archery
      VR (`sravrQuestScript`), which polls the controllers when a bow is in hand. Seen's crash was 68 ms after the
      load *request*, with a `MovementHandlerArbiter` in the registers; this one is after the load *finished*, in a
      different module. What cannot be told here is whether a controller reads as missing at that moment with real
      hardware too, or only with the rig's two virtual ones (the same address is where the game died with no
      controllers at all on 2026-10-01). Putting the bow away and taking it out again after a respawn does not
      crash. Not changed: nothing is known yet about what a fix would have to restore.

### 3. Known broken, plan already written, waiting on a session

- [ ] **Invisible player, worse indoors.** Plan in section 0a: measure render state (fade, cull) rather than actor
      state, and recover automatically instead of reconnecting. Nothing built yet.
- [ ] **Friend's health bar.** Plan in section 0a, blocked on the one-minute PvP test only the players can run.
      Note the damage itself works: the log shows hits landing and the player going down, so what is missing is the
      feedback, not the damage.

### 4. Position accuracy: two problems that look alike and are not

- [ ] **The other player's hands sit slightly off (palms together, seen lower).** Structural, not a bug. `VRPose`
      carries bone **rotations only** and `PoseActor` keeps each bone's own translation, so anything VRIK does by
      moving a bone rather than turning it -- height calibration, crouch, arm length -- cannot be reproduced, and
      the arm lands at the receiver's own skeleton's height.
      **Measurement shipped first (2026-09-25), because the cause is not known.** Three things can move the copy's
      hand away from where the sender had it, and they need different fixes: the skeleton root sitting at a
      different height (VRIK height calibration moves it by translation), different bone lengths (a different
      skeleton, or a scale this does not capture), or VRIK moving the hand itself by translation, which rotations
      can never reproduce. `CopyDiag` now prints `copy reach [...]` and `player reach [...]`: the skeleton root's
      local Z, the head and both hands' heights above the 3D root, and the upper-arm and forearm lengths.
      **How to read it:** same root height and same arm lengths but a different hand height means VRIK is moving
      the hand directly, and only sending hand translations will fix it. A different root height means the body
      is standing at a different height and the root offset is the fix -- cheaper, and it would move everything
      consistently. Different arm lengths mean the skeletons differ and neither would fully close the gap.
      Guessing between those three would have meant shipping a change to how every remote body looks with no way
      to tell whether it helped.
- [ ] **Your own sword hitting where the blade is not.** The screenshots of 2026-09-24 are a first-person view of
      the player's own weapon striking the Earth Stone with the impact well off the blade. This client never writes
      the local player's bones, it only reads them, so this is not sync and not ours: it is the weapon collision
      offset of the VR setup (VRIK or HIGGS).
      **Checked rather than assumed, 2026-09-25:** `VRBodySync::SetRemotePose` has exactly one caller,
      `InterpolationSystem`, which runs on remote actors; and there is no hook anywhere in this client on the melee
      hit path -- no `HitData`, no weapon swing, no hit handler. There is no code here that could move where your
      own blade lands.
      **Debug plan:** reproduce in solo with the mod disconnected. If it still happens it is a VRIK/HIGGS setting
      and belongs in their configuration. Do that before any time is spent here.

- [x] **[untested] Swept the null-extension crash family out of every engine hook (2026-09-25).**
      `Actor::GetExtension()` returns null by design for an actor this client did not allocate, and 62 call sites
      dereferenced it without checking. Two of those killed both players: `HookActorProcess` on 2026-09-23 (a
      Redoran Guard during a crime response) and the same family on 2026-09-24.
      Every dereference inside a hook the engine calls with an arbitrary actor is now guarded -- 14 sites across
      `Actor.cpp`, `References.cpp`, `SubtitleManager.cpp`, `InvisibilityEffect.cpp`, `ActorMagicCaster.cpp` and
      `TESObjectREFR.cpp`. All were read-only questions about whether an actor is one of ours, so the fallback is
      the same everywhere: no extension means not ours, condition false, engine path taken.
      **Not done by making GetExtension() return a dummy**, which would have fixed all 62 at once: 23 sites
      *write* through it (`SetRemote`, `SetPlayer`, `GraphDescriptorHash`), and a shared dummy would swallow those
      silently. One deref remains inside a hook, in `PlayerCharacter.cpp`, and it is on the local player, who
      always has an extension.

- [x] **[untested] Two unchecked lookups in the message handlers, from the audit (2026-09-25).**
      `OnBeastFormChange` ran `std::find_if` for the player's entity and dereferenced the result without checking
      it against `end()`. That is undefined behaviour whenever the player has no entity -- before it is created,
      or after teardown -- so a werewolf or vampire lord transformation at either moment would have taken the game
      with it. `OnNotifyNewPackage` passed the result of `Cast<TESPackage>` straight to `SetPackage` without
      checking it, so a form id that resolves to something that is not a package (a mod mismatch between the two
      games) handed it null.
      Swept the rest of the client for the same shape: the two other dereferenced `find_if` results, in
      `CharacterService` and `ObjectService`, are both already guarded. `OnBeastFormChange` was the only one.

### Bot coverage (2026-09-24)

- [ ] **What the bot still cannot cover, and what to do about it.** It has no game behind it, so nothing visual is
      testable: invisible bodies, dragged corpses, hand positions, the health bar. Those need a headset. What
      could be added without one: ~~an NPC-ownership churn test~~ (done 2026-09-30, below), a test that a looping
      animation does not starve the stream, and ~~a test that a spell hit reaches the target's health~~ (done
      2026-09-30: the `spell` pair sends a concentration spell the way the client does -- changes under a point,
      added up and sent every 250 ms -- twelve changes of -2; the victim reads 76 and so does a copy rebuilt from the
      server. Whether the game raises those changes for a spell landing on a remote copy at all still needs a headset).

      Also found on 2026-09-30: **the first bot on a fresh server never sent a movement.** Its character is entity 0,
      and `SendMovement` / `SendEquip` tested the id instead of whether there was a character, so the server never
      learnt which cell that bot's character stood in. Every pair whose watcher connected first ran with a watcher
      frozen at its join cell; the `dropmove` pair walked into a room with it and was never given it. Fixed in the bot.
      NPC copies are now recorded too (`known npc`, `health npc`, `dead npc`), which the bot used to ignore.
      Note on the animation one: the bot has no animation-variable support at all, and the sliding it would be
      for is not a stream problem -- see "Sliding: the descriptor lookup" in the 2026-09-29 notes.

### 5. Smaller, known, unglamorous

- [x] **The crime alarm guard is installed but has never fired.** It has now (2026-10-03, `live-crime`): it ran, the
      game carried on, and no null actor was skipped.
- [ ] **Superseded: `This isn't a crime faction! 4018279`.** Every multiplayer death calls `PayCrimeGoldToAllFactions` with a
      hard-coded faction id that depends on load order, and it failed on both deaths logged. Clearing bounties on
      every co-op death is also a gameplay decision nobody asked for. Small fix, worth a decision first.
- [x] **[untested] NPC health now has an authoritative correction (2026-09-25).** An NPC's health on the
      receiving side was only ever the sum of the damage deltas that happened to arrive: a delta lost to a starved
      stream, a hit applied on one side only, an owner handover mid-fight, and the number drifted away from the
      owner's for as long as the actor lived, with nothing to repair it. Health snapshots were applied to player
      copies only.
      The owner's snapshot is now used for NPCs too, but **as a correction rather than a constant override**, and
      only when the gap is 25 or more: a small difference mid-combat is normal and is left alone, and the delta
      path still carries deaths, which a continuous override would fight every frame. Dead actors are untouched.
      Every correction is logged (rate limited to one line per five seconds) as
      `NPC X health corrected from A to its owner's B`.
      **What to watch:** if that line appears constantly rather than occasionally, the stream is dropping deltas
      and this is papering over it -- which would itself be the finding.
- [ ] **Superseded: NPC health has no authoritative correction.** Snapshots are ignored for NPCs in favour of accumulated
      deltas, so once a value diverges nothing repairs it. Larger work, and it interacts with the essential-NPC
      handling above.
- [x] **[untested] A duplicate spawn now refreshes the ownership epoch (2026-09-25).** A spawn for a character
      that already exists here was discarded whole. The server re-sends one when something about the character
      changed, and the epoch is the part that matters: upstream's ownership rework refuses any claim whose epoch
      does not match the server's, so a stale epoch meant every later attempt to take that actor was rejected and
      nothing said why. It is refreshed now, and a change is logged.
      **Position, cell and death state are deliberately still ignored** -- they arrive continuously through the
      movement and death paths, and forcing them from a spawn message would fight those. So the audit's finding
      is only partly addressed, on purpose.
      `churn.txt` still passes and still does not reproduce the original symptom, so this is reasoned from the
      code rather than from a repro.
- [ ] **Superseded: duplicate spawn messages are discarded rather than refreshing state**, so a newer spawn carrying updated
      ownership, cell or death state is dropped. Explains some of the "already spawned" warnings.

---

## 0a. The three open complaints, with a plan each (2026-09-23)

- [ ] **Invisible player: stop guessing at actor state, look at render state.** Five occurrences now, and every
      time `CopyDiag` reads perfect: present, metres away, right scene, invisibility 0.00, scales 1.000, spine
      rotation clean, 3D root with its children. Four causes were proposed and all four were disproved by that
      probe (health floor, body scale, the invisibility actor value, bone drift). They share a blind spot: they
      are all *actor* state. A body can be flawless as an actor and still not be drawn, and the render state is
      the one thing never measured. New clue from 2026-09-23: it happens far more indoors, and fade and LOD
      behave differently in interiors.
    - **Stage A is built and deployed (2026-09-24, `7bb3c6d`), and it does not guess.** `CopyDiag` now prints the
      eight words just past the end of a VR `NiNode` (0x140 to 0x15C) for the copy **and** for the player, whose
      body is always drawn. The fade offset was not taken on trust: CommonLibVR puts `BSFadeNode`'s data at
      +0x128, but VR keeps `NiNode::children` at +0x138, so +0x128 is still inside `NiNode` here and cannot be the
      fade. Reading it anyway would have been the fifth wrong offset on this bug. Instead: whatever the player
      reads while visible is the "drawn" value, and the word that differs when a body vanishes is the fade.
      **What to do with the next occurrence:** find a `CopyDiag` line from while the body was invisible and compare
      its `copy words` against `player words` on the same line. One should differ. That names the offset, and
      Stage B can then force it.
    - **Stage A (superseded plan).** Add to `CopyDiag`, for the copy's 3D root: whether it is a `BSFadeNode` and its
      `currentFade` (CommonLibVR puts the runtime block at +0x128 and `currentFade` at +0x08 inside it, so
      +0x130), the cull flag on the root and on each skinned child, and the copy's parent cell and worldspace
      against the player's. Log the raw values first and cross-check the offsets against neighbours the way
      `charController` was checked, before any of them is acted on; VR layouts have burned us twice this week.
    - **Stage B, recover without a reconnect.** When a copy is close and reads as not drawn (fade at or near
      zero, or culled) for more than about three seconds, clear it: force the fade up, clear the cull, and if
      that does not take, rebuild the 3D. Automatic, no key press, and a log line every time it fires. Even if
      the cause is something else, this ends the reconnect ritual, and the log then says how often it was needed.
    - Stage B is only worth shipping with Stage A's numbers in the same build, so one session answers both.

- [ ] **Friend's health bar: the message is right, the widget refuses it.** Proven on 2026-09-23: our message is
      the game's own (a `HUDData` with type 0xB at +0x10, level at +0x20, flags 0x0101 at +0x22, the handle at
      +0x28, sent to `WSEnemyMeters` as `kUpdate`, which is 0 and matches what the probe sees the game send).
      It was pointed at the friend **107 times** that session and drawn **once** — and the once was the moment
      she burned him with fire.
    - **Hypothesis:** the widget only draws for an actor the game holds as a valid hostile target, and friendly
      fire briefly made him one. That single data point fits nothing else.
    - **Decisive test, one minute in game:** make the two players hostile (PvP) and watch whether the bar becomes
      reliable. If it does, the game's enemy meter structurally cannot show a friendly player and no amount of
      message-fixing will change it.
    - **Then it is a choice, and it is the user's:** drive a different element the HUD already has, read
      `EnemyHealth::Update` to find what it tests and satisfy that without making anyone hostile, or accept a
      minimal element of our own. The last one was refused before on immersion grounds, so it only comes back if
      the game's own widget is proven incapable.

---

- [x] **[untested] Levelled NPC reconciliation is back on for VR (2026-09-23), after being off since it crashed
      Seen on 2026-09-22.** All three engine calls are now accounted for. Watch the next join closely: this is the
      code path that crashed him, and the one part still taken on trust is `CreateTemplateActorBase`.
    - `TESObjectREFR::SetLeveledCreature`: **address supplied 2026-09-23**, AE 20231 -> VR 0x1402b8f10
      (SE 0x1402a77a0, SE id 19826). This is the step that actually applies the pick, so until now the feature
      could not have worked even without the crash.
    - **`CreateTemplateActorBase` is now proven by running it (2026-09-25).** The host's log shows
      `Applied leveled NPC pick` three times on 2026-09-24 (18:52:14, 19:27:25 twice), and that line is written
      only after both `CreateTemplateActorBase` and `SetLeveledCreature` have returned. No crash followed the
      19:27 pair. An earlier note here said reconciliation never ran; that was read from the friend's log, not the
      host's, and was wrong. The address is good.
    - `TESActorBaseData::CreateTemplateActorBase`: AE 14375 -> VR 0x19c0c0. Was not proven by running it, but
      corroborated: cmpayc/TiltedEvolutionVR derived the same address independently and declares it
      `TESNPC* thiscall(TESNPC*, TESNPC*)` where upstream declares `TESActorBase* fastcall(TESActorBase*,
      TESActorBase*)`. On x64 those pass in the same registers and return the same way, and TESNPC derives from
      TESActorBase, so the two declarations are compatible. This is the residual risk.
    - `GarbageCollector::Add`: **not called on VR at all.** Upstream calls AE 36460, the TESBoundObject overload;
      the merge paired it with VR 35492, which CommonLibVR shows is the TESObjectREFR overload taking
      `(TESObjectREFR*, bool)`. We called it with a base form and a junk second argument, which is what crashed
      Seen inside `BSExtraDataList::GetExtraDataWithoutLocking`. Skipping it leaks one temporary base form per
      reconciliation, which is a much better bargain. Restore it if the TESBoundObject overload's VR address
      turns up.
      Superseded detail from when this was off:
    - `TESObjectREFR::SetLeveledCreature` (AE 20231) has no VR address. That is the step that actually applies the
      pick, so even before the crash the feature could not work: it logged
      `SetLeveledCreature has no VR address` and carried on to the rest.
    - `GarbageCollector::Add` (AE 36460, given VR 35492 in the merge) is **confirmed wrong**. It crashed Seen on
      join at 22:14:26, in `BSExtraDataList::GetExtraDataWithoutLocking`, while disposing the temporary base
      `FF000C6E` ("Reaver Highwayman") that the log had queued for reconciliation a moment earlier; the crash
      registers still held the owner's pick `301E828` ("Reaver Outlaw") and a `GarbageCollector*`.
    - `TESActorBaseData::CreateTemplateActorBase` (AE 14375) resolves to a VR address our own override table
      labels `TESNPC::SetLeveledNpc`. Plausibly the same function under two names, never verified.
      Both entry points (`CharacterService::ApplyLeveledNpcPick` and `LeveledNpcSystem::ApplyPick`) now return
      early on VR, so no actor is disabled and re-enabled for a reconciliation that cannot finish, and a levelled
      NPC that rolled differently on each side is covered the way it was before the merge, by the stand-in and
      ghost handling. To turn it back on, all three addresses have to be confirmed first; that is a job for the
      address work, not for another play session.

- [~] **[untested] Seeing the other player handle a body (HIGGS grab).** Built 2026-09-22, never played.
      It needs nothing from HIGGS and no protocol change, which is why it went in at once: `higgs_vr.dll` exports
      only the two SKSE entry points, so its `IHiggsInterface001` can only be reached through SKSE messaging, and
      guessing at the interface would crash the game. Instead the body itself is watched.
    - **Sending.** `VRBodySync::CaptureBodyPose` reads the upper-body bones of a dead actor this machine owns,
      within 600 units of the player, and `AnimationSystem::Serialize` puts them in the movement snapshot that
      actor already sends. The `VRPose` field has been on every reference update since the player pose was built,
      so nothing new travels. A body that is not moving sends nothing at all; sending carries on for two seconds
      after it comes to rest, because the receiver only buffers two points and the final pose has to be repeated
      to be the one that lands. After that the body is left to its own ragdoll on both sides.
    - **Receiving.** The frame-end pass no longer skips a dead body when bones arrive for it. Dead *player*
      copies are still skipped: posing them fights the death animation and leaves the body standing in the air.
      The bone-usability guard from the invisible-body fix applies unchanged.
    - **Ownership.** `CharacterService::RunBodyGrabUpdates`, every 250 ms: a dead body owned by the other player
      that this machine has physically moved more than 32 units from where the network is playing it back is
      claimed through the ordinary `RequestOwnership`, at most once every three seconds per body. The corpse path
      tolerates 64 units of drift before it snaps a body back, so the claim goes out before the snap. Living NPCs,
      player copies and summons are never taken this way.
      To watch for on the first session: a body that stutters between the two sides while a claim is in flight,
      a claim that is refused and leaves the grab fighting the owner, and whether the receiving side's ragdoll
      re-derives itself from physics hard enough to ignore the written transforms. That last one is the real
      unknown and is what the first test is for; if it shows, the copy has to be put into ragdoll, or its
      physics frozen, while it is driven.

**Still to check:** VRIK menu only for the caster, killed NPCs stay dead, dragon and dialogue fixes, weapons at
spawn, reconnect after a drop, shouts (ported, untested), PvP sword hits (new, untested).

---

### From TiltedEvolutionVR and upstream, not done yet (decisions)

- [ ] **TiltedEvolutionVR `aaf5d83`, item and body drift.** A remote body is moved by teleporting, and a teleport
      into a loose object makes havok fling it (their collision-layer table patch stops that); an NPC's capsule can
      be shoved 54 units off its body by a player and stay there (they read the capsule and warp it back). 1200
      lines, address-heavy; take it when under-the-ground or item drift is next.
- [ ] **TiltedEvolutionVR `1660eb0`, ownership blacklists and former-owner updates.** Read 2026-09-19: a follow-up
      to upstream #887 (ownership epochs); every change needs the epoch fields, so it only comes with the rework.
- [ ] **TiltedEvolutionVR `29f99ed`, two havok crash guards** (`SkyrimVR.exe+0AB1ABA` ragdoll add,
      `+03AD7B1` shadow scene listener on a temporary with no 3D). None of our dumps have those addresses; port
      them the day one does, they name the reference.

- [ ] Upstream per-dungeon respawn positions (needs cell editor IDs, which VR may not keep) and the
      Companions "Brotherhood" quest patch plugin (needs the 1.70 header and an MO2 slot). Low value for now.

---

## 1. Lag and stutter (top priority)

What we know:

- The mod's own per-frame update (World::Update) measures about 0.1 ms, which is negligible.
- Frames run at 22-28 ms, with spikes up to 40 ms, and the other PC runs at about 28 ms all the
  time.
- Nobody has measured the cost of the hooks that run _inside_ the engine (animation update, equip,
  inventory), of logging, or of spawning remote actors. That's where the remaining suspects are.

### 1.1 Measure first [measure]

- [ ] Benchmark 60 s at the same save and spot, standing still and then walking, in four setups:
    1. FUS without Skyrim Together;
    2. with Skyrim Together, not connected;
    3. connected alone to a local server;
    4. co-op.

    Keep the four summaries in this file, so every later change is compared against real numbers.

- [ ] Don't attach the debugger watcher during normal sessions. It stops the game on every thread
      start and exit (DynDOLOD alone runs 600+ threads).

### 1.2 Likely wins (do after 1.1 confirms them)

- [ ] **Spawn bursts on cell change.** Entering an area creates dozens of actors in one frame, each
      with a full inventory rebuild (remove everything, then add and equip item by item).
    - Fix: limit spawns and inventory applies to a few per frame, and queue the rest.
- [ ] **Remote movement is shown 300 ms late** (`CharacterService::RunRemoteUpdates`).
    - Fix: base the delay on measured ping and jitter, clamped to 100-200 ms. Keep the 300 ms delay
      for NPCs if they get jittery.
- [ ] `RunNakedNPCBugChecks` looks at every actor's worn items once a second. Measure it, then limit
      it to a few actors per tick.
- [ ] The new equipment snapshot reads the whole player inventory once a second, and FUS
      inventories are big. Measured since 2026-09-20 ("equipment snapshot" in the perf line); if it shows,
      only rebuild when an equip hook fired or the hand/spell pointers changed.
- [ ] Look for other per-frame costs: linear `find_if` searches over all entities in hot paths, and
      `TESForm::GetById` inside loops.

---

## 2. Sync correctness ("NPCs and animals feel buggy")

### 2.1 Combat and NPCs

- [ ] **Hits from VR weapons.** A VR sword hit lands on the attacker's copy of the NPC. Check that
      damage reaches the NPC's owner, and that the health change is sent back when the NPC is owned by
      the other player.
- [ ] **Actor ownership warnings.** Look into `Actor for ownership transfer not found` and
      `OnNotifyActorTeleport: failed to retrieve actor` once the churn fix is confirmed.
- [ ] **Shared follower loops** (the Lydia case). A follower owned by one player gets pulled by the
      other player's game. The 2026-09-18 thrash (removed and re-assigned 11 times a second) was a different bug,
      fixed in `DiscoveryService`; Lydia followed and fought fine afterwards. The tug of war itself is still open.
    - Decide the rule (the follower's owner = the player it follows), then apply it during
      ownership transfer.

### 2.2 Items and inventory

### 2.3 VR body

- [ ] **Legs when moving.** Check that smooth locomotion plays a walk or run on the remote copy,
      rather than sliding.
- [ ] **Face "looks off"** (FaceGen on VR). Parked earlier, still open.

### 2.4 World, quests, dialogue

- [ ] **Message boxes show up for both players** ("player 1 opens text, player 2 sees the popup").
      No code syncs message boxes. The likely path is activation sync: the other client replays the activation
      with the remote player as activator, and the object's script shows its message. Note which object it was
      next time before changing activation sync.
- [ ] **Quest NPCs out of sync** (Bastianus Axius). Check quest sync with a party on the next test.
- [ ] **Weather and time.** `WeatherService` and `CalendarService` exist; confirm they work on VR.
- [ ] **[untested] Mount sync on** (InitiateMountPackage, address found by the friend and checked). Mount a horse:
  the other player sees you ride it, and only one player sits on it. `Rider not found` or `Mount not found`
  warnings mean a mount event couldn't be matched.

---

## 3. Stability

- [ ] **CEF crash guard audit.** Before the VR menu is created, any overlay call is a hard crash. Check
      that every `OverlayService` / `ExecuteAsync` / `CefListValue` path is guarded.
- [ ] **Script-engine crash on a freed temporary form (`SkyrimVR.exe+0x93CE17`) -- the game's, not ours.** Emma has
      it ten times in single-player Crash Logger logs since 2024-12, most before this mod existed; Seen and Emma both hit
      it on 2026-09-30, 40 s apart, same address. Our code is in neither call chain and our copy deletion is cleared.
      Only open question for us: whether co-op makes it more frequent. Evidence and suspects in `KNOWN_ISSUES.md`.

---

## 4. Convenience: make it shareable

The goal is that a new player installs one package, edits one line, and plays, all without taking
the headset off.

### 4.1 No keyboard needed in VR

- [ ] **In-headset menu:** the normal Skyrim Together UI as a SteamVR dashboard tab
      (`Systems/VRDashboard.cpp`). Renders for the friend, still blank for the host; top of section 0.
    - [ ] Adapt the page layout for the dashboard (large text, no empty full-screen areas).
    - [ ] A small always-visible overlay for chat and notifications while playing.

### 4.2 Versions and compatibility

- [ ] Add a protocol number that changes with every message change, so unrelated client changes stop forcing
      server restarts.
- [ ] **Startup checks with plain-language HUD errors:**
    - [x] uGridsToLoad = 5 (stops the automatic connection);
    - [x] SKSE VR loaded (warning only);
    - [x] `connect.txt` missing;
    - VR Address Library present;
    - fewer than 255 plugins;
    - `SkyrimTogether.esp` with the 1.70 header.

### 4.3 Packaging and install

- [ ] **Install script** (PowerShell):
    - finds the MO2 instance and copies the tool into it;
    - adds the MO2 executable entry;
    - creates `%LOCALAPPDATA%\SkyrimTogetherVR\connect.txt` from a template on first run;
    - checks the requirements listed in 4.2.
- [ ] **Guide for non-FUS modlists:** what's required, what's known to conflict, and the plugin
      limit.
- [ ] **Licensing before sharing.** Tilted Online is GPL-3: publish the source for every shared
      build, and keep the upstream license and credits. Check the Skyrim Together Reborn project's rules
      on redistributing forks.

---

## 5. Polish

- [ ] Clean remote spawn: fade in instead of popping or falling. Place on the ground if the
      interpolated Z is invalid ("mammoth fell from the sky").
- [ ] Death and bleedout: HUD message "X is down", and optionally revive by activating the downed
      player. Needs a new message: a downed player bleeds out and never reports a death state.
- [ ] Sensible defaults for VR in `STServer.ini` (difficulty sync, PvP off, time scale).

---

## 6. Working method (to cut the "restarted hundreds of times" cost)

- [ ] **A test script per build:** a short checklist of scenarios to play, and which log lines
      prove each one, handed over together with each deploy.
- [ ] **Batch several fixes per test session** (as agreed). Keep each change's log lines so a
      single session answers every question.
- [ ] No debugger attached by default (see 1.1).

### 6.1 Test bot: a second player without a second headset (planned 2026-09-20)

A console program, target `STBot` under `Code/bot`, built from the same tree (same version string), that connects
to a server exactly like a client and drives one player character from a script. Phase 1 changes no client or
server code; it only links `SkyrimEncoding` and `TiltedConnect`. Built 2026-09-20 01:20; the handshake, the mod
list, the host position from the host's log and the scouting were checked against the local server. The spawn,
movement, death and respawn paths are **[untested]** until a headset is in the game.

**Run it** (server, then the game joined to it, then the bot, from `build\windowsd
elease`):
`STBot.exe ..\..\..\..\Codeot\scripts\copy-respawn.txt` (defaults: `--server 127.0.0.1:10578`, the
password from `config\STServer.ini` via `--password`, `--hostlog` the host's `tp_client.log`, `--spacing 200`).
Its log is `logsot.log` next to the exe. The clone carries the host's face, outfit and in-game name.

- [ ] **[untested] Character:** wait for the host's own `CharacterSpawnRequest`, reuse its `AppearanceBuffer` and `ChangeFlags`
      (the bot is a clone of the host, so the receiving client builds the face from data it already accepts), then
      `AssignCharacterRequest` 200 units from the host with the same worldspace and cell, Nord race, iron sword and
      Flames in the inventory, health 100. Then `EnterExteriorCellRequest` from the host's grid.
- [ ] **[untested] First scenario, the bug of 2026-09-19** (`Code/bot/scripts/copy-respawn.txt`): spawn, walk past the host, take damage to -4, respawn, keep walking.
      Pass: the host's log shows `copy would have spawned with the owner's health -4 ... started at 1` and the copy
      stays visible after the respawn.
- [ ] **Phase 2, real animation:** record the host's own outgoing packets into `logs\session.stpcap` (a log like
      `tp_client.log`, capped and rotated, not a toggle) and let the bot replay a recording with its server ids
      remapped, so the copy walks, fights and casts like a real player.
- [ ] **Where it runs:** on the host's PC against the local server. Never on the friend's server during play.
