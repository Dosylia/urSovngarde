"""Plays a rig game through its real input: a VR body that stands, turns and aims, walking, and pictures from two views.

    python Tools/VR/play.py <port> where                 the player, and the actors near by distance
    python Tools/VR/play.py <port> walk <seconds>        straight on, where the head looks
    python Tools/VR/play.py <port> shot <name> [<file>]  a picture of what the game shows
    python Tools/VR/play.py <port> console <command>

The port is DevBench's, 8921 unless another program holds it.
What was measured on the rig (FUS, the null driver's controllers):
    - The null headset rests at height 0, so a game with no pose held shows the floor.
    - Walking follows the headset's direction; the character's heading (player.setangle) does not steer it.
    - A spell goes about 90 degrees up and 60 degrees left of where the raw controller matrix points (SPELL_UP, SPELL_LEFT).
    - A pose can only change by a new sequence, and between two sequences the headset drops to its rest for about 2 frames.
      Body therefore starts one only when the pose has to change.
"""
import json
import math
import os
import shutil
import sys
import threading
import time
import urllib.error
import urllib.request

TRIGGER = 1 << 33
SPELL_UP = math.radians(90)
SPELL_LEFT = math.radians(60)
LONG_MS = 60000
PLAYER_FORM = '0x00000014'
FUS_ROOTS = [os.environ.get('SKYRIM_FUS_ROOT') or '', 'C:/FUS', 'E:/FUS']


def rotated(yaw, pitch, x, y, z):
    """OpenVR pose: forward is -z. Yaw turns left, pitch tilts up."""
    c, s, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    return [c, s * sp, s * cp, x, 0, cp, -sp, y, -s, c * sp, c * cp, z]


class Player:
    def __init__(self, port=8921):
        self.url = 'http://127.0.0.1:%d/api/tool/' % port
        self.health_url = 'http://127.0.0.1:%d/api/health' % port

    def tool(self, name, body, timeout=15):
        req = urllib.request.Request(self.url + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def alive(self):
        try:
            with urllib.request.urlopen(self.health_url, timeout=5) as r:
                return json.loads(r.read().decode())
        except Exception:
            return None

    def console(self, command):
        return self.tool('console', {'action': 'exec', 'command': command})

    def console_answer(self, command):
        self.tool('console', {'action': 'exec', 'command': command, 'capture': True})
        time.sleep(0.4)
        return ' '.join(self.tool('console', {'action': 'read'}).get('lines', []))

    def actors(self):
        return self.tool('inspect', {'kind': 'refs', 'formType': 'Actor', 'radius': 0, 'limit': 500}).get('refs', [])

    def position(self):
        return next((ref['position'] for ref in self.actors() if ref['formId'] == PLAYER_FORM), None)

    def heading(self):
        return self.tool('papyrus', {'action': 'call', 'script': 'ObjectReference', 'function': 'GetAngleZ', 'self': {'form': '0x14'}}).get('returned')

    def hold_key(self, key, ms):
        return self.tool('input', {'action': 'down', 'device': 'keyboard', 'key': key, 'maxHoldMs': int(ms), 'owner': 'play'})

    def worn(self, form):
        """What an actor has equipped, as (form id, name)."""
        items = self.tool('inspect', {'kind': 'inventory', 'formId': form, 'limit': 500}).get('items', [])
        return [(item.get('formId'), item.get('name')) for item in items if item.get('equipped')]

    def where(self):
        refs = self.actors()
        me = next((ref for ref in refs if ref['formId'] == PLAYER_FORM), None)
        if not me:
            print('no player among the actors')
            return
        print('player at (%.0f, %.0f, %.0f), cell %s' % (*me['position'], me.get('cell', {}).get('editorId') or me.get('cell', {}).get('formId')))
        near = sorted((math.dist(ref['position'], me['position']), i) for i, ref in enumerate(refs) if ref is not me)
        for gap, i in near[:25]:
            ref = refs[i]
            actor = ref.get('actor', {})
            print('  %6.0f  %-11s %-26s %s health %.0f/%.0f%s%s' % (gap, ref['formId'], (ref.get('name') or '?')[:26], 'alive' if actor.get('alive') else 'DEAD ', actor.get('health', 0),
                                                                     actor.get('healthMax', 0), ' HOSTILE' if actor.get('hostileToPlayer') else '', '' if actor.get('loaded3D') else ' no3D'))

    def shot(self, name, out=None):
        r = self.tool('capture', {'kind': 'native', 'checkpointId': name, 'cleanup': True}, timeout=40)
        if not r.get('ok'):
            return None
        for root in FUS_ROOTS:
            captures = os.path.join(root, 'overwrite', 'SKSE', 'Plugins', 'devbench', 'captures')
            for base, _, files in os.walk(captures) if root else ():
                if name + '.png' in files:
                    target = out or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'play', name + '.png')
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    shutil.copyfile(os.path.join(base, name + '.png'), target)
                    os.remove(os.path.join(base, name + '.png'))
                    return os.path.abspath(target)
        return None

    def shot_of(self, name, target, out=None):
        """The second point of view: a free camera outside the character, looking at that place."""
        eye = (target[0], target[1] - 330.0, target[2] + 190.0)
        aim = (target[0], target[1], target[2] + 60.0)
        dx, dy, dz = aim[0] - eye[0], aim[1] - eye[1], aim[2] - eye[2]
        try:
            self.tool('camera', {'action': 'freecam', 'on': True})
            self.tool('camera', {'action': 'drive', 'x': eye[0], 'y': eye[1], 'z': eye[2], 'pitch': math.atan2(-dz, math.hypot(dx, dy)), 'yaw': math.atan2(dx, dy)})
            time.sleep(0.4)
            return self.shot(name, out)
        finally:
            self.tool('camera', {'action': 'freecam', 'on': False})


class Body(threading.Thread):
    """Holds the virtual headset up and aims the hands: set yaw (the head), hand_yaw, pitch and cast, and it follows."""

    def __init__(self, player):
        super().__init__(daemon=True)
        self.p = player
        self.yaw, self.hand_yaw, self.pitch, self.cast = 0.0, 0.0, 0.0, False
        self.done = False
        self.token = None
        self.playing = None
        self.playing_since = 0.0
        self.restarts = 0
        self.errors = 0

    def face(self, room, target_degrees):
        """Turns head and hands to a world heading; room is the heading the room faces with the head straight."""
        self.yaw = self.hand_yaw = math.radians((room - target_degrees + 180) % 360 - 180)

    def _changed(self):
        if self.playing is None or time.time() - self.playing_since > (LONG_MS / 1000 - 3):
            return True
        yaw, hand_yaw, pitch, cast = self.playing
        return (abs(self.yaw - yaw) > math.radians(25) or abs(self.hand_yaw - hand_yaw) > math.radians(8)
                or abs(self.pitch - pitch) > math.radians(8) or self.cast != cast)

    def _frames(self):
        yaw, hand_yaw, pitch, cast = self.yaw, self.hand_yaw, self.pitch, self.cast
        start_yaw = self.playing[0] if self.playing else yaw
        base = self.p.tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
        out = []
        # Dense frames for the head turn, then a held pose: a small message, so the gap between two sequences stays short.
        for i, t_ms in enumerate(list(range(0, 260, 25)) + list(range(1000, LONG_MS + 1, 1000))):
            f = json.loads(json.dumps(base))
            f.pop('seq', None)
            f['tMs'] = t_ms
            head = start_yaw + (yaw - start_yaw) * min(1.0, t_ms / 250.0)
            f['hmd']['matrix'] = rotated(head, 0.0, 0, 1.6, 0)
            c, s = math.cos(yaw), math.sin(yaw)
            for side, across in (('left', -0.25), ('right', 0.25)):
                f[side]['matrix'] = rotated(hand_yaw, pitch, across * c - 0.35 * s, 1.2, -across * s - 0.35 * c)
            for d in ('hmd', 'left', 'right'):
                f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
                if f[d].get('index') is None:
                    f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
                for key in ('velocity', 'angularVelocity'):
                    f[d].setdefault(key, [0.0, 0.0, 0.0])
                if d != 'hmd' and not f[d].get('controller'):
                    f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
            for side in ('left', 'right'):
                hand = f[side]['controller']
                hand['packetNumber'] = (base[side].get('controller') or {}).get('packetNumber', 1) + i + 1
                hand['pressed'] = TRIGGER if cast else 0
                hand['touched'] = TRIGGER if cast else 0
                hand['axes'] = [[0.0, 0.0], [1.0 if cast else 0.0, 0.0], [0.0, 0.0], [0.0, 0.0], [0.0, 0.0]]
            out.append(f)
        return out, (yaw, hand_yaw, pitch, cast)

    def run(self):
        while not self.done:
            try:
                if self._changed():
                    frames, state = self._frames()
                    message = json.dumps({'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'play'}).encode()
                    if self.token:
                        try:
                            self.p.tool('input', {'action': 'stop', 'device': 'vrTrackedSet', 'controlToken': self.token, 'owner': 'play'})
                        except urllib.error.HTTPError:
                            pass
                    request = urllib.request.Request(self.p.url + 'input', data=message, headers={'Content-Type': 'application/json'})
                    with urllib.request.urlopen(request, timeout=30) as r:
                        self.token = json.loads(r.read().decode()).get('controlToken')
                    self.playing, self.playing_since = state, time.time()
                    self.restarts += 1
            except Exception:
                self.errors += 1
                self.playing = None
                time.sleep(1.0)
            time.sleep(0.05)

    def finish(self):
        self.done = True
        time.sleep(0.2)
        if self.token:
            try:
                self.p.tool('input', {'action': 'stop', 'device': 'vrTrackedSet', 'controlToken': self.token, 'owner': 'play'})
            except Exception:
                pass


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    player = Player(int(sys.argv[1]))
    action, args = sys.argv[2], sys.argv[3:]
    if action == 'where':
        player.where()
    elif action == 'walk':
        start = player.position()
        player.hold_key('w', float(args[0]) * 1000)
        time.sleep(float(args[0]) + 0.3)
        print('walked %.0f units' % math.dist(start, player.position()))
    elif action == 'shot':
        print(player.shot(args[0], args[1] if len(args) > 1 else None))
    elif action == 'console':
        print(str(player.console(' '.join(args)))[:300])
    else:
        raise SystemExit('unknown action %s' % action)


if __name__ == '__main__':
    main()
