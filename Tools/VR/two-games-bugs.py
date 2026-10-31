"""Two real clients against what players reported, with a scanner comparing both games all the time.

    powershell -File Tools/VR/headless.ps1 up -AutoConnect -Twin [-ServerAddress <address:port> -ServerPassword <password>]
    python Tools/VR/two-games-bugs.py [--until HH:MM] [test ...]

It brings up the second game if it is not there. It detects only: each test writes what it measured, and the scanner writes what the two games disagree on.
Results: build/desync/two-games-bugs/, with results.json, findings and pictures. Exit code 5 means the first game is gone and the rig has to come up again.
"""
import argparse
import ctypes
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import play  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'build', 'desync', 'two-games-bugs')
LOGS = r'C:\Fus\tools\urSovngarde\logs'
ASK_FILE = os.path.join(tempfile.gettempdir(), 'two-games-second')
OLD_ORC, BANDIT_MELEE, BANDIT_TANK, MAGE = '62128', 'F62D7', 'F62D9', '753CB'
ITEM = '12EB7'
FIREBOLT, SPARKS = '12FD0', '2DD2A'
MISTWATCH, WINDHELM = 'Tamriel 34 -9', 'Tamriel 33 6'
NEAR = 3000


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def log(*args):
    line = ' '.join(str(a) for a in args)
    print(time.strftime('%H:%M:%S'), line, flush=True)


class Game:
    def __init__(self, label, port, index):
        self.label, self.index = label, index
        self.p = play.Player(port)
        self.log_path = os.path.join(LOGS, 'tp_client.log' if index == 1 else 'tp_client_instance_%d.log' % index)
        self.body = None
        self.room = 0.0
        self.pid = None
        self.sids = {}
        self.offset = 0

    def start_body(self):
        self.body = play.Body(self.p)
        self.body.start()
        time.sleep(1.2)
        self.room = self.p.heading()

    def view(self):
        refs = self.p.actors()
        me = next((r for r in refs if r['formId'] == play.PLAYER_FORM), None)
        out = []
        for r in refs:
            if r['formId'] == play.PLAYER_FORM:
                continue
            a = r.get('actor', {})
            out.append({'form': r['formId'], 'name': r.get('name') or '?', 'pos': r['position'], 'alive': bool(a.get('alive')), 'hp': a.get('health', 0.0), 'has3d': bool(a.get('loaded3D'))})
        return {'at': me['position'] if me else None, 'name': me.get('name') if me else None, 'actors': out}

    def me(self):
        return self.p.tool('inspect', {'kind': 'player'})['actorValues']

    def health(self):
        return self.me()['health']['current']

    def copies(self):
        v = self.view()
        return [a for a in v['actors'] if a['name'] == v['name']]

    def look_at(self, where):
        me = self.p.position()
        self.body.face(self.room, math.degrees(math.atan2(where[0] - me[0], where[1] - me[1])) % 360)

    def aim_at(self, where):
        me = self.p.position()
        self.look_at(where)
        flat = math.hypot(where[0] - me[0], where[1] - me[1])
        self.body.hand_yaw = self.body.yaw + play.SPELL_LEFT
        self.body.pitch = math.atan2((where[2] + 60) - (me[2] + 84), flat) + play.SPELL_UP

    def rest_aim(self):
        self.body.cast = False
        self.body.hand_yaw = self.body.yaw
        self.body.pitch = 0.0

    def set_ai(self, on):
        answer = self.p.console_answer('tcai')
        if ('is On' in answer) != on:
            self.p.console_answer('tcai')

    def tough(self):
        self.p.console('player.setav health 5000')
        self.p.console('player.setav magicka 3000')
        self.p.console('player.restoreav health 100000')
        self.p.console('player.restoreav magicka 100000')

    def normal(self, health=240, magicka=200):
        self.p.console('player.setav health %d' % health)
        self.p.console('player.setav magicka %d' % magicka)
        self.p.console('player.restoreav health 100000')

    def spawn(self, base):
        before = {a['form'] for a in self.view()['actors']}
        self.p.console('player.placeatme %s 1' % base)
        end = time.time() + 6
        while time.time() < end:
            new = {a['form'] for a in self.view()['actors']} - before
            if new:
                return sorted(new)[0]
            time.sleep(0.3)
        return None

    def kill(self, form):
        self.p.tool('papyrus', {'action': 'call', 'script': 'Actor', 'function': 'Kill', 'self': {'form': form}, 'args': [None]})

    def actor(self, form):
        return next((a for a in self.view()['actors'] if a['form'] == form), None)

    def read_log(self):
        """The new part of this game's log since the last call, and the server ids it names."""
        try:
            if os.path.getsize(self.log_path) < self.offset:
                self.offset = 0
            with open(self.log_path, 'r', encoding='utf-8', errors='replace') as f:
                f.seek(self.offset)
                text = f.read()
                self.offset = f.tell()
        except OSError:
            return ''
        for m in re.finditer(r'New entity remotely managed, form id: ([0-9A-Fa-f]+), server id: ([0-9A-Fa-f]+)|Entity: local actor ([0-9A-Fa-f]+) has server id ([0-9A-Fa-f]+)', text):
            form, sid = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
            self.sids[sid.upper()] = '0x' + form.upper().zfill(8)
        for m in re.finditer(r'CharacterSpawnRequest, server id: ([0-9A-Fa-f]+), form id: ([0-9A-Fa-f]+)', text):
            self.sids[m.group(1).upper()] = '0x' + m.group(2).upper().zfill(8)
        return text

    def lines_since(self, offset, pattern):
        with open(self.log_path, 'r', encoding='utf-8', errors='replace') as f:
            f.seek(offset)
            return [line.rstrip() for line in f if re.search(pattern, line)]

    def log_size(self):
        return os.path.getsize(self.log_path)

    def connected(self):
        with open(self.log_path, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
        session = text[text.rfind('Skyrim Together client, build'):]
        return session.rfind('Connected: the server accepted') > session.rfind('Disconnected from server')


class Scanner(threading.Thread):
    """Compares both games once a second, by form for placed actors and by server id for the rest, near a player only."""

    def __init__(self, a, b, findings_path, shots):
        super().__init__(daemon=True)
        self.a, self.b = a, b
        self.path = findings_path
        self.shots = shots
        self.paused = False
        self.done = False
        self.open = {}
        self.found = []
        self.test = '-'
        self.lock = threading.Lock()

    def note(self, key, holding, need, text, where=None):
        now = time.time()
        if not holding:
            state = self.open.pop(key, None)
            if state and state.get('said'):
                self.write('ended after %.0f s (worst %s): %s' % (now - state['since'], state.get('worst'), key))
            return
        state = self.open.setdefault(key, {'since': now})
        state['worst'] = text
        if not state.get('said') and now - state['since'] >= need:
            state['said'] = True
            self.write('FINDING %s: %s' % (key, text))
            self.found.append({'test': self.test, 'time': time.strftime('%H:%M:%S'), 'key': key, 'text': text})
            if where is not None:
                self.shots.append((key, where))

    def write(self, text):
        log('  [scanner]', self.test, text)
        with self.lock, open(self.path, 'a', encoding='utf-8') as f:
            f.write('%s %s %s\n' % (time.strftime('%H:%M:%S'), self.test, text))

    def key_of(self, game, form):
        """The same name for this actor in both games: its form when placed, its server id when temporary."""
        if not form.startswith('0xFF'):
            return form
        return next(('sid' + sid for sid, f in game.sids.items() if f == form), None)

    def scan(self):
        for g in (self.a, self.b):
            g.read_log()
        va, vb = self.a.view(), self.b.view()
        if not va['at'] or not vb['at']:
            return
        for me, other, label in ((va, vb, self.a.label), (vb, va, self.b.label)):
            drawn = [c for c in me['actors'] if c['name'] == me['name'] and c['has3d']]
            self.note('player-drawn-twice-in-%s' % label, len(drawn) > 1, 2, '%s draws the other player %d times: %s' % (label, len(drawn), [c['form'] for c in drawn]), drawn[0]['pos'] if drawn else None)
        keyed_a = {self.key_of(self.a, x['form']): x for x in va['actors'] if x['name'] != va['name']}
        keyed_b = {self.key_of(self.b, x['form']): x for x in vb['actors'] if x['name'] != vb['name']}
        for key in set(keyed_a) & set(keyed_b):
            if key is None:
                continue
            x, y = keyed_a[key], keyed_b[key]
            near = min(math.dist(x['pos'], va['at']), math.dist(y['pos'], vb['at'])) < NEAR
            name = '%s %s' % (x['name'], key)
            self.note('life %s' % name, near and x['alive'] != y['alive'], 5, 'dead in one game, alive in the other: first %s, second %s, %d units from the nearer player' % (
                'alive' if x['alive'] else 'dead', 'alive' if y['alive'] else 'dead', min(math.dist(x['pos'], va['at']), math.dist(y['pos'], vb['at']))), x['pos'])
            gap = math.dist(x['pos'], y['pos'])
            limit = 150 if not x['alive'] and not y['alive'] else 300
            self.note('place %s' % name, near and gap > limit, 5, '%.0f units apart: first %s, second %s, %s' % (gap, [round(v) for v in x['pos']], [round(v) for v in y['pos']], 'dead' if not x['alive'] else 'alive'), x['pos'])
        for me, there, label in ((va, vb, self.a.label), (vb, va, self.b.label)):
            bodies = [x for x in me['actors'] if not x['alive'] and x['has3d'] and x['name'] != me['name'] and math.dist(x['pos'], me['at']) < NEAR]
            elsewhere = [y for y in there['actors'] if not y['alive'] and y['has3d']]
            for i, x in enumerate(bodies):
                twins = [y for y in bodies[i + 1:] if y['name'] == x['name'] and math.dist(x['pos'], y['pos']) < 300]
                others = [y for y in elsewhere if y['name'] == x['name'] and math.dist(x['pos'], y['pos']) < 600]
                self.note('more-bodies %s %s %s' % (label, x['name'], x['form']), len(twins) + 1 > len(others) and bool(twins), 5, '%s draws %d bodies of %s within 300 units, the other game %d: %s' % (
                    label, 1 + len(twins), x['name'], len(others), [x['form']] + [y['form'] for y in twins]), x['pos'])
                self.note('below %s %s %s' % (label, x['name'], x['form']), me['at'][2] - x['pos'][2] > 700 and math.dist(x['pos'][:2], me['at'][:2]) < 500, 5, '%s: body of %s %.0f units below the player, right under it' % (label, x['name'], me['at'][2] - x['pos'][2]), x['pos'])

    def run(self):
        while not self.done:
            if not self.paused:
                try:
                    self.scan()
                except Exception:
                    pass
            time.sleep(1.0)


class Run:
    def __init__(self, a, b, scanner):
        self.a, self.b, self.scanner = a, b, scanner
        self.results = []
        self.shots_taken = 0

    def picture(self, game, name, where=None):
        if self.shots_taken >= 200:
            return None
        self.shots_taken += 1
        path = os.path.join(OUT, '%s-%s-%s.png' % (time.strftime('%H%M%S'), name, game.label))
        try:
            return game.p.shot_of(name, where, path) if where is not None else game.p.shot(name, path)
        except Exception as e:
            log('  no picture', name, type(e).__name__)
            return None

    def both_at(self, travel=None, gap=300):
        """Both games at one place, the second player a little to the side, each player facing the other."""
        if travel:
            for g in (self.a, self.b):
                g.p.console('cow ' + travel)
            end = time.time() + 120
            time.sleep(4)
            while time.time() < end:
                if all('Loading Menu' not in g.p.tool('menu', {'action': 'list'}).get('openMenus', []) for g in (self.a, self.b)):
                    break
                time.sleep(2)
            time.sleep(8)
        at = self.a.p.position()
        self.b.p.console('player.setpos x %.0f' % (at[0] + gap))
        self.b.p.console('player.setpos y %.0f' % at[1])
        self.b.p.console('player.setpos z %.0f' % (at[2] + 30))
        time.sleep(4)
        self.a.look_at(self.b.p.position())
        self.b.look_at(self.a.p.position())
        time.sleep(1)


def walk_to(game, where, reach=120, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        me = game.p.position()
        if math.dist(me[:2], where[:2]) <= reach:
            return True
        game.look_at(where)
        time.sleep(0.4)
        game.p.hold_key('w', 700)
        time.sleep(0.8)
    return False


def measured_setup(g, health=2000, magicka=3000):
    """No regeneration, so that what a spell takes stays visible in the numbers."""
    g.p.console('player.setav healrate 0')
    g.p.console('player.setav magickarate 0')
    g.p.console('player.setav combathealthregenmult 0')
    g.p.console('player.setav health %d' % health)
    g.p.console('player.setav magicka %d' % magicka)
    g.p.console('player.restoreav health 100000')
    g.p.console('player.restoreav magicka 100000')


def measured_end(g):
    g.p.console('player.setav healrate 0.7')
    g.p.console('player.setav magickarate 3')
    g.p.console('player.setav combathealthregenmult 0.7')


def hand_cast(caster, where, seconds, pulses=None):
    """Casts with the hands at a place: held for a concentration spell, in pulses for one that charges and is released."""
    caster.aim_at(where)
    time.sleep(0.8)
    if pulses:
        for _ in range(pulses):
            caster.body.cast = True
            time.sleep(1.2)
            caster.body.cast = False
            time.sleep(0.8)
    else:
        caster.body.cast = True
        time.sleep(seconds)
    caster.rest_aim()
    time.sleep(1.5)


def t_spells(r, n):
    """7:57 the spells do no damage. Sparks held and Firebolt charged, from the hands and fired by the game, on the other player and on NPCs of either game, with regeneration off."""
    for g in (r.a, r.b):
        g.set_ai(False)
        measured_setup(g)
    r.both_at(gap=230)
    for caster, target in ((r.a, r.b), (r.b, r.a)):
        copy = next((c for c in caster.copies() if c['has3d']), None)
        if not copy:
            n.append('%s has no copy of the other player' % caster.label)
            continue
        for spell, label, seconds, pulses in ((SPARKS, 'Sparks held 6 s', 6, None), (FIREBOLT, 'Firebolt charged 3 times', 0, 3)):
            caster.p.console('player.equipspell %s left' % spell)
            caster.p.console('player.equipspell %s right' % spell)
            time.sleep(1)
            offset, target_log = caster.log_size(), target.log_size()
            before, magicka = target.health(), caster.me()['magicka']['current']
            hand_cast(caster, copy['pos'], seconds, pulses)
            if pulses is None:
                r.picture(target, 'spell-pvp-target-view', None)
            n.append('%s on the other player, %s from the hands, %.0f units: %.0f magicka; target health %.1f to %.1f; lines %s' % (
                caster.label, label, math.dist(caster.p.position(), copy['pos']), magicka - caster.me()['magicka']['current'], before, target.health(),
                [x[-115:] for x in target.lines_since(target_log, 'PvP:')[-2:]]))
            before = target.health()
            for _ in range(3):
                caster.p.tool('papyrus', {'action': 'call', 'script': 'Spell', 'function': 'Cast', 'self': {'form': '0x' + spell}, 'args': [{'form': '0x14'}, {'form': copy['form']}]})
                time.sleep(1)
            time.sleep(2)
            n.append('    the same fired 3 times by the game: target health %.1f to %.1f' % (before, target.health()))
    for owner, other in ((r.a, r.b), (r.b, r.a)):
        for spell, label, seconds, pulses in ((SPARKS, 'Sparks held 6 s', 6, None), (FIREBOLT, 'Firebolt charged 3 times', 0, 3)):
            form = owner.spawn(BANDIT_TANK)
            if not form:
                n.append('%s could not place an NPC' % owner.label)
                continue
            time.sleep(4)
            owner.read_log()
            other.read_log()
            sid = next((s for s, f in owner.sids.items() if f == form), None)
            there = other.sids.get(sid) if sid else None
            at = owner.actor(form)['pos']
            # The NPC stands where the test can reach it from both players.
            mid = [(a + b) / 2 for a, b in zip(owner.p.position(), other.p.position())]
            owner.p.tool('papyrus', {'action': 'call', 'script': 'ObjectReference', 'function': 'SetPosition', 'self': {'form': form}, 'args': [mid[0], mid[1], at[2]]})
            time.sleep(2)
            for caster, fid in ((owner, form), (other, there)):
                npc = caster.actor(fid) if fid else None
                now = owner.actor(form)
                if not npc or not now or not now['alive']:
                    n.append('%s has no living NPC to cast at (%s)' % (caster.label, sid))
                    continue
                caster.p.console('player.equipspell %s left' % spell)
                caster.p.console('player.equipspell %s right' % spell)
                time.sleep(1)
                start, magicka = now['hp'], caster.me()['magicka']['current']
                hand_cast(caster, npc['pos'], seconds, pulses)
                after = owner.actor(form)
                seen = other.actor(there) if there else None
                n.append('%s on the NPC of %s, %s from the hands, %.0f units: %.0f magicka; health at the owner %.1f to %s, on the other side %s' % (
                    caster.label, owner.label, label, math.dist(caster.p.position(), npc['pos']), magicka - caster.me()['magicka']['current'], start, after and round(after['hp'], 1), seen and round(seen['hp'], 1)))
                if after and after['alive']:
                    before = after['hp']
                    for _ in range(3):
                        caster.p.tool('papyrus', {'action': 'call', 'script': 'Spell', 'function': 'Cast', 'self': {'form': '0x' + spell}, 'args': [{'form': '0x14'}, {'form': fid}]})
                        time.sleep(1)
                    time.sleep(2)
                    after = owner.actor(form)
                    n.append('    the same fired 3 times by the game: at the owner %.1f to %s' % (before, after and round(after['hp'], 1)))
            owner.kill(form)
            time.sleep(1)
    for g in (r.a, r.b):
        g.p.console('player.equipspell %s left' % SPARKS)
        g.p.console('player.equipspell %s right' % SPARKS)
        measured_end(g)
        g.set_ai(True)


def spell_on_own_npc(caster, label, n, r):
    """A new NPC of this game in front of the player, at a reach Sparks has; Sparks and Firebolt swept across it from the hands, then fired by the game."""
    for spell, name in ((SPARKS, 'Sparks'), (FIREBOLT, 'Firebolt')):
        form = caster.spawn(BANDIT_TANK)
        if not form:
            n.append('%s %s: no NPC' % (label, name))
            continue
        time.sleep(2)
        me, npc = caster.p.position(), caster.actor(form)
        heading = math.radians(caster.room)
        at = (me[0] + 200 * math.sin(heading), me[1] + 200 * math.cos(heading), npc['pos'][2])
        caster.p.tool('papyrus', {'action': 'call', 'script': 'ObjectReference', 'function': 'SetPosition', 'self': {'form': form}, 'args': list(at)})
        time.sleep(2)
        caster.p.console('player.equipspell %s left' % spell)
        caster.p.console('player.equipspell %s right' % spell)
        time.sleep(1)
        npc = caster.actor(form)
        start, magicka = npc['hp'], caster.me()['magicka']['current']
        caster.aim_at(npc['pos'])
        time.sleep(0.8)
        base_yaw, base_pitch = caster.body.hand_yaw, caster.body.pitch
        shot = False
        for offset in (-30, -20, -10, 0, 10, 20, 30, 20, 10, 0, -10, -20, -30):
            caster.body.hand_yaw = base_yaw + math.radians(offset)
            for tilt in (-10, 0, 10):
                caster.body.pitch = base_pitch + math.radians(tilt)
                caster.body.cast = True
                time.sleep(0.7 if spell == SPARKS else 1.2)
                if spell != SPARKS:
                    caster.body.cast = False
                    time.sleep(0.3)
                if not shot and offset == 0:
                    shot = True
                    r.picture(caster, 'baseline-%s-%s' % (label.replace(' ', '-'), name), None)
        caster.rest_aim()
        time.sleep(1.5)
        after = caster.actor(form)
        hands = after['hp'] if after else None
        n.append('%s, %s swept from the hands, %.0f units: %.0f magicka; NPC health %.1f to %s' % (label, name, math.dist(caster.p.position(), npc['pos']), magicka - caster.me()['magicka']['current'], start, hands))
        if after and after['alive']:
            before = after['hp']
            for _ in range(3):
                caster.p.tool('papyrus', {'action': 'call', 'script': 'Spell', 'function': 'Cast', 'self': {'form': '0x' + spell}, 'args': [{'form': '0x14'}, {'form': form}]})
                time.sleep(1)
            time.sleep(2)
            after = caster.actor(form)
            n.append('    the same fired 3 times by the game: %.1f to %s' % (before, after and round(after['hp'], 1)))
        caster.kill(form)
        time.sleep(1)


def t_spells_baseline(r, n):
    """Do spells from the hands hurt at all on this rig? The same cast online and with the game off the server: damage only offline is the reported bug, none in both is the rig's aim."""
    for caster, other in ((r.a, r.b),):
        caster.set_ai(False)
        measured_setup(caster)
        spell_on_own_npc(caster, caster.label + ' online', n, r)
        toggle_connection(caster, other)
        time.sleep(6)
        n.append('%s connected: %s (should be False), %s connected: %s' % (caster.label, caster.connected(), other.label, other.connected()))
        if not caster.connected():
            measured_setup(caster)
            spell_on_own_npc(caster, caster.label + ' offline', n, r)
        toggle_connection(caster, other)
        end = time.time() + 40
        while time.time() < end and not caster.connected():
            time.sleep(1)
        n.append('%s back on the server: %s' % (caster.label, caster.connected()))
        time.sleep(5)
        caster.p.console('player.equipspell %s left' % SPARKS)
        caster.p.console('player.equipspell %s right' % SPARKS)
        measured_end(caster)
        caster.set_ai(True)


def t_respawn(r, n):
    """8:00 on a respawn the body flickers and the head is gone for a few seconds. One player goes down and respawns; the other watches its copy closely."""
    r.both_at(gap=250)
    for dying, watcher in ((r.b, r.a),):
        dying.normal()
        watcher.tough()
        time.sleep(2)
        copy = next((c for c in watcher.copies() if c['has3d']), None)
        if not copy:
            n.append('%s has no copy of the other player' % watcher.label)
            continue
        where = copy['pos']
        watcher.look_at(where)
        offset = dying.log_size()
        dying.kill('0x00000014')
        started = time.time()
        history, ups = [], False
        last = None
        pictures = 0
        while time.time() - started < 100:
            drawn = [(c['form'], c['has3d'], c['alive']) for c in watcher.copies()]
            if drawn != last:
                history.append((round(time.time() - started, 1), drawn))
                last = drawn
            lines = dying.lines_since(offset, 'PlayerService: respawning player|PlayerService: .*up again')
            if lines and not ups:
                ups = time.time()
                history.append((round(ups - started, 1), 'log: ' + lines[-1][-90:]))
            if ups and pictures < 8 and time.time() - ups > pictures * 0.5:
                drawn_now = [c for c in watcher.copies() if c['has3d']] or watcher.copies()
                if drawn_now:
                    r.picture(watcher, 'respawn-copy-%d' % pictures, drawn_now[-1]['pos'])
                pictures += 1
            if ups and time.time() - ups > 15:
                break
            time.sleep(0.15)
        n.append('%s went down; what %s drew of it (seconds, [form, 3D, alive]): %s' % (dying.label, watcher.label, history[:24]))
        n.append('  changes: %d; log: %s' % (len(history), [x[-100:] for x in dying.lines_since(offset, 'PlayerService:')[-5:]]))
        dying.normal()
        time.sleep(5)
    r.a.tough()
    r.b.tough()


def t_corpse(r, n):
    """8:02 a body: the first death location differs, and when the other player comes close it disappears. Dragging itself needs hands, so this kills and walks up."""
    for g in (r.a, r.b):
        g.tough()
        g.set_ai(False)
    r.both_at(gap=500)
    for owner, other in ((r.a, r.b), (r.b, r.a)):
        form = owner.spawn(MAGE)
        if not form:
            n.append('%s could not place an NPC' % owner.label)
            continue
        time.sleep(4)
        owner.read_log()
        other.read_log()
        sid = next((s for s, f in owner.sids.items() if f == form), None)
        there = other.sids.get(sid) if sid else None
        owner.kill(form)
        time.sleep(6)
        body = owner.actor(form)
        copy = other.actor(there) if there else None
        if not body:
            n.append('%s: its own body is gone' % owner.label)
            continue
        n.append('%s killed its NPC %s: copy in %s %s; apart at first: %s' % (owner.label, sid, other.label, there, round(math.dist(body['pos'], copy['pos'])) if copy else 'no copy'))
        if not there:
            continue
        lost = {owner.label: 0, other.label: 0}
        closest = 99999
        walker = threading.Thread(target=walk_to, args=(other, body['pos'], 60, 15), daemon=True)
        walker.start()
        for _ in range(45):
            x, y = owner.actor(form), other.actor(there)
            lost[owner.label] += 0 if x and x['has3d'] else 1
            lost[other.label] += 0 if y and y['has3d'] else 1
            if y:
                closest = min(closest, math.dist(other.p.position(), y['pos']))
            time.sleep(0.4)
        walker.join(1)
        x, y = owner.actor(form), other.actor(there)
        n.append('%s walks up to that body, down to %.0f units: looks with no body: %s; apart now %s' % (other.label, closest, lost, round(math.dist(x['pos'], y['pos'])) if x and y else 'gone'))
        if x:
            r.picture(other, 'corpse-near', x['pos'])
    for g in (r.a, r.b):
        g.set_ai(True)


def t_drops(r, n):
    """8:06 dropped items were not visible for the other player, maybe because it was in a paused menu."""
    for g in (r.a, r.b):
        g.tough()
        g.set_ai(False)
    r.both_at(gap=300)
    for dropper, seer in ((r.a, r.b), (r.b, r.a)):
        dropper.p.console('player.additem %s 12' % ITEM)
        time.sleep(1)
        placed = lambda start: len(seer.lines_since(start, 'DroppedItem: placed'))
        for i in range(3):
            start = seer.log_size()
            dropper.p.console('player.drop %s 1' % ITEM)
            time.sleep(4)
            n.append('%s drops an item (%d): %d placed in %s; %s' % (dropper.label, i + 1, placed(start), seer.label, [x[-110:] for x in seer.lines_since(start, 'DroppedItem:')[-2:]]))
            dropper.p.console('player.setpos x %.0f' % (dropper.p.position()[0] + 400 * (1 if i % 2 else -1)))
            time.sleep(1.5)
        start = seer.log_size()
        seer.p.tool('menu', {'action': 'open', 'name': 'Journal Menu'})
        time.sleep(1.5)
        dropper.p.console('player.drop %s 1' % ITEM)
        time.sleep(5)
        during = placed(start)
        seer.p.tool('menu', {'action': 'close', 'name': 'Journal Menu'})
        time.sleep(6)
        n.append('dropped while %s is in a paused menu: %d placed during, %d after it closed; %s' % (seer.label, during, placed(start), [x[-110:] for x in seer.lines_since(start, 'DroppedItem:')[-2:]]))
        r.picture(seer, 'drop-seen', dropper.p.position())
    for g in (r.a, r.b):
        g.set_ai(True)


def anim_of(game, form):
    def call(script, function, args=None):
        body = {'action': 'call', 'script': script, 'function': function, 'self': {'form': form}}
        if args:
            body['args'] = args
        return game.p.tool('papyrus', body).get('returned')
    return {'sneak': call('Actor', 'IsSneaking'), 'swim': call('Actor', 'IsSwimming'), 'run': call('Actor', 'IsRunning'), 'drawn': call('Actor', 'IsWeaponDrawn'),
            'speed': call('ObjectReference', 'GetAnimationVariableFloat', ['Speed'])}


def t_npc_animation(r, n):
    """8:09 to 8:14: at Windhelm an NPC moves as if frozen on one side, a guard slides or crouches, Durak swims on snow. Every living NPC near both players, compared for minutes."""
    for g in (r.a, r.b):
        g.tough()
        g.set_ai(True)
    r.both_at(WINDHELM, gap=200)
    table = {}
    samples = {}
    end = time.time() + 300
    round_no = 0
    left = None
    began = time.time()
    while time.time() < end:
        round_no += 1
        if left is None and time.time() - began > 90:
            left = r.b.p.position()
            n.append('%s the second player leaves, so the first owns what is here' % time.strftime('%H:%M:%S'))
            r.b.p.console('cow ' + MISTWATCH)
            time.sleep(20)
            r.b.p.console('cow ' + WINDHELM)
            time.sleep(20)
            n.append('%s the second player is back' % time.strftime('%H:%M:%S'))
        if left is not None and left is not False and time.time() - began > 190:
            left = False
            n.append('%s the first player leaves, so the second owns what is here' % time.strftime('%H:%M:%S'))
            r.a.p.console('cow ' + MISTWATCH)
            time.sleep(20)
            r.a.p.console('cow ' + WINDHELM)
            time.sleep(20)
            n.append('%s the first player is back' % time.strftime('%H:%M:%S'))
        for g in (r.a, r.b):
            g.read_log()
        va, vb = r.a.view(), r.b.view()
        placed = {x['form']: x for x in va['actors'] if x['alive'] and x['has3d'] and not x['form'].startswith('0xFF') and x['name'] != va['name'] and math.dist(x['pos'], va['at']) < 2500}
        other = {x['form']: x for x in vb['actors'] if x['has3d']}
        common = sorted((f for f in placed if f in other), key=lambda f: math.dist(placed[f]['pos'], va['at']))[:6]
        states = {}

        def sample(game, into):
            for f in common:
                try:
                    into[f] = (anim_of(game, f), time.time())
                except Exception:
                    pass
        from_a, from_b = {}, {}
        threads = [threading.Thread(target=sample, args=(r.a, from_a)), threading.Thread(target=sample, args=(r.b, from_b))]
        for t in threads:
            t.start()
        for t in threads:
            t.join(30)
        va2, vb2 = r.a.view(), r.b.view()
        after_a = {x['form']: x for x in va2['actors']}
        after_b = {x['form']: x for x in vb2['actors']}
        for f in common:
            if f not in from_a or f not in from_b or f not in after_a or f not in after_b:
                continue
            x, y = from_a[f][0], from_b[f][0]
            name = placed[f]['name']
            entry = table.setdefault(f, {'name': name, 'samples': 0, 'sneak': 0, 'swim': 0, 'run': 0, 'drawn': 0, 'speed': 0, 'slide_first': 0, 'slide_second': 0})
            entry['samples'] += 1
            for key in ('sneak', 'swim', 'run', 'drawn'):
                if x[key] != y[key]:
                    entry[key] += 1
            speed_x, speed_y = x['speed'] or 0.0, y['speed'] or 0.0
            if abs(speed_x - speed_y) > 60:
                entry['speed'] += 1
            moved_a = math.dist(after_a[f]['pos'], placed[f]['pos'])
            moved_b = math.dist(after_b[f]['pos'], other[f]['pos'])
            if moved_a > 80 and speed_x < 5:
                entry['slide_first'] += 1
            if moved_b > 80 and speed_y < 5:
                entry['slide_second'] += 1
            odd = x['sneak'] != y['sneak'] or x['swim'] != y['swim'] or abs(speed_x - speed_y) > 120
            if odd and samples.get(f, 0) < 2:
                samples[f] = samples.get(f, 0) + 1
                n.append('%s %s (%s): first %s, second %s' % (time.strftime('%H:%M:%S'), name, f, x, y))
                r.picture(r.a, 'anim-%s-%d' % (f[2:], samples[f]), after_a[f]['pos'])
                r.picture(r.b, 'anim-%s-%d' % (f[2:], samples[f]), after_b[f]['pos'])
        if round_no % 4 == 0:
            walker = r.a if round_no % 8 == 0 else r.b
            walker.body.yaw = walker.body.hand_yaw = (walker.body.yaw + math.pi / 2)
            walker.p.hold_key('w', 3000)
        time.sleep(1)
    for f, e in sorted(table.items(), key=lambda kv: -sum(kv[1][k] for k in ('sneak', 'swim', 'speed', 'slide_first', 'slide_second'))):
        n.append('%s %s: %d samples; differs: sneak %d, swim %d, run %d, drawn %d, speed by over 60: %d; moved with no walk: first %d, second %d' % (
            e['name'], f, e['samples'], e['sneak'], e['swim'], e['run'], e['drawn'], e['speed'], e['slide_first'], e['slide_second']))
    for g in (r.a, r.b):
        lines = g.lines_since(max(0, g.log_size() - 3000000), 'ActionFlood')
        n.append('%s: %d ActionFlood lines in the log tail' % (g.label, len(lines)))


def nearest_door(game):
    refs = game.p.tool('inspect', {'kind': 'refs', 'formType': 'Door', 'radius': 0, 'limit': 200}).get('refs', [])
    me = game.p.position()
    refs = [x for x in refs if x.get('door', {}).get('teleport', True)]
    return sorted(refs, key=lambda x: math.dist(x['position'], me))


def t_door(r, n):
    """8:11 the other player goes inside: its copy stays until its load ended, and later a copy of its follower appears stuck in the door."""
    for g in (r.a, r.b):
        g.tough()
    r.both_at(WINDHELM, gap=200)
    for goer, watcher in ((r.b, r.a), (r.a, r.b)):
        doors = nearest_door(goer)
        n.append('%s: %d doors, nearest %s' % (goer.label, len(doors), [(d.get('formId'), d.get('name'), round(math.dist(d['position'], goer.p.position()))) for d in doors[:3]]))
        goer.p.console('prid A2C94')
        goer.p.console('moveto player')
        goer.p.console('setplayerteammate 1')
        try:
            answer = goer.p.tool('papyrus', {'action': 'call', 'script': 'DialogueFollowerScript', 'function': 'SetFollower', 'self': {'form': '0x000750BA'}, 'args': [{'form': '0x000A2C94'}]})
            n.append('follower set: %s' % str(answer)[:90])
        except Exception as e:
            n.append('follower not set: %s' % type(e).__name__)
        time.sleep(6)
        if doors:
            d = doors[0]['position']
            for axis, value in zip('xyz', (d[0], d[1] - 90, d[2] + 10)):
                goer.p.console('player.setpos %s %.0f' % (axis, value))
            time.sleep(6)
            watcher.look_at(goer.p.position())
        offset = watcher.log_size()
        if doors:
            goer.p.tool('papyrus', {'action': 'call', 'script': 'ObjectReference', 'function': 'Activate', 'self': {'form': doors[0]['formId']}, 'args': [{'form': '0x14'}, False]})
        started = time.time()
        time.sleep(2.5)
        if not doors or 'Loading Menu' not in goer.p.tool('menu', {'action': 'list'}, 3).get('openMenus', []):
            n.append('the door did not load; console travel instead')
            goer.p.console('coc CandlehearthHall')
        load_seen, copy_gone, follower_seen = None, None, []
        while time.time() - started < 45:
            try:
                loading = 'Loading Menu' in goer.p.tool('menu', {'action': 'list'}, 3).get('openMenus', [])
            except Exception:
                loading = True
            if loading and load_seen is None:
                load_seen = time.time() - started
            drawn = [c for c in watcher.copies() if c['has3d']]
            if not drawn and copy_gone is None:
                copy_gone = time.time() - started
            lydia = [(x['form'], [round(v) for v in x['pos']]) for x in watcher.view()['actors'] if x['name'] == 'Lydia' and x['has3d']]
            if lydia and (not follower_seen or follower_seen[-1][1] != lydia):
                follower_seen.append((round(time.time() - started, 1), lydia))
            time.sleep(0.3)
        n.append('%s through a door: loading screen after %s s, its copy gone in %s after %s s; Lydia there over 45 s: %s' % (
            goer.label, load_seen and round(load_seen, 1), watcher.label, copy_gone and round(copy_gone, 1), follower_seen[:8]))
        n.append('  %s' % [x[-130:] for x in watcher.lines_since(offset, 'CellRange|Actor removed.*FF|CopyDiag|Lydia')[:6]])
        r.picture(watcher, 'door-after', None)
        goer.p.console('prid A2C94')
        goer.p.console('setplayerteammate 0')
        for g in (goer,):
            g.p.console('cow ' + WINDHELM)
        time.sleep(25)
        r.both_at(None, gap=200)


def suspended(pid, seconds, during):
    """Stops a game for a moment, so that a key pressed for the other one does not reach it."""
    handle = ctypes.windll.kernel32.OpenProcess(0x0800, False, pid)
    ctypes.windll.ntdll.NtSuspendProcess(handle)
    try:
        during()
        time.sleep(seconds)
    finally:
        ctypes.windll.ntdll.NtResumeProcess(handle)
        ctypes.windll.kernel32.CloseHandle(handle)


def press_f6():
    ctypes.windll.user32.keybd_event(0x75, 0, 0, 0)
    time.sleep(0.3)
    ctypes.windll.user32.keybd_event(0x75, 0, 2, 0)


def toggle_connection(game, other):
    """F6 reaches both games, so the other one is stopped while it is pressed."""
    suspended(other.pid, 0.6, press_f6)


def t_reconnect_body(r, n):
    """8:16 after a reconnect the dead Old Orc was there twice, and one fell through the floor. An Old Orc killed by one game; first the other player leaves and rejoins, then the owner itself."""
    for g in (r.a, r.b):
        g.tough()
        g.set_ai(False)
    r.both_at(gap=300)
    for owner, leaver in ((r.a, r.b), (r.b, r.a), (r.a, r.a), (r.b, r.b)):
        stayer = r.b if leaver is r.a else r.a
        form = owner.spawn(OLD_ORC)
        if not form:
            n.append('%s could not place an Old Orc' % owner.label)
            continue
        time.sleep(4)
        owner.kill(form)
        time.sleep(7)
        body = owner.actor(form)
        if not body:
            n.append('%s: its Old Orc body is not found' % owner.label)
            continue

        def bodies(game):
            return sorted((round(x['pos'][2] - body['pos'][2]), round(math.dist(x['pos'][:2], body['pos'][:2])), x['form'], x['has3d']) for x in game.view()['actors'] if x['name'] == 'Old Orc' and not x['alive'] and math.dist(x['pos'][:2], body['pos'][:2]) < 150)
        n.append('%s owns the body %s, %s leaves. Before: %s %s; %s %s' % (owner.label, form, leaver.label, stayer.label, bodies(stayer), leaver.label, bodies(leaver)))
        toggle_connection(leaver, stayer)
        time.sleep(7)
        n.append('  after the key: %s connected %s, %s connected %s' % (leaver.label, leaver.connected(), stayer.label, stayer.connected()))
        if not stayer.connected():
            n.append('  the key also reached %s; it is connected again' % stayer.label)
            toggle_connection(stayer, leaver)
            time.sleep(10)
        toggle_connection(leaver, stayer)
        end = time.time() + 40
        while time.time() < end and not leaver.connected():
            time.sleep(1)
        n.append('  %s back: %s' % (leaver.label, leaver.connected()))
        time.sleep(4)
        for wait in (0, 5, 10, 20, 30):
            time.sleep(wait)
            n.append('  rejoin +%d s: %s %s; %s %s' % (wait, stayer.label, bodies(stayer), leaver.label, bodies(leaver)))
            if wait == 5:
                r.picture(leaver, 'rejoin-body', body['pos'])
                r.picture(stayer, 'rejoin-body', body['pos'])
    for g in (r.a, r.b):
        g.set_ai(True)


def t_brawl(r, n):
    """Overall: both players fight NPCs of both games at once, while the scanner compares."""
    for g in (r.a, r.b):
        g.tough()
        g.set_ai(True)
    r.both_at(gap=300)
    made = []
    for g in (r.a, r.b):
        for base in (BANDIT_MELEE, BANDIT_MELEE, MAGE):
            f = g.spawn(base)
            if f:
                made.append((g, f))
    end = time.time() + 120
    while time.time() < end:
        for g in (r.a, r.b):
            v = g.view()
            enemies = sorted((x for x in v['actors'] if x['alive'] and x['has3d'] and x['name'] != v['name']), key=lambda x: math.dist(x['pos'], v['at']))
            if enemies:
                g.aim_at(enemies[0]['pos'])
                g.body.cast = True
        time.sleep(5)
    for g in (r.a, r.b):
        g.rest_aim()
    time.sleep(2)
    for g, f in made:
        x = g.actor(f)
        n.append('%s NPC %s: %s, %s' % (g.label, f, x and x['name'], x and ('alive %.0f' % x['hp'] if x['alive'] else 'dead')))
        if x and x['alive']:
            g.kill(f)
    time.sleep(5)


TESTS = {'spells-baseline': t_spells_baseline, 'spells': t_spells, 'respawn': t_respawn, 'corpse': t_corpse, 'drops': t_drops, 'reconnect-body': t_reconnect_body, 'door': t_door, 'npc-animation': t_npc_animation, 'brawl': t_brawl}


def game_ids():
    out = subprocess.run(['powershell', '-NoProfile', '-Command', "Get-Process urSovngarde -ErrorAction SilentlyContinue | Sort-Object StartTime | ForEach-Object { $_.Id }"], capture_output=True, text=True).stdout
    return [int(x) for x in out.split()]


def bring_up_second(second):
    keep = second.log_path
    if os.path.exists(keep):
        try:
            shutil.copyfile(keep, os.path.join(OUT, 'second-before-%s.log' % time.strftime('%H%M%S')))
            os.remove(keep)
        except OSError:
            pass
    second.offset = 0
    open(ASK_FILE, 'w').close()
    def wait(test, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                if test():
                    return True
            except Exception:
                pass
            time.sleep(2)
        return False
    if not wait(second.p.alive, 180):
        return False
    if not wait(lambda: 'Main Menu' in second.p.tool('menu', {'action': 'list'}).get('openMenus', []), 480):
        return False
    second.p.tool('game', {'action': 'loadLast'}, 60)
    if not wait(lambda: (second.p.alive() or {}).get('lastLifecycle') == 'postLoadGame', 240):
        return False
    time.sleep(3)
    second.p.console('cow ' + MISTWATCH)
    time.sleep(3)
    wait(lambda: 'Loading Menu' not in second.p.tool('menu', {'action': 'list'}).get('openMenus', []), 180)
    return wait(second.connected, 90)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tests', nargs='*')
    parser.add_argument('--until', default='')
    args = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)
    a, b = Game('first', 8921, 1), Game('second', 8922, 2)
    if not a.p.alive():
        return 5
    if not b.p.alive():
        log('bringing up the second game')
        if not bring_up_second(b):
            log('the second game did not come up')
            return 6
    ids = game_ids()
    if len(ids) < 2:
        log('games found:', ids)
        return 6
    a.pid, b.pid = ids[0], ids[1]
    log('games', ids, 'connected', a.connected(), b.connected())
    a.start_body()
    b.start_body()
    shots = []
    scanner = Scanner(a, b, os.path.join(OUT, 'findings.txt'), shots)
    scanner.start()
    run = Run(a, b, scanner)
    deadline = None
    if args.until:
        hour, minute = args.until.split(':')
        deadline = time.mktime(time.localtime()[:3] + (int(hour), int(minute), 0) + time.localtime()[6:]) + 180
    names = args.tests or list(TESTS)
    results_path = os.path.join(OUT, 'results.json')
    results = json.load(open(results_path, encoding='utf-8')) if os.path.exists(results_path) else []
    done_path = os.path.join(OUT, 'done.txt')
    done = open(done_path, encoding='utf-8').read().split() if os.path.exists(done_path) else []
    code = 0
    rounds = 0
    while True:
        rounds += 1
        for name in names:
            if deadline and time.time() > deadline:
                break
            if name in done and not args.tests and done.count(name) >= rounds:
                continue
            scanner.test = name
            notes = []
            log('=== %s, round %d' % (name, rounds))
            started = time.time()
            error = None
            try:
                TESTS[name](run, notes)
            except Exception as e:
                error = '%s: %s' % (type(e).__name__, e)
            alive = {g.label: bool(g.p.alive()) for g in (a, b)}
            for line in notes:
                log('   ', line[:400])
            if error:
                log('    ERROR', error[:300])
            results.append({'test': name, 'round': rounds, 'started': time.strftime('%H:%M:%S', time.localtime(started)), 'seconds': round(time.time() - started), 'notes': notes, 'error': error, 'alive': alive,
                            'findings': [f for f in scanner.found if f['test'] == name]})
            with open(results_path, 'w', encoding='utf-8') as f:
                json.dump(results, f, indent=1)
            with open(done_path, 'a', encoding='utf-8') as f:
                f.write(name + '\n')
            done.append(name)
            while shots and run.shots_taken < 200:
                key, where = shots.pop(0)
                scanner.paused = True
                safe = re.sub(r'[^A-Za-z0-9]+', '-', key)[:50]
                run.picture(a, 'finding-' + safe, where)
                run.picture(b, 'finding-' + safe, where)
                scanner.paused = False
            if not alive['first']:
                log('THE FIRST GAME IS GONE during', name)
                code = 5
                break
            if not alive['second']:
                log('THE SECOND GAME IS GONE during', name)
                if not bring_up_second(b):
                    code = 6
                    break
                ids = game_ids()
                b.pid = ids[-1]
                b.start_body()
        if code or not deadline or time.time() > deadline or args.tests:
            break
    scanner.done = True
    for g in (a, b):
        try:
            g.body.finish()
        except Exception:
            pass
    log('finished, code', code)
    return code


if __name__ == '__main__':
    sys.exit(main())
