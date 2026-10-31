"""Reads an actor from the game itself while a script runs, for live-takeover-walk.

    python Tools\\VR\\takeover-probe.py <form hex> <seconds> <output csv>

Each row: time, position, how far it travelled since the last row, the graph's Speed and SpeedSampled, the game's sneaking
and swimming, whether its weapon and the player's are drawn, the current package, and how far each foot lies from the pelvis,
sideways. A walking stride at about 0.65 s beats with the rows, so swinging legs can read steady: a stride needs a faster sample.
Feet that swing are legs that walk, whether or not the body travels. A body travelling with still feet slides.
"""
import csv
import math
import sys
import time
import urllib.request
import json

DEVBENCH = 'http://127.0.0.1:8921/api/tool/'


def tool(name, body):
    req = urllib.request.Request(DEVBENCH + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=4) as r:
        return json.loads(r.read().decode())


def papyrus(script, function, form, args=None):
    body = {'action': 'call', 'script': script, 'function': function, 'self': {'form': '0x' + form}}
    if args is not None:
        body['args'] = args
    try:
        return tool('papyrus', body).get('returned')
    except Exception as e:
        return 'ERR ' + type(e).__name__


def node_xy(form, name):
    at = []
    for axis in 'XY':
        try:
            at.append(tool('papyrus', {'action': 'call', 'script': 'NetImmerse', 'function': 'GetNodeWorldPosition' + axis,
                                       'args': [{'form': '0x' + form}, name, False]}).get('returned'))
        except Exception:
            return None
    return at if all(isinstance(v, (int, float)) for v in at) else None


def main():
    form, seconds, path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    end = time.time() + seconds
    last = None
    with open(path, 'w', newline='') as out:
        rows = csv.writer(out)
        rows.writerow(['time', 'x', 'y', 'z', 'step', 'speed', 'sampled', 'sneak', 'swim', 'drawn', 'player_drawn', 'package', 'foot_l', 'foot_r'])
        while time.time() < end:
            now = time.time()
            x, y = papyrus('ObjectReference', 'GetPositionX', form), papyrus('ObjectReference', 'GetPositionY', form)
            z = papyrus('ObjectReference', 'GetPositionZ', form)
            if not all(isinstance(v, (int, float)) for v in (x, y, z)):
                rows.writerow([time.strftime('%H:%M:%S', time.localtime(now)), x, y, z])
                out.flush()
                time.sleep(0.5)
                continue
            step = math.dist((x, y), last) if last else 0.0
            last = (x, y)
            speed = papyrus('ObjectReference', 'GetAnimationVariableFloat', form, ['Speed'])
            sampled = papyrus('ObjectReference', 'GetAnimationVariableFloat', form, ['SpeedSampled'])
            sneak = papyrus('Actor', 'IsSneaking', form, [])
            swim = papyrus('Actor', 'IsSwimming', form, [])
            drawn = papyrus('Actor', 'IsWeaponDrawn', form, [])
            player_drawn = papyrus('Actor', 'IsWeaponDrawn', '14', [])
            package = papyrus('Actor', 'GetCurrentPackage', form, [])
            package = package.get('formId') if isinstance(package, dict) else package
            pelvis = node_xy(form, 'NPC Pelvis [Pelv]')
            left, right = node_xy(form, 'NPC L Foot [Lft ]'), node_xy(form, 'NPC R Foot [Rft ]')
            foot_l = round(math.dist(left, pelvis), 1) if left and pelvis else ''
            foot_r = round(math.dist(right, pelvis), 1) if right and pelvis else ''
            stamp = time.strftime('%H:%M:%S', time.localtime(now)) + ('%.1f' % (now % 1))[1:]
            rows.writerow([stamp, round(x), round(y), round(z), round(step, 1), speed, sampled, sneak, swim, drawn, player_drawn, package, foot_l, foot_r])
            out.flush()
            # DevBench answers about 27 calls a second, and the running script needs its share.
            time.sleep(0.3)


main()
