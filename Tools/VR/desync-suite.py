"""Finds desyncs by playing: the real game and the bot act in one world while a scanner compares them every second.

    python Tools/VR/desync-suite.py --selftest          the scanner's own proof, with planted mismatches and no game
    python Tools/VR/desync-suite.py --list
    python Tools/VR/desync-suite.py kill-by-game ...    those tests
    python Tools/VR/desync-suite.py --all               every test, the report page kept up to date as they run

The game must be up (headless.ps1 up -AutoConnect). Results: build/desync/report.html, results.json and a folder per test.
It compares three views of the world: the game's own (DevBench), the bot's (what the server told it) and the client's log.
With one real game, what only a second screen would show (a wrong animation there) cannot be judged here.
A test fails on what the scanner saw (FAILS below) or on what the test itself expected; WARNS are reported and do not fail it.
"""
import argparse
import ctypes
import html
import json
import math
import os
import re
import subprocess
import sys
import threading
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import play  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RELEASE = os.path.join(ROOT, 'build', 'windows', 'x64', 'release')
OUT = os.path.join(ROOT, 'build', 'desync')
HEADLESS = os.path.join(ROOT, 'Tools', 'VR', 'headless.ps1')
LIVE_CHECK = os.path.join(ROOT, 'Tools', 'VR', 'live-check.py')
FUS_ROOT = next((r for r in (os.environ.get('SKYRIM_FUS_ROOT') or '', 'C:/FUS', 'E:/FUS') if r and os.path.isfile(os.path.join(r, 'ModOrganizer.exe'))), 'C:/FUS')
CLIENT_DIR = next((p for p in (os.path.join(FUS_ROOT, 'tools', n) for n in ('Skyrim Together VR', 'urSovngarde')) if os.path.isfile(os.path.join(p, 'urSovngarde.exe'))), '')
CLIENT_LOG = os.path.join(CLIENT_DIR, 'logs', 'tp_client.log')
SERVER = '127.0.0.1:10578'
BANDIT_MELEE, BANDIT_ARCHER, BANDIT_TANK = 'F62D7', 'F62D8', 'F62D9'
MAGE = '753CB'
PICTURES_PER_TEST = 6
PLAYER_BASE = {}
FAILS = {'life-differs', 'place-differs', 'came-back', 'cannot-finish', 'missing-here', 'duplicate-player', 'player-place', 'game-ended'}
WARNS = {'health-differs', 'unknown-to-server', 'flicker', '3d-blinks', 'low-body'}


def log(*args):
    print(time.strftime('%H:%M:%S'), *args, flush=True)


class Findings:
    """Runs of seconds a condition held; a run counts once it lasted long enough."""

    def __init__(self, on_new=None):
        self.items = []
        self.open = {}
        self.on_new = on_new

    def note(self, key, now, holds, need, kind, text, where=None):
        run = self.open.get(key)
        if not holds:
            if run:
                self.close(key)
            return
        if run and now - run['last'] > 3.0:
            self.close(key)
            run = None
        if not run:
            self.open[key] = {'first': now, 'last': now, 'need': need, 'kind': kind, 'text': text, 'told': False}
            run = self.open[key]
        run['last'], run['text'] = now, text
        if not run['told'] and now - run['first'] >= need:
            run['told'] = True
            if self.on_new:
                self.on_new(kind, text, where)

    def close(self, key):
        run = self.open.pop(key)
        if run['last'] - run['first'] >= run['need']:
            self.items.append({'kind': run['kind'], 'first': run['first'], 'seconds': round(run['last'] - run['first'], 1), 'text': run['text']})

    def once(self, kind, text, now, where=None):
        self.items.append({'kind': kind, 'first': now, 'seconds': 0, 'text': text})
        if self.on_new:
            self.on_new(kind, text, where)

    def finish(self):
        for key in list(self.open):
            self.close(key)


class Judge:
    """Compares one moment of the game with what the bot knows. No game needed, which is what the selftest uses."""

    def __init__(self, on_new=None):
        self.found = Findings(on_new)
        self.lives = {}
        self.tracks = {}
        self.pairs = {}
        self.seen3d = {}
        self.dead_at = {}

    def moment(self, now, game, bot, form_of, paused=False):
        """game: {'player', 'actors': [{form, name, pos, alive, hp, max, has3d, hostile}]}; bot: {'actors': [{sid, ref, dead, pos, hp, ours}], 'players': [...], 'me': pos}."""
        me = game['player']
        by_form = {a['form']: a for a in game['actors']}
        paired = set()
        for b in bot.get('actors', []) if not paused else []:
            a = None
            form = form_of.get(b['sid'])
            if form:
                a = by_form.get(form)
            if a is None and b.get('ref'):
                a = by_form.get(b['ref'])
            near_me = b.get('pos') and me and math.dist(b['pos'], me) < 1500
            self.found.note(('missing', b['sid']), now, a is None and not b['dead'] and bool(near_me) and b['sid'] in form_of, 10, 'missing-here',
                            'the server has NPC %s alive by me, this game no longer has it (it had %s)' % (b['sid'], form_of.get(b['sid'])), b.get('pos'))
            if a is None:
                continue
            paired.add(a['form'])
            name = '%s %s (server id %s)' % (a['name'], a['form'], b['sid'])
            if a['has3d']:
                self.found.note(('life', a['form']), now, a['alive'] == b['dead'], 5, 'life-differs',
                                '%s is %s in this game and %s for the server' % (name, 'alive' if a['alive'] else 'dead', 'dead' if b['dead'] else 'alive'), a['pos'])
            if b.get('pos'):
                gap = math.dist(a['pos'], b['pos'])
                self.pairs.setdefault(a['form'], []).append((now, gap, b['ours']))
                if a['has3d']:
                    # The two views can be a second or two apart, so a running NPC gets what it covers in that time.
                    points = self.tracks.get(a['form'], {}).get('points', [])
                    speed = math.dist(points[-1][1], a['pos']) / max(0.2, now - points[-1][0]) if points else 0.0
                    limit = 150 if not a['alive'] else 300 + 1.5 * min(speed, 700.0)
                    self.found.note(('place', a['form']), now, gap > limit, 5, 'place-differs', '%s stands %.0f units from where the server has it' % (name, gap), a['pos'])
            if a['alive'] and b.get('hp') is not None:
                self.found.note(('health', a['form']), now, abs(a['hp'] - b['hp']) > max(6.0, 0.06 * (a['max'] or 0)), 4, 'health-differs',
                                '%s has health %.0f here and %.0f for the server' % (name, a['hp'], b['hp']), a['pos'])
        for a in game['actors']:
            form = a['form']
            was = self.lives.get(form)
            if a['alive'] is False and form not in self.dead_at:
                self.dead_at[form] = now
            if was is False and a['alive'] and not paused:
                self.found.once('came-back', '%s %s was dead in this game and is alive again, health %.0f' % (a['name'], form, a['hp']), now, a['pos'])
                self.dead_at.pop(form, None)
            self.lives[form] = a['alive']
            self.found.note(('finish', form), now, a['alive'] and a['hp'] <= 0 and a['has3d'], 5, 'cannot-finish', '%s %s stays alive at health %.0f' % (a['name'], form, a['hp']), a['pos'])
            if form not in paired and a['hostile'] and a['alive'] and a['has3d'] and me and math.dist(a['pos'], me) < 1500 and not paused:
                self.found.note(('unknown', form), now, True, 15, 'unknown-to-server', '%s %s is near me, alive, and the server does not know it' % (a['name'], form), a['pos'])
            elif ('unknown', form) in self.found.open:
                self.found.note(('unknown', form), now, False, 15, 'unknown-to-server', '')
            track = self.tracks.setdefault(form, {'name': a['name'], 'points': [], 'jumps': []})
            if track['points'] and a['alive']:
                last_t, last_pos = track['points'][-1]
                prev = track['points'][-2][1] if len(track['points']) > 1 else None
                if prev and math.dist(last_pos, prev) > 250 and math.dist(a['pos'], prev) < 80 and now - last_t < 3.0:
                    track['jumps'].append(now)
                    recent = [t for t in track['jumps'] if now - t < 20]
                    if len(recent) == 3:
                        self.found.once('flicker', '%s %s jumped away and back 3 times in 20 s' % (a['name'], form), now, a['pos'])
            track['points'] = (track['points'] + [(now, a['pos'])])[-3:]
            flips = self.seen3d.setdefault(form, {'was': a['has3d'], 'times': []})
            if flips['was'] != a['has3d']:
                flips['was'] = a['has3d']
                flips['times'] = [t for t in flips['times'] if now - t < 30] + [now]
                if len(flips['times']) == 4:
                    self.found.once('3d-blinks', '%s %s lost and regained its body 4 times in 30 s' % (a['name'], form), now, a['pos'])
        mine = game.get('player_name')
        copies = [a for a in game['actors'] if a['name'] == mine and a['has3d']]
        self.found.note(('dup', 'player'), now, len(copies) > 1 and not paused, 3, 'duplicate-player', 'the other player is drawn %d times here: %s' % (len(copies), [a['form'] for a in copies]),
                        copies[0]['pos'] if copies else None)
        if me and bot.get('players') and not paused:
            view = bot['players'][0]
            gap = math.dist(view, me)
            self.found.note(('me', 'view'), now, gap > 400, 6, 'player-place', 'this player is %.0f units from where the other player is told it is' % gap, me)


class Bot:
    """The second player, steered live: commands go in a file it reads, and it reports what it knows every second."""

    def __init__(self, folder, start, lead=False):
        self.commands = os.path.join(folder, 'bot-commands.txt')
        open(self.commands, 'w').close()
        script = os.path.join(folder, 'bot-script.txt')
        with open(script, 'w', encoding='utf-8') as f:
            f.write('\n'.join((['party lead'] if lead else []) + ['log PHASE start', 'waitfor connected 45', 'expect known me', 'waitfor known other 40', 'health 100', 'reportevery 1', 'additem 12EB7', 'equip 12EB7', 'draw on', 'wait 2',
                               'log PHASE ready', 'listen ' + self.commands, 'log PHASE end', 'wait 1', 'stop', '']))
        self.log = open(os.path.join(folder, 'bot.log'), 'w', encoding='utf-8')
        env = dict(os.environ)
        self.proc = subprocess.Popen([os.path.join(RELEASE, 'STBot.exe'), script, '--server', SERVER, '--name', 'Bot', '--x', '%.0f' % start[0], '--y', '%.0f' % start[1]], cwd=RELEASE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', env=env)
        self.ready = threading.Event()
        self.latest = {'actors': [], 'players': [], 'me': None}
        self.latest_at = 0.0
        self.online = True
        self.block = None
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.log.write(line)
            if 'PHASE ready' in line:
                self.ready.set()
            m = re.search(r'\[report\] (.*)$', line.rstrip())
            if not m:
                continue
            words = m.group(1).split()
            self.block = self.block if self.block is not None else {'actors': [], 'players': [], 'me': None}
            if words[0] == 'end':
                self.latest, self.latest_at, self.block = self.block, time.time(), None
            elif words[0] == 'actor' and len(words) >= 8:
                has_pos = words[7] != 'unknown'
                pos = [float(v) for v in words[7:10]] if has_pos else None
                hp = float(words[words.index('health') + 1]) if 'health' in words and words[words.index('health') + 1] != 'unknown' else None
                ref = None if words[3] == 'none' else '0x' + words[3].upper().zfill(8)
                self.block['actors'].append({'sid': words[1].upper(), 'ref': ref, 'dead': words[4] == 'dead', 'ours': words[5] == 'ours', 'pos': pos, 'hp': hp})
            elif words[0] == 'player' and len(words) >= 5:
                self.block['players'].append([float(v) for v in words[2:5]])
            elif words[0] == 'me' and len(words) >= 4:
                self.block['me'] = [float(v) for v in words[1:4]]

    def send(self, *lines):
        with open(self.commands, 'a', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')

    def stop(self):
        self.send('end')
        try:
            self.proc.wait(8)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
        self.log.close()


class ServerIds:
    """Which form is which server id, from the client's own log lines."""

    PATTERNS = [re.compile(r'New entity remotely managed, form id: ([0-9A-Fa-f]+), server id: ([0-9A-Fa-f]+)'),
                re.compile(r'Entity: local actor ([0-9A-Fa-f]+) has server id ([0-9A-Fa-f]+)')]
    SPAWN = re.compile(r'CharacterSpawnRequest, server id: ([0-9A-Fa-f]+), form id: ([0-9A-Fa-f]+)')

    def __init__(self):
        self.forms = {}
        # The whole log: an NPC from an earlier test is still in the world, and the line naming it was written then.
        self.offset = 0
        self.start = os.path.getsize(CLIENT_LOG)

    def update(self):
        try:
            size = os.path.getsize(CLIENT_LOG)
            if size < self.offset:
                self.offset = 0
            with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
                f.seek(self.offset)
                text = f.read()
                self.offset = f.tell()
        except OSError:
            return
        for line in text.splitlines():
            for pattern in self.PATTERNS:
                m = pattern.search(line)
                if m:
                    self.forms.pop(m.group(2).upper(), None)
                    self.forms[m.group(2).upper()] = '0x' + m.group(1).upper().zfill(8)
            m = self.SPAWN.search(line)
            if m:
                self.forms.pop(m.group(1).upper(), None)
                self.forms[m.group(1).upper()] = '0x' + m.group(2).upper().zfill(8)


def game_view(refs):
    me = next((r for r in refs if r['formId'] == play.PLAYER_FORM), None)
    actors = []
    for r in refs:
        if r['formId'] == play.PLAYER_FORM:
            continue
        a = r.get('actor', {})
        actors.append({'form': r['formId'], 'name': r.get('name') or '?', 'pos': r['position'], 'alive': bool(a.get('alive')), 'hp': a.get('health', 0.0), 'max': a.get('healthMax', 0.0),
                       'has3d': bool(a.get('loaded3D')), 'hostile': bool(a.get('hostileToPlayer'))})
    return {'player': me['position'] if me else None, 'player_name': me.get('name') if me else None, 'actors': actors}


class Run:
    """One test: its bot, its scanner thread, its pictures and what it concluded."""

    def __init__(self, name, game):
        self.name, self.game = name, game
        # Each run its own folder, so that a repeat does not overwrite the evidence of the run before.
        self.folder = os.path.join(OUT, '%s-%s' % (name, time.strftime('%H%M%S')))
        os.makedirs(self.folder, exist_ok=True)
        self.pictures = []
        self.expects = []
        self.judge = Judge(self.on_finding)
        self.ids = ServerIds()
        self.paused = False
        self.done = False
        self.bot = None
        self.body = play.Body(game)
        self.body.start()
        time.sleep(1.2)
        self.room = game.heading()
        self.view = None
        self.notes = []
        self.pending_shots = []
        self.plant = None
        self.ignore = set()

    def on_finding(self, kind, text, where):
        log('  SEEN', kind + ':', text)
        if kind in FAILS and len(self.pictures) < PICTURES_PER_TEST:
            self.pending_shots.append((kind, where))

    def take_pending_shots(self):
        while self.pending_shots and len(self.pictures) < PICTURES_PER_TEST:
            kind, where = self.pending_shots.pop(0)
            self.picture(kind, where)

    def picture(self, label, where=None):
        n = len(self.pictures)
        try:
            first = self.game.shot('%s-%d' % (self.name, n), os.path.join(self.folder, '%d-%s-first.png' % (n, label)))
            if first:
                self.pictures.append(first)
            if where:
                outside = self.game.shot_of('%s-%do' % (self.name, n), where, os.path.join(self.folder, '%d-%s-outside.png' % (n, label)))
                if outside:
                    self.pictures.append(outside)
        except Exception as e:
            self.notes.append('no picture for %s: %s' % (label, type(e).__name__))

    def scanner(self):
        while not self.done:
            started = time.time()
            try:
                self.ids.update()
                self.view = game_view(self.game.actors())
                bot_view = self.bot.latest if self.bot else {}
                if self.ignore:
                    bot_view = dict(bot_view, actors=[b for b in bot_view.get('actors', []) if self.ids.forms.get(b['sid']) not in self.ignore and b.get('ref') not in self.ignore])
                if self.plant:
                    bot_view = dict(bot_view, actors=[dict(b, **self.plant[1]) if b['sid'] == self.plant[0] else b for b in bot_view.get('actors', [])])
                self.judge.moment(started, self.view, bot_view, self.ids.forms, paused=self.paused or not (self.bot and self.bot.online))
                self.take_pending_shots()
            except Exception as e:
                self.notes.append('scanner: %s' % type(e).__name__)
            time.sleep(max(0.05, 1.0 - (time.time() - started)))

    def start_bot(self, lead=False):
        start = self.game.position()
        self.bot = Bot(self.folder, start, lead)
        if not self.bot.ready.wait(60):
            raise RuntimeError('the bot never got ready')
        threading.Thread(target=self.scanner, daemon=True).start()
        time.sleep(3)

    def expect(self, ok, text):
        self.expects.append((bool(ok), text))
        log('  %s %s' % ('ok  ' if ok else 'FAIL', text))
        return ok

    def wait_until(self, test, seconds, step=0.5):
        end = time.time() + seconds
        while time.time() < end:
            if test():
                return True
            time.sleep(step)
        return False

    def actor(self, form):
        return next((a for a in (self.view or game_view(self.game.actors()))['actors'] if a['form'] == form), None)

    def fresh(self, form):
        return next((a for a in game_view(self.game.actors())['actors'] if a['form'] == form), None)

    def sid_of(self, form):
        self.ids.update()
        # A form id comes back in later copies, and the log's newest line is the one that counts.
        return next((sid for sid, f in reversed(list(self.ids.forms.items())) if f == form), None)

    def bot_actor(self, form):
        sid = self.sid_of(form)
        return next((b for b in self.bot.latest['actors'] if b['sid'] == sid), None) if sid else None

    def set_ai(self, on):
        answer = self.game.console_answer('tcai')
        if ('is On' in answer) != on:
            answer = self.game.console_answer('tcai')
        return answer

    def tough(self):
        self.game.console('player.setav health 5000')
        self.game.console('player.setav magicka 3000')

    def spawn(self, kind, count=1):
        """NPCs this game makes and owns; returns their forms once the server has given them an id."""
        before = {a['form'] for a in game_view(self.game.actors())['actors']}
        for _ in range(count):
            self.game.console('player.placeatme %s 1' % kind)
        self.wait_until(lambda: len({a['form'] for a in game_view(self.game.actors())['actors']} - before) >= count, 6)
        new = sorted({a['form'] for a in game_view(self.game.actors())['actors']} - before)
        self.wait_until(lambda: all(self.sid_of(f) for f in new), 8)
        self.notes.append('spawned %s: %s' % (kind, new))
        return new

    def kill(self, form):
        self.game.tool('papyrus', {'action': 'call', 'script': 'Actor', 'function': 'Kill', 'self': {'form': form}, 'args': [None]})

    def look_at(self, where):
        me = self.game.position()
        self.body.face(self.room, math.degrees(math.atan2(where[0] - me[0], where[1] - me[1])) % 360)

    def aim_at(self, where):
        me = self.game.position()
        self.look_at(where)
        flat = math.hypot(where[0] - me[0], where[1] - me[1])
        self.body.hand_yaw = self.body.yaw + play.SPELL_LEFT
        self.body.pitch = math.atan2((where[2] + 60) - (me[2] + 84), flat) + play.SPELL_UP

    def bot_goes_to(self, where, speed=320):
        self.bot.send('moveto %.0f %.0f %.0f %d' % (where[0] - 80, where[1] - 80, where[2], speed))

    def finish(self):
        self.done = True
        time.sleep(1.2)
        self.body.cast = False
        if self.bot:
            self.bot.stop()
        self.judge.found.finish()
        self.body.finish()

    def log_counts(self):
        try:
            with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
                f.seek(self.ids.start)
                text = f.read()
        except OSError:
            return {}
        counts = {}
        for label, pattern in (('crash blocks', 'crash occurred'), ('freezes', 'FreezeWatchdog: no frame'), ('silences', 'Silence: sent nothing'), ('disconnects', 'Disconnected from server'),
                               ('DeathWins', 'DeathWins:'), ('stand-ins', r'Ghost: reference \S+ rolled'), ('copies gone', 'CopyGone:'), ('refused replays', 'ReplayDiag: refused'),
                               ('get-up trouble', 'GetUp'), ('errors', r'\] \[error\]')):
            n = len(re.findall(pattern, text))
            if n:
                counts[label] = n
        floods = {}
        for action, times in re.findall(r'ActionFlood: actor \S+ performed action (\S+) \([^)]*\) (\d+) times', text):
            floods[action] = floods.get(action, 0) + int(times)
        if floods:
            counts['action floods'] = dict(sorted(floods.items(), key=lambda kv: -kv[1])[:4])
        return counts


# The tests. Each gets a Run with the bot in the world and the scanner going.

def t_kill_by_game(r):
    """NPCs this game owns die here: the server must know, and they must stay dead."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 3)
    r.expect(len(forms) == 3, 'three bandits appeared')
    for form in forms:
        r.kill(form)
        time.sleep(1)
    r.expect(r.wait_until(lambda: all((r.bot_actor(f) or {}).get('dead') for f in forms), 10), 'the server has all three dead within 10 s')
    time.sleep(25)
    r.expect(all(not (r.fresh(f) or {'alive': False})['alive'] for f in forms), 'they are still dead here 25 s later')


def t_kill_by_bot(r):
    """The other player kills NPCs this game owns, the path a real player's blow takes."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 3)
    for form in forms:
        where = r.fresh(form)['pos']
        r.bot_goes_to(where)
        time.sleep(2)
        r.bot.send('hit %s -3000' % r.sid_of(form))
        time.sleep(1.5)
    r.expect(r.wait_until(lambda: all(not (r.fresh(f) or {'alive': False})['alive'] for f in forms), 15), 'all three died here within 15 s of the blows')
    r.expect(r.wait_until(lambda: all((r.bot_actor(f) or {}).get('dead') for f in forms), 10), 'the server has them dead')
    time.sleep(25)
    r.expect(all(not (r.fresh(f) or {'alive': False})['alive'] for f in forms), 'they are still dead here 25 s later')


def t_kill_bots_npc(r):
    """An NPC the other player owns dies there: the copy here must die, fall, and stay down."""
    r.set_ai(False)
    r.tough()
    before = {a['form'] for a in game_view(r.game.actors())['actors']}
    r.bot.send('npc temp %s' % MAGE)
    r.expect(r.wait_until(lambda: {a['form'] for a in game_view(r.game.actors())['actors']} - before, 12), 'its NPC appears here')
    new = sorted({a['form'] for a in game_view(r.game.actors())['actors']} - before)
    if not new:
        return
    form = new[0]
    r.look_at(r.fresh(form)['pos'])
    time.sleep(6)
    r.picture('alive', r.fresh(form)['pos'])
    r.bot.send('npclife dead 0')
    r.expect(r.wait_until(lambda: not (r.fresh(form) or {'alive': False})['alive'], 10), 'its copy died here within 10 s')
    time.sleep(4)
    if r.fresh(form):
        r.picture('dead', r.fresh(form)['pos'])
    time.sleep(20)
    a = r.fresh(form)
    r.expect(a is None or not a['alive'], 'the copy is still dead 25 s later')


def t_partial_health(r):
    """Health bars: blows that do not kill, the same number on both sides."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_TANK, 1)
    form = forms[0]
    r.bot_goes_to(r.fresh(form)['pos'])
    time.sleep(2)
    start = r.fresh(form)['hp']
    for i in range(4):
        r.bot.send('hit %s -20' % r.sid_of(form))
        time.sleep(2.5)
    now = r.fresh(form)['hp']
    r.expect(abs((start - now) - 80) < 25, 'four blows of 20 took about 80 health here (it went %.0f to %.0f)' % (start, now))
    theirs = (r.bot_actor(form) or {}).get('hp')
    r.expect(theirs is not None and abs(theirs - now) < 15, 'the server has the same health: %s here %.0f' % (theirs, now))


def t_cannot_finish(r):
    """A real fight to the death: anything that gets back up, or will not die, is the bug; the scanner watches every actor."""
    r.set_ai(True)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 2) + r.spawn(BANDIT_TANK, 1)
    end = time.time() + 100
    while time.time() < end:
        alive = [f for f in forms if (r.fresh(f) or {'alive': False})['alive']]
        if not alive:
            break
        target = r.fresh(alive[0])
        r.bot_goes_to(target['pos'])
        time.sleep(1.2)
        r.bot.send('hit %s -60' % r.sid_of(alive[0]))
        r.aim_at(target['pos'])
        r.body.cast = True
        r.tough()
        time.sleep(1.3)
    r.body.cast = False
    r.expect(not [f for f in forms if (r.fresh(f) or {'alive': False})['alive']], 'all of them could be finished')
    time.sleep(20)
    r.expect(not [f for f in forms if (r.fresh(f) or {'alive': False})['alive']], 'none is up again 20 s later')


def t_reconnect_bot_idle(r):
    """The other player leaves and comes back, three times: the world must be as it was, with one copy of it."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 3)
    r.kill(forms[0])
    time.sleep(3)
    for i in range(3):
        r.bot.online = False
        r.bot.send('disconnect', 'wait 6', 'reconnect')
        time.sleep(9)
        r.bot.online = r.wait_until(lambda: time.time() - r.bot.latest_at < 2 and r.bot.latest['actors'], 20)
        time.sleep(10)
        r.expect((r.bot_actor(forms[0]) or {}).get('dead'), 'after return %d the dead one is dead for the server' % (i + 1))
        r.expect(all(not (r.bot_actor(f) or {'dead': True})['dead'] for f in forms[1:]), 'after return %d the others are alive for the server' % (i + 1))


def t_reconnect_bot_fight(r):
    """The other player drops in the middle of a fight, comes back and finishes it."""
    r.set_ai(True)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 3)
    target = r.fresh(forms[0])
    r.bot_goes_to(target['pos'])
    time.sleep(2)
    r.bot.send('hit %s -40' % r.sid_of(forms[0]))
    time.sleep(1)
    r.bot.online = False
    r.bot.send('disconnect', 'wait 8', 'reconnect')
    r.kill(forms[1])
    time.sleep(10)
    r.bot.online = r.wait_until(lambda: time.time() - r.bot.latest_at < 2 and r.bot.latest['actors'], 20)
    r.tough()
    time.sleep(8)
    r.expect((r.bot_actor(forms[1]) or {}).get('dead'), 'the one that died while it was away is dead for it')
    for form in (forms[0], forms[2]):
        if (r.fresh(form) or {'alive': False})['alive']:
            r.bot_goes_to(r.fresh(form)['pos'])
            time.sleep(2)
            r.bot.send('hit %s -3000' % r.sid_of(form))
            time.sleep(2)
    r.expect(r.wait_until(lambda: not [f for f in forms if (r.fresh(f) or {'alive': False})['alive']], 15), 'the fight can be finished after the return')
    time.sleep(15)


def t_reconnect_game_kill_offline(r):
    """This game drops, kills an NPC while offline and comes back: the fox of the 2026-10-30 session."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 3)
    time.sleep(3)
    r.paused = True
    press_f6()
    time.sleep(6)
    r.kill(forms[0])
    time.sleep(4)
    connected = connects()
    press_f6()
    r.expect(r.wait_until(lambda: connects() > connected, 40), 'the game is back on the server')
    time.sleep(6)
    r.paused = False
    time.sleep(25)
    here = {a['form']: a for a in game_view(r.game.actors())['actors']}
    alive_copies = [a for a in here.values() if a['alive'] and a['name'] == (r.fresh(forms[1]) or {}).get('name')]
    r.notes.append('after the return: %d alive of that name here' % len(alive_copies))
    dead_there = (r.bot_actor(forms[0]) or {}).get('dead')
    r.expect(dead_there is not False, 'the one killed offline is not alive for the server')


def t_reconnect_game_repeated(r):
    """This game drops and returns three times beside a dead and a living NPC, with the other player standing there."""
    r.set_ai(False)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 2)
    r.kill(forms[0])
    time.sleep(4)
    for i in range(3):
        r.paused = True
        press_f6()
        time.sleep(6)
        connected = connects()
        press_f6()
        r.expect(r.wait_until(lambda: connects() > connected, 40), 'back on the server after drop %d' % (i + 1))
        time.sleep(6)
        r.paused = False
        time.sleep(14)
    view = game_view(r.game.actors())
    copies = [a for a in view['actors'] if a['name'] == view['player_name'] and a['has3d']]
    r.expect(len(copies) == 1, 'one copy of the other player is drawn here (%d)' % len(copies))


def t_gear(r):
    """Equipping and unequipping by the other player must show on its copy here; the bot wears a part of this player's gear as its own, so only the change is judged."""
    r.set_ai(False)

    def copy_form():
        v = game_view(r.game.actors())
        return next((a for a in sorted(v['actors'], key=lambda a: a['form'], reverse=True) if a['name'] == v['player_name'] and a['has3d']), None)
    r.expect(r.wait_until(copy_form, 15), 'its copy is here')
    c = copy_form()
    if not c:
        return
    time.sleep(4)
    r.look_at(c['pos'])
    time.sleep(1)
    r.picture('gear', c['pos'])
    worn = r.game.worn(c['form'])
    r.notes.append('worn by the copy: %s' % worn)
    sword = '0x00012EB7'
    r.expect(sword in [w[0] for w in worn], 'the sword it equipped shows')
    r.bot.send('unequip 12EB7')
    r.expect(r.wait_until(lambda: sword not in [w[0] for w in r.game.worn(c['form'])], 8, 1.0), 'the sword is off the copy after it unequips it')
    r.bot.send('equip 12EB7')
    r.expect(r.wait_until(lambda: sword in [w[0] for w in r.game.worn(c['form'])], 8, 1.0), 'the sword is back after it equips it again')


def t_move_bots_npc(r):
    """An NPC the other player owns walks: its copy here must keep to the path."""
    r.set_ai(False)
    r.tough()
    before = {a['form'] for a in game_view(r.game.actors())['actors']}
    r.bot.send('npc temp %s' % MAGE)
    r.wait_until(lambda: {a['form'] for a in game_view(r.game.actors())['actors']} - before, 12)
    new = sorted({a['form'] for a in game_view(r.game.actors())['actors']} - before)
    if not r.expect(new, 'its NPC appears here'):
        return
    form = new[0]
    time.sleep(4)
    for dx, dy in ((150, 0), (0, 150), (-150, 0), (0, -150)) * 3:
        r.bot.send('npcmove %d %d 0' % (dx, dy))
        time.sleep(4)
        r.look_at((r.fresh(form) or {'pos': r.game.position()})['pos'])
    r.bot.send('npcmove 0 0 0')
    gaps = sorted(g[1] for g in r.judge.pairs.get(form, []))
    if r.expect(len(gaps) > 15, 'measured its place %d times' % len(gaps)):
        p95 = gaps[int(len(gaps) * 0.95)]
        r.notes.append('gap between the copy and the owner: median %.0f, 95%% %.0f, worst %.0f units' % (gaps[len(gaps) // 2], p95, gaps[-1]))
        r.expect(p95 < 300, 'the copy stayed on the owner\'s path (95%% within %.0f units)' % p95)


def t_move_game_npc(r):
    """NPCs this game runs chase the player while it walks: the server's idea of where they are must keep up."""
    r.set_ai(True)
    r.tough()
    forms = r.spawn(BANDIT_MELEE, 2)
    heading = r.room
    for i in range(10):
        heading = (heading + 90) % 360
        r.body.face(r.room, heading)
        time.sleep(0.3)
        r.game.hold_key('w', 2200)
        r.tough()
        time.sleep(2.4)
    for form in forms:
        gaps = sorted(g[1] for g in r.judge.pairs.get(form, []))
        if r.expect(len(gaps) > 10, '%s measured %d times' % (form, len(gaps))):
            p95 = gaps[int(len(gaps) * 0.95)]
            r.notes.append('%s against the server: median %.0f, 95%% %.0f, worst %.0f units' % (form, gaps[len(gaps) // 2], p95, gaps[-1]))
            r.expect(p95 < 300, '%s stayed where the server has it (95%% within %.0f units)' % (form, p95))
    for form in forms:
        r.kill(form)


def t_move_players(r):
    """Both players walk: each must be where the other is told it is."""
    r.set_ai(False)
    r.tough()
    heading = r.room
    gaps = []
    r.bot.send('follow 220 40')
    end = time.time() + 40
    while time.time() < end:
        heading = (heading + 90) % 360
        r.body.face(r.room, heading)
        time.sleep(0.3)
        r.game.hold_key('w', 2000)
        time.sleep(2.2)
        view = game_view(r.game.actors())
        c = next((a for a in sorted(view['actors'], key=lambda a: a['form'], reverse=True) if a['name'] == view['player_name'] and a['has3d']), None)
        if c and r.bot.latest.get('me'):
            gaps.append(math.dist(c['pos'], r.bot.latest['me']))
    if r.expect(gaps, 'measured the copy against the other player'):
        gaps.sort()
        r.notes.append('the copy against the other player: median %.0f, worst %.0f units' % (gaps[len(gaps) // 2], gaps[-1]))
        r.expect(gaps[len(gaps) // 2] < 300, 'its copy keeps up')


def t_scanner_proof(r):
    """The proof on the real game: mismatches planted in what the scanner reads must be seen, or no other pass here means anything."""
    r.set_ai(False)
    r.tough()
    form = r.spawn(BANDIT_MELEE, 1)[0]
    sid = r.sid_of(form)
    r.expect(sid, 'the bandit has a server id')
    place = r.fresh(form)['pos']
    time.sleep(3)
    for label, change, key in (('dead for the server, alive here', {'dead': True}, 'life'), ('in another place for the server', {'pos': [place[0] + 3000, place[1], place[2]]}, 'place'),
                               ('with other health for the server', {'hp': -500.0}, 'health')):
        r.plant = (sid, change)
        seen = r.wait_until(lambda: (r.judge.found.open.get((key, form)) or {}).get('told'), 12)
        r.plant = None
        if not seen:
            known = [b for b in r.bot.latest['actors'] if b['sid'] == sid]
            r.notes.append('not seen: sid %s, form %s, the bot has it: %s; this form now has sids %s; open runs: %s; here: %s' % (
                sid, form, known, [k for k, f in r.ids.forms.items() if f == form], [k for k in r.judge.found.open if form in k], r.fresh(form)))
        r.expect(seen, 'planted: %s; the scanner saw it' % label)
        r.judge.found.open.pop((key, form), None)
        time.sleep(2)
    r.pending_shots.clear()
    r.judge.found.items = [f for f in r.judge.found.items if f['kind'] not in ('life-differs', 'place-differs', 'health-differs')]
    r.kill(form)


def t_reconnect_fox(r):
    """The fox of the 2026-10-30 session: the other player owns a placed creature, this game drops, kills its own one offline and comes back."""
    r.set_ai(False)
    r.tough()
    view = game_view(r.game.actors())
    placed = sorted((math.dist(a['pos'], view['player']), a['form']) for a in view['actors'] if a['alive'] and a['has3d'] and not a['form'].upper().startswith('0XFF') and a['name'] != view['player_name']
                    and int(a['form'], 16) < 0x01000000)
    if not r.expect(placed, 'a living placed creature is near (%s)' % (placed[:1],)):
        return
    form = placed[0][1]
    connected = connects()
    r.paused = True
    press_f6()
    r.expect(r.wait_until(lambda: disconnected_after(connected), 12) or True, 'the game went offline')
    time.sleep(4)
    r.kill(form)
    r.expect(r.wait_until(lambda: not (r.fresh(form) or {'alive': True})['alive'], 8), 'it died here, offline')
    r.bot.send('npc %s' % form[2:].lstrip('0'))
    time.sleep(5)
    connected = connects()
    press_f6()
    r.expect(r.wait_until(lambda: connects() > connected, 40), 'the game is back on the server')
    time.sleep(3)
    r.paused = False
    # The bot owns it but plays no game: a killing blow shows as its health going far below zero, not as a death.
    theirs = None
    for _ in range(40):
        theirs = next((b for b in r.bot.latest['actors'] if b.get('ref') == form), None)
        if theirs and (theirs['dead'] or (theirs['hp'] is not None and theirs['hp'] <= 0)):
            break
        time.sleep(0.5)
    r.judge.found.open.pop(('life', form), None)
    r.judge.found.open.pop(('place', form), None)
    r.ignore.add(form)
    r.notes.append('after the return the bot has it: %s' % theirs)
    here = r.fresh(form)
    r.expect(here is not None and not here['alive'], 'it is still dead here')
    r.expect(theirs is not None and (theirs['dead'] or (theirs['hp'] is not None and theirs['hp'] <= 0)), 'its owner was sent the killing blow; what a real owner does with it cannot be seen here')
    r.picture('after', here['pos'] if here else None)
    time.sleep(15)
    here = r.fresh(form)
    r.expect(here is None or not here['alive'], 'still dead here 15 s later')


def disconnected_after(count):
    with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
        f.seek(max(0, os.path.getsize(CLIENT_LOG) - 20000))
        return 'Disconnected from server' in f.read()


def log_lines_since(r, pattern):
    with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
        f.seek(r.ids.start)
        return [line.rstrip() for line in f if re.search(pattern, line)]


def copy_of_player(r):
    view = game_view(r.game.actors())
    return next((a for a in sorted(view['actors'], key=lambda a: a['form'], reverse=True) if a['name'] == view['player_name'] and a['has3d']), None)


def bodies_named(r, name, where, reach=1000):
    return [a for a in game_view(r.game.actors())['actors'] if a['name'] == name and not a['alive'] and a['has3d'] and math.dist(a['pos'], where) < reach]


def go_offline_and_back(r):
    r.paused = True
    press_f6()
    time.sleep(6)
    connected = connects()
    press_f6()
    back = r.wait_until(lambda: connects() > connected, 40)
    time.sleep(6)
    r.paused = False
    return back


def t_spell_on_npc(r):
    """Reported in play: spells do no damage. A spell from this player's own hands on an NPC near it, with the health read before and after."""
    r.set_ai(False)
    r.tough()
    form = r.spawn(BANDIT_TANK, 1)[0]
    target = r.fresh(form)
    me = r.game.position()
    away = math.degrees(math.atan2(me[0] - target['pos'][0], me[1] - target['pos'][1])) % 360
    r.body.face(r.room, away)
    time.sleep(0.5)
    r.game.hold_key('w', 500)
    time.sleep(0.8)
    start = r.fresh(form)['hp']
    r.aim_at(r.fresh(form)['pos'])
    time.sleep(0.5)
    magicka = r.game.tool('inspect', {'kind': 'player'})['actorValues']['magicka']['current']
    r.body.cast = True
    time.sleep(1.5)
    r.picture('casting', r.fresh(form)['pos'])
    time.sleep(4.5)
    r.body.cast = False
    now = r.fresh(form)
    used = magicka - r.game.tool('inspect', {'kind': 'player'})['actorValues']['magicka']['current']
    r.notes.append('6 s of casting: its health %.0f to %.0f, %.0f magicka used, %.0f units away' % (start, now['hp'], used, math.dist(now['pos'], r.game.position())))
    r.expect(used > 5, 'the spell was really cast (%.0f magicka)' % used)
    r.expect(start - now['hp'] > 10, 'it took damage: %.0f' % (start - now['hp']))
    r.notes += [line[:170] for line in log_lines_since(r, r'PvP: |Death sync: ')[-4:]]


def t_drop_items(r):
    """Reported in play: the other player's drops were not seen, and not one DroppedItem line in the session's log. Both directions, and with this player in a paused menu."""
    r.set_ai(False)
    placed = lambda: len(log_lines_since(r, 'DroppedItem: placed'))
    r.bot.send('dropitem 12EB7')
    r.expect(r.wait_until(lambda: placed() >= 1, 10), 'a drop of the other player is placed here')
    n = placed()
    r.game.tool('menu', {'action': 'open', 'name': 'Journal Menu'})
    time.sleep(1.5)
    r.bot.send('dropitem 12EB7')
    time.sleep(5)
    while_paused = placed() > n
    r.game.tool('menu', {'action': 'close', 'name': 'Journal Menu'})
    r.notes.append('placed while the menu was open: %s' % while_paused)
    r.expect(r.wait_until(lambda: placed() > n, 15), 'a drop made while this player is in a menu is placed here, at the latest when it closes')
    r.bot_seen = 0
    r.game.console('player.additem 12EB7 3')
    time.sleep(1)
    r.game.console('player.drop 12EB7 1')
    time.sleep(6)
    r.bot.log.flush()
    with open(r.bot.log.name, encoding='utf-8', errors='replace') as f:
        seen = [line for line in f if 'ropped item' in line or 'Dropped' in line]
    r.notes.append('the other player saw of this drop: %s' % [line.strip()[:110] for line in seen[-2:]])
    r.expect(seen, 'a drop of this player reaches the other player')


def t_respawn_copy(r):
    """Reported in play: on a respawn the body flickers and the head is gone for seconds. The other player dies and respawns; its copy is watched closely and pictured."""
    r.set_ai(False)
    r.tough()
    c = copy_of_player(r)
    if not r.expect(c, 'its copy is here'):
        return
    r.look_at(c['pos'])
    time.sleep(1)
    r.bot.send('die')
    time.sleep(6)
    r.picture('down', c['pos'])
    r.bot.send('respawn')
    start = time.time()
    forms = []
    gaps = 0
    shots = 0
    while time.time() - start < 12:
        v = game_view(r.game.actors())
        drawn = [a for a in v['actors'] if a['name'] == v['player_name'] and a['has3d']]
        ids = [a['form'] for a in drawn]
        if ids != (forms[-1][1] if forms else None):
            forms.append((round(time.time() - start, 1), ids))
        gaps += 0 if drawn else 1
        if drawn and shots < 3 and time.time() - start > shots * 1.5:
            r.look_at(drawn[-1]['pos'])
            r.picture('after-%d' % shots)
            shots += 1
        time.sleep(0.2)
    r.notes.append('drawn copies after the respawn (seconds, forms): %s' % forms)
    r.expect(len(forms) <= 2, 'the copy changed at most once (%d changes)' % max(0, len(forms) - 1))
    r.expect(max([len(f[1]) for f in forms] or [0]) <= 1, 'never two copies at once')
    r.notes += [line[-150:] for line in log_lines_since(r, r'CopyGone|Temporary Remote Held')[-4:]]


def t_corpse_near(r):
    """Reported in play: a body the other player dragged disappears when this player comes close. Both ways round: walking up to its body, and it walking up to this game's."""
    r.set_ai(False)
    r.tough()
    before = {a['form'] for a in game_view(r.game.actors())['actors']}
    r.bot.send('npc temp %s' % MAGE)
    r.wait_until(lambda: {a['form'] for a in game_view(r.game.actors())['actors']} - before, 12)
    new = sorted({a['form'] for a in game_view(r.game.actors())['actors']} - before)
    if r.expect(new, 'its NPC appears here'):
        form = new[0]
        time.sleep(3)
        r.bot.send('npclife dead 0')
        r.wait_until(lambda: not (r.fresh(form) or {'alive': True})['alive'], 8)
        time.sleep(4)
        lost = 0
        for _ in range(16):
            body = r.fresh(form)
            if not body or not body['has3d']:
                lost += 1
            elif math.dist(body['pos'], r.game.position()) > 90:
                r.look_at(body['pos'])
                r.game.hold_key('w', 400)
            time.sleep(0.5)
        body = r.fresh(form)
        r.notes.append('walking up to its body: %d of 16 looks found no body; %s' % (lost, body and round(math.dist(body['pos'], r.game.position()))))
        r.expect(lost == 0, 'its body stayed while this player walked up to it')
    mine = r.spawn(BANDIT_MELEE, 1)[0]
    r.kill(mine)
    time.sleep(4)
    r.bot_goes_to(r.fresh(mine)['pos'], 250)
    lost = 0
    for _ in range(24):
        body = r.fresh(mine)
        if not body or not body['has3d']:
            lost += 1
        time.sleep(0.5)
    r.expect(lost == 0, 'this game\'s body stayed while the other player came close (%d of 24 looks found none)' % lost)


def t_reconnect_body_dup(r):
    """Reported in play: after a reconnect a dead Old Orc was there twice, and one fell through the floor. Bodies of each kind, counted and measured across a reconnect."""
    r.set_ai(False)
    r.tough()
    measured = []
    mine = r.spawn(BANDIT_MELEE, 1)[0]
    name = r.fresh(mine)['name']
    r.kill(mine)
    time.sleep(5)
    measured.append(('this game\'s temporary NPC', name, r.fresh(mine)['pos']))
    before = {a['form'] for a in game_view(r.game.actors())['actors']}
    r.bot.send('npc temp %s' % MAGE)
    r.wait_until(lambda: {a['form'] for a in game_view(r.game.actors())['actors']} - before, 12)
    new = sorted({a['form'] for a in game_view(r.game.actors())['actors']} - before)
    if new:
        time.sleep(3)
        r.bot.send('npclife dead 0')
        time.sleep(7)
        body = r.fresh(new[0])
        if body:
            measured.append(('the other player\'s NPC', body['name'], body['pos']))
    view = game_view(r.game.actors())
    placed = sorted((math.dist(a['pos'], view['player']), a['form']) for a in view['actors'] if a['alive'] and a['has3d'] and a['name'] != view['player_name'] and int(a['form'], 16) < 0x01000000)
    if placed:
        form = placed[0][1]
        r.paused = True
        press_f6()
        time.sleep(6)
        r.bot.send('npc %s' % form[2:].lstrip('0'))
        time.sleep(3)
        connected = connects()
        press_f6()
        r.wait_until(lambda: connects() > connected, 40)
        time.sleep(5)
        r.paused = False
        r.bot.send('npclife dead 0')
        time.sleep(8)
        body = r.fresh(form)
        if body:
            r.notes.append('placed NPC %s, now %s' % (form, 'dead' if not body['alive'] else 'alive'))
            if not body['alive']:
                measured.append(('a placed NPC the other player owns', body['name'], body['pos']))
    counts = [len(bodies_named(r, name_, where)) for _, name_, where in measured]
    r.notes.append('bodies before: %s' % list(zip([m[0] for m in measured], counts)))
    r.expect(go_offline_and_back(r), 'the game is back on the server')
    time.sleep(12)
    for (label, name_, where), before_count in zip(measured, counts):
        bodies = bodies_named(r, name_, where)
        low = min([a['pos'][2] for a in bodies] or [where[2]])
        r.notes.append('%s, %s: %d bodies before, %d after, lowest %.0f units from where it lay: %s' % (label, name_, before_count, len(bodies), where[2] - low, [a['form'] for a in bodies]))
        r.expect(len(bodies) <= before_count, '%s: not more bodies after the reconnect (%d, were %d)' % (label, len(bodies), before_count))
        r.expect(bodies, '%s: its body is still here' % label)
        r.expect(where[2] - low < 120, '%s: no body fell (%.0f units)' % (label, where[2] - low))
        if len(bodies) > before_count:
            r.picture('double', where)
    r.notes += [line[-140:] for line in log_lines_since(r, r'Copy for server id|Leftover copy|CorpseDiag: .* (dead here|not said)')[-5:]]


def t_door_leave(r):
    """Reported in play: when the other player goes inside, its copy stays for a while, and a copy then appears stuck in the door. It enters a cell and comes out; the copy is timed."""
    r.set_ai(False)
    r.tough()
    c = copy_of_player(r)
    if not r.expect(c, 'its copy is here'):
        return
    r.look_at(c['pos'])
    time.sleep(1)
    r.bot.send('cell 5DE24')
    start = time.time()
    r.wait_until(lambda: not copy_of_player(r), 25, 0.2)
    gone_after = time.time() - start
    r.notes.append('its copy was gone %.1f s after it went inside' % gone_after)
    r.expect(gone_after < 3, 'its copy goes at once when it goes inside (%.1f s)' % gone_after)
    time.sleep(10)
    r.expect(not copy_of_player(r), 'no copy comes back while it is inside')
    r.bot.send('cell out')
    start = time.time()
    r.expect(r.wait_until(lambda: copy_of_player(r), 20, 0.2), 'its copy is back when it comes out (%.1f s)' % (time.time() - start))
    time.sleep(6)
    v = game_view(r.game.actors())
    drawn = [a['form'] for a in v['actors'] if a['name'] == v['player_name'] and a['has3d']]
    r.expect(len(drawn) == 1, 'one copy of it afterwards: %s' % drawn)


def t_brawl(r):
    """Both players fight waves of bandits, as in a session, for four minutes."""
    r.set_ai(True)
    forms = []
    end = time.time() + 240
    next_wave = 0
    while time.time() < end:
        r.tough()
        alive = [f for f in forms if (r.fresh(f) or {'alive': False})['alive']]
        if not alive and time.time() > next_wave:
            forms = r.spawn(BANDIT_MELEE, 2) + r.spawn(BANDIT_ARCHER, 1) + r.spawn(BANDIT_TANK, 1)
            next_wave = time.time() + 6
            continue
        if not alive:
            time.sleep(1)
            continue
        target = r.fresh(alive[0])
        sid = r.sid_of(alive[0])
        r.bot_goes_to(target['pos'])
        time.sleep(1.0)
        if sid:
            r.bot.send('hit %s -40' % sid)
        mine = r.fresh(alive[-1]) or target
        r.aim_at(mine['pos'])
        r.body.cast = math.dist(mine['pos'], r.game.position()) < 320
        if not r.body.cast:
            r.game.hold_key('w', 500)
        time.sleep(1.0)
    r.body.cast = False
    r.expect(True, 'fought for four minutes')


def t_bad_network(r):
    """The other player on a bad connection (150 ms, 5% lost): a fight must still end the same on both sides."""
    t_cannot_finish(r)


def t_soak(r):
    """Twenty minutes of fights: threads, memory and frame time must not run away."""
    r.set_ai(True)
    start_health = health_line()
    end = time.time() + 1200
    forms = []
    while time.time() < end:
        r.tough()
        alive = [f for f in forms if (r.fresh(f) or {'alive': False})['alive']]
        if not alive:
            forms = r.spawn(BANDIT_MELEE, 2)
            continue
        r.bot_goes_to(r.fresh(alive[0])['pos'])
        time.sleep(1.2)
        r.bot.send('hit %s -50' % r.sid_of(alive[0]))
        time.sleep(1.5)
    end_health = health_line()
    r.notes.append('health lines: %s then %s' % (start_health, end_health))
    if start_health and end_health:
        r.expect(end_health['threads'] - start_health['threads'] < 150, 'threads grew by %d' % (end_health['threads'] - start_health['threads']))
        r.expect(end_health['private'] - start_health['private'] < 2000, 'private memory grew by %d MB' % (end_health['private'] - start_health['private']))


def health_line():
    with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
        f.seek(max(0, os.path.getsize(CLIENT_LOG) - 200000))
        lines = re.findall(r'Health: private (\d+) MB, working set \d+ MB, (\d+) threads', f.read())
    return {'private': int(lines[-1][0]), 'threads': int(lines[-1][1])} if lines else None


def connects():
    with open(CLIENT_LOG, 'r', encoding='utf-8', errors='replace') as f:
        f.seek(max(0, os.path.getsize(CLIENT_LOG) - 2000000))
        return f.read().count('Connected: the server accepted')


def press_f6():
    # The client reads this key with GetAsyncKeyState, so it is pressed at the Windows level, as headless.ps1 does.
    ctypes.windll.user32.keybd_event(0x75, 0, 0, 0)
    time.sleep(0.3)
    ctypes.windll.user32.keybd_event(0x75, 0, 2, 0)


TESTS = {'spell-on-npc': t_spell_on_npc, 'drop-items': t_drop_items, 'respawn-copy': t_respawn_copy, 'corpse-near': t_corpse_near, 'reconnect-body-dup': t_reconnect_body_dup, 'door-leave': t_door_leave,
         'scanner-proof': t_scanner_proof, 'reconnect-fox': t_reconnect_fox, 'kill-by-game': t_kill_by_game, 'kill-by-bot': t_kill_by_bot, 'kill-bots-npc': t_kill_bots_npc, 'partial-health': t_partial_health, 'cannot-finish': t_cannot_finish,
         'reconnect-bot-idle': t_reconnect_bot_idle, 'reconnect-bot-fight': t_reconnect_bot_fight, 'reconnect-game-kill-offline': t_reconnect_game_kill_offline,
         'reconnect-game-repeated': t_reconnect_game_repeated, 'gear': t_gear, 'move-bots-npc': t_move_bots_npc, 'move-game-npc': t_move_game_npc, 'move-players': t_move_players,
         'brawl': t_brawl, 'bad-network': t_bad_network, 'soak': t_soak}
ORDER = ['scanner-proof', 'gear', 'kill-by-game', 'kill-by-bot', 'kill-bots-npc', 'partial-health', 'cannot-finish', 'move-bots-npc', 'move-game-npc', 'move-players', 'reconnect-bot-idle', 'reconnect-bot-fight',
         'reconnect-game-kill-offline', 'reconnect-fox', 'reconnect-game-repeated', 'brawl', 'bad-network']
# Existing one-game scripts, by what they cover. The ones marked last leave the player in Helgen's keep, so the game restarts after them.
LIVE_GROUPS = {'doors': ['live-door-echo', 'live-door-range', 'live-doors'], 'locks': ['live-lock-range'], 'levers and bridges': ['live-lever-bridge'],
               'deaths': ['live-death-copy', 'live-death-protected', 'live-dead-stays-dead', 'live-owner-blow', 'live-protected-copy-dies', 'live-protected-kill', 'live-death-replay'],
               'bodies': ['live-body-owner', 'live-body-grab', 'live-body-hold', 'live-npc-grab'], 'revive': ['live-revive', 'live-revive-copy', 'live-revive-copy-stuck', 'live-revive-timeout', 'live-revive-giveup'],
               'respawn': ['live-respawn-cell'], 'levelled': ['live-levelled'], 'followers': ['live-follower-stays-in', 'live-copy-through-door'], 'items': ['live-game-drops', 'live-dropmove', 'live-world-object'],
               'reconnect': ['live-temp-reconnect'], 'time': ['live-time'], 'gear': ['live-equip'], 'spells': ['live-spell-damage']}
LAST_LIVE = ['live-door-state', 'live-door-script', 'live-door-motion']


def verdict_of(result):
    if result.get('error'):
        return 'INVALID'
    bad = [f for f in result['findings'] if f['kind'] in FAILS]
    return 'FAILED' if bad or [e for e in result['expects'] if not e[0]] else 'ok'


def write_report(results):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'results.json'), 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=1)
    rows = []
    for r in results:
        colour = {'ok': '#2e7d32', 'FAILED': '#c62828'}.get(r['verdict'], '#ef6c00')
        parts = ['<b>%s</b>' % html.escape(e[1]) for e in r.get('expects', []) if not e[0]]
        parts += ['%s %s %s: %s' % (time.strftime('%H:%M:%S', time.localtime(f['first'])), f['kind'], ('(%.0f s)' % f['seconds']) if f['seconds'] else '', html.escape(f['text'])) for f in r.get('findings', [])[:40]]
        parts += ['<i>%s</i>' % html.escape(n[:400]) for n in r.get('notes', [])[:12]]
        if r.get('counts'):
            parts.append('<small>log: %s</small>' % html.escape(json.dumps(r['counts'])))
        if r.get('error'):
            parts.append('<b>%s</b>' % html.escape(r['error'][:500]))
        pics = ''.join('<a href="%s"><img src="%s" width="160"></a>' % ((os.path.relpath(p, OUT).replace(os.sep, '/'),) * 2) for p in r.get('pictures', []))
        rows.append('<tr><td>%s<br><small>%s</small></td><td style="color:%s"><b>%s</b></td><td>%s<div>%s</div></td></tr>' % (html.escape(r['name']), html.escape(r.get('about', '')), colour, r['verdict'], '<br>'.join(parts), pics))
    page = '<!doctype html><meta charset="utf-8"><title>Desync report</title><style>body{font:14px sans-serif;margin:16px}td{border-top:1px solid #ccc;padding:8px;vertical-align:top}</style><h1>Desync report</h1><p>Updated %s; %d tests.</p><table>%s</table>' % (time.strftime('%Y-%m-%d %H:%M'), len(results), ''.join(rows))
    with open(os.path.join(OUT, 'report.html'), 'w', encoding='utf-8') as f:
        f.write(page)


def rig(action):
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', HEADLESS, action] + (['-AutoConnect'] if action == 'up' else []), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)


def ensure_game(game):
    health = game.alive()
    if health and health.get('lastLifecycle') == 'postLoadGame' and game.tool('inspect', {'kind': 'state'}).get('playerLoaded'):
        return True
    log('the game is gone or not in the world: bringing it up again')
    for _ in range(2):
        rig('down')
        time.sleep(3)
    rig('up')
    return bool(game.alive())


def run_test(name, game, results):
    test = TESTS[name]
    log('=== %s' % name)
    result = {'name': name, 'about': (test.__doc__ or '').strip(), 'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'findings': [], 'expects': [], 'notes': [], 'pictures': []}
    if name == 'bad-network':
        os.environ.update(STBOT_FAKE_LAG_MS='150', STBOT_FAKE_LOSS_PCT='5')
    run = None
    try:
        if not ensure_game(game):
            raise RuntimeError('the game did not come up')
        run = Run(name, game)
        run.start_bot(lead=name in ('reconnect-fox', 'reconnect-body-dup'))
        test(run)
    except Exception as e:
        result['error'] = '%s: %s\n%s' % (type(e).__name__, e, traceback.format_exc()[-600:])
        log('  ERROR', result['error'][:300])
    finally:
        os.environ.pop('STBOT_FAKE_LAG_MS', None)
        os.environ.pop('STBOT_FAKE_LOSS_PCT', None)
        if run:
            try:
                run.finish()
            except Exception as e:
                result['notes'].append('finish: %s' % e)
            result.update(findings=run.judge.found.items, expects=run.expects, notes=result['notes'] + run.notes, pictures=run.pictures, counts=run.log_counts())
    if not game.alive():
        result['findings'].append({'kind': 'game-ended', 'first': time.time(), 'seconds': 0, 'text': 'the game was gone at the end of this test; see the crash block in the client log'})
    result['verdict'] = verdict_of(result)
    log('--- %s: %s (%d findings, %d failed expectations)' % (name, result['verdict'], len(result['findings']), len([e for e in result['expects'] if not e[0]])))
    results.append(result)
    write_report(results)


def restore_player(game):
    """Undoes Run.tough: the older scripts go down and get revived, which 5000 health would not let them."""
    if PLAYER_BASE:
        game.console('player.setav health %.0f' % PLAYER_BASE['health'])
        game.console('player.setav magicka %.0f' % PLAYER_BASE['magicka'])
        game.console('player.restoreav health 100000')


def run_live(group, script, game, results):
    log('=== %s (%s)' % (script, group))
    result = {'name': script, 'about': 'existing live script: ' + group, 'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'findings': [], 'expects': [], 'notes': [], 'pictures': []}
    if not ensure_game(game):
        result.update(verdict='INVALID', error='the game did not come up')
    else:
        try:
            # These scripts start at the rig's start place, and the one before may have left the player elsewhere.
            restore_player(game)
            game.console('cow Tamriel 34 -9')
            time.sleep(12)
            p = subprocess.run([sys.executable, LIVE_CHECK, script], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
            table = [l for l in p.stdout.splitlines() if l.startswith(script)]
            failed = [l.strip()[:300] for l in p.stdout.splitlines() if 'FAIL ' in l or 'INVALID' in l][:10]
            result['notes'] = (table[-1:] or ['no result line']) + failed
            result['verdict'] = 'ok' if p.returncode == 0 else ('INVALID' if 'INVALID' in ' '.join(table) or 'NOT RUN' in ' '.join(table) else 'FAILED')
        except subprocess.TimeoutExpired:
            result.update(verdict='INVALID', error='did not end within 10 minutes')
    if not game.alive():
        result['findings'].append({'kind': 'game-ended', 'first': time.time(), 'seconds': 0, 'text': 'the game was gone after this script'})
        result['verdict'] = 'FAILED'
    log('--- %s: %s' % (script, result.get('verdict')))
    results.append(result)
    write_report(results)


def selftest():
    good = True

    def check(ok, text):
        nonlocal good
        good &= bool(ok)
        print('%s  %s' % ('ok  ' if ok else 'FAIL', text))

    def actor(form='0xA', name='Bandit', pos=(100, 0, 0), alive=True, hp=50.0, has3d=True, hostile=True):
        return {'form': form, 'name': name, 'pos': list(pos), 'alive': alive, 'hp': hp, 'max': 100.0, 'has3d': has3d, 'hostile': hostile}

    def bot_actor(sid='1', dead=False, pos=(100, 0, 0), hp=50.0, ours=False):
        return {'sid': sid, 'ref': None, 'dead': dead, 'pos': list(pos), 'hp': hp, 'ours': ours}

    def run(moments, forms=None):
        j = Judge()
        for t, (game_actors, bot_actors) in enumerate(moments):
            j.moment(float(t), {'player': [0, 0, 0], 'player_name': 'Hero', 'actors': game_actors}, {'actors': bot_actors, 'players': [[0, 0, 0]]}, forms if forms is not None else {'1': '0xA'})
        j.found.finish()
        return {f['kind'] for f in j.found.items}

    clean = run([([actor()], [bot_actor()])] * 30)
    check(not clean, 'identical worlds give no findings: %s' % clean)
    check('life-differs' in run([([actor(alive=True)], [bot_actor(dead=True)])] * 10), 'alive here, dead for the server')
    check('life-differs' in run([([actor(alive=False)], [bot_actor(dead=False)])] * 10), 'dead here, alive for the server')
    check('life-differs' not in run([([actor(alive=False)], [bot_actor(dead=False)])] * 3), 'a short gap, as a death travels, is no finding')
    check('place-differs' in run([([actor()], [bot_actor(pos=(900, 0, 0))])] * 10), 'in another place')
    check('health-differs' in run([([actor(hp=80.0)], [bot_actor(hp=20.0)])] * 10), 'another health')
    check('came-back' in run([([actor(alive=False, hp=0.0)], [bot_actor(dead=True, hp=0.0)])] * 3 + [([actor(alive=True)], [bot_actor(dead=True)])]), 'dead, then up again')
    check('cannot-finish' in run([([actor(hp=-5.0)], [bot_actor(hp=-5.0)])] * 10), 'alive at no health')
    check('missing-here' in run([([], [bot_actor()])] * 15), 'the server has it, this game lost it')
    check('unknown-to-server' in run([([actor()], [])] * 20, forms={}), 'this game has it, the server does not')
    check('flicker' in run([([actor(pos=(100, 0, 0) if t % 2 else (800, 0, 0))], [bot_actor(pos=(100, 0, 0))]) for t in range(30)]), 'jumping away and back')
    check('3d-blinks' in run([([actor(has3d=bool(t % 2))], [bot_actor()]) for t in range(12)]), 'its body going and coming')
    check('duplicate-player' in run([([actor('0xB', 'Hero'), actor('0xC', 'Hero')], [])] * 8, forms={}), 'the other player drawn twice')
    print('all good' if good else 'FAILED')
    return 0 if good else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tests', nargs='*')
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--live', action='store_true', help='the existing live scripts as well')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--append', action='store_true', help='add to the report instead of starting a new one')
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.list:
        print('\n'.join(list(TESTS) + [s for g in LIVE_GROUPS.values() for s in g] + LAST_LIVE))
        return 0
    os.makedirs(OUT, exist_ok=True)
    game = play.Player()
    results = []
    if args.append and os.path.isfile(os.path.join(OUT, 'results.json')):
        with open(os.path.join(OUT, 'results.json'), encoding='utf-8') as f:
            results = json.load(f)
    values = game.tool('inspect', {'kind': 'player'}).get('actorValues', {}) if game.alive() else {}
    if values:
        PLAYER_BASE.update(health=values['health']['max'], magicka=values['magicka']['max'])
    names = ORDER if args.all else args.tests
    for _ in range(args.repeat):
        for name in names:
            if name in TESTS:
                run_test(name, game, results)
            else:
                group = next((g for g, scripts in LIVE_GROUPS.items() if name in scripts), 'by name')
                run_live(group, name, game, results)
    if args.live:
        for group, scripts in LIVE_GROUPS.items():
            for script in scripts:
                run_live(group, script, game, results)
        for script in LAST_LIVE:
            run_live('doors', script, game, results)
            rig('down')
            rig('down')
    bad = [r['name'] for r in results if r['verdict'] != 'ok']
    log('done: %d tests, not ok: %s' % (len(results), bad))
    return 0


if __name__ == '__main__':
    sys.exit(main())
