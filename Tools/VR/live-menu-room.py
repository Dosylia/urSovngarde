"""The in-game menu panel stays where it opened while the head turns, and shows where the hand points, as a player sees it.

    python Tools/VR/live-menu-room.py

The VR rig must be up (headless.ps1 up) in a loaded save. DevBench holds the headset at 1.6 m looking ahead while the panel
opens, then turns it 40 degrees to the right, and moves the right controller.
Checks:
  - with the head turned, aimed at where the panel opened, the page gets the pointer near its centre;
  - with the head turned, aimed at where a panel fixed to the head would now be, the page gets no pointer;
  - a controller behind the panel, pointing back at its centre, gives the page no pointer;
  - opened while looking straight up, the panel is ahead, not behind: aimed at where it opens when looking ahead, the pointer is near the page centre;
  - the beam and a hand ring were drawn into the left eye's image this game (the drawer reads the pixel back once).
"""
import importlib.util
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from plugin_call import call  # noqa: E402
import play  # noqa: E402

spec = importlib.util.spec_from_file_location('menu_laser', os.path.join(HERE, 'live-menu-laser.py'))
laser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(laser)
shared = laser.shared

TURN = 40
DRAWN = r'VREyeDraw: {what} drawn into the left eye at \d+,\d+; pixel before (?P<before>\d+,\d+,\d+), after (?P<after>\d+,\d+,\d+), {colour} (?P<expected>\d+,\d+,\d+)'


def head_locked_centre():
    """Where the panel's centre would be now if it followed the head: turned with it around the headset."""
    yaw = math.radians(TURN)
    ahead, below = laser.PANEL_AHEAD, laser.PANEL_BELOW_EYES
    return (laser.HEADSET[0] + math.sin(yaw) * ahead, laser.HEADSET[1] - below, laser.HEADSET[2] - math.cos(yaw) * ahead)


def drawn(log, what, colour):
    """The drawer's last check of that mark in this game's log: its pixel after, and the colour it should be."""
    found = None
    for m in re.finditer(DRAWN.format(what=what, colour=colour), log):
        found = m
    if not found:
        return None
    return [int(v) for v in found['after'].split(',')], [int(v) for v in found['expected'].split(',')]


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    pid = laser.menu.game_pid()
    if not pid:
        print('INVALID: urSovngarde.exe is not running')
        return 2
    game = play.Player(8921)
    page = shared.DevTools()
    call(pid, 'urSovngarde_SetMenuOpen', 0)
    shared.wait(lambda: not call(pid, 'urSovngarde_IsMenuOpen'), 5)
    if not laser.open_panel(game, pid):
        print('INVALID: the menu did not open on the panel')
        return 2
    page.evaluate(laser.LISTEN)
    turned = laser.yaw_rotation(TURN)

    page.evaluate('window.__panelPointer.move = null; true')
    where_it_opened = laser.aim_rotation(laser.panel_point(laser.PAGE[0] / 2, laser.PAGE[1] / 2))
    seen = laser.perform(game, [(where_it_opened, 0, (0.0, 0.0), 1.5)], read=lambda: page.evaluate('window.__panelPointer.move'), read_at=1.2, head=turned)
    expected = (laser.PAGE[0] / 2 / laser.ZOOM, laser.PAGE[1] / 2 / laser.ZOOM)
    near = bool(seen) and abs(seen[0] - expected[0]) <= 40 and abs(seen[1] - expected[1]) <= 40
    check(f'head turned {TURN} degrees, aimed where the panel opened: the pointer is near the page centre', near,
          f'page saw {seen}, centre {expected[0]:.0f},{expected[1]:.0f}')

    page.evaluate('window.__panelPointer.move = null; true')
    where_head_is = laser.aim_rotation(head_locked_centre())
    seen = laser.perform(game, [(where_head_is, 0, (0.0, 0.0), 1.5)], read=lambda: page.evaluate('window.__panelPointer.move'), read_at=1.2, head=turned)
    check(f'head turned {TURN} degrees, aimed where a head-fixed panel would be: no pointer', seen is None, f'page saw {seen}')

    page.evaluate('window.__panelPointer.move = null; true')
    behind = (0.0, laser.HEADSET[1] - laser.PANEL_BELOW_EYES, laser.HEADSET[2] - laser.PANEL_AHEAD - 0.4)
    from_behind = laser.aim_rotation(laser.panel_point(laser.PAGE[0] / 2, laser.PAGE[1] / 2), at=behind)
    seen = laser.perform(game, [(from_behind, 0, (0.0, 0.0), 1.5)], read=lambda: page.evaluate('window.__panelPointer.move'), read_at=1.2, at=behind)
    check('a controller behind the panel, pointing back at it: no pointer', seen is None, f'page saw {seen}')

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    shared.wait(lambda: not call(pid, 'urSovngarde_IsMenuOpen'), 5)
    if laser.open_panel(game, pid, head=laser.pitch_rotation(90)):
        page.evaluate('window.__panelPointer.move = null; true')
        seen = laser.perform(game, [(where_it_opened, 0, (0.0, 0.0), 1.5)], read=lambda: page.evaluate('window.__panelPointer.move'), read_at=1.2)
        near = bool(seen) and abs(seen[0] - expected[0]) <= 40 and abs(seen[1] - expected[1]) <= 40
        check('opened looking straight up, the panel is ahead: the pointer is near the page centre', near, f'page saw {seen}')
    else:
        check('opened looking straight up, the panel is ahead: the pointer is near the page centre', False, 'the menu did not open')

    log = laser.menu.this_game_log()
    for what, colour in (('beam', 'beam'), ('hand ring', 'ring')):
        found = drawn(log, what, colour)
        close = bool(found) and all(abs(a - e) <= 12 for a, e in zip(found[0], found[1]))
        check(f'the {what} was drawn into the eye image in its colour', close, f'after, expected: {found}' if found else 'no check line this game')

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
