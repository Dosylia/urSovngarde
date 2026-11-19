"""Holds the game's own input back while the in-game menu is open, without pausing the game.

    python Tools/VR/live-menu-hold.py           the VR game (headless.ps1 up), once its panel can be shown
    python Tools/VR/live-menu-hold.py --flat    the flat game (Tools/Flat/flat-rig.ps1 up), alone on the PC

The game must be in a loaded save, with room to walk forward. DevBench holds W through the game's own input devices.
The flat game reads them with its window in the background (the rig starts it as a test game), so nothing is brought to the front.
Checks: W walks the player with the menu closed; with the menu open it does not, and the log says the input is held;
once the menu is closed W walks the player again.
"""
import importlib.util
import math
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

WALKED = 50.0
STILL = 15.0


def walk(game):
    """How far W held for 1.5 s moves the player."""
    start = game.position()
    game.hold_key('w', 1500)
    time.sleep(2.2)
    end = game.position()
    return math.dist(start, end) if start and end else -1.0


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    pid = menu.game_pid()
    if not pid:
        print(f'INVALID: {menu.GAME} is not running')
        return 2
    game = play.Player(8922 if shared.FLAT else 8921)
    if call(pid, 'urSovngarde_IsMenuOpen') is None:
        check('the game has the menu calls', False)
        print(f'FAIL: {sum(results)} of {len(results)} checks')
        return 1
    call(pid, 'urSovngarde_SetMenuOpen', 0)
    # A game menu left open pauses the game; the rig has seen the map and the inventory open by themselves at start-up.
    for name in game.tool('menu', {'action': 'list'}).get('openMenus', []):
        if name not in ('HUD Menu', 'Cursor Menu'):
            print(f'note: closing the game menu {name}, open before the test')
            game.tool('menu', {'action': 'close', 'name': name})
    time.sleep(1)

    moved = walk(game)
    if moved < WALKED:
        print(f'INVALID: W does not walk the player with the menu closed ({moved:.0f} units); is the way blocked?')
        return 2
    check('W walks the player with the menu closed', True, f'{moved:.0f} units')

    offset = shared.log_size()
    call(pid, 'urSovngarde_SetMenuOpen', 1)
    if not shared.wait(lambda: call(pid, 'urSovngarde_IsMenuOpen'), 5):
        print('INVALID: the menu did not open')
        return 2
    moved = walk(game)
    check('W does not move the player with the menu open', 0 <= moved < STILL, f'{moved:.0f} units')
    check('the log says the input is held', shared.wait(lambda: 'GameInputHold: the game\'s input is held' in shared.log_since(offset), 5))

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    time.sleep(1)
    moved = walk(game)
    check('W walks the player again once the menu is closed', moved >= WALKED, f'{moved:.0f} units')

    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
