"""Opens and closes the in-game menu panel with both thumbstick clicks held, as a player does.

    python Tools/VR/live-menu-chord.py

The VR rig must be up (headless.ps1 up) in a loaded save. DevBench presses the controllers' stick clicks (OpenVR's touchpad
button, as the game reads a thumbstick press) through the game's own calls.
Checks: a short press of both opens nothing; both held for over a second open the panel; held again they close it;
one stick held alone does nothing.
"""
import importlib.util
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from plugin_call import call  # noqa: E402
import play  # noqa: E402

spec = importlib.util.spec_from_file_location('menu_open', os.path.join(HERE, 'live-menu-open.py'))
menu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(menu)
shared = menu.shared

STICK_CLICK = 1 << 32


def press(game, left, right, seconds):
    """Holds both controllers still at the player's sides, their stick clicks pressed as given, in one sequence."""
    base = game.tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
    frames = []
    for i in range(int(seconds * 50)):
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
            f[d]['velocity'] = [0.0, 0.0, 0.0]
            f[d]['angularVelocity'] = [0.0, 0.0, 0.0]
            if d != 'hmd' and not f[d].get('controller'):
                f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
        for d, pressed in (('left', left), ('right', right)):
            f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
            f[d]['controller']['pressed'] = STICK_CLICK if pressed else 0
            f[d]['controller']['touched'] = STICK_CLICK if pressed else 0
        frames.append(f)
    game.tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-menu-chord'}, timeout=15)
    time.sleep(seconds + 0.3)


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    pid = menu.game_pid()
    if not pid:
        print('INVALID: urSovngarde.exe is not running')
        return 2
    game = play.Player(8921)
    call(pid, 'urSovngarde_SetMenuOpen', 0)
    time.sleep(0.5)
    is_open = lambda: call(pid, 'urSovngarde_IsMenuOpen')

    press(game, True, True, 0.4)
    press(game, False, False, 0.4)
    check('a short press of both stick clicks opens nothing', is_open() is False)

    offset = shared.log_size()
    press(game, True, True, 1.6)
    press(game, False, False, 0.4)
    check('both held over a second open the panel', is_open() is True)
    check('the log says so', shared.wait(lambda: 'VRPanel: opened by both stick clicks' in shared.log_since(offset), 5))

    press(game, True, True, 1.6)
    press(game, False, False, 0.4)
    check('held again they close it', is_open() is False)

    press(game, False, True, 1.6)
    press(game, False, False, 0.4)
    check('one stick held alone does nothing', is_open() is False)

    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
