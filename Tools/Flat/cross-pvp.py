"""A flat player and a VR player cannot hurt each other: each casts a Fireball at the other's copy, and nobody loses health.

    python Tools/Flat/cross-pvp.py
    python Tools/Flat/cross-pvp.py --expect-fail     on a build without the fix: must be RED

Both rigs up and connected: Tools/Flat/flat-rig.ps1 up first, then Tools/VR/headless.ps1 up -AutoConnect.
The server's PvP must be on (bEnablePvp), or nothing could hurt anybody and the run says nothing.
The Fireball is Skyrim.esm's, the same form in both games, cast by Papyrus from the player at the other player's copy.

What each player sees: their own health, read in their own game, before and for a few seconds after the other casts.
The attacker's game must also say the cast reached the copy: "PvP: spell ... hit" before the fix, "PvP: no hit between" after it.
"""
import argparse
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request

FIREBALL = '0x0001C789'
WATCH = 5.0
# Health a player may regain or lose to anything else while watched.
NOISE = 3.0


def game(name, port, root_variable, client_folder):
    root = os.environ.get(root_variable)
    if not root:
        sys.exit('INVALID: set the %s user variable' % root_variable)
    return {'name': name, 'devbench': 'http://127.0.0.1:%d/api/tool/' % port,
            'log': os.path.join(root, 'tools', client_folder, 'logs', 'tp_client.log')}


def tool(target, name, body):
    request = urllib.request.Request(target['devbench'] + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=8) as answer:
            return json.loads(answer.read().decode())
    except urllib.error.HTTPError as error:
        return {'error': 'HTTP %d %s' % (error.code, error.read().decode(errors='replace')[:200])}
    except Exception as error:
        return {'error': type(error).__name__}


def papyrus(target, script, function, args, self_form=None):
    body = {'action': 'call', 'script': script, 'function': function, 'args': args}
    if self_form:
        body['self'] = {'form': self_form}
    return tool(target, 'papyrus', body).get('returned')


def health(target):
    value = papyrus(target, 'Actor', 'GetActorValue', ['Health'], '0x14')
    return value if isinstance(value, (int, float)) else None


def log_size(target):
    return os.path.getsize(target['log'])


def log_since(target, offset):
    with open(target['log'], 'rb') as f:
        f.seek(offset)
        return f.read().decode('utf-8', errors='replace')


def connected(target):
    text = log_since(target, 0)
    return text.rfind('Connected: the server accepted this player') > text.rfind('Disconnected from server')


def position(target, form='0x14'):
    at = [papyrus(target, 'ObjectReference', 'GetPosition' + axis, [], form) for axis in 'XYZ']
    return at if all(isinstance(v, (int, float)) for v in at) else None


def copy_of(target, other):
    """The form id of this game's copy of the other game's player: the copy standing nearest to where that player is."""
    there = position(other)
    answer = tool(target, 'inspect', {'kind': 'refs', 'formType': 'Actor', 'limit': 300})
    best, best_distance = None, 150.0
    for ref in answer.get('refs', []) if there and isinstance(answer, dict) else []:
        form, at = str(ref.get('formId', '')), ref.get('position')
        if not form.upper().startswith('0XFF') or not at:
            continue
        distance = sum((a - b) ** 2 for a, b in zip(at, there)) ** 0.5
        if distance < best_distance:
            best, best_distance = form, distance
    return best


def stand_ahead(attacker, defender):
    """Stands the defender 200 units ahead of the attacker: a cast Fireball flies along its caster's heading.
    The defender is moved, not the attacker, since only a VR player's headset turns a VR player."""
    here = position(attacker)
    heading = papyrus(attacker, 'ObjectReference', 'GetAngleZ', [], '0x14')
    if not here or not isinstance(heading, (int, float)):
        return False
    ahead = [here[0] + 200.0 * math.sin(math.radians(heading)), here[1] + 200.0 * math.cos(math.radians(heading)), here[2]]
    papyrus(defender, 'ObjectReference', 'SetPosition', ahead, '0x14')
    time.sleep(4)
    return True


def one_way(attacker, defender, check, invalid):
    if not stand_ahead(attacker, defender):
        invalid.append('no position or heading for the %s player' % attacker['name'])
        return
    copy = copy_of(attacker, defender)
    if not copy:
        invalid.append('%s has no copy of the %s player near where that player is' % (attacker['name'], defender['name']))
        return
    before = health(defender)
    if before is None:
        invalid.append('the %s player\'s health is unread' % defender['name'])
        return
    offset = log_size(attacker)
    cast = papyrus(attacker, 'Spell', 'Cast', [{'form': '0x14'}, {'form': copy}], FIREBALL)
    lowest = before
    end = time.time() + WATCH
    while time.time() < end:
        now = health(defender)
        if now is not None:
            lowest = min(lowest, now)
        time.sleep(0.2)
    # Before the fix the attacker sends its spell's hit; after it, the rule says the two may not fight.
    said = [line.strip() for line in log_since(attacker, offset).splitlines() if 'PvP:' in line and ('spell' in line or 'no hit between' in line)]
    print('   %s casts at copy %s (Cast %s); the %s player had %.0f health, lowest %.0f' % (attacker['name'], copy, cast, defender['name'], before, lowest), flush=True)
    for line in said[:2]:
        print('   %s: %s' % (attacker['name'], line[:220]), flush=True)
    if not said:
        invalid.append('%s logged nothing of its Fireball reaching the copy' % attacker['name'])
        return
    check('the %s player is not hurt by the %s player' % (defender['name'], attacker['name']), before - lowest <= NOISE,
          'lost %.0f health' % (before - lowest))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='on a build without the fix: the run must be red')
    options = parser.parse_args()

    flat = game('flat', 8922, 'SKYRIM_FLAT_ROOT', 'Skyrim Together')
    vr = game('VR', 8921, 'SKYRIM_FUS_ROOT', 'Skyrim Together VR')
    for target in (flat, vr):
        if not os.path.isfile(target['log']) or not connected(target):
            print('INVALID: the %s game is not connected (%s)' % (target['name'], target['log']))
            return 2

    results, invalid = [], []

    # An open menu pauses a flat game, and a paused game neither casts nor shows a hit.
    for target in (flat, vr):
        for menu in (tool(target, 'menu', {'action': 'list'}) or {}).get('openMenus', []):
            if menu not in ('HUD Menu', 'Cursor Menu'):
                tool(target, 'menu', {'action': 'close', 'name': menu})
                print('   (%s: closed %s)' % (target['name'], menu), flush=True)
    time.sleep(2)

    def check(name, ok, detail=''):
        results.append(bool(ok))
        print(('ok    ' if ok else 'FAIL  ') + name + (' (%s)' % detail if detail else ''), flush=True)

    print('-- flat attacks VR', flush=True)
    one_way(flat, vr, check, invalid)
    time.sleep(3)
    print('-- VR attacks flat', flush=True)
    one_way(vr, flat, check, invalid)

    # Both players back to full health for whatever runs next.
    for target in (flat, vr):
        papyrus(target, 'Actor', 'RestoreActorValue', ['Health', 10000.0], '0x14')

    if invalid:
        for reason in invalid:
            print('INVALID: ' + reason)
        return 2
    passed = all(results)
    print('%s: %d of %d checks' % ('PASS' if passed else 'FAIL', sum(results), len(results)))
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
