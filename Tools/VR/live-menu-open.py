"""Opens and closes the in-game menu through the mod's exported call, as an opening control or another plugin would.

    python Tools/VR/live-menu-open.py           the VR game (headless.ps1 up)
    python Tools/VR/live-menu-open.py --flat    the flat game (Tools/Flat/flat-rig.ps1 up), alone on the PC

The game must be in a loaded save; a connection is not needed.
Checks: the game has the call; the menu starts closed; opening it activates the page on this game's own surface
(the panel in VR, the screen on flat), and the log names that surface; closing it deactivates the page.
In VR also: the page renders while the panel shows it, and the panel lands in the game's left eye image with the page's colours.
"""
import importlib.util
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from plugin_call import call  # noqa: E402

spec = importlib.util.spec_from_file_location('launcher_join', os.path.join(HERE, 'live-launcher-join.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)

GAME = 'urSovngardeSE.exe' if shared.FLAT else 'urSovngarde.exe'
SURFACE = 'screen' if shared.FLAT else 'panel'
# The drawer's check once per showing: the eye image's pixel under the panel centre, before and after, and the page's centre.
DRAWN = re.compile(r'VREyeDraw: panel drawn into the left eye at \d+,\d+; pixel before (?P<before>\d+,\d+,\d+), after (?P<after>\d+,\d+,\d+), picture centre (?P<picture>\d+,\d+,\d+)')
# The surface the open page shows on, empty while it is closed.
OPEN_SURFACE = "document.querySelector('.app-root-controls')?.getAttribute('data-surface') ?? ''"


def this_game_log():
    """The client log since this game started."""
    text = shared.log_since(0)
    start = text.rfind('Skyrim Together client, build')
    return text[start:] if start >= 0 else text


def game_pid():
    listing = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {GAME}', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    for line in listing.splitlines():
        fields = [field.strip('"') for field in line.split('","')]
        if len(fields) > 1 and fields[0].lower() == GAME.lower():
            return int(fields[1])
    return None


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    pid = game_pid()
    if not pid:
        print(f'INVALID: {GAME} is not running')
        return 2
    try:
        page = shared.DevTools()
    except (OSError, StopIteration, ConnectionError) as error:
        print(f'INVALID: no page on the debugging port ({error})')
        return 2

    was_open = call(pid, 'urSovngarde_IsMenuOpen')
    if not check('the game has the menu calls', was_open is not None):
        print(f'FAIL: {sum(results)} of {len(results)} checks')
        return 1
    if was_open:
        call(pid, 'urSovngarde_SetMenuOpen', 0)
    check('the menu starts closed', shared.wait(lambda: call(pid, 'urSovngarde_IsMenuOpen') is False, 5))

    offset = shared.log_size()
    call(pid, 'urSovngarde_SetMenuOpen', 1)
    check('the menu reports open', shared.wait(lambda: call(pid, 'urSovngarde_IsMenuOpen'), 5))
    shown = shared.wait(lambda: page.evaluate(OPEN_SURFACE) == SURFACE, 5)
    check(f'the page is active on the {SURFACE}', shown, page.evaluate(OPEN_SURFACE) or 'not active')
    # The log reaches the file a moment later.
    check('the log names the surface', shared.wait(lambda: f'MenuService: opened on the {SURFACE}' in shared.log_since(offset), 5))

    if not shared.FLAT:
        # In VR the page renders only while a presenter shows it, and the panel draws it into the game's eye images.
        visible = shared.wait(lambda: page.evaluate('document.visibilityState') == 'visible', 5)
        check('the page renders while the panel shows it', visible, page.evaluate('document.visibilityState'))
        # The drawer checks once per game, at the panel's first showing, which may have been another test's.
        drawn = shared.wait(lambda: DRAWN.search(this_game_log()), 10)
        if check('the panel is drawn into the left eye image', bool(drawn)):
            before, after, picture = [tuple(int(v) for v in drawn.group(name).split(',')) for name in ('before', 'after', 'picture')]
            close = all(abs(a - p) <= 12 for a, p in zip(after, picture))
            check('the eye image shows the page at the panel centre', close, f'before {before}, after {after}, page {picture}')

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    check('the menu reports closed', shared.wait(lambda: call(pid, 'urSovngarde_IsMenuOpen') is False, 5))
    check('the page is no longer active', shared.wait(lambda: page.evaluate(OPEN_SURFACE) == '', 5))
    time.sleep(0.5)

    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
