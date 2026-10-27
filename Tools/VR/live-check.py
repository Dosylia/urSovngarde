"""Runs a bot against the real game and checks, in the game itself, what the bot's script says should be true.

    python Tools/VR/live-check.py live-copy [live-npc ...]
    python Tools/VR/live-check.py --all            every script in ALL, in that order
    python Tools/VR/live-check.py --sync           the sync and animation set (SYNC; Tools/VR/README-rig-checks.md)
    python Tools/VR/live-check.py --expect-fail live-x   on a build without the fix live-x guards: it must fail
    powershell -File Tools/VR/run-live.ps1         the same, with the game brought up before and down after

Each script ends with one verdict, also appended to the history file (toolbox test-history/live.jsonl, with the
build, the rig's frame rate and the bot's fake network): ok; FAILED (a check failed); INVALID (a `log REQUIRE`
precondition did not hold, or the bot warned that a line could not do its part, such as "nobody to hit": the run
proves nothing either way); INCONCLUSIVE (nothing failed, but a check found nothing to judge and said SKIPPED);
NOT RUN. With --expect-fail: RED (a check failed, so the test catches the bug) or NOT RED (it passed without the fix).
A test counts once it has been RED on a build without its fix and ok with it.

The game must be up and connected (Tools/VR/headless.ps1 up). A script marks a check with a line `log CHECK ...`;
when the bot prints it, this asks the running game through DevBench (http://127.0.0.1:8921) and prints PASS or FAIL
with the value it read. The bot alone can only say what the server sent; this says what the game did with it.

Checks:
    copy exists | copy 3d | copy alive | copy dead | copy health <n>      the game's copy of the bot's character
    npc exists  | npc alive | npc dead | npc health <n|full>             its copy of an NPC the bot owns
    npc gone | copy gone                                                  that copy is no longer in this game (deleted or disabled)
                                                                          (exists: in this game and enabled; a DevBench timeout makes either INVALID)
    player cell <hex>                                                     the cell the player stands in, by form id
    player mark | player dropped <n>                                      the local player's health, before and after
    drop exists | drop gone | drop mark | drop moved <dx> <dy> <dz>       the item the game placed for the bot's drop
    player alive                                                          the local player is not dead
    player has <hex>                                                      the local player carries that item
    hands within <units>                                                  copies' hands are where their owners have
                                                                          them (median of the game's measurements)
    player in <worldspace>                                                outdoors in that worldspace (Tamriel), not
                                                                          in an interior
    ref <hex> near <x> <y> <z> <units> | ref <hex> near player <units>    a reference of the game's own is that close
                                                                          to that place, or to the player
    scene <hex> playing                                                   a scene of the game's own plays
    ref <hex> cell same | ref <hex> cell other                            a reference of the game's own is in the
                                                                          player's cell, or in another one
    ref <hex> open | ref <hex> closed                                     a door of the game's own is open (or
                                                                          opening), or closed (or closing)
    ref <hex> locked | ref <hex> unlocked                                 a door or container of the game's own is
                                                                          locked, or not
    game alive | game inworld                                            the game still answers | and is in the
                                                                          world with a save loaded, not at a menu
    slide mark | slide below <pct> | slide above <pct>                    the game's own SlideDiag since the mark: the
                                                                          share of a remote body's moves that came
                                                                          with animation variables that said nothing
    copy gait walking <s> | copy gait sliding <s>                         watches the copy for that many seconds: when
                                                                          it moves, does its own animation graph have
                                                                          a speed (walking) or none (sliding)
    copy feet still <s> | copy feet moving <s>                            watches the copy's feet against its pelvis:
                                                                          planted (it stands) or swinging (its legs
                                                                          walk, on the spot or not)

A line `log DO console <command>` runs that console command in the game at that point of the script, and
`log DO f6` presses F6, which connects or disconnects the game. `log DO key <name> <ms>` holds a key down in the game for that long (DevBench's keyboard input; `w` walks the player
forward even in VR), `log DO look <degrees> <ms> [hold <ms>] [back]` turns the virtual headset that far over that time
while both hands rest still at the sides (positive turns left; `back` returns it), and `log DO load last` loads the most recent save (while connected, which is the point of asking); `log DO load save <name>` loads that save. `log DO combat on` and
`log DO combat off` switch combat AI (off is the run's default, so a creature a test spawns just stands there; on makes
it come for the player, which is how it is made to move). `log DO god on` and
`log DO god off` set god mode for a script that sends the player somewhere dangerous: the away tests go to wherever
Lydia stands, which in the test save is under water, where the player can drown.

`log DO server restart` stops the server and starts it again, and waits until the game has gone back in by itself
(the game reconnects on its own; so does the bot, and its script carries on once it is back in the world).
The bot's script does not stop while the driver is busy with that, so its own `wait` lines run out meanwhile:
`log DO sleep <seconds>` makes the driver itself wait before it reads the next line.
`log DO server restart after console <command>` runs that command while the server is down (the game offline).
`log DO menu <seconds> <menu name> [shot <name>]` keeps a menu open that long (the driver waits; the bot's script does not), with `shot` taking a SHOT picture of it halfway.
`log DO pickup own` has the player pick up the newest item this game dropped itself.
`log DO papyrus <Script> <Function> <self>|global [form ...]` calls a native function on a reference, or a global one (ids in hex; a number as n:<value>,
a float as f:<value>, a bool as b:true or b:false, a string as s:<text>).

`log CHECK copy|npc mounted|unmounted` asks whether the copy rides (IsOnMount). `log CHECK copy|npc equipped|unequipped <hex>` asks the game whether its copy has that item equipped (not for the left hand: the
game's IsEquipped does not count it); `lefthand|righthand <hex>` asks which weapon is in that hand; `has <hex>` whether it owns one.
`log CHECK distinct <most> <regular expression with one group>` passes when the client has logged at least one and
at most that many different values of the group since the script began.
`log CHECK absent <regular expression>` passes when the client has written no such line since the script began.
`log CHECK smooth all|<kind> ...|except <kind> ...` passes when the client logged none of those kinds of jank since the script
began (JANK: corpse-jump, died-early, point-jump, getup-jump, stuck-down, refused, no-reports, ragdoll-apart, door-differs, copy-lost-3d).
After every script the jank it found is printed and kept in the history, judged or not.
The bot's script line `report` prints what the bot knows of the world. `log CHECK desync all` compares its last report with the game:
each NPC copy's life and place, no placed reference of the game's own drawn beside its copy, and each door and lock the bot knows.
`log CHECK told door <hex> open|closed` and `told lock <hex> locked|unlocked` pass when the bot's last report has it so: what
this game did reached the other player.
`log CHECK npc fell within <ms>` times the fall from this game applying the owner's death to the first `DO watch npc` sample with the body down.
`log DO record <name> copy|npc|placed|player|ref <hex> <seconds>` samples in the background while the script goes on: a body's
pelvis and height (its whole body in the last 2 s), or any other reference's open state. Checks on recordings:
`pose <b> like <a> <units>` (resting poses), `fall <b> within <ms>` and `fall <b> like <a> <ms>` (from its death in this game to
down), `place <b> like <a> <units>` (where it came to rest against where it stood), `still <a> <units>` (no jump once down), and
`door <a> once open|closed|toggled` (one movement to that end, no flicker; only doors show their moving state).
After a run, logs/live-report.html shows every script's verdict, checks, jank and screenshots. Tools/VR/README-rig-checks.md
says how these fit together and what to know before writing a test with them.
`log CHECK spot mark` notes where the player stands; `spot back <units>` passes when the player is within that many
units of it in the same worldspace or interior, `spot away <units>` when farther or elsewhere.
`log CHECK logged <regular expression>` passes when the client has written a matching line since the script began.
`log DO logmark` makes `logged`, `absent`, `atleast`, `fewer` and `distinct` count only the client lines written after it: a run's first lines can still describe the script before.
`log CHECK ids reused <name>` passes when the game gave an actor of that name a form id under which this client had
deleted a copy earlier in the session (what live-temp-reuse needs to have happened to mean anything).
`log CHECK ref <hex> enabled|disabled` asks whether a reference is switched on or off. `log CHECK ref <hex> 3d` asks whether a reference's 3D is loaded; `ref <hex> 3d steady <seconds>` samples it the whole
time (the driver waits; the bot's script goes on) and fails if any sample found it unloaded: an object that blinks.
`log DO mark <name> player|copy|npc|ref <hex>|held left|right` notes where that thing is now.
An actor is measured by its pelvis, where its body is drawn: a corpse's reference stays where it fell while its ragdoll is dragged.
`log CHECK moved <name> atleast|atmost <units>` passes when it has since moved at least or at most that far.
`log CHECK near <name> <units>` passes when it is now within that many units of the player.
`log CHECK shot differs|same <a> <b> [<percent>] [in <x0> <y0> <x1> <y1>]` compares two of the script's SHOT pictures.
It counts the pixels that changed by more than a tenth of the brightness range, in the whole picture or in that part of it (fractions of its width and height).
differs passes when more than <percent> of them changed (default 0.5), same when fewer did.
`log REQUIRE <check>` is any check used as a precondition: when it does not hold, the run is INVALID, not failed.
Put one before a script's decisive checks for what must have happened for them to mean anything (the copy went down,
the hit was sent, the recording holds the action).

Before the first script, combat AI is switched off and god mode off, by reading what the console answers rather than
by toggling blind: a wild snow bear killed the player in the middle of the first full run (2026-10-02), and a
script that toggles leaves the next one with the opposite setting.
A line `log SHOT <name>` takes a screenshot of the game (DevBench's capture tool, the game's own screenshot) and
saves it as build/.../logs/shots/<script>-<name>.png. `log SHOT <name> of copy|npc|drop|player` first puts a free
camera 260 units south of that thing and a little above it, looking at it, and puts the view back afterwards --
turning the player does not turn a VR view, which follows the headset. Nothing judges a picture unless a
`CHECK shot` compares two.

Every script also fails on anything the client could not find on this setup while it ran.
That is a hook not installed (beyond the three VR leaves out by design), an address id that did not resolve, or a Papyrus native the game never registered.
The game's start-up is checked the same way once, as the first row of the table, together with every SKSE plugin SKSE's log says it could not load.
Each result in the history carries the modlist and the Mod Organizer profile it ran on (SKYRIM_FUS_ROOT picks the modlist, as for headless.ps1).

Checks are asked while the script carries on, so a script puts a wait after each group of them.
"""
import io, json, math, os, re, shutil, subprocess, sys, threading, time, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RELEASE = os.path.join(ROOT, 'build', 'windows', 'x64', 'release')
FUS_ROOT = os.environ.get('SKYRIM_FUS_ROOT') or next(
    (path for path in ('C:/FUS', 'E:/FUS') if os.path.isfile(os.path.join(path, 'ModOrganizer.exe'))),
    'E:/FUS',
)
# The client's folder among MO2's tools: 'Skyrim Together VR' on the host's PC, 'urSovngarde' on another player's.
CLIENT_DIR = next(
    (path for path in (os.path.join(FUS_ROOT, 'tools', name) for name in ('Skyrim Together VR', 'urSovngarde')) if os.path.isfile(os.path.join(path, 'urSovngarde.exe'))),
    os.path.join(FUS_ROOT, 'tools', 'Skyrim Together VR'),
)
CLIENT_LOG = os.path.join(CLIENT_DIR, 'logs', 'tp_client.log')
DEVBENCH = 'http://127.0.0.1:8921/api/tool/'
HEALTH_TOLERANCE = 1.5
PLAYER_TOLERANCE = 2.0
DROP_TOLERANCE = 4.0    # units; a position travels packed to about one unit
SHOTS = os.path.join(RELEASE, 'logs', 'shots')
CAPTURES = os.path.join(FUS_ROOT, 'overwrite', 'SKSE', 'Plugins', 'devbench', 'captures')   # where MO2 puts what DevBench writes under Data

# Order matters: the "away" scripts travel by cell (`cow Tamriel 34 8` and back to 34 -9, Mistwatch) and leave the
# player at the centre of the Mistwatch cell. Not "go to Lydia": she follows the player back, and the next "go to
# Lydia" is then two paces that unload nothing (found 2026-10-02, a session that passed for that reason alone).
ALL = ['live-copy', 'live-npc', 'live-dropmove', 'live-look', 'live-walk', 'live-copy-quiet', 'live-creatures', 'live-npc-away', 'live-away-seeker',
       'live-temp-remove', 'live-temp-reuse', 'live-copy-abandon', 'live-temp-reconnect', 'live-crime', 'live-netch',
       'live-sender', 'live-game-drops', 'live-equip', 'live-factions', 'live-menu-hold', 'live-levelled', 'live-whiterun', 'live-time', 'live-hit-npc', 'live-world-object', 'live-door-range', 'live-npc-indoors', 'live-npc-grace-outdoors', 'live-door-echo', 'live-body-owner', 'live-hands', 'live-death', 'live-load', 'live-load-dead', 'live-load-soak', 'live-door-state']
# live-door-state is last: it leaves the player in Helgen's keep, where the bot of any later script finds nobody.
# live-npc-grace, live-door-join and live-door-join-locked leave the player there too, so they are not in the list: run each by name after a fresh `headless.ps1 up`.
# The sync and animation set (--sync, Tools/VR/README-rig-checks.md): deaths, bodies, locks, levers, followers and doors.
# Some fail on purpose on today's build, each on a known bug its header names. live-door-motion leaves the player in
# Helgen's keep, so it is last; live-door-script does too, so it runs by name after a fresh `headless.ps1 up`.
SYNC = ['live-death-copy', 'live-death-protected', 'live-death-replay', 'live-death-replay-blast', 'live-lock-range', 'live-lever-bridge',
        'live-follower-stays-in', 'live-door-motion']
# Not in the list, run by name: live-seeker-copy-remote and live-seeker-copy-local (the two halves that showed the
# crash of live-copy-abandon needs a copy and its original together).


def tool(name, body, timeout=6):
    req = urllib.request.Request(DEVBENCH + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


# What papyrus() answers for a reference the game does not have (DevBench: "self form '...' not found").
NOT_FOUND = 'ERR not found'
# What it answers when DevBench gave up waiting for the game (HTTP 504): the game is up but did not answer in time.
TIMED_OUT = 'ERR timed out (504)'
# A check that met TIMED_OUT proves nothing: its run is INVALID rather than failed, passed or red.
UNASKED = 'could not ask the game: DevBench timed out (504)'


def papyrus(script, function, form, args=None):
    body = {'action': 'call', 'script': script, 'function': function, 'self': {'form': '0x' + form}}
    if args is not None:
        body['args'] = args
    try:
        return tool('papyrus', body).get('returned')
    except urllib.error.HTTPError as e:
        # A deleted reference is refused with "self form ... not found"; any other refusal says nothing about it.
        if e.code == 504:
            return TIMED_OUT
        try:
            text = e.read().decode(errors='replace')
        except Exception:
            text = ''
        return NOT_FOUND if re.search(r"self form '[^']*' not found", text) else 'ERR HTTPError %d' % e.code
    except Exception as e:
        return 'ERR ' + type(e).__name__


def papyrus_global(script, function, args=None):
    """A global (static) Papyrus function, e.g. HiggsVR.GrabObject: no self."""
    body = {'action': 'call', 'script': script, 'function': function}
    if args is not None:
        body['args'] = args
    try:
        return tool('papyrus', body).get('returned')
    except Exception as e:
        return 'ERR ' + type(e).__name__


def node_xy(form, name):
    """Where a node of an actor's skeleton is drawn, seen from above (world x and y), or None."""
    at = [papyrus_global('NetImmerse', 'GetNodeWorldPosition' + axis, [{'form': '0x' + form}, name, False]) for axis in 'XY']
    return at if all(isinstance(v, (int, float)) for v in at) and any(at) else None


def body_pose(form):
    """Where an actor's body is drawn: its pelvis node, and the pelvis's height above the lower foot, or None.
    Measured against the feet, not the reference: a copy's reference can sit under the ground while its body stands
    on it (a replay elsewhere keeps the recorded heights). On its feet the height is about 60 units, lying flat under 25."""
    def node(name):
        at = [papyrus_global('NetImmerse', 'GetNodeWorldPosition' + axis, [{'form': '0x' + form}, name, False]) for axis in 'XYZ']
        return at if all(isinstance(v, (int, float)) for v in at) and any(at) else None
    pelvis = node('NPC Pelvis [Pelv]')
    feet = [foot for foot in (node('NPC L Foot [Lft ]'), node('NPC R Foot [Rft ]')) if foot]
    if not pelvis or not feet:
        return None
    return pelvis, pelvis[2] - min(foot[2] for foot in feet)


def latest_blade_centre(lines, copy_form):
    """Where the copy's sword body's centre of mass is held, from the client's VRWeaponBody line."""
    centre = None
    for l in lines:
        m = re.search(r'VRWeaponBody: actor ([0-9A-F]+) right weapon body .* centre of mass at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)', l)
        if m and (not copy_form or m.group(1).upper() == copy_form.upper()):
            centre = tuple(float(m.group(i)) for i in (2, 3, 4))
    return centre


# What looks wrong on screen, from the client's own diagnostics: a kind, its line, and the size (units) from which it shows.
# Read after every script (the "jank" line) and judged by "CHECK smooth".
JANK = [
    ('corpse-jump', r"CorpseDiag: \w+'s ragdoll lay (?P<size>\d+) units from where its owner's corpse is; moved there", 40),
    ('died-early', r"CorpseDiag: \w+ is dead here, but its owner's game has not said it died", 0),
    ('point-jump', r'JumpDiag: a buffered point was (?P<size>\d+) units from the one before it', 0),
    ('getup-jump', r'KnockDown: NPC copy \w+ is up again after \d+ ms, (?P<size>\d+) units from where its owner has it', 40),
    ('stuck-down', r'KnockDown: NPC copy \w+ still lies after 15 s', 0),
    ('refused', r"ReplayDiag: refused '", 0),
    ('no-reports', r'QuietStop: copy \w+ had no report for', 0),
    ('ragdoll-apart', r"VRRagdoll: actor \w+: its ragdoll's model-to-world .* is not where the copy is", 0),
    ('door-differs', r'Door \w+: .* here it is in state \d+; left as it is', 0),
    # A copy that lost its 3D while its actor stays is torn down and made again: a body near the player vanishes and reappears.
    ('copy-lost-3d', r'CopyGone: \w+ removed as remote; the actor is still there, no 3D', 0),
]


def jank(lines, kinds=None):
    """The jank among these client lines: kind -> (how many, the largest size, the first such line)."""
    found = {}
    for kind, pattern, least in JANK:
        if kinds and kind not in kinds:
            continue
        rx = re.compile(pattern)
        for l in lines:
            m = rx.search(l)
            size = int(m.group('size')) if m and 'size' in rx.groupindex else 0
            if not m or size < least:
                continue
            count, largest, first = found.get(kind, (0, 0, l))
            found[kind] = (count + 1, max(largest, size), first)
    return found


def jank_text(found):
    return ', '.join('%s %d%s' % (kind, count, (' (up to %d units)' % largest) if largest else '') for kind, (count, largest, _) in found.items()) or 'none'


# The nodes "DO record" samples on an actor: enough to tell how a body stands, falls and lies.
BODY_NODES = {'pelvis': 'NPC Pelvis [Pelv]', 'head': 'NPC Head [Head]', 'lhand': 'NPC L Hand [LHnd]', 'rhand': 'NPC R Hand [RHnd]',
              'lfoot': 'NPC L Foot [Lft ]', 'rfoot': 'NPC R Foot [Rft ]'}


class Recording:
    """What "DO record" samples in the background while the script goes on: (seconds since the epoch, sample) pairs.
    An actor's sample is where its nodes are; any other reference's is its open state (a door's or a bridge's, 1 to 4).
    DevBench answers about 27 Papyrus calls a second, one per game frame. So a body is sampled by its pelvis and its
    reference's height (4 calls, about 6 a second) until the last 2 s, which read all of BODY_NODES for its resting pose."""

    def __init__(self, form, seconds):
        self.form = form
        self.body = papyrus_global('NetImmerse', 'GetNodeWorldPositionZ', [{'form': '0x' + form}, BODY_NODES['pelvis'], False]) not in (None, 0, 0.0)
        # Only a door shows its moving state (opening, closing); a bridge or gate run by a lever goes from one end to the other.
        base = None if self.body else papyrus('ObjectReference', 'GetBaseObject', form)
        self.door = isinstance(base, dict) and papyrus('Form', 'GetType', base.get('formId', '').replace('0x', '')) == 29
        self.samples = []
        self.thread = threading.Thread(target=self.sample, args=(seconds,), daemon=True)
        self.thread.start()

    def sample(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            at = time.time()
            if self.body:
                nodes = {}
                for key, node in BODY_NODES.items() if at >= end - 2.0 else [('pelvis', BODY_NODES['pelvis'])]:
                    xyz = [papyrus_global('NetImmerse', 'GetNodeWorldPosition' + axis, [{'form': '0x' + self.form}, node, False]) for axis in 'XYZ']
                    nodes[key] = tuple(xyz) if all(isinstance(v, (int, float)) for v in xyz) and any(xyz) else None
                if len(nodes) == 1:
                    ground = papyrus('ObjectReference', 'GetPositionZ', self.form)
                    nodes['ground'] = ground if isinstance(ground, (int, float)) else None
                if nodes['pelvis']:
                    self.samples.append((at, nodes))
            else:
                state = papyrus('ObjectReference', 'GetOpenState', self.form)
                if isinstance(state, int):
                    self.samples.append((at, state))
                time.sleep(0.05)

    def done(self):
        """The samples, once the sampling is over (a check waits for it)."""
        self.thread.join()
        return self.samples


def body_height(nodes):
    """How high the pelvis is above the lower foot, or above its reference (where it stands) in a quick sample:
    about 60 on its feet, under 40 down, under 25 lying flat."""
    feet = [nodes[key][2] for key in ('lfoot', 'rfoot') if nodes.get(key)]
    if feet:
        return nodes['pelvis'][2] - min(feet)
    return nodes['pelvis'][2] - nodes['ground'] if nodes.get('ground') is not None else None


def pose_signature(nodes):
    """How a body is laid out, whichever way it faces: the distance between each two of its sampled nodes."""
    keys = [key for key in BODY_NODES if nodes.get(key)]
    return {(a, b): math.dist(nodes[a], nodes[b]) for i, a in enumerate(keys) for b in keys[i + 1:]}


def resting_pose(samples):
    """The pose a body came to rest in: the node distances averaged over its last whole-body samples."""
    last = [nodes for at, nodes in samples if 'head' in nodes]
    signatures = [pose_signature(nodes) for nodes in last]
    pairs = set.intersection(*(set(s) for s in signatures)) if signatures else set()
    return {pair: sum(s[pair] for s in signatures) / len(signatures) for pair in pairs}


def death_time(form, lines):
    """When this game killed that actor: its owner's death applied to a copy, or this game's own actor's death."""
    found = None
    for l in lines:
        if re.search(r'Death sync: (remote actor %s set dead by its owner|local actor %s is now dead)' % (form, form), l, re.I):
            found = log_time(l)
    return found


def fall_delay(recording, lines):
    """Seconds from that actor's death in this game to its first sample with the body down, or a reason it has none."""
    died = death_time(recording.form, lines)
    if died is None:
        return None, 'no death of %s in the log' % recording.form
    down = next((at for at, nodes in recording.done() if at >= died - 0.1 and (body_height(nodes) or 99) < 40.0), None)
    return (down - died, '') if down is not None else (None, '%s never down in its samples after its death' % recording.form)


def log_time(line):
    """When the client wrote a line, in seconds since the epoch, or None."""
    m = re.match(r'\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\.(\d{3})\]', line)
    return time.mktime(time.strptime(m.group(1), '%Y-%m-%d %H:%M:%S')) + int(m.group(2)) / 1000.0 if m else None


def session_lines():
    """This game session's part of the client log, also once the log has rotated (5 MB) during the session."""
    def read(path):
        try:
            with io.open(path, encoding='utf-8', errors='replace') as f:
                return f.read().splitlines()
        except OSError:
            return []

    def last_start(lines):
        found = None
        for i, l in enumerate(lines):
            if 'Skyrim Together client, build' in l:
                found = i
        return found

    lines = read(CLIENT_LOG)
    start = last_start(lines)
    if start is not None:
        return lines[start:]
    # The session's first line is in the rotated file: the session is its end and all of the current file.
    older = read(CLIENT_LOG.replace('tp_client.log', 'tp_client.1.log'))
    older_start = last_start(older)
    return (older[older_start:] if older_start is not None else []) + lines


def client_build(lines):
    """The client build the session runs, from its first line."""
    for l in lines[:50]:
        m = re.search(r'Skyrim Together client, build (\S+)', l)
        if m:
            return m.group(1)
    return '?'


def health_problems(lines):
    """What the client could not find on this setup in these lines: hooks, address ids, Papyrus natives, each named once."""
    problems = []
    for l in lines:
        # The hooks VR leaves out by design are not tried there at all, so every line of this kind is a real miss.
        m = re.search(r'Hook (\S+) not installed: its game address did not resolve', l)
        if m:
            problems.append('hook %s not installed' % m.group(1))
        m = re.search(r'Address id (\d+) did not resolve on this build', l)
        if m:
            problems.append('address id %s did not resolve' % m.group(1))
        m = re.search(r'Papyrus: (\S+) not found among', l)
        if m:
            problems.append('Papyrus %s not found' % m.group(1))
    return list(dict.fromkeys(problems))


def skse_problems():
    """The SKSE plugins this game start could not load, from SKSE's own log (one file for every modlist, rewritten at each start)."""
    import ctypes

    documents = ctypes.create_unicode_buffer(260)
    ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, documents)  # CSIDL_PERSONAL: Documents, wherever it was moved
    try:
        with io.open(os.path.join(documents.value, 'My Games', 'Skyrim VR', 'SKSE', 'sksevr.log'), encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return ['SKSE wrote no log']
    found = ['SKSE did not load %s (error %s)' % (os.path.basename(path), code) for path, code in re.findall(r"couldn't load plugin (.+?) \(Error (\d+)\)", text)]
    found += ['SKSE found %s incompatible' % os.path.basename(path) for path in re.findall(r'plugin (\S+\.dll) .*reported as incompatible', text)]
    return list(dict.fromkeys(found))


def setup():
    """The modlist (the folder Mod Organizer runs from) and the profile it has selected."""
    profile = '?'
    try:
        with io.open(os.path.join(FUS_ROOT, 'ModOrganizer.ini'), encoding='utf-8', errors='replace') as f:
            for l in f:
                m = re.match(r'selected_profile=(?:@ByteArray\()?(.+?)\)?\s*$', l)
                if m:
                    profile = m.group(1)
                    break
    except OSError:
        pass
    return os.path.basename(os.path.normpath(FUS_ROOT)), profile


SETUP = setup()
HISTORY = os.environ.get('LIVE_CHECK_HISTORY') or os.path.join(os.path.dirname(ROOT), 'TiltedEvolution-toolbox', 'test-history', 'live.jsonl')


def record(entry):
    """One line per script run, so a test's record can be read later: flaky, red without its fix, green with it."""
    entry.setdefault('modlist', SETUP[0])
    entry.setdefault('profile', SETUP[1])
    try:
        os.makedirs(os.path.dirname(HISTORY), exist_ok=True)
        with io.open(HISTORY, 'a', encoding='utf-8') as f:
            f.write(json.dumps(entry) + '\n')
    except OSError as e:
        print('(history not written: %s)' % e, flush=True)


def stop_release_servers():
    """Stops the servers started from the release folder, never one started elsewhere (such as the owner's own)."""
    command = ("Get-Process urSovngardeServer, SkyrimTogetherServer -ErrorAction SilentlyContinue | "
               "Where-Object { $_.Path -and $_.Path.StartsWith('%s', 'OrdinalIgnoreCase') } | Stop-Process -Force" % RELEASE)
    subprocess.run(['powershell', '-NoProfile', '-Command', command], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def restart_server(while_down=None):
    """Server down and up again; returns how long the game took to be back in, or raises. `while_down` is a console
    command run once the game has noticed the server is gone, before it comes back."""
    before = len(session_lines())
    stop_release_servers()
    started = time.time()
    # The game only notices once its connection times out; a server that is back before that is a server that never
    # left, as far as the game can tell.
    deadline = started + 60
    while time.time() < deadline:
        if any('Disconnected' in l or 'disconnected' in l for l in session_lines()[before:]):
            break
        time.sleep(1)
    else:
        raise RuntimeError('the game never noticed the server was gone')
    noticed = time.time() - started
    if while_down:
        tool('console', {'action': 'exec', 'command': while_down})
        print('   (console while the server is down: %s)' % while_down, flush=True)
        time.sleep(3)
    subprocess.Popen([os.path.join(RELEASE, 'urSovngardeServer.exe')], cwd=RELEASE, creationflags=0x00000010 | 0x00000200,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 150
    while time.time() < deadline:
        new = session_lines()[before:]
        tries = [i for i, l in enumerate(new) if 'VRConnectService: connecting to' in l]
        if tries and any('Grid change: reporting centre' in l for l in new[tries[-1]:]):
            return noticed, time.time() - started
        time.sleep(1)
    raise RuntimeError('the game did not reconnect in 150 s')


def form_of_server_id(server_id):
    """The actor the game runs for a server id; the newest line wins.

    "New entity remotely managed" is the binding itself, so it is preferred to the spawn line: a copy placed at the edge
    of the loaded cells can be gone a tenth of a second after the spawn made it, and the client then makes another one
    for the same character (`Spawned character for entity`), which only the binding line names. Following the spawn
    line alone, live-copy asked five times about a copy that no longer existed (2026-10-03, after live-spawn-burst)."""
    form = None
    wanted = server_id.upper()
    for l in session_lines():
        m = (re.search(r'CharacterSpawnRequest, server id: ([0-9A-Fa-f]+), form id: ([0-9A-Fa-f]+)', l)
             or re.search(r'New entity remotely managed, form id: ([0-9A-Fa-f]+), server id: ([0-9A-Fa-f]+)', l))
        if not m:
            continue
        if 'New entity' in l:
            if m.group(2).upper() == wanted:
                form = m.group(1).upper()
        elif m.group(1).upper() == wanted:
            form = m.group(2).upper()
    return form


def dropped_ref():
    """The reference the game placed for the newest drop it was sent, or None once it has been removed."""
    ref = None
    for l in session_lines():
        m = re.search(r'DroppedItem: placed \d+ \(.*\) as ([0-9A-F]{8})', l)
        if m:
            ref = m.group(1)
        elif ref and ('removed ' + ref) in l:
            ref = None
    return ref


def position(form):
    p = [papyrus('ObjectReference', f, form) for f in ('GetPositionX', 'GetPositionY', 'GetPositionZ')]
    return p if all(isinstance(v, (int, float)) for v in p) else None


def ensure(command, wanted):
    """Puts a console toggle in a known state: runs it, reads the answer, and runs it once more if it went the wrong way."""
    for _ in range(2):
        try:
            tool('console', {'action': 'exec', 'command': command, 'capture': True})
            time.sleep(1.2)
            answer = ' '.join(tool('console', {'action': 'read'}).get('lines', []))
        except Exception:
            return False   # the game is gone; the caller reports it, and the run goes on to its table
        if wanted.lower() in answer.lower():
            return True
    return False


def set_velocities(frames, side):
    """Gives the moving controller the velocity its frames imply, the others none.

    Every frame starts from the resting frame DevBench last saw, velocity included: a scripted swing then moves a
    controller whose reported speed is zero, and PLANCK, which takes a hit's force from that speed, never hits."""
    for i, f in enumerate(frames):
        for d in ('hmd', 'left', 'right'):
            f[d]['velocity'] = [0.0, 0.0, 0.0]
            f[d]['angularVelocity'] = [0.0, 0.0, 0.0]
        if i + 1 < len(frames):
            here, there = f[side]['matrix'], frames[i + 1][side]['matrix']
            seconds = max(0.001, (frames[i + 1]['tMs'] - f['tMs']) / 1000.0)
            f[side]['velocity'] = [(there[3] - here[3]) / seconds, (there[7] - here[7]) / seconds, (there[11] - here[11]) / seconds]
    return frames


def hand_path(side, waypoints):
    """Queues one controller's path (DevBench `input vrTrackedSet`, 20 ms frames built on the current frame): waypoints
    of (ms, x, y, z, grip) in room space (metres: x right, y up, -z forward), straight lines between them, the grip
    button (OpenVR button 2) held where the waypoint ending a stretch says so. The headset stands at 1.6 m looking
    forward, the other hand rests low at its side, as in "DO swing". Returns DevBench's answer."""
    other = 'left' if side == 'right' else 'right'
    base = tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
    frames = []
    end = waypoints[-1][0]
    for i in range(end // 20 + 1):
        t = i * 20
        k = next((k for k in range(1, len(waypoints)) if waypoints[k][0] >= t), len(waypoints) - 1)
        (t0, x0, y0, z0, _), (t1, x1, y1, z1, grip) = waypoints[k - 1], waypoints[k]
        a = min(1.0, max(0.0, (t - t0) / float(t1 - t0))) if t1 > t0 else 1.0
        f = json.loads(json.dumps(base))
        f.pop('seq', None)
        f['tMs'] = t
        f['hmd']['matrix'] = [1, 0, 0, 0, 0, 1, 0, 1.6, 0, 0, 1, 0]
        f[side]['matrix'] = [1, 0, 0, x0 + (x1 - x0) * a, 0, 1, 0, y0 + (y1 - y0) * a, 0, 0, 1, z0 + (z1 - z0) * a]
        f[other]['matrix'] = [1, 0, 0, -0.25 if other == 'left' else 0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
        for d in ('hmd', 'left', 'right'):
            f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
            if f[d].get('index') is None:
                f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
            for key in ('velocity', 'angularVelocity'):
                f[d].setdefault(key, [0.0, 0.0, 0.0])
            if d != 'hmd' and not f[d].get('controller'):
                f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
        for d in ('left', 'right'):
            f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
        f[side]['controller']['pressed'] = 4 if grip else 0
        f[side]['controller']['touched'] = 4 if grip else 0
        frames.append(f)
    set_velocities(frames, side)
    return tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-check'}, timeout=15)


def both_grips(ms):
    """Both controllers resting at the player's sides with both grip buttons (OpenVR button 2) held for ms, then let go
    (DevBench `input vrTrackedSet`, 20 ms frames built on the current frame, the headset at 1.6 m looking forward)."""
    base = tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
    frames = []
    steps = ms // 20 + 1
    for i in range(steps + 1):
        held = i < steps
        f = json.loads(json.dumps(base))
        f.pop('seq', None)
        f['tMs'] = i * 20
        f['hmd']['matrix'] = [1, 0, 0, 0, 0, 1, 0, 1.6, 0, 0, 1, 0]
        f['left']['matrix'] = [1, 0, 0, -0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
        f['right']['matrix'] = [1, 0, 0, 0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
        for d in ('hmd', 'left', 'right'):
            f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
            if f[d].get('index') is None:
                f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
            for key in ('velocity', 'angularVelocity'):
                f[d].setdefault(key, [0.0, 0.0, 0.0])
            if d != 'hmd' and not f[d].get('controller'):
                f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
        for d in ('left', 'right'):
            f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
            f[d]['controller']['pressed'] = 4 if held else 0
            f[d]['controller']['touched'] = 4 if held else 0
        frames.append(f)
    return tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-check'}, timeout=15)


def screenshot(name, target=None):
    """The game's own screenshot through DevBench, moved out of the mod manager's overwrite folder.

    With a target (a world position), the picture is taken from a free camera looking at it."""
    if target:
        eye = (target[0], target[1] - 260.0, target[2] + 150.0)
        aim = (target[0], target[1], target[2] + 60.0)          # about the middle of a body
        dx, dy, dz = aim[0] - eye[0], aim[1] - eye[1], aim[2] - eye[2]
        yaw = math.atan2(dx, dy)                                 # 0 is north (+y), growing towards east (+x)
        pitch = math.atan2(-dz, math.hypot(dx, dy))              # positive looks down
        tool('camera', {'action': 'freecam', 'on': True})
        tool('camera', {'action': 'drive', 'x': eye[0], 'y': eye[1], 'z': eye[2], 'pitch': pitch, 'yaw': yaw})
        time.sleep(0.5)                                          # a rendered frame or two before the picture
    try:
        r = tool('capture', {'kind': 'native', 'checkpointId': name, 'cleanup': True}, timeout=40)
    finally:
        if target:
            tool('camera', {'action': 'freecam', 'on': False})
    if not r.get('ok'):
        return None
    os.makedirs(SHOTS, exist_ok=True)
    target = os.path.join(SHOTS, name + '.png')
    for base, _, files in os.walk(CAPTURES):
        if name + '.png' in files:
            # Copy and delete, not a rename: the overwrite folder and the build folder are on different drives.
            shutil.copyfile(os.path.join(base, name + '.png'), target)
            os.remove(os.path.join(base, name + '.png'))
            return target
    return None


def changed_share(path_a, path_b, region=None):
    """The percentage of pixels that changed by more than 24 of 255 in a colour between two pictures of one size.
    A region (x0, y0, x1, y1) in fractions of the width and height compares only that part."""
    from PIL import Image
    import numpy

    a = numpy.asarray(Image.open(path_a).convert('RGB'), dtype=numpy.int16)
    b = numpy.asarray(Image.open(path_b).convert('RGB'), dtype=numpy.int16)
    if a.shape != b.shape:
        raise ValueError('the pictures differ in size: %s and %s' % (a.shape[:2], b.shape[:2]))
    if region:
        height, width = a.shape[:2]
        x0, y0, x1, y1 = int(region[0] * width), int(region[1] * height), int(region[2] * width), int(region[3] * height)
        a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
    return 100.0 * float((numpy.abs(a - b).max(axis=2) > 24).mean())


class Run:
    def __init__(self, script):
        self.script = script
        self.drop_mark = None
        self.slide_mark = 0
        self.shots = []
        self.bot_id = None     # the bot character's server id
        self.npc_id = None     # the NPC it registered
        self.mark = None
        self.spot = None       # "CHECK spot mark": the player's place (worldspace or cell) and position
        self.heal_rate = None
        self.results = []
        # Preconditions that did not hold, and setup the bot could not do: the run then proves nothing either way.
        self.invalid = []
        # The bot's output so far, for "bot logged".
        self.bot_lines = []
        # The NPC copy's pose samples from "DO watch npc": (seconds since the start, pelvis x y z, body height).
        self.watch = []
        self.watch_start = 0.0
        self.watch_form = None
        # "DO record <name> ...": name -> Recording, for "pose", "fall", "place", "still" and "door ... once".
        self.records = {}
        # "DO mark": name -> (what it is, where it was).
        self.marks = {}
        # "CHECK bounty <faction> mark": faction -> the player's bounty with it then.
        self.bounty_marks = {}
        # "CHECK health <actor> mark": actor -> its health then; "CHECK health <actor> lost <name>": name -> what it lost since.
        self.health_marks = {}
        self.losses = {}

    def subject(self, who):
        sid = self.bot_id if who == 'copy' else self.npc_id
        if not sid:
            return None, 'the bot never reported that id'
        form = form_of_server_id(sid)
        if not form:
            return None, 'the game logged no spawn for server id %s' % sid
        return form, None

    def report(self):
        """The bot's last "report": actors (server id -> (Skyrim.esm reference or None, dead, "ours" or "theirs", position or None)),
        doors (reference -> open) and locks (reference -> locked); three Nones before the bot has given one."""
        block, last = None, None
        for line in self.bot_lines:
            m = re.search(r'\[report\] (.*)$', line.rstrip())
            if not m:
                continue
            words = m.group(1).split()
            block = block or ({}, {}, {})
            if words[0] == 'end':
                last, block = block, None
            elif words[0] == 'actor' and len(words) >= 8:
                at = tuple(float(v) for v in words[7:10]) if words[7] != 'unknown' else None
                block[0][words[1].upper()] = (None if words[3] == 'none' else words[3].upper(), words[4] == 'dead', words[5], at)
            elif words[0] == 'door':
                block[1][words[1].upper()] = words[2] == 'open'
            elif words[0] == 'lock':
                block[2][words[1].upper()] = words[2] == 'locked'
        return last or (None, None, None)

    def thing(self, words):
        """What "DO mark" names, as a fixed reference: ('player',), ('actor', form) or ('ref', form), or a reason it has none."""
        if words[0] == 'player':
            return ('player',), None
        if words[0] in ('copy', 'npc'):
            form, why = self.subject(words[0])
            return (('actor', form), None) if form else (None, why)
        if words[0] == 'ref' and len(words) > 1:
            return ('ref', words[1]), None
        if words[0] == 'held' and len(words) > 1:
            # The object in that hand now, kept as a reference so it can still be measured once it is let go.
            held = papyrus_global('HiggsVR', 'GetGrabbedObject', [words[1] == 'left'])
            if not isinstance(held, dict) or not held.get('formId'):
                return None, 'the %s hand holds nothing (%s)' % (words[1], held)
            return ('ref', '%X' % (int(str(held['formId']), 16) & 0xFFFFFFFF)), None
        return None, 'unknown thing: %s' % ' '.join(words)

    @staticmethod
    def where(thing):
        """Where a marked thing is now: an actor's pelvis when its body is drawn, else its reference."""
        if thing[0] == 'player':
            return position('14')
        if thing[0] == 'actor':
            pose = body_pose(thing[1])
            return pose[0] if pose else position(thing[1])
        return position(thing[1])

    def check(self, text, require=False):
        words = text.split()
        who, what = words[0], words[1]
        ok, detail = False, ''
        try:
            if who == 'game':
                # "game alive": it still answers, and has run frames since the last time it was asked.
                health = json.loads(urllib.request.urlopen('http://127.0.0.1:8921/api/health', timeout=6).read().decode())
                ok = bool(health.get('ok'))
                detail = 'frame %s, %s' % (health.get('frame'), health.get('lastLifecycle'))
                if ok and what == 'inworld':
                    menus = tool('menu', {'action': 'list'}).get('openMenus', [])
                    loaded = tool('inspect', {'kind': 'state'}).get('playerLoaded')
                    at_menu = [m for m in menus if m in ('Main Menu', 'Loading Menu')]
                    ok = bool(loaded) and not at_menu
                    detail += ', player loaded %s, menus %s' % (loaded, menus)
            elif who == 'drop' and words[1:4] == ['on', 'copy', 'weapon']:
                # "drop on copy weapon <units>": the item put above the copy's weapon by "DO drop" is still no more than
                # that far below the weapon -- something physical held it -- rather than lying on the ground.
                units = float(words[4]) if len(words) > 4 else 20.0
                if not getattr(self, 'drop_test', None):
                    detail = 'nothing was dropped onto the weapon'
                else:
                    ref, drawn = self.drop_test
                    at = position(ref)
                    if not at:
                        detail = 'the dropped item %s has no position' % ref
                    else:
                        ok = at[2] >= drawn[2] - units
                        detail = 'item %s at z %.0f, the weapon at z %.0f: %.0f units %s it' % (ref, at[2], drawn[2], abs(at[2] - drawn[2]), 'above' if at[2] >= drawn[2] else 'below')
            elif who == 'drop' and words[1] == 'fell':
                # "drop fell <units>": the item put by "DO drop" fell more than that far below where it was aimed --
                # nothing physical there (the control for "drop on copy weapon").
                units = float(words[2]) if len(words) > 2 else 20.0
                if not getattr(self, 'drop_test', None):
                    detail = 'nothing was dropped'
                else:
                    ref, drawn = self.drop_test
                    at = position(ref)
                    if not at:
                        detail = 'the dropped item %s has no position' % ref
                    else:
                        ok = at[2] < drawn[2] - units
                        detail = 'item %s at z %.0f, aimed at z %.0f: %.0f units %s it' % (ref, at[2], drawn[2], abs(at[2] - drawn[2]), 'above' if at[2] >= drawn[2] else 'below')
            elif who == 'distinct':
                # "distinct <most> <regular expression with one group>": how many different things the client has
                # logged since the script began -- "Spawn Actor: (\w+), and NPC Seeker" counts the Seekers it has seen.
                most = int(words[1])
                rx = re.compile(' '.join(words[2:]))
                found = sorted({m.group(1) for m in (rx.search(l) for l in session_lines()[self.log_from:]) if m})
                ok = 0 < len(found) <= most
                detail = '%d: %s' % (len(found), ', '.join(found))
            elif who == 'ref' and words[2] == '3d' and words[3:4] == ['steady']:
                # "ref <hex> 3d steady <seconds>": the reference's 3D stays loaded the whole time, sampled as fast as
                # DevBench answers. An object built again on every move blinks: some samples find it unloaded.
                seconds = float(words[4]) if len(words) > 4 else 3.0
                end = time.time() + seconds
                samples = gaps = 0
                while time.time() < end:
                    samples += 1
                    if papyrus('ObjectReference', 'Is3DLoaded', words[1]) is not True:
                        gaps += 1
                ok = samples > 0 and gaps == 0
                detail = 'reference %s: %d of %d samples over %.0f s found its 3D unloaded' % (words[1], gaps, samples, seconds)
            elif who == 'ref' and words[2] == '3d':
                # "ref <hex> 3d": the game has that reference's 3D loaded (an object it shows).
                loaded = papyrus('ObjectReference', 'Is3DLoaded', words[1])
                ok = loaded is True
                detail = 'reference %s, Is3DLoaded %s' % (words[1], loaded)
            elif who == 'ref' and words[2] == 'base':
                # "ref <hex> base <hex>": the base the game's levelled actor at that reference has now.
                form = words[1]
                base = papyrus('Actor', 'GetLeveledActorBase', form)
                got = base.get('formId', '') if isinstance(base, dict) else str(base)
                ok = isinstance(base, dict) and int(got, 16) & 0xFFFFFF == int(words[3], 16) & 0xFFFFFF
                detail = 'reference %s, levelled base %s (%s)' % (form, got, base.get('editorId', '') if isinstance(base, dict) else '')
            elif who == 'ref' and words[2] == 'near':
                # "ref <hex> near <x> <y> <z> <units> [flat]": a reference of the game's own is that close to that place.
                # "ref <hex> near player <units> [flat]": that close to where the player stands now.
                # "flat" measures from above only: on MGO a held object's reference sits on the ground while its 3D is held up.
                form = words[1]
                at = [papyrus('ObjectReference', 'GetPosition' + axis, form) for axis in 'XYZ']
                if words[3] == 'player':
                    words = words[:3] + [str(v) for v in (position('14') or (0.0, 0.0, 0.0))] + words[4:]
                wanted = [float(v) for v in words[3:6]]
                flat = words[-1] == 'flat'
                known = all(isinstance(v, (int, float)) for v in at)
                apart = (math.dist(at[:2], wanted[:2]) if flat else math.dist(at, wanted)) if known else None
                ok = known and apart <= float(words[6])
                detail = 'reference %s at %s, %s units from (%s)%s' % (form, [round(v) for v in at] if known else at,
                                                                     round(apart) if known else '?', ', '.join(words[3:6]), ', seen from above' if flat else '')
            elif who == 'scene':
                # "scene <hex> playing": a scene of the game's own plays.
                playing = papyrus('Scene', 'IsPlaying', words[1])
                ok = playing is True
                detail = 'scene %s, IsPlaying %s' % (words[1], playing)
            elif who == 'ref' and words[2] == 'cell':
                # "ref <hex> cell same|other": in the cell the player stands in, or in another one.
                # Positions alone cannot tell: an interior's coordinates are as good as any outside.
                def cell_of(form):
                    cell = papyrus('ObjectReference', 'GetParentCell', form)
                    return cell.get('formId') if isinstance(cell, dict) else None
                mine, players = cell_of(words[1]), cell_of('14')
                known = mine is not None and players is not None
                ok = known and (mine == players) == (words[3] == 'same')
                detail = 'reference %s in cell %s, the player in %s' % (words[1], mine, players)
            elif who == 'ref' and words[2] in ('open', 'closed'):
                # "ref <hex> open|closed": a door of the game's own is open (or opening), or closed (or closing).
                state = papyrus('ObjectReference', 'GetOpenState', words[1])
                ok = state in ((1, 2) if words[2] == 'open' else (3, 4))
                detail = 'door %s, GetOpenState %s (1 open, 2 opening, 3 closed, 4 closing)' % (words[1], state)
            elif who == 'ref' and words[2] in ('enabled', 'disabled'):
                # "ref <hex> enabled|disabled": a reference of the game's own is switched on, or off (IsDisabled).
                disabled = papyrus('ObjectReference', 'IsDisabled', words[1])
                ok = isinstance(disabled, bool) and disabled is (words[2] == 'disabled')
                detail = 'reference %s, IsDisabled %s' % (words[1], disabled)
            elif who == 'ref' and words[2] in ('locked', 'unlocked'):
                # "ref <hex> locked|unlocked": a door or container of the game's own is locked, or not.
                locked = papyrus('ObjectReference', 'IsLocked', words[1])
                ok = isinstance(locked, bool) and locked is (words[2] == 'locked')
                detail = 'reference %s, IsLocked %s' % (words[1], locked)
            elif who == 'ref':
                # "ref <hex> alive|dead": a reference of the game's own, asked directly.
                form, state = words[1], words[2]
                dead = papyrus('Actor', 'IsDead', form)
                ok = isinstance(dead, bool) and dead is (state == 'dead')
                detail = 'reference %s, IsDead %s' % (form, dead)
            elif who in ('moved', 'near'):
                # "moved <name> atleast|atmost <units>", "near <name> <units>": against what "DO mark <name>" noted.
                if what not in self.marks:
                    detail = 'nothing was marked as %s' % what
                else:
                    thing, was = self.marks[what]
                    now = self.where(thing)
                    if not now:
                        detail = '%s has no position now' % what
                    elif who == 'moved':
                        units = float(words[3])
                        travelled = math.dist(was, now)
                        ok = travelled >= units if words[2] == 'atleast' else travelled <= units
                        detail = '%s moved %.0f units, from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)' % (what, travelled, *was, *now)
                    else:
                        player = position('14')
                        units = float(words[2])
                        far = math.dist(now, player) if player else float('inf')
                        ok = far <= units
                        detail = '%s %.0f units from the player' % (what, far)
            elif who == 'shot' and what in ('differs', 'same'):
                # "shot differs|same <a> <b> [<percent>] [in <x0> <y0> <x1> <y1>]": two of this script's SHOT pictures.
                rest = words[4:]
                region = None
                if 'in' in rest:
                    at = rest.index('in')
                    region = [float(v) for v in rest[at + 1:at + 5]]
                    rest = rest[:at]
                limit = float(rest[0]) if rest else 0.5
                a, b = (os.path.join(SHOTS, '%s-%s.png' % (self.script, name)) for name in words[2:4])
                share = changed_share(a, b, region)
                ok = share > limit if what == 'differs' else share < limit
                detail = '%.2f%% of the pixels changed%s (%s %.2f%%)' % (share, ' in that part' if region else '', 'above' if what == 'differs' else 'below', limit)
            elif who == 'bounty':
                # "bounty <faction hex> mark", then "bounty <faction hex> raised": the player's bounty with that crime
                # faction (Faction.GetCrimeGold) went up since the mark.
                gold = papyrus('Faction', 'GetCrimeGold', words[1])
                if words[2] == 'mark':
                    self.bounty_marks[words[1]] = gold
                    ok = isinstance(gold, (int, float))
                    detail = 'bounty with %s is %s' % (words[1], gold)
                else:
                    before = self.bounty_marks.get(words[1])
                    ok = isinstance(gold, (int, float)) and isinstance(before, (int, float)) and gold > before
                    detail = 'bounty with %s went from %s to %s' % (words[1], before, gold)
            elif who == 'health' and words[2] in ('mark', 'lost'):
                # "health <hex|placed> mark", then "health <hex|placed> lost <name>": the health that actor lost since the
                # mark, kept as <name> for "lost <a> over <b>".
                form = getattr(self, 'placed', None) if what == 'placed' else what
                health = papyrus('Actor', 'GetActorValue', form, ['Health']) if form else 'ERR nothing placed'
                if words[2] == 'mark':
                    self.health_marks[what] = health
                    ok = isinstance(health, (int, float))
                    detail = '%s at %s health' % (what, health)
                else:
                    before = self.health_marks.get(what)
                    known = isinstance(health, (int, float)) and isinstance(before, (int, float))
                    self.losses[words[3]] = before - health if known else None
                    ok = known and health < before
                    detail = '%s went from %s to %s health' % (what, before, health)
            elif who == 'lost':
                # "lost <a> over <b> <low> <high>": the health lost as <a>, divided by that lost as <b>, is between low and high.
                a, b = self.losses.get(words[1]), self.losses.get(words[3])
                ratio = a / b if a and b else None
                ok = ratio is not None and float(words[4]) <= ratio <= float(words[5])
                detail = '%s lost %s, %s lost %s: %s' % (words[1], a, words[3], b, '%.3f' % ratio if ratio is not None else '?')
            elif who == 'gamehour':
                # "gamehour away <hours>": the game clock (global 38) is not at that hour any more.
                hour = papyrus('GlobalVariable', 'GetValue', '38')
                ok = isinstance(hour, (int, float)) and abs(float(hour) - float(words[2])) > 0.5
                detail = 'GameHour %s' % hour
            elif who == 'fewer':
                # "fewer <n> <regular expression>": the client has written fewer than n such lines since the script began.
                rx = re.compile(' '.join(words[2:]))
                hits = [l for l in session_lines()[self.log_from:] if rx.search(l)]
                ok = len(hits) < int(words[1])
                detail = '%d lines' % len(hits)
            elif who == 'smooth':
                # "smooth all | smooth <kind> ... | smooth except <kind> ...": the client logged no jank of those kinds (JANK) since the script began.
                kinds = [kind for kind, _, _ in JANK]
                if what == 'except':
                    kinds = [kind for kind in kinds if kind not in words[2:]]
                elif what != 'all':
                    kinds = words[1:]
                found = jank(session_lines()[self.log_from:], kinds)
                ok = not found
                detail = jank_text(found) + ('; first: ' + next(iter(found.values()))[2][35:200].strip() if found else '')
            elif who in ('pose', 'fall', 'place', 'still', 'door') and what not in self.records:
                detail = 'nothing was recorded as %s ("DO record")' % what
            elif who == 'pose':
                # "pose <b> like <a> <units>": the body recorded as b came to rest laid out as a did: the distances between its
                # sampled nodes (BODY_NODES, whichever way it faces) differ by at most that much on average.
                other = self.records.get(words[3])
                mine, theirs = (resting_pose(r.done()) if r and r.body and r.done() else None for r in (self.records[what], other))
                if not mine or not theirs:
                    detail = 'no resting pose for %s' % (what if not mine else words[3])
                else:
                    diffs = sorted(((abs(mine[p] - theirs[p]), p) for p in mine if p in theirs), reverse=True)
                    mean = sum(d for d, _ in diffs) / len(diffs)
                    ok = mean <= float(words[4])
                    pelvis = [self.records[name].done()[-1][1]['pelvis'] for name in (what, words[3])]
                    detail = 'node distances differ by %.0f units on average (limit %s); most: %s; pelvis %s at %s, %s at %s' % (
                        mean, words[4], ', '.join('%s-%s %.0f' % (a, b, d) for d, (a, b) in diffs[:3]),
                        what, '(%.0f, %.0f, %.0f)' % pelvis[0], words[3], '(%.0f, %.0f, %.0f)' % pelvis[1])
            elif who == 'fall':
                # "fall <b> within <ms>": the body recorded as b was down that soon after this game killed it.
                # "fall <b> like <a> <ms>": it took at most that much longer, or shorter, than a's from death to down.
                lines = session_lines()[self.log_from:]
                mine, why = fall_delay(self.records[what], lines)
                if words[2] == 'within':
                    ok = mine is not None and mine * 1000 <= float(words[3])
                    detail = why or '%s down %.0f ms after its death (limit %s)' % (what, mine * 1000, words[3])
                else:
                    theirs, why_theirs = fall_delay(self.records[words[3]], lines) if words[3] in self.records else (None, 'nothing recorded as ' + words[3])
                    ok = mine is not None and theirs is not None and abs(mine - theirs) * 1000 <= float(words[4])
                    detail = why or why_theirs or '%s down %.0f ms after its death, %s %.0f ms (limit %s apart)' % (what, mine * 1000, words[3], theirs * 1000, words[4])
            elif who == 'place':
                # "place <b> like <a> <units>": the body recorded as b came to rest as far, and the same way, from where it stood as a did.
                found = {}
                for name in (what, words[3]):
                    samples = self.records[name].done() if name in self.records else []
                    if len(samples) >= 2 and self.records[name].body:
                        first, last = samples[0][1]['pelvis'], samples[-1][1]['pelvis']
                        found[name] = (last[0] - first[0], last[1] - first[1])
                if len(found) < 2:
                    detail = 'no samples for %s' % ' or '.join(n for n in (what, words[3]) if n not in found)
                else:
                    apart = math.dist(found[what], found[words[3]])
                    ok = apart <= float(words[4])
                    detail = '%s came to rest %.0f units from where %s would have (it moved %.0f, %s %.0f; limit %s)' % (
                        what, apart, words[3], math.hypot(*found[what]), words[3], math.hypot(*found[words[3]]), words[4])
            elif who == 'still':
                # "still <a> <units>": once the body recorded as a had been down 1.5 s, its pelvis never moved more than that between
                # two samples: no corpse jump, pop or slide after it landed.
                samples = self.records[what].done()
                down = next((at for at, nodes in samples if (body_height(nodes) or 99) < 40.0), None)
                after = [nodes['pelvis'] for at, nodes in samples if down is not None and at >= down + 1.5]
                steps = [math.dist(a, b) for a, b in zip(after, after[1:])]
                if down is None:
                    detail = '%s never went down in its samples' % what
                elif not steps:
                    ok, detail = None, 'SKIPPED: fewer than two samples after %s landed' % what
                else:
                    ok = max(steps) <= float(words[2])
                    detail = 'after landing, its pelvis moved at most %.0f units between two samples (limit %s, %d samples)' % (max(steps), words[2], len(after))
            elif who == 'door':
                # "door <a> once open|closed|toggled": the reference recorded as a went to that end once (toggled: the other end
                # from where it started), through its moving state, with no turn back on the way (open, closing, open: a
                # flicker) and without jumping over it (a snap). Doors, and bridges and gates that open like one.
                samples = self.records[what].done()
                states = [state for i, (_, state) in enumerate(samples) if i == 0 or state != samples[i - 1][1]]
                wanted = words[3] if words[3] != 'toggled' else ('closed' if states and states[0] in (1, 2) else 'open')
                end, moving = (1, 2) if wanted == 'open' else (3, 4)
                if not states:
                    detail = 'no open state read for %s' % what
                else:
                    turned = any(states[i] in (2, 4) and states[i + 1] not in ((1,) if states[i] == 2 else (3,)) for i in range(len(states) - 1))
                    # Without a moving state (not a door), going back and forth shows as more than one change.
                    turned = turned or (not self.records[what].door and len(states) > 2)
                    snapped = self.records[what].door and moving not in states and states[0] != end
                    ok = states[-1] == end and not turned and not snapped
                    detail = 'states %s (1 open, 2 opening, 3 closed, 4 closing) over %.1f s, %d samples%s%s' % (
                        ' > '.join(str(s) for s in states), samples[-1][0] - samples[0][0], len(samples), '; it turned back' if turned else '', '; it snapped over its moving state' if snapped else '')
            elif who == 'desync':
                # "desync all": the bot's last "report" against this game.
                # Each copy of the bot's NPCs is alive or dead as the bot has it, and within 200 units of where the bot has it (a body: its pelvis, 150).
                # No placed reference of this game's own is drawn beside its copy (a duplicate), and each door and lock is as the bot knows it.
                # The NPCs this game runs are left out: what the bot holds of them is only what this game last sent.
                actors, doors, locks = self.report()
                if actors is None:
                    detail = 'no "report" from the bot to compare'
                else:
                    wrong, compared = [], 0
                    for sid, (ref, dead, owner, at) in actors.items():
                        form = form_of_server_id(sid) if owner == 'ours' else None
                        if not form:
                            continue
                        compared += 1
                        here_dead = papyrus('Actor', 'IsDead', form)
                        if isinstance(here_dead, bool) and here_dead != dead:
                            wrong.append('actor %s (server id %s) is %s here, %s at its owner' % (form, sid, 'dead' if here_dead else 'alive', 'dead' if dead else 'alive'))
                        pose = body_pose(form) if dead else None
                        here = pose[0] if pose else position(form)
                        if at and here and math.dist(at, here) > (150.0 if dead else 200.0):
                            wrong.append('actor %s is %.0f units from where its owner has it' % (form, math.dist(at, here)))
                        if ref and ref.upper() != form.upper() and papyrus('ObjectReference', 'IsEnabled', ref) is True and papyrus('ObjectReference', 'Is3DLoaded', ref) is True:
                            wrong.append('reference %s of this game is drawn beside its copy %s: a duplicate' % (ref, form))
                    # A door counts whether or not it is drawn: a disabled or unloaded door keeps its state for when it is.
                    for ref, is_open in doors.items():
                        state = papyrus('ObjectReference', 'GetOpenState', ref)
                        if state not in (1, 2, 3, 4):
                            continue
                        compared += 1
                        if (state in (1, 2)) != is_open:
                            wrong.append('door %s is %s here, %s for the other player' % (ref, 'open' if state in (1, 2) else 'closed', 'open' if is_open else 'closed'))
                    for ref, locked in locks.items():
                        compared += 1
                        here_locked = papyrus('ObjectReference', 'IsLocked', ref)
                        if isinstance(here_locked, bool) and here_locked != locked:
                            wrong.append('reference %s is %s here, %s for the other player' % (ref, 'locked' if here_locked else 'unlocked', 'locked' if locked else 'unlocked'))
                    ok = not wrong
                    detail = ('%d things compared, %d differ: ' % (compared, len(wrong)) + '; '.join(wrong)) if wrong else '%d things compared, all the same' % compared
            elif who == 'told':
                # "told door <hex> open|closed", "told lock <hex> locked|unlocked": the bot's last "report" has that reference so.
                # What this game did to it reached the other player.
                _, doors, locks = self.report()
                if doors is None:
                    detail = 'no "report" from the bot to read'
                elif what == 'door':
                    known = doors.get(words[2].upper())
                    ok = known is not None and known == (words[3] == 'open')
                    detail = 'door %s: %s for the other player' % (words[2], 'never told' if known is None else 'open' if known else 'closed')
                else:
                    known = locks.get(words[2].upper())
                    ok = known is not None and known == (words[3] == 'locked')
                    detail = 'lock %s: %s for the other player' % (words[2], 'never told' if known is None else 'locked' if known else 'unlocked')
            elif who == 'absent':
                # "absent <regular expression>": the client has written no such line since the script began.
                rx = re.compile(' '.join(words[1:]))
                hits = [l for l in session_lines()[self.log_from:] if rx.search(l)]
                ok = not hits
                detail = 'none' if ok else '%d lines, e.g. %s' % (len(hits), hits[0][35:140].strip())
            elif who == 'atleast':
                # "atleast <n> <regular expression>": the client wrote at least n such lines since this script began.
                wanted = int(words[1])
                rx = re.compile(' '.join(words[2:]))
                hits = [l for l in session_lines()[self.log_from:] if rx.search(l)]
                ok = len(hits) >= wanted
                detail = '%d lines (wanted %d)%s' % (len(hits), wanted, ', the last ' + hits[-1][35:140].strip() if hits else '')
            elif who == 'logged':
                # "logged <regular expression>": the client wrote such a line since this script began. For a test
                # whose point is that a certain path was taken, not only that the game survived it.
                rx = re.compile(' '.join(words[1:]))
                hits = [l for l in session_lines()[self.log_from:] if rx.search(l)]
                ok = bool(hits)
                detail = ('%d lines, e.g. %s' % (len(hits), hits[0][35:140].strip())) if ok else 'no such line since the script began'
            elif who == 'bot' and what == 'logged':
                # "bot logged <regular expression>": the bot printed such a line so far, e.g. what its capture recorded.
                rx = re.compile(' '.join(words[2:]))
                hits = [l for l in self.bot_lines if rx.search(l)]
                ok = bool(hits)
                detail = ('%d lines, e.g. %s' % (len(hits), hits[-1].strip()[:200])) if ok else 'the bot printed no such line'
            elif who == 'ids' and what == 'reused':
                # "ids reused Seeker": the game gave an actor of that name a form id this client had deleted a copy
                # under earlier in the session. Not a fault: it is what makes live-temp-reuse a test of anything.
                deleted, reused = set(), []
                for l in session_lines():
                    m = re.search(r'Deleted actor ([0-9A-Fa-f]+)', l)
                    if m:
                        deleted.add(m.group(1).upper())
                    m = re.search(r'Spawn Actor: ([0-9A-Fa-f]+), and NPC (.*)$', l)
                    if m and m.group(1).upper() in deleted and m.group(2).strip() == ' '.join(words[2:]):
                        reused.append(m.group(1).upper())
                # Not a fault when none came back: the game hands ids out as it likes, and in a long session (the full
                # suite, 2026-10-03) it did not happen to reuse one. The run then tests nothing, and says so.
                ok = True
                detail = ('reused: %s' % ', '.join(sorted(set(reused)))) if reused else 'SKIPPED: none of the %d deleted ids came back, so this run tests nothing' % len(deleted)
            elif who == 'hands' and what == 'within':
                # "hands within <units>": the copies' hands against the owners' own numbers, from the game's
                # "VRBodySync: actor X hands -- owner L(..) R(..); copy L(..) R(..)" lines since the script began
                # (one every 5 s per copy). Passes when the median distance, both hands, is within that many units.
                rx = re.compile(r'hands -- owner L\(([-\d.]+), ([-\d.]+), ([-\d.]+)\) R\(([-\d.]+), ([-\d.]+), ([-\d.]+)\); copy L\(([-\d.]+), ([-\d.]+), ([-\d.]+)\) R\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)')
                gaps = []
                for l in session_lines()[self.log_start:]:
                    m = rx.search(l)
                    if m:
                        v = [float(x) for x in m.groups()]
                        gaps += [math.dist(v[0:3], v[6:9]), math.dist(v[3:6], v[9:12])]
                gaps.sort()
                median = gaps[len(gaps) // 2] if gaps else None
                ok = median is not None and median <= float(words[2])
                detail = ('%d hand(s) measured, median %.1f units, worst %.1f' % (len(gaps), median, gaps[-1])) if gaps else 'no hand measurement logged'
            elif who == 'player' and what == 'has':
                # "player has <hex>": the local player carries at least one of that item.
                count = papyrus('ObjectReference', 'GetItemCount', '14', [{'form': '0x' + words[2]}])
                ok = isinstance(count, int) and count > 0
                detail = 'GetItemCount(%s) %s' % (words[2], count)
            elif who == 'player' and what == 'in':
                # "player in <worldspace editor id>": the player is outdoors in that worldspace (not in an interior).
                scene = tool('inspect', {'kind': 'scene'})
                ws = (scene.get('worldspace') or {}).get('editorId')
                cell = (scene.get('cell') or {}).get('editorId') or (scene.get('cell') or {}).get('formId')
                ok = ws == words[2]
                detail = 'worldspace %s, cell %s' % (ws, cell)
            elif who == 'player' and what == 'cell':
                # "player cell <hex>": the cell the player stands in, by form id (an outdoor cell too).
                scene = tool('inspect', {'kind': 'scene'})
                got = str((scene.get('cell') or {}).get('formId') or '')
                try:
                    ok = int(got, 16) & 0xFFFFFF == int(words[2], 16) & 0xFFFFFF
                except ValueError:
                    ok = False
                detail = 'cell %s (%s), wanted %s' % (got, (scene.get('cell') or {}).get('editorId'), words[2])
            elif who == 'spot':
                # "spot mark": where the player stands.
                # "spot back <units>": within that distance of the mark, in the same worldspace or interior.
                # "spot away <units>": farther than that, or elsewhere.
                scene = tool('inspect', {'kind': 'scene'})
                place = (scene.get('worldspace') or {}).get('editorId') or (scene.get('cell') or {}).get('editorId') or (scene.get('cell') or {}).get('formId')
                at = position('14')
                if what == 'mark':
                    self.spot = (place, at)
                    ok = at is not None
                    detail = '%s at %s' % (place, ('(%.0f, %.0f, %.0f)' % tuple(at)) if at else 'no position')
                elif not self.spot or not self.spot[1] or not at:
                    ok, detail = False, 'no mark taken, or no position'
                else:
                    wanted = float(words[2])
                    apart = math.dist(at, self.spot[1])
                    same = place == self.spot[0]
                    ok = (same and apart <= wanted) if what == 'back' else (not same or apart > wanted)
                    detail = '%.0f units from the mark, in %s (the mark in %s), wanted %s %.0f' % (apart, place, self.spot[0], 'within' if what == 'back' else 'beyond', wanted)
            elif who == 'player' and what == 'alive':
                dead = papyrus('Actor', 'IsDead', '14')
                health = tool('inspect', {'kind': 'player'})['actorValues']['health']['current']
                ok = dead is False and health > 0
                detail = 'IsDead %s, health %.0f' % (dead, health)
            elif who == 'slide':
                lines = session_lines()
                if what == 'mark':
                    self.slide_mark = len(lines)
                    ok, detail = True, 'from log line %d of this session' % self.slide_mark
                else:
                    frozen = moved = 0
                    for l in lines[self.slide_mark:]:
                        m = re.search(r'SlideDiag: (\d+) of (\d+) moves', l)
                        if m:
                            frozen += int(m.group(1))
                            moved += int(m.group(2))
                    wanted = float(words[2])
                    if not moved:
                        ok, detail = False, 'the game logged no SlideDiag line since the mark (the body did not move, or 10 s have not passed)'
                    else:
                        pct = 100.0 * frozen / moved
                        ok = pct < wanted if what == 'below' else pct > wanted
                        detail = '%d of %d moves with unchanged animation variables (%.0f%%), wanted %s %.0f%%' % (frozen, moved, pct, what, wanted)
            elif who == 'drop':
                ref = dropped_ref()
                if what == 'gone':
                    ok, detail = ref is None, ('no placed item left' if ref is None else 'still there as %s' % ref)
                elif not ref:
                    ok, detail = False, 'the game logged no placed item'
                else:
                    at = position(ref)
                    if what == 'exists':
                        ok = at is not None
                        detail = 'item %s at %s' % (ref, ('(%.0f, %.0f, %.0f)' % tuple(at)) if at else 'no position')
                    elif what == 'mark':
                        self.drop_mark = at
                        ok = at is not None
                        detail = 'item %s at %s' % (ref, ('(%.0f, %.0f, %.0f)' % tuple(at)) if at else 'no position')
                    elif what == 'moved':
                        wanted = [float(w) for w in words[2:5]]
                        if at is None or self.drop_mark is None:
                            ok, detail = False, 'no position, or no mark taken'
                        else:
                            moved = [at[i] - self.drop_mark[i] for i in range(3)]
                            ok = all(abs(moved[i] - wanted[i]) <= DROP_TOLERANCE for i in range(3))
                            detail = 'item %s moved (%.0f, %.0f, %.0f), wanted (%.0f, %.0f, %.0f)' % tuple([ref] + moved + wanted)
                    else:
                        detail = 'unknown check'
            elif who == 'player':
                health = tool('inspect', {'kind': 'player'})['actorValues']['health']['current']
                if what == 'mark':
                    # The player regenerates a few points a second, which read as "the spell did 14 of its 24".
                    # Switched off for the length of the run and put back at the end; nothing is saved.
                    if self.heal_rate is None:
                        # The multiplier, forced: setting the rate itself leaves whatever a perk or an enchantment
                        # adds on top, which still gave back a point a second.
                        self.heal_rate = papyrus('Actor', 'GetActorValue', '14', ['HealRateMult'])
                        papyrus('Actor', 'ForceActorValue', '14', ['HealRateMult', 0.0])
                        time.sleep(0.3)
                        health = tool('inspect', {'kind': 'player'})['actorValues']['health']['current']
                    self.mark = health
                    ok, detail = True, 'health %.0f' % health
                else:
                    wanted = float(words[2])
                    dropped = (self.mark if self.mark is not None else health) - health
                    ok = abs(dropped - wanted) <= PLAYER_TOLERANCE
                    detail = 'health %.0f -> %.0f, dropped %.0f (wanted %.0f)' % (self.mark, health, dropped, wanted)
            else:
                form, why = self.subject(who)
                if not form:
                    ok, detail = False, why
                elif what in ('exists', 'gone'):
                    # "copy exists" / "npc exists": the copy the game made is in this game and drawn.
                    # A copy taken away can stay disabled for a while before it is deleted, so it is not one that exists.
                    # "copy gone" / "npc gone": the game no longer has it, or no longer draws it.
                    x = papyrus('ObjectReference', 'GetPositionX', form)
                    enabled = papyrus('ObjectReference', 'IsEnabled', form) if isinstance(x, (int, float)) else None
                    if TIMED_OUT in (x, enabled):
                        detail = UNASKED
                    else:
                        drawn = isinstance(x, (int, float)) and enabled is True
                        missing = x == NOT_FOUND or enabled is False
                        ok = drawn if what == 'exists' else missing
                        detail = 'actor %s, x %s, enabled %s' % (form, x, enabled)
                elif what == 'turned' and words[2] == 'below':
                    # "copy turned below <degrees> <seconds>": the copy's heading, sampled five times a second, stays
                    # within that many degrees over that time (a head turn of its player must not turn its body).
                    limit, seconds = float(words[3]), float(words[4])
                    angles = []
                    end = time.time() + seconds
                    while time.time() < end:
                        a = papyrus('ObjectReference', 'GetAngleZ', form)
                        if isinstance(a, (int, float)):
                            angles.append(float(a))
                        time.sleep(0.2)
                    if angles:
                        # Unwrapped, so a heading crossing 0/360 counts as the small turn it is.
                        unwrapped = [angles[0]]
                        for a in angles[1:]:
                            unwrapped.append(unwrapped[-1] + ((a - unwrapped[-1] + 180.0) % 360.0 - 180.0))
                        turned = max(unwrapped) - min(unwrapped)
                        ok = turned < limit
                        detail = 'actor %s turned %.0f degrees over %d samples' % (form, turned, len(angles))
                    else:
                        detail = 'actor %s: no heading read' % form
                elif what == '3d':
                    loaded = papyrus('ObjectReference', 'Is3DLoaded', form)
                    ok = loaded is True
                    detail = 'actor %s, Is3DLoaded %s' % (form, loaded)
                elif what in ('alive', 'dead'):
                    dead = papyrus('Actor', 'IsDead', form)
                    ok = dead is (what == 'dead')
                    detail = 'actor %s, IsDead %s' % (form, dead)
                elif what == 'gait':
                    # The body's position and the "Speed" its animation graph holds, sampled together. A body that
                    # travels while its graph says zero is sliding; this asks the copy itself, not the stream.
                    seconds = float(words[3]) if len(words) > 3 else 8.0
                    end = time.time() + seconds
                    last = position(form)
                    moving = legs = 0
                    top = 0.0
                    while time.time() < end:
                        time.sleep(0.3)
                        at = position(form)
                        speed = papyrus('ObjectReference', 'GetAnimationVariableFloat', form, ['Speed'])
                        if at and last and isinstance(speed, (int, float)):
                            if math.dist(at, last) > 10.0:
                                moving += 1
                                if abs(speed) > 1.0:
                                    legs += 1
                                top = max(top, abs(speed))
                        last = at
                    share = (100.0 * legs / moving) if moving else 0.0
                    if not moving:
                        # Nothing to judge, which is not the same as sliding: a Netch hovers on the spot.
                        ok, detail = True, 'SKIPPED: actor %s did not travel in %.0f s, so there is no gait to read' % (form, seconds)
                    else:
                        # 50, not 80: a creature lunges, stops and is pushed about, and its copy trails the stream by
                        # 300 ms, so even a healthy one is in the 70s. The broken cases measured 0 and 17.
                        ok = share >= 50.0 if words[2] == 'walking' else share <= 20.0
                        detail = 'actor %s moved in %d samples, %d of them with a speed in its animation graph (%.0f%%, top %.0f)' % (form, moving, legs, share, top)
                elif what == 'feet':
                    # "copy feet still <s>" | "copy feet moving <s>": each foot against the pelvis, sideways only, sampled that
                    # long. Feet that swing are legs that walk, whether or not the body travels; a body walking on the spot
                    # has them swinging while it stands. Not the graph's speed: the client writes that itself.
                    seconds = float(words[3]) if len(words) > 3 else 3.0
                    end = time.time() + seconds
                    offsets = {'NPC L Foot [Lft ]': [], 'NPC R Foot [Rft ]': []}
                    while time.time() < end:
                        pelvis = node_xy(form, 'NPC Pelvis [Pelv]')
                        for name, got in offsets.items():
                            foot = node_xy(form, name)
                            if pelvis and foot:
                                got.append((foot[0] - pelvis[0], foot[1] - pelvis[1]))
                    spans = [max(math.dist(a, b) for a in got for b in got) for got in offsets.values() if len(got) >= 5]
                    if not spans:
                        ok, detail = False, 'actor %s: its feet could not be read' % form
                    else:
                        # Walking legs read 39-67 units, standing ones 3 (2026-10-21). A body sliding in its standing
                        # state read 17: its feet shuffle under it, which is not walking.
                        swing = max(spans)
                        ok = swing <= 8.0 if words[2] == 'still' else swing >= 30.0
                        detail = 'actor %s: a foot moved at most %.0f units against the pelvis in %d samples' % (form, swing, min(len(v) for v in offsets.values()))
                elif what in ('infaction', 'notinfaction'):
                    # "npc infaction <hex>": the game's copy is in that faction (Actor.IsInFaction).
                    inside = papyrus('Actor', 'IsInFaction', form, [{'form': '0x' + words[2]}])
                    ok = isinstance(inside, bool) and inside is (what == 'infaction')
                    detail = 'actor %s, IsInFaction(%s) %s' % (form, words[2], inside)
                elif what in ('mounted', 'unmounted'):
                    # "copy mounted": the game's copy is riding (Actor.IsOnMount), for a rider's MountRequest (2026-10-10).
                    riding = papyrus('Actor', 'IsOnMount', form, [])
                    ok = isinstance(riding, bool) and riding is (what == 'mounted')
                    detail = 'actor %s, IsOnMount %s' % (form, riding)
                elif what == 'has':
                    # "copy has <hex>": the copy's inventory holds at least one.
                    n = papyrus('ObjectReference', 'GetItemCount', form, [{'form': '0x' + words[2]}])
                    ok = isinstance(n, int) and n > 0
                    detail = 'actor %s, GetItemCount(%s) %s' % (form, words[2], n)
                elif what == 'count':
                    # "copy count <hex> <n>": the copy's inventory holds exactly that many.
                    n = papyrus('ObjectReference', 'GetItemCount', form, [{'form': '0x' + words[2]}])
                    ok = isinstance(n, int) and n == int(words[3])
                    detail = 'actor %s, GetItemCount(%s) %s, wanted %s' % (form, words[2], n, words[3])
                elif what in ('lefthand', 'righthand'):
                    # "copy lefthand <hex>": the weapon the game says is in that hand (GetEquippedWeapon).
                    weapon = papyrus('Actor', 'GetEquippedWeapon', form, [what == 'lefthand'])
                    got = weapon.get('formId', '') if isinstance(weapon, dict) else str(weapon)
                    ok = isinstance(weapon, dict) and int(got, 16) & 0xFFFFFF == int(words[2], 16) & 0xFFFFFF
                    detail = 'actor %s, %s holds %s' % (form, 'left hand' if what == 'lefthand' else 'right hand', got or weapon)
                elif what in ('equipped', 'unequipped'):
                    # "copy equipped <hex>": whether the game's copy has that item equipped, asked of the game itself.
                    held = papyrus('Actor', 'IsEquipped', form, [{'form': '0x' + words[2]}])
                    ok = isinstance(held, bool) and held is (what == 'equipped')
                    detail = 'actor %s, IsEquipped(%s) %s' % (form, words[2], held)
                elif what in ('standing', 'lying', 'down'):
                    # "npc standing|lying|down": the copy's body height now (body_pose), the middle of three readings a half second apart.
                    # Down is kneeling or lying: a bleedout kneels at about 26 units, under the 25 of lying flat only once it curls up.
                    heights = []
                    for i in range(3):
                        pose = body_pose(form)
                        if pose:
                            heights.append(pose[1])
                        if i < 2:
                            time.sleep(0.5)
                    if not heights:
                        detail = 'actor %s: no pelvis or feet read' % form
                    else:
                        height = sorted(heights)[len(heights) // 2]
                        ok = height >= 45.0 if what == 'standing' else height < 25.0 if what == 'lying' else height < 40.0
                        detail = 'actor %s, pelvis %.0f units above its lower foot (readings %s)' % (form, height, ', '.join('%.0f' % h for h in heights))
                elif what == 'fell' and words[2:3] == ['within']:
                    # "npc fell within <ms>": from this game applying its owner's death ("set dead by its owner") to the first
                    # "DO watch npc" sample with the body down (under 40 units). Samples come every few hundred ms, so the time is that coarse.
                    limit = float(words[3])
                    applied = None
                    # The copy that was watched: a copy torn down and made again since has another form id.
                    watched = self.watch_form or form
                    for l in session_lines()[self.log_from:]:
                        if re.search(r'Death sync: remote actor %s set dead by its owner' % watched, l, re.I):
                            applied = log_time(l)
                    times = [(self.watch_start + t, h) for t, _, h, _ in self.watch]
                    early = [at for at, h in times if h < 40.0 and applied and at < applied - 0.1]
                    down = next((at for at, h in times if h < 40.0 and applied and at >= applied - 0.1), None)
                    if applied is None:
                        detail = 'actor %s: no "set dead by its owner" line, so no death to time' % form
                    elif early:
                        detail = 'actor %s was down %.0f ms before its owner\'s death was applied here' % (form, (applied - early[0]) * 1000)
                    elif down is None:
                        detail = 'actor %s: no sample with the body down after its owner\'s death was applied here' % form
                    else:
                        ok = (down - applied) * 1000 <= limit
                        detail = 'actor %s was down %.0f ms after its owner\'s death was applied here (limit %.0f; %d samples)' % (form, (down - applied) * 1000, limit, len(times))
                elif what == 'went' and words[2:3] == ['down']:
                    # "npc went down": two "DO watch npc" samples in a row with the body under 25 units high.
                    # The precondition of a downed-copy test: a copy that never lay down proves nothing about getting up.
                    heights = [h for _, _, h, _ in self.watch]
                    lying = [i for i, h in enumerate(heights) if h < 25.0]
                    ok = any(i + 1 in lying for i in lying)
                    detail = ('actor %s, %d samples, %d of them lying; body height lowest %.0f, highest %.0f' % (form, len(heights), len(lying), min(heights), max(heights))
                              if heights else 'no "DO watch npc" samples to judge')
                elif what == 'slide':
                    # "npc slide below <units>": how far the lying copy was carried along the floor, from the "DO watch npc" samples.
                    # Horizontal travel of its reference while the body is under 25 units high, counted once it has lain there 1.5 s.
                    # Carrying a copy moves its reference, and the ragdoll with it; a slide behind the owner goes on for as long as it lies.
                    # The pelvis alone also counts the ragdoll rolling and settling on a slope (0 to 200 units on one spot), so it is only reported.
                    limit = float(words[3])
                    travel, pelvis_travel, lying_since, previous = 0.0, 0.0, None, None
                    for t, pelvis, height, ref in self.watch:
                        if height >= 25.0:
                            lying_since = None
                        elif lying_since is None:
                            lying_since = t
                        elif previous and t - lying_since >= 1.5:
                            pelvis_travel += math.dist(pelvis[:2], previous[0][:2])
                            if ref and previous[1]:
                                travel += math.dist(ref[:2], previous[1][:2])
                        previous = (pelvis, ref)
                    if not any(h < 25.0 for _, _, h, _ in self.watch):
                        ok, detail = True, 'SKIPPED: actor %s never lay down in the samples, so there is no slide to judge' % form
                    elif not all(ref for _, _, h, ref in self.watch if h < 25.0):
                        detail = 'actor %s: its reference could not be read while it lay' % form
                    else:
                        ok = travel <= limit
                        detail = 'actor %s: the lying copy was carried %.0f units along the floor (limit %.0f); its ragdoll pelvis moved %.0f' % (form, travel, limit, pelvis_travel)
                elif what == 'health':
                    value = papyrus('Actor', 'GetActorValue', form, ['Health'])
                    base = papyrus('Actor', 'GetBaseActorValue', form, ['Health'])
                    # "full" is whatever this game says the actor's own health is, which a script cannot know.
                    # "below <n>": anything under that, for a hit whose exact damage is the game's business.
                    if words[2] == 'below':
                        wanted = float(words[3])
                        ok = isinstance(value, (int, float)) and value < wanted
                    else:
                        wanted = float(base) if words[2] == 'full' and isinstance(base, (int, float)) else float(words[2] if words[2] != 'full' else 'nan')
                        ok = isinstance(value, (int, float)) and abs(value - wanted) <= HEALTH_TOLERANCE
                    detail = 'actor %s, health %s (wanted %.0f; its base health here is %s)' % (
                        form, ('%.1f' % value) if isinstance(value, (int, float)) else value, wanted,
                        ('%.0f' % base) if isinstance(base, (int, float)) else base)
                else:
                    detail = 'unknown check'
        except Exception as e:
            timed_out = isinstance(e, urllib.error.HTTPError) and e.code == 504
            ok, detail = False, UNASKED if timed_out else 'could not ask the game: %s %s' % (type(e).__name__, e)
        if not ok and detail == UNASKED:
            # The game is up but DevBench gave up waiting: the check proves nothing either way.
            # A game that no longer answers at all still fails, so a crash is never hidden here.
            self.invalid.append('%s (%s)' % (text, detail))
            print('   %s  %-24s %s' % ('INVALID', text, detail), flush=True)
            return
        skipped = bool(ok) and isinstance(detail, str) and detail.startswith('SKIPPED')
        if require:
            # A precondition: when it does not hold, what the script checks afterwards is not what it was written for.
            held = bool(ok) and not skipped
            if not held:
                self.invalid.append('%s (%s)' % (text, detail))
            print('   %s  %-24s %s' % ('NEEDED' if held else 'INVALID', text, detail), flush=True)
            return
        # A skipped check ran into nothing to judge: it is neither a pass nor a failure (None).
        self.results.append((None if skipped else bool(ok), text, detail))
        print('   %s  %-24s %s' % ('SKIP' if skipped else ('PASS' if ok else 'FAIL'), text, detail), flush=True)

    def run(self):
        exe = os.path.join(RELEASE, 'STBot.exe')
        cmd = [exe, os.path.join('scripts', self.script + '.txt'), '--server', '127.0.0.1:10578', '--name', 'Bot']
        # Where the player stands, asked of the game. Left to itself the bot looks for a position line near the end
        # of the client log, and after a busy script there is none there: it then waits at the world origin for
        # five minutes and the script never starts (second run of live-temp-reuse, 2026-10-02).
        try:
            where = position('14')
            if where:
                cmd += ['--x', '%.0f' % where[0], '--y', '%.0f' % where[1]]
        except Exception:
            pass
        print('== %s' % self.script, flush=True)
        self.log_start = len(session_lines())
        self.log_from = self.log_start
        proc = subprocess.Popen(cmd, cwd=RELEASE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        log = io.open(os.path.join(RELEASE, 'logs', self.script + '-bot.log'), 'w', encoding='utf-8')
        for line in proc.stdout:
            log.write(line)
            self.bot_lines.append(line)
            m = re.search(r'ok: known me \(([0-9A-Fa-f]+) is known\)', line) or re.search(r'Character assigned.*?([0-9A-Fa-f]+)', line)
            if m and not self.bot_id:
                self.bot_id = m.group(1)
            m = re.search(r'NPC registered as actor ([0-9A-Fa-f]+)', line)
            if m:
                self.npc_id = m.group(1)
            m = re.search(r'\[script\] (CHECK .*|REQUIRE .*|DO face .*|DO console .*|DO key .*|DO f6|DO look .*|DO swing .*|DO load last|DO load save .*|DO server restart.*|DO sleep \d+|DO papyrus .*|DO pickup own|DO logmark|DO place .*|DO drop .*|DO grab .*|DO grip both \d+|DO release (?:left|right)|DO reach .*|DO hold .*|DO walk .*|DO bring .*|DO sample .*|DO watch (?:npc|copy) \d+|DO record .*|DO mark .*|DO menu .*|DO god on|DO god off|DO combat on|DO combat off|SHOT .*|--.*|done)$', line.rstrip())
            if m:
                text = m.group(1)
                if text.startswith('CHECK '):
                    self.check(text[6:].strip())
                elif text.startswith('REQUIRE '):
                    self.check(text[8:].strip(), require=True)
                elif text.strip() in ('DO combat on', 'DO combat off'):
                    on = text.strip().endswith('on')
                    if ensure('tcai', 'is On' if on else 'is Off'):
                        print('   (combat AI %s)' % ('on' if on else 'off'), flush=True)
                    else:
                        print('   FAIL  combat AI could not be set', flush=True)
                        self.results.append((False, text.strip(), 'console did not confirm'))
                elif text.strip() in ('DO god on', 'DO god off'):
                    on = text.strip().endswith('on')
                    if ensure('tgm', 'enabled' if on else 'disabled'):
                        print('   (god mode %s)' % ('on' if on else 'off'), flush=True)
                    else:
                        print('   FAIL  god mode could not be set', flush=True)
                        self.results.append((False, text.strip(), 'console did not confirm'))
                elif text.startswith('DO swing ') and ' through copy' in text:
                    # "DO swing right|left through copy <ms> [past <m>] [hold <ms>]": the same swing, aimed: the middle of
                    # the copy's sword (or its right hand, where the bot's "handtargets" put it) is turned into room space
                    # from the copy's and the player's positions and headings (papyrus), about 70 game units a metre, and
                    # the controller sweeps right to left from 45 cm before it to <past> metres beyond (45 cm), then
                    # stays there for <hold> ms (none).
                    words = text.split()
                    try:
                        side, ms = words[2], int(words[5])
                        options = dict(zip(words[6::2], words[7::2]))
                        past = float(options.get('past', 0.45))
                        hold = int(options.get('hold', 0))
                        copy, why = self.subject('copy')
                        if not copy:
                            raise RuntimeError('no copy: %s' % why)
                        def pos(form):
                            return [papyrus('ObjectReference', 'Get' + a, form) for a in ('PositionX', 'PositionY', 'PositionZ', 'AngleZ')]
                        cx, cy, cz, ca = pos(copy)
                        px, py, pz, pa = pos('14')
                        ca, pa = math.radians(ca), math.radians(pa)
                        # The middle of the copy's sword (its body's centre of mass, from the client's VRWeaponBody line),
                        # or else its right hand: its local right, forward, up turned by its heading.
                        centre = latest_blade_centre(session_lines(), copy)
                        if centre:
                            hx, hy, hz = centre
                        else:
                            hx = cx + 15 * math.cos(ca) + 20 * math.sin(ca)
                            hy = cy - 15 * math.sin(ca) + 20 * math.cos(ca)
                            hz = cz + 80
                        dx, dy, dz = hx - px, hy - py, hz - pz
                        right = dx * math.cos(pa) - dy * math.sin(pa)
                        forward = dx * math.sin(pa) + dy * math.cos(pa)
                        rx, ry, rz = right / 70.0, dz / 70.0, -forward / 70.0
                        print('   (the copy\'s %s is %.0f right, %.0f forward, %.0f up of the player: room (%.2f, %.2f, %.2f))' % ('blade' if centre else 'right hand', right, forward, dz, rx, ry, rz), flush=True)
                        # The hand passes 45 cm short of it, so the middle of the player's own blade crosses it.
                        text = 'DO swing %s %.3f %.3f %.3f %.3f %d hold %d' % (side, rx + 0.45, rx - past, ry, min(-0.2, rz + 0.45), ms, hold)
                    except Exception as e:
                        print('   FAIL  swing through copy: %s' % e, flush=True)
                        self.results.append((False, text.strip(), str(e)))
                        text = ''
                if text.startswith('DO swing '):
                    # "DO swing right|left <x0> <x1> <y> <z> <ms> [hold <ms>]": that controller moves from x0 to x1 (metres
                    # in room space: x right, y up, -z forward) at height y and depth z over <ms>, then stays at x1 for
                    # the hold, the headset standing at 1.6 m looking forward, the other hand resting low at its side
                    # (DevBench `input vrTrackedSet`, one sequence of 20 ms frames built on the current frame). The blade
                    # work's way to swing a sword (2026-10-10). The driver does not wait for it; the bot's script waits.
                    words = text.split()
                    try:
                        side, other = words[2], ('left' if words[2] == 'right' else 'right')
                        x0, x1, y, z, ms = float(words[3]), float(words[4]), float(words[5]), float(words[6]), int(words[7])
                        hold = int(words[9]) if len(words) > 9 and words[8] == 'hold' else 0
                        base = tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
                        steps = max(2, ms // 20)
                        frames = []
                        for i in range(steps + 1 + hold // 20):
                            f = json.loads(json.dumps(base))
                            f.pop('seq', None)
                            f['tMs'] = int(i * ms / steps) if i <= steps else ms + (i - steps) * 20
                            f['hmd']['matrix'] = [1, 0, 0, 0, 0, 1, 0, 1.6, 0, 0, 1, 0]
                            f[side]['matrix'] = [1, 0, 0, x0 + (x1 - x0) * min(i, steps) / steps, 0, 1, 0, y, 0, 0, 1, z]
                            f[other]['matrix'] = [1, 0, 0, -0.25 if other == 'left' else 0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
                            for d in ('hmd', 'left', 'right'):
                                f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
                                # SteamVR switches idle controllers off, and then they come without an index or a state.
                                if f[d].get('index') is None:
                                    f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
                                for key in ('velocity', 'angularVelocity'):
                                    f[d].setdefault(key, [0.0, 0.0, 0.0])
                                if d != 'hmd' and not f[d].get('controller'):
                                    f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
                            for d in ('left', 'right'):
                                f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
                            frames.append(f)
                        set_velocities(frames, side)
                        r = tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-check'}, timeout=15)
                        print('   (%s hand swings from %.2f to %.2f over %d ms%s: %s)' % (side, x0, x1, ms, ', then holds %d ms' % hold if hold else '', 'queued' if r.get('queued') else r), flush=True)
                    except Exception as e:
                        print('   FAIL  swing: %s' % type(e).__name__, flush=True)
                        self.results.append((False, text.strip(), type(e).__name__))
                elif text.startswith('DO look '):
                    # "DO look <degrees> <ms> [hold <ms>] [back]": the headset turns from straight ahead to that yaw (positive
                    # turns left) over <ms>, holds, and with "back" turns straight ahead again over <ms>.
                    # Both hands rest still low at the sides: a head turn with the body still.
                    words = text.split()
                    try:
                        degrees, ms = float(words[2]), int(words[3])
                        hold = int(words[5]) if len(words) > 5 and words[4] == 'hold' else 0
                        back = words[-1] == 'back'
                        base = tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
                        steps = max(2, ms // 20)
                        held = hold // 20
                        frames = []
                        for i in range(steps + 1 + held + (steps if back else 0)):
                            if i <= steps + held:
                                share = min(i, steps) / steps
                            else:
                                share = 1.0 - (i - steps - held) / steps
                            yaw = math.radians(degrees) * share
                            c_, s_ = math.cos(yaw), math.sin(yaw)
                            f = json.loads(json.dumps(base))
                            f.pop('seq', None)
                            f['tMs'] = i * 20
                            f['hmd']['matrix'] = [c_, 0, s_, 0, 0, 1, 0, 1.6, -s_, 0, c_, 0]
                            f['left']['matrix'] = [1, 0, 0, -0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
                            f['right']['matrix'] = [1, 0, 0, 0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
                            for d in ('hmd', 'left', 'right'):
                                f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
                                if f[d].get('index') is None:
                                    f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
                                for key in ('velocity', 'angularVelocity'):
                                    f[d].setdefault(key, [0.0, 0.0, 0.0])
                                if d != 'hmd' and not f[d].get('controller'):
                                    f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
                            for d in ('left', 'right'):
                                f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
                            frames.append(f)
                        r = tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-check'}, timeout=15)
                        print('   (headset turns %.0f degrees over %d ms%s%s: %s)' % (degrees, ms, ', then holds %d ms' % hold if hold else '', ' and back' if back else '',
                                                                              'queued' if r.get('queued') else r), flush=True)
                    except Exception as e:
                        print('   FAIL  look: %s' % type(e).__name__, flush=True)
                        self.results.append((False, text.strip(), type(e).__name__))
                elif text.strip() == 'DO f6':
                    # "DO f6": presses F6, which connects or disconnects the game.
                    # The client reads it with GetAsyncKeyState, which DevBench's keys do not reach, so it is pressed at the Windows level, as headless.ps1 does.
                    import ctypes
                    ctypes.windll.user32.keybd_event(0x75, 0, 0, 0)
                    time.sleep(0.3)
                    ctypes.windll.user32.keybd_event(0x75, 0, 2, 0)
                    print('   (F6 pressed)', flush=True)
                elif text.startswith('DO key '):
                    parts = text.split()
                    try:
                        r = tool('input', {'action': 'down', 'device': 'keyboard', 'key': parts[2], 'maxHoldMs': int(parts[3]), 'owner': 'live-check'})
                        print('   (key %s held for %s ms: %s)' % (parts[2], parts[3], 'accepted' if r.get('accepted') else r), flush=True)
                    except Exception as e:
                        print('   FAIL  key %s: %s' % (parts[2], type(e).__name__), flush=True)
                        self.results.append((False, text.strip(), type(e).__name__))
                elif text.startswith('DO papyrus '):
                    # "DO papyrus <Script> <Function> <self> [form ...]": a native function called on a reference,
                    # with forms as its arguments (hex ids).
                    words = text.split()
                    # Arguments: a form by its hex id, or a plain number written "n:<value>".
                    def arg(w):
                        if w.startswith('n:'):
                            return int(w[2:])
                        if w.startswith('f:'):
                            return float(w[2:])
                        if w in ('b:true', 'b:false'):
                            return w == 'b:true'
                        if w.startswith('s:'):
                            return w[2:]
                        if w in ('copy', 'npc'):
                            f, _ = self.subject(w)
                            return {'form': '0x' + (f or '0')}
                        if w == 'placed':
                            return {'form': '0x' + (getattr(self, 'placed', None) or '0')}
                        return {'form': '0x' + w}
                    args = [arg(w) for w in words[5:]]
                    target = words[4]
                    if target in ('copy', 'npc'):
                        target, _ = self.subject(target)
                    elif target == 'placed':
                        target = getattr(self, 'placed', None)
                    # "global" in place of the reference: a global function, such as Debug.SendAnimationEvent copy s:<event>.
                    if words[4] == 'global':
                        result = papyrus_global(words[2], words[3], args)
                    else:
                        result = papyrus(words[2], words[3], target, args) if target else 'ERR no %s' % words[4]
                    print('   (papyrus %s.%s on %s: %s)' % (words[2], words[3], words[4], result), flush=True)
                    if isinstance(result, str) and result.startswith('ERR'):
                        self.results.append((False, text.strip(), result))
                elif text.strip() in ('DO face copy', 'DO face npc'):
                    # "DO face copy|npc": turn the player towards that actor. A copy outside the headset's view is
                    # not posed (VRBodySync), and the rig's headset looks wherever the player faces.
                    who = text.split()[2]
                    form, _ = self.subject(who)
                    try:
                        mine = position('14')
                        theirs = position(form) if form else None
                        if not mine or not theirs:
                            raise RuntimeError('no position for the player or the %s' % who)
                        angle = math.degrees(math.atan2(theirs[0] - mine[0], theirs[1] - mine[1])) % 360.0
                        tool('console', {'action': 'exec', 'command': 'player.setangle z %.1f' % angle})
                        print('   (player turned to %.0f degrees, towards %s %s)' % (angle, who, form), flush=True)
                    except Exception as e:
                        print('   FAIL  face %s: %s' % (who, e), flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.startswith('DO sleep '):
                    time.sleep(int(text.split()[2]))
                elif text.startswith('DO menu '):
                    # "DO menu <seconds> <menu name> [shot <name>]": open a menu, keep it open, close it again (DevBench's menu tool).
                    # With "shot", a SHOT picture of the open menu is taken halfway through.
                    words = text.split(None, 3)
                    shot_name = None
                    if ' shot ' in words[3]:
                        words[3], shot_name = [w.strip() for w in words[3].split(' shot ', 1)]
                    try:
                        tool('menu', {'action': 'open', 'name': words[3]})
                        if shot_name:
                            time.sleep(float(words[2]) / 2)
                            path = screenshot('%s-%s' % (self.script, shot_name))
                            if path:
                                self.shots.append(path)
                                print('   (screenshot: %s)' % path, flush=True)
                            else:
                                self.results.append((False, 'screenshot ' + shot_name, 'no picture came back'))
                            time.sleep(float(words[2]) / 2)
                        else:
                            time.sleep(float(words[2]))
                        tool('menu', {'action': 'close', 'name': words[3]})
                        print('   (menu %s open for %s s)' % (words[3], words[2]), flush=True)
                    except Exception as e:
                        print('   FAIL  menu %s: %s' % (words[3], e), flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.strip() == 'DO logmark':
                    # From here on, logged, absent, atleast, fewer and distinct count only the client lines written after this one.
                    self.log_from = len(session_lines())
                    print('   (log mark: the checks count client lines from line %d of this session)' % self.log_from, flush=True)
                elif text.strip() == 'DO pickup own':
                    # The newest item this game dropped itself, picked up by the player through the game's own Activate.
                    refs = [m.group(1) for m in (re.search(r'DroppedItem: dropped \S+ x\d+ as ([0-9A-Fa-f]+)', l) for l in session_lines()) if m]
                    if not refs:
                        print('   FAIL  pickup: this game has dropped nothing', flush=True)
                        self.results.append((False, text.strip(), 'nothing dropped'))
                    else:
                        # Both arguments: DevBench refuses a call that leaves out one with no known default (abDefaultProcessingOnly).
                        result = papyrus('ObjectReference', 'Activate', refs[-1], [{'form': '0x14'}, False])
                        print('   (picked up %s: %s)' % (refs[-1], result), flush=True)
                elif text.startswith('DO drop ') and text.split()[3:4] == ['here']:
                    # "DO drop <base hex> here": the player drops one of that item where it stands (to be grabbed).
                    base = text.split()[2]
                    tool('console', {'action': 'exec', 'command': 'player.additem %s 1' % base})
                    time.sleep(0.5)
                    tool('console', {'action': 'exec', 'command': 'player.drop %s 1' % base})
                    ref = None
                    for _ in range(20):
                        time.sleep(0.25)
                        refs = [m2.group(1) for m2 in (re.search(r'DroppedItem: dropped \S+ x\d+ as ([0-9A-Fa-f]+)', l) for l in session_lines()[self.log_start:]) if m2]
                        if refs:
                            ref = refs[-1]
                            break
                    self.dropped_here = ref
                    print('   (dropped %s here as %s)' % (base, ref), flush=True)
                    if not ref:
                        self.results.append((False, text.strip(), 'the drop was never announced by the client'))
                elif text.startswith('DO walk '):
                    # "DO walk <dx> <dy> <dz> <steps> [<seconds a step>]": the player moves by that much a step, five steps a
                    # second unless told otherwise, and drags whatever a HIGGS hand holds along (a corpse grabbed with
                    # "DO grab npc"). Small fast steps are a hand dragging smoothly; big ones are jumps.
                    w = text.split()
                    step = [float(w[2]), float(w[3]), float(w[4])]
                    pause = float(w[6]) if len(w) > 6 else 0.2
                    for _ in range(int(w[5])):
                        me = position('14')
                        if me:
                            papyrus('ObjectReference', 'SetPosition', '14', [me[0] + step[0], me[1] + step[1], me[2] + step[2]])
                        time.sleep(pause)
                    print('   (walked %d steps of (%.0f, %.0f, %.0f))' % (int(w[5]), step[0], step[1], step[2]), flush=True)
                elif text.startswith('DO place '):
                    # "DO place <base hex> [essential]": one of that actor placed 90 units ahead of the player, remembered
                    # as "placed" for later "DO papyrus" lines; "essential" makes its base essential, so it goes down instead
                    # of dying. Papyrus through DevBench needs every argument, PlaceAtMe's defaults included.
                    words = text.split()
                    spawned_before = len(session_lines())
                    returned = papyrus('ObjectReference', 'PlaceAtMe', '14', [{'form': '0x' + words[2]}, 1, False, False])
                    self.placed = returned.get('formId', '').replace('0x', '') if isinstance(returned, dict) else None
                    if not self.placed and isinstance(returned, dict):
                        # DevBench returns no form id for a reference with a script attached (Lydia's WIDeadBodyCleanupScript).
                        # The client logs every new actor it finds, so the newest of that name is the one placed.
                        name = papyrus('Form', 'GetName', words[2])
                        for _ in range(20):
                            time.sleep(0.25)
                            found = [m2.group(1) for m2 in (re.search(r'Spawn Actor: ([0-9A-Fa-f]+), and NPC (.*)$', l) for l in session_lines()[spawned_before:])
                                     if m2 and m2.group(2).strip() == name]
                            if found:
                                self.placed = found[-1]
                                break
                    if self.placed:
                        heading = math.radians(papyrus('ObjectReference', 'GetAngleZ', '14') or 0.0)
                        papyrus('ObjectReference', 'MoveTo', self.placed, [{'form': '0x14'}, 90.0 * math.sin(heading), 90.0 * math.cos(heading), 0.0, False])
                        if 'essential' in words[3:]:
                            base = papyrus('ObjectReference', 'GetBaseObject', self.placed)
                            if isinstance(base, dict):
                                papyrus('ActorBase', 'SetEssential', base.get('formId', '').replace('0x', ''), [True])
                    print('   (placed %s as %s)' % (words[2], self.placed), flush=True)
                    if not self.placed:
                        self.results.append((False, text.strip(), 'PlaceAtMe returned %s' % returned))
                elif text.startswith('DO grip both '):
                    # "DO grip both <ms>": both controller grips held that long, then let go (both_grips), as a downed player gives up.
                    ms = int(text.split()[3])
                    try:
                        r = both_grips(ms)
                        print('   (both grips held %d ms: %s)' % (ms, 'queued' if r.get('queued') else r), flush=True)
                        time.sleep(ms / 1000.0 + 0.5)
                    except Exception as e:
                        print('   FAIL  grip both: %s' % e, flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.startswith('DO release '):
                    # "DO release left|right": HIGGS's hand lets go of what it holds (the hand switched off and on again).
                    is_left = text.split()[2] == 'left'
                    papyrus_global('HiggsVR', 'DisableHand', [is_left])
                    time.sleep(0.5)
                    papyrus_global('HiggsVR', 'EnableHand', [is_left])
                    print('   (HIGGS %s hand let go; it now holds %s)' % ('left' if is_left else 'right', papyrus_global('HiggsVR', 'GetGrabbedObject', [is_left])), flush=True)
                elif text.startswith('DO grab dropped ') or text.startswith('DO grab npc ') or text.startswith('DO grab ref '):
                    # "DO grab dropped left|right": HIGGS's hand takes what "DO drop ... here" dropped (HiggsVR.GrabObject;
                    # no controllers needed). "DO grab npc left|right": the game's copy of the bot's NPC (a corpse, say).
                    # "DO grab ref <hex> left|right [pull]": any reference, such as an object the other player just moved.
                    # With "pull" the hold is not checked: the rig's controllers make HIGGS pull an object to the player's
                    # feet but never close the hand on it, so only the pull (sent as this player's carry) can be checked.
                    words = text.split()
                    pull_only = words[-1] == 'pull'
                    if words[2] == 'ref':
                        ref, is_left = words[3], words[4] == 'left'
                    else:
                        is_left = words[3] == 'left'
                        ref = getattr(self, 'dropped_here', None) if words[2] == 'dropped' else self.subject('npc')[0]
                    # Why a hand would refuse: HIGGS grabs only with an idle (or selecting) hand that is not disabled and
                    # not the hand of a drawn weapon.
                    for side in (False, True):
                        print('   (HIGGS %s hand: can grab %s, disabled %s)' % ('left' if side else 'right', papyrus_global('HiggsVR', 'CanGrabObject', [side]),
                                                                            papyrus_global('HiggsVR', 'IsDisabled', [side])), flush=True)
                    can = papyrus_global('HiggsVR', 'CanGrabObject', [is_left])
                    if can is False and papyrus_global('HiggsVR', 'CanGrabObject', [not is_left]) is True:
                        is_left = not is_left
                        print('   (the other hand can: grabbing with the %s)' % ('left' if is_left else 'right'), flush=True)
                        can = True
                    grab = papyrus_global('HiggsVR', 'GrabObject', [{'form': '0x' + ref}, is_left]) if ref else 'nothing dropped'
                    time.sleep(1.0)
                    held = papyrus_global('HiggsVR', 'GetGrabbedObject', [is_left])
                    held_id = held.get('formId', '') if isinstance(held, dict) else str(held)
                    ok = bool(ref) and isinstance(held, dict) and int(held_id, 16) & 0xFFFFFFFF == int(ref, 16) & 0xFFFFFFFF
                    print('   (HIGGS: can grab %s, grab %s, now holds %s)' % (can, grab, held), flush=True)
                    if not pull_only:
                        self.results.append((ok, text.strip(), 'holds %s' % held))
                elif text.strip() == 'DO bring held to copy weapon':
                    # Moves the player by however far the held item is from the centre of the copy's sword body, so the
                    # hand -- and what it holds -- goes there.
                    copy_form, _ = self.subject('copy')
                    ref = getattr(self, 'dropped_here', None)
                    centre = latest_blade_centre(session_lines()[self.log_start:], copy_form)
                    held_at = position(ref) if ref else None
                    player_at = position('14')
                    if not (centre and held_at and player_at):
                        self.results.append((False, text.strip(), 'blade %s, held %s, player %s' % (centre, held_at, player_at)))
                    else:
                        delta = [centre[i] - held_at[i] for i in range(3)]
                        wanted = [player_at[i] + delta[i] for i in range(3)]
                        papyrus('ObjectReference', 'SetPosition', '14', wanted)
                        self.bring = (centre, ref)
                        print('   (player moved by (%.0f, %.0f, %.0f) so the held item meets the blade at (%.0f, %.0f, %.0f))' % (delta[0], delta[1], delta[2], centre[0], centre[1], centre[2]), flush=True)
                        # Where the player and the held item stay: a player put in the air falls, and one put into rock is pushed out.
                        t0 = time.time()
                        for at_s in (0.3, 1.0, 2.0):
                            time.sleep(max(0.0, t0 + at_s - time.time()))
                            me, held = position('14'), position(ref)
                            if me and held:
                                print('   (%.1f s later: player off where it was put by (%.0f, %.0f, %.0f), held item %.0f from the blade centre)' % (
                                    at_s, me[0] - wanted[0], me[1] - wanted[1], me[2] - wanted[2], math.dist(held, centre)), flush=True)
                elif text.startswith('DO reach npc '):
                    # "DO reach npc right|left <up>": the player is stood 50 units from the bot's NPC, facing it; that
                    # controller goes to the NPC, <up> units above its feet (600 ms), presses grip (300 ms), pulls back
                    # 40 cm still gripping (1000 ms), holds 1500 ms and lets go. Meanwhile HIGGS is asked what that hand
                    # holds, and where this game shows the NPC. A living NPC is held by PLANCK's grab, which needs real
                    # controllers, not HiggsVR.GrabObject (live-npc-grab, 2026-10-06: "now holds None").
                    words = text.split()
                    side, up = words[3], float(words[4])
                    is_left = side == 'left'
                    form, why = self.subject('npc')
                    try:
                        if not form:
                            raise RuntimeError('no npc: %s' % why)
                        theirs, mine = position(form), position('14')
                        if not theirs or not mine:
                            raise RuntimeError('no position for the player or the npc')
                        dx, dy = theirs[0] - mine[0], theirs[1] - mine[1]
                        far = math.hypot(dx, dy) or 1.0
                        papyrus('ObjectReference', 'SetPosition', '14', [theirs[0] - 50 * dx / far, theirs[1] - 50 * dy / far, theirs[2]])
                        tool('console', {'action': 'exec', 'command': 'player.setangle z %.1f' % (math.degrees(math.atan2(dx, dy)) % 360.0)})
                        time.sleep(1.0)
                        y = up / 70.0
                        rest = (0.25 if not is_left else -0.25, 1.0, -0.1)
                        r = hand_path(side, [(0, rest[0], rest[1], rest[2], False), (600, 0.0, y, -50 / 70.0, False), (900, 0.0, y, -50 / 70.0, True),
                                             (1900, 0.0, y, -50 / 70.0 + 0.4, True), (3400, 0.0, y, -50 / 70.0 + 0.4, True), (3600, 0.0, y, -50 / 70.0 + 0.4, False)])
                        print('   (%s hand to the npc %.0f up, grip, pull 40 cm: %s)' % (side, up, 'queued' if r.get('queued') else r), flush=True)
                        t0 = time.time()
                        held_any = None
                        while time.time() - t0 < 4.5:
                            held = papyrus_global('HiggsVR', 'GetGrabbedObject', [is_left])
                            at, me = position(form), position('14')
                            if isinstance(held, dict):
                                held_any = held
                            print('   (%.1f s: hand holds %s; npc at %s, %s from the player)' % (time.time() - t0, held.get('formId') if isinstance(held, dict) else held,
                                  '(%.0f, %.0f, %.0f)' % tuple(at) if at else '?', '%.0f' % math.dist(at, me) if at and me else '?'), flush=True)
                            time.sleep(0.4)
                        print('   (the %s hand held %s)' % (side, held_any), flush=True)
                    except Exception as e:
                        print('   FAIL  reach npc: %s' % e, flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.startswith('DO hold '):
                    # "DO hold copy|npc right|left <up> <seconds>": the player is stood 50 units from the bot's character or NPC, facing it;
                    # that controller goes to it, <up> units above its feet (600 ms), stays there <seconds> without gripping, and comes back.
                    # A revive is a hand held on a downed player (ReviveService); "DO reach" grips and pulls instead.
                    words = text.split()
                    who, side, up, seconds = words[2], words[3], float(words[4]), float(words[5])
                    form, why = self.subject(who)
                    try:
                        if not form:
                            raise RuntimeError('no %s: %s' % (who, why))
                        theirs, mine = position(form), position('14')
                        if not theirs or not mine:
                            raise RuntimeError('no position for the player or the %s' % who)
                        dx, dy = theirs[0] - mine[0], theirs[1] - mine[1]
                        far = math.hypot(dx, dy) or 1.0
                        papyrus('ObjectReference', 'SetPosition', '14', [theirs[0] - 50 * dx / far, theirs[1] - 50 * dy / far, theirs[2]])
                        tool('console', {'action': 'exec', 'command': 'player.setangle z %.1f' % (math.degrees(math.atan2(dx, dy)) % 360.0)})
                        time.sleep(1.0)
                        y = up / 70.0
                        rest = (0.25 if side != 'left' else -0.25, 1.0, -0.1)
                        held_until = 600 + int(seconds * 1000)
                        r = hand_path(side, [(0, *rest, False), (600, 0.0, y, -50 / 70.0, False), (held_until, 0.0, y, -50 / 70.0, False), (held_until + 600, *rest, False)])
                        print('   (%s hand on the %s %.0f up for %.1f s: %s)' % (side, who, up, seconds, 'queued' if r.get('queued') else r), flush=True)
                    except Exception as e:
                        print('   FAIL  hold %s: %s' % (who, e), flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.startswith('DO mark '):
                    # "DO mark <name> player|copy|npc|ref <hex>|held left|right": where it is now, for "moved" and "near".
                    words = text.split()
                    thing, why = self.thing(words[3:])
                    at = self.where(thing) if thing else None
                    if at:
                        self.marks[words[2]] = (thing, at)
                        print('   (%s marked at (%.0f, %.0f, %.0f))' % (words[2], *at), flush=True)
                    else:
                        print('   FAIL  mark %s: %s' % (words[2], why or 'no position'), flush=True)
                        self.results.append((False, text.strip(), why or 'no position'))
                elif text.startswith('DO sample npc '):
                    # "DO sample npc <seconds>": where this game shows the bot's NPC, and how far from the player, every
                    # half second (the bot, which owns it, keeps it where it stands).
                    seconds = float(text.split()[3])
                    form, _ = self.subject('npc')
                    t0 = time.time()
                    while time.time() - t0 < seconds:
                        at = position(form) if form else None
                        me = position('14')
                        if at and me:
                            print('   (npc at (%.0f, %.0f, %.0f), %.0f from the player)' % (at[0], at[1], at[2], math.dist(at, me)), flush=True)
                        time.sleep(0.5)
                elif text.startswith('DO record '):
                    # "DO record <name> copy|npc|placed|player|ref <hex> <seconds>": samples that actor's body (or that reference's
                    # open state) in the background for that long, while the script goes on; the checks that name it wait for the end.
                    words = text.split()
                    name, who = words[2], words[3]
                    form = words[4] if who == 'ref' else getattr(self, 'placed', None) if who == 'placed' else '14' if who == 'player' else self.subject(who)[0]
                    seconds = float(words[-1])
                    if form:
                        self.records[name] = Recording(form, seconds)
                        print('   (recording %s: %s %s for %.0f s, %s)' % (name, who, form, seconds, 'its body' if self.records[name].body else 'its open state'), flush=True)
                    else:
                        print('   FAIL  record %s: no %s to sample' % (name, who), flush=True)
                        self.results.append((False, text.strip(), 'no %s to sample' % who))
                elif text.startswith('DO watch npc ') or text.startswith('DO watch copy '):
                    # "DO watch npc|copy <seconds>": that copy's pelvis, body height (body_pose) and reference, sampled for that long and kept for
                    # "npc|copy went down" and "npc slide below". Put it before the bot's "replay", which goes on meanwhile.
                    who = text.split()[2]
                    seconds = float(text.split()[3])
                    form, why = self.subject(who)
                    self.watch = []
                    t0 = time.time()
                    self.watch_start = t0
                    self.watch_form = form
                    while form and time.time() - t0 < seconds:
                        pose = body_pose(form)
                        if pose:
                            self.watch.append((time.time() - t0, pose[0], pose[1], position(form)))
                        time.sleep(0.25)
                    heights = [h for _, _, h, _ in self.watch]
                    print('   (watched %s %s for %.0f s: %d samples, body height %s)' % (
                        who, form or '?', seconds, len(heights), ('lowest %.0f, highest %.0f' % (min(heights), max(heights))) if heights else 'never read: %s' % (why or 'no pose')), flush=True)
                elif text.startswith('DO sample held '):
                    # "DO sample held <seconds>": where the held item is against the blade centre and the player, every half second.
                    seconds = float(text.split()[3])
                    centre, ref = getattr(self, 'bring', (None, getattr(self, 'dropped_here', None)))
                    t0 = time.time()
                    while time.time() - t0 < seconds:
                        at = position(ref) if ref else None
                        me = position('14')
                        if at and me:
                            print('   (held at (%.0f, %.0f, %.0f): %s from the blade centre, %.0f from the player)' % (
                                at[0], at[1], at[2], '%.0f' % math.dist(at, centre) if centre else '?', math.dist(at, me)), flush=True)
                        time.sleep(0.5)
                elif text.startswith('DO drop '):
                    # "DO drop <base hex> onto copy weapon <units>": the player drops one of that item and it is put that
                    # far above where the copy's drawn weapon is (the client's "PhysicsProbe: ... right weapon drawn at"
                    # line), to see whether anything physical is there to catch it ("CHECK drop on copy weapon").
                    # "DO drop <base hex> onto copy weapon body <units>": above the centre of mass of the weapon's body
                    # instead ("VRWeaponBody: ... centre of mass at"), mid-blade rather than the grip.
                    # "DO drop <base hex> again <units>": above the same place as the last drop, e.g. once the weapon is
                    # sheathed, where nothing should catch it ("CHECK drop fell").
                    words = text.split()
                    base = words[2]
                    up = float(words[-1]) if len(words) > 3 and re.match(r'^-?[\d.]+$', words[-1]) else 10.0
                    try:
                        copy_form, _ = self.subject('copy')
                        drawn = None
                        if words[3] == 'again':
                            if not getattr(self, 'drop_test', None):
                                raise RuntimeError('nothing was dropped before')
                            drawn = self.drop_test[1]
                        else:
                            pattern = (r'VRWeaponBody: actor ([0-9A-F]+) right weapon body .* centre of mass at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)'
                                       if 'body' in words[3:7] else
                                       r'PhysicsProbe: actor ([0-9A-F]+) right weapon drawn at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)')
                            for l in session_lines()[self.log_start:]:
                                m2 = re.search(pattern, l)
                                if m2 and (not copy_form or m2.group(1).upper() == copy_form.upper()):
                                    drawn = tuple(float(m2.group(i)) for i in (2, 3, 4))
                        if not drawn:
                            raise RuntimeError("the client has not said where the copy's weapon is")
                        tool('console', {'action': 'exec', 'command': 'player.additem %s 1' % base})
                        time.sleep(0.5)
                        tool('console', {'action': 'exec', 'command': 'player.drop %s 1' % base})
                        ref = None
                        for _ in range(20):
                            time.sleep(0.25)
                            refs = [m2.group(1) for m2 in (re.search(r'DroppedItem: dropped \S+ x\d+ as ([0-9A-Fa-f]+)', l) for l in session_lines()[self.log_start:]) if m2]
                            if refs:
                                ref = refs[-1]
                                break
                        if not ref:
                            raise RuntimeError('the drop was never announced by the client')
                        papyrus('ObjectReference', 'SetPosition', ref, [drawn[0], drawn[1], drawn[2] + up])
                        self.drop_test = (ref, drawn)
                        print("   (dropped %s as %s, put %.0f units above the copy's weapon at (%.0f, %.0f, %.0f))" % (base, ref, up, drawn[0], drawn[1], drawn[2]), flush=True)
                    except Exception as e:
                        print('   FAIL  drop onto weapon: %s' % e, flush=True)
                        self.results.append((False, text.strip(), str(e)))
                elif text.strip().startswith('DO server restart'):
                    try:
                        rest = text.strip()[len('DO server restart'):].strip()
                        noticed, back = restart_server(rest[len('after console '):] if rest.startswith('after console ') else None)
                        print('   (server restarted: the game noticed after %.0f s and was back in after %.0f s)' % (noticed, back), flush=True)
                    except Exception as e:
                        print('   FAIL  server restart: %s' % e, flush=True)
                        self.results.append((False, 'server restart', str(e)))
                elif text.strip() == 'DO load last':
                    try:
                        # The save the session started from (headless.ps1 keeps its name), not the most recent one:
                        # that is wherever the player last played.
                        started_from = ''
                        try:
                            with io.open(os.path.join(os.environ.get('TEMP', ''), 'st-headless-state.json'), encoding='utf-8-sig') as f:
                                started_from = json.load(f).get('save') or ''
                        except Exception:
                            pass
                        r = tool('game', {'action': 'load', 'name': started_from}) if started_from else tool('game', {'action': 'loadLast'})
                        print('   (load: %s)' % r.get('name'), flush=True)
                    except Exception as e:
                        print('   FAIL  load last: %s' % type(e).__name__, flush=True)
                        self.results.append((False, 'load last', type(e).__name__))
                elif text.startswith('DO load save '):
                    # "DO load save <name>": that save of the profile (another character's, say), while connected.
                    name = text[len('DO load save '):].strip()
                    try:
                        r = tool('game', {'action': 'load', 'name': name})
                        print('   (load: %s)' % r.get('name'), flush=True)
                    except Exception as e:
                        print('   FAIL  load save %s: %s' % (name, type(e).__name__), flush=True)
                        self.results.append((False, 'load save ' + name, type(e).__name__))
                elif text.startswith('DO console '):
                    # Something only the game can do, at a moment only the script knows: move the player, say.
                    command = text[len('DO console '):].strip()
                    try:
                        tool('console', {'action': 'exec', 'command': command})
                        print('   (console: %s)' % command, flush=True)
                    except Exception as e:
                        print('   FAIL  console %s: %s' % (command, type(e).__name__), flush=True)
                        self.results.append((False, 'console ' + command, type(e).__name__))
                elif text.startswith('SHOT '):
                    words = text[5:].split()
                    name = '%s-%s' % (self.script, words[0])
                    target = None
                    if len(words) >= 3 and words[1] == 'of':
                        if words[2] == 'player':
                            target = position('14')
                        elif words[2] == 'drop':
                            ref = dropped_ref()
                            target = position(ref) if ref else None
                        elif words[2] == 'placed':
                            target = position(self.placed) if getattr(self, 'placed', None) else None
                        else:
                            form, _ = self.subject(words[2])
                            target = position(form) if form else None
                        if not target:
                            print('   FAIL  screenshot %s: nothing to point the camera at' % name, flush=True)
                            self.results.append((False, 'screenshot ' + name, 'no %s to look at' % words[2]))
                            continue
                    try:
                        path = screenshot(name, target)
                    except Exception as e:
                        path = None
                        print('   FAIL  screenshot %s: %s' % (name, type(e).__name__), flush=True)
                    if path:
                        self.shots.append(path)
                        print('   (screenshot: %s)' % path, flush=True)
                    else:
                        self.results.append((False, 'screenshot ' + name, 'no picture came back'))
                else:
                    print('   ' + text, flush=True)
        proc.wait()
        log.close()
        # Anything the client could not find while this script ran fails it: it does nothing on this setup.
        for problem in health_problems(session_lines()[self.log_start:]):
            self.results.append((False, 'health: ' + problem, 'logged during this script'))
            print('   FAIL  health: %s' % problem, flush=True)
        # What looked wrong on screen while it ran, after every script: reported here, judged only by "CHECK smooth".
        self.jank = jank(session_lines()[self.log_start:])
        print('   jank: %s' % jank_text(self.jank), flush=True)
        # The bot's own checks ("expect ..."), which the game is not asked about: its verdict is the last word of its log.
        # Without this a script that only uses them -- live-sender, the real game as the sender -- read as "not run".
        with io.open(os.path.join(RELEASE, 'logs', self.script + '-bot.log'), encoding='utf-8', errors='replace') as f:
            text = f.read()
        # The bot warns and goes on when a line cannot do its part ("nobody to capture", "nothing to replay",
        # "no clash relayed yet"): what follows then tests something else, often nothing.
        for line in text.splitlines():
            if re.search(r'\[(warning|error)\] (Line \d+:|\[script\] (?!FAILED line|\d+ of \d+ checks failed))', line) and 'checks failed' not in line:
                self.invalid.append('bot: ' + line.split('] ', 2)[-1].strip())
        verdict = re.findall(r'\[script\] all (\d+) checks passed|\[script\] (\d+) of (\d+) checks failed', text)
        if verdict:
            passed_all, failed, of = verdict[-1]
            if passed_all:
                self.results.append((True, 'bot: all %s of its own checks' % passed_all, ''))
                print('   PASS  bot: all %s of its own checks' % passed_all, flush=True)
            else:
                self.results.append((False, 'bot: %s of %s of its own checks' % (failed, of), 'see logs/%s-bot.log' % self.script))
                print('   FAIL  bot: %s of %s of its own checks failed' % (failed, of), flush=True)
        if isinstance(self.heal_rate, (int, float)):
            papyrus('Actor', 'ForceActorValue', '14', ['HealRateMult', float(self.heal_rate)])
        return self.results


# Result rows that are not checks: what an action of the script reported when it could not be done.
ACTION_ROWS = ('DO ', 'screenshot ', 'console ', 'server restart', 'load last', 'load save ')


# Scripts whose red run is the bot behaving as before the fix, with the variable that makes it so.
RED_ENV = {'live-loss-playback': 'STBOT_RELIABLE_MOVEMENT'}


def verdict(run, results, expect_fail):
    """What a script's run proves.

    INVALID when a precondition did not hold or the bot could not do its part: the run proves nothing either way.
    INCONCLUSIVE when nothing failed but a check found nothing to judge (SKIP).
    With --expect-fail (a build without the fix): RED when a check failed, which shows the test catches the bug;
    NOT RED when it passed anyway, which shows it does not; INVALID when one of its actions failed."""
    bad = [text for ok, text, _ in results if ok is False]
    skipped = [text for ok, text, _ in results if ok is None]
    if not results and not run.invalid:
        return 'not run', 'NOT RUN: no check was reached; see logs/%s-bot.log' % run.script
    # The bot's old behaviour belongs only to the red run of a script that asks for it.
    # The variable lasts as long as the shell, so a later run in the same shell would test the old behaviour unseen.
    needed = RED_ENV.get(run.script)
    if os.environ.get('STBOT_RELIABLE_MOVEMENT') == '1' and not (expect_fail and needed == 'STBOT_RELIABLE_MOVEMENT'):
        return 'invalid', 'INVALID: the bot sent movement reliably (STBOT_RELIABLE_MOVEMENT=1), as before playback session B; remove it (Remove-Item Env:STBOT_RELIABLE_MOVEMENT) for this run'
    if expect_fail and needed and os.environ.get(needed) != '1':
        return 'invalid', 'INVALID: the red run of this script is the bot behaving as before the fix: set %s=1 for it' % needed
    if run.invalid:
        return 'invalid', 'INVALID: ' + '; '.join(run.invalid)
    if expect_fail:
        # Only a failed check shows the bug.
        # A failed action means the script could not do its part, and a health problem is not what the fix is about.
        actions = [text for text in bad if text.startswith(ACTION_ROWS)]
        if actions:
            return 'invalid', 'INVALID: the script could not do its part: ' + '; '.join(actions)
        health = [text for text in bad if text.startswith('health: ')]
        checks = [text for text in bad if not text.startswith('health: ')]
        also = (' (also logged: %s)' % '; '.join(health)) if health else ''
        return ('red', 'RED as expected: ' + '; '.join(checks) + also) if checks else ('not red', 'NOT RED: it passed without the fix, so it does not catch the bug' + also)
    if bad:
        return 'failed', 'FAILED: ' + '; '.join(bad)
    if skipped:
        return 'inconclusive', 'INCONCLUSIVE: nothing to judge in ' + '; '.join(skipped)
    return 'ok', 'ok'


def write_report(rows, setup):
    """One page for a person to look over after a run: each script's verdict, checks, jank and SHOT pictures.
    Written next to the pictures (logs/live-report.html) and replaced by the next run."""
    import html
    logs = os.path.join(RELEASE, 'logs')
    out = ['<!doctype html><meta charset="utf-8"><title>Rig run</title><style>'
           'body{font:14px system-ui,sans-serif;margin:24px;max-width:1100px}h2{margin:28px 0 6px}.ok{color:#26703c}.bad{color:#a32020}'
           'table{border-collapse:collapse}td{padding:2px 10px 2px 0;vertical-align:top}img{max-width:340px;margin:4px 8px 4px 0}</style>',
           '<h1>Rig run %s</h1><p>Modlist %s, profile %s</p>' % (html.escape(time.strftime('%Y-%m-%d %H:%M')), html.escape(setup[0]), html.escape(setup[1]))]
    for name, kind, note, results, jank_line, shots in rows:
        out.append('<h2>%s <span class="%s">%s</span></h2><p>jank: %s</p><table>' % (html.escape(name), 'ok' if kind in ('ok', 'red') else 'bad', html.escape(note), html.escape(jank_line)))
        for ok, text, detail in results:
            mark = 'skip' if ok is None else 'pass' if ok else 'FAIL'
            out.append('<tr><td class="%s">%s</td><td>%s</td><td>%s</td></tr>' % ('ok' if ok else 'bad' if ok is False else '', mark, html.escape(text), html.escape(str(detail)[:300])))
        out.append('</table>')
        for path in shots:
            rel = os.path.relpath(path, logs).replace(os.sep, '/')
            out.append('<a href="%s"><img src="%s" alt="%s"></a>' % (html.escape(rel), html.escape(rel), html.escape(os.path.basename(path))))
    path = os.path.join(logs, 'live-report.html')
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))
    return path


def run_conditions(lines):
    """The rig's frame rate and the bot's network during a run, kept with its result."""
    fps = sorted(int(m.group(1)) for m in (re.search(r'Perf last 30 s: .*\((\d+) fps\)', l) for l in lines) if m)
    network = {k: os.environ[k] for k in ('STBOT_FAKE_LAG_MS', 'STBOT_FAKE_LOSS_PCT', 'STBOT_FAKE_REORDER_PCT', 'STBOT_RELIABLE_MOVEMENT') if os.environ.get(k)}
    return (fps[len(fps) // 2] if fps else None), network


def main():
    args = sys.argv[1:]
    if args == ['--list']:
        print('\n'.join(ALL))
        return 0
    # --expect-fail: the build in the game is one without the fix the scripts guard; each must fail to count as a test.
    expect_fail = '--expect-fail' in args
    args = [a for a in args if a != '--expect-fail']
    scripts = ALL if (not args or args == ['--all']) else SYNC if args == ['--sync'] else args
    try:
        state = tool('inspect', {'kind': 'state'})
    except Exception as e:
        print('The game does not answer on DevBench (%s). Run Tools/VR/headless.ps1 up first.' % type(e).__name__)
        return 2
    if not state.get('playerLoaded'):
        print('The game is up but no save is loaded.')
        return 2

    # Scripts are tracked in Code/bot/scripts; the bot reads them from the release folder.
    src = os.path.join(ROOT, 'Code', 'bot', 'scripts')
    for name in scripts:
        with io.open(os.path.join(src, name + '.txt'), 'rb') as f:
            data = f.read()
        with io.open(os.path.join(RELEASE, 'scripts', name + '.txt'), 'wb') as f:
            f.write(data)

    if not ensure('tcai', 'is Off'):
        print('Could not switch combat AI off; wildlife may attack the player during the run.')
    if not ensure('tgm', 'disabled'):
        print('Could not confirm god mode is off; the damage checks may read zero.')

    failed = 0
    total = 0
    table = []
    shots = []
    report_rows = []
    print('Modlist %s, profile %s' % SETUP, flush=True)
    # The game's start-up, its load and its connection, before any script: what the client could not find on this setup,
    # and the SKSE plugins that did not load (a test without one does not stand for the players' game).
    lines = session_lines()
    startup = health_problems(lines) + skse_problems()
    print('Start-up health: %s' % ('; '.join(startup) or 'nothing missing'), flush=True)
    record({'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'script': 'start-up health', 'build': client_build(lines),
            'verdict': 'failed' if startup else 'ok', 'expect_fail': expect_fail, 'problems': startup})
    table.append(('start-up health', 1, 1 if startup else 0, ('FAILED: ' + '; '.join(startup)) if startup else 'ok'))
    total += 1
    if startup:
        failed += 1
    for name in scripts:
        started = time.strftime('%Y-%m-%d %H:%M:%S')
        # A game that went down takes every later script with it; say so once instead of failing each in turn.
        try:
            urllib.request.urlopen('http://127.0.0.1:8921/api/health', timeout=6).read()
        except Exception:
            table.append((name, 0, 0, 'NOT RUN: the game is gone'))
            failed += 1
            record({'time': started, 'script': name, 'verdict': 'not run', 'note': 'the game is gone', 'expect_fail': expect_fail})
            continue
        run = Run(name)
        results = run.run()
        kind, note = verdict(run, results, expect_fail)
        bad = [text for ok, text, _ in results if ok is False]
        total += len(results)
        # Anything but a plain pass (or, expecting failure, a red) is not a result to rely on.
        if kind not in ('ok', 'red'):
            failed += 1
        shots += run.shots
        lines = session_lines()
        fps, network = run_conditions(lines[getattr(run, 'log_start', 0):])
        record({'time': started, 'script': name, 'build': client_build(lines), 'verdict': kind, 'expect_fail': expect_fail,
                'fps': fps, 'network': network, 'invalid': run.invalid, 'jank': {kind: count for kind, (count, _, _) in getattr(run, 'jank', {}).items()},
                'checks': [{'result': 'skip' if ok is None else ('pass' if ok else 'fail'), 'check': text, 'detail': str(detail)[:300]}
                           for ok, text, detail in results]})
        table.append((name, len(results), len(bad), note))
        report_rows.append((name, kind, note, results, jank_text(getattr(run, 'jank', {})), run.shots))
        time.sleep(6)  # let the server finish removing the bot before the next one joins

    print('\n%-20s %6s %6s  %s' % ('script', 'checks', 'failed', ''))
    for name, n, bad, note in table:
        print('%-20s %6d %6d  %s' % (name, n, bad, note))
    print('%d checks; %d scripts not giving a plain result (failed, invalid, inconclusive or not run)' % (total, failed))
    print('history: ' + HISTORY)
    for path in shots:
        print('screenshot: ' + path)
    print('report: ' + write_report(report_rows, SETUP))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
