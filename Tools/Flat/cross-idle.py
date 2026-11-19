"""Each player plays an idle from a plugin the two games load at different indexes; the other game's copy must play it.

    python Tools/Flat/cross-idle.py
    python Tools/Flat/cross-idle.py --expect-fail     on a build without the fix: must be RED

Both rigs up and connected: Tools/Flat/flat-rig.ps1 up first, then Tools/VR/headless.ps1 up -AutoConnect.
Flat Skyrim 1.7.104 loads Dawnguard.esm at index 02; the VR install may load it elsewhere (04 in FUS).
An id sent as the sending game's own form id then names another form in the other game, here Dragonborn's landscape.

What the other player sees: each idle raises a hand, so the copy's hands rise above its pelvis in the other game.
The control is a wave from Skyrim.esm, which both games load first; if the copy does not wave, the run is INVALID.
The test is a welcome gesture from Dawnguard.esm; the copy must raise its hand for it as it does for the wave.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

CONTROL = ('Skyrim.esm', 0x03EA32, 'IdleWave')
TEST = ('Dawnguard.esm', 0x00D285, 'IdleDialogueWelcomeHandGesture')
HANDS = ('NPC R Hand [RHnd]', 'NPC L Hand [LHnd]')
PELVIS = 'NPC Pelvis [Pelv]'
# Units a hand must rise over where it hung, in the copy, for the idle to count as played there.
RISE = 15.0
WATCH = 4.0


def game(name, port, root_variable, client_folder):
    root = os.environ.get(root_variable)
    if not root:
        sys.exit('INVALID: set the %s user variable' % root_variable)
    return {'name': name, 'devbench': 'http://127.0.0.1:%d/api/tool/' % port,
            'log': os.path.join(root, 'tools', client_folder, 'logs', 'tp_client.log')}


def papyrus(target, script, function, args, self_form=None):
    body = {'action': 'call', 'script': script, 'function': function, 'args': args}
    if self_form:
        body['self'] = {'form': self_form}
    request = urllib.request.Request(target['devbench'] + 'papyrus', data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=8) as answer:
            return json.loads(answer.read().decode()).get('returned')
    except urllib.error.HTTPError as error:
        return 'ERR HTTP %d' % error.code
    except Exception as error:
        return 'ERR ' + type(error).__name__


def log_text(target):
    with open(target['log'], 'rb') as f:
        return f.read().decode('utf-8', errors='replace')


def connected(target):
    text = log_text(target)
    return text.rfind('Connected: the server accepted this player') > text.rfind('Disconnected from server')


def other_players_copy(target):
    """The form id of the newest player copy this game logged, as 0x... text, or None."""
    found = re.findall(r'[Pp]layer copy ([0-9A-F]{8})', log_text(target))
    return '0x' + found[-1] if found else None


def hand_rise(target, copy):
    """How far the copy's higher hand is above its pelvis, or None when the game does not say."""
    def z(node):
        value = papyrus(target, 'NetImmerse', 'GetNodeWorldPositionZ', [{'form': copy}, node, False])
        return value if isinstance(value, (int, float)) else None
    pelvis = z(PELVIS)
    hands = [z(hand) for hand in HANDS]
    if pelvis is None or None in hands:
        return None
    return max(hands) - pelvis


def played(sender, receiver, copy, idle):
    """The most the copy's hand rose over where it hung while the sender played the idle, or None when unread."""
    plugin, form_in_plugin, name = idle
    found = papyrus(sender, 'Game', 'GetFormFromFile', [form_in_plugin, plugin])
    idle_id = found.get('formId') if isinstance(found, dict) else None
    if not idle_id:
        print('   %s: %s %X not found (%s)' % (sender['name'], plugin, form_in_plugin, found), flush=True)
        return None
    before = [rise for rise in (hand_rise(receiver, copy) for _ in range(5)) if rise is not None]
    if not before:
        return None
    answer = papyrus(sender, 'Actor', 'PlayIdle', [{'form': idle_id}], '0x14')
    highest = max(before)
    end = time.time() + WATCH
    while time.time() < end:
        rise = hand_rise(receiver, copy)
        if rise is not None:
            highest = max(highest, rise)
    lift = highest - max(before)
    print('   %s plays %s (%s there, PlayIdle %s): the copy in %s raised a hand %.0f units' % (sender['name'], name, idle_id, answer, receiver['name'], lift), flush=True)
    time.sleep(4)
    return lift


def one_way(sender, receiver, check, invalid):
    copy = other_players_copy(receiver)
    if not copy:
        invalid.append('%s logged no player copy' % receiver['name'])
        return
    print('   (%s shows %s as %s)' % (receiver['name'], sender['name'], copy), flush=True)
    # Dialogue gestures play only with the weapon away.
    papyrus(sender, 'Actor', 'SheatheWeapon', [], '0x14')
    time.sleep(4)
    control = played(sender, receiver, copy, CONTROL)
    if control is None or control < RISE:
        invalid.append('the %s copy in %s did not wave for the control (%s)' % (sender['name'], receiver['name'], 'unread' if control is None else '%.0f units' % control))
        return
    test = played(sender, receiver, copy, TEST)
    check('%s shows the %s player\'s %s gesture' % (receiver['name'], sender['name'], TEST[0]), test is not None and test >= RISE,
          'unread' if test is None else 'the hand rose %.0f units, the wave %.0f' % (test, control))


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

    def check(name, ok, detail=''):
        results.append(bool(ok))
        print(('ok    ' if ok else 'FAIL  ') + name + (' (%s)' % detail if detail else ''), flush=True)

    print('-- flat plays them, VR shows them', flush=True)
    one_way(flat, vr, check, invalid)
    print('-- VR plays them, flat shows them', flush=True)
    one_way(vr, flat, check, invalid)

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
