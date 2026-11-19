"""Points at the in-game menu panel with a controller, as a player does: the dot follows, the trigger clicks, the stick scrolls.

    python Tools/VR/live-menu-laser.py

The VR rig must be up (headless.ps1 up) in a loaded save. DevBench holds the headset still at 1.6 m looking ahead, and
moves the right controller; the game's own controller nodes and devices follow it, as with a real controller.
The panel opens in front of the headset and stays there, so it is opened while DevBench holds the headset (open_panel).
Checks: aimed at the panel's centre, the page gets the pointer near its centre; a trigger pull is a click there; the stick
scrolls; aimed at the menu's "Join a friend" button, a pull opens the join panel.
"""
import importlib.util
import json
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

# The panel as it opens in front of the headset (VRPanel): metres, x right, y up, -z ahead.
PANEL_WIDTH = 1.4
PANEL_HEIGHT = PANEL_WIDTH * 900 / 1600
PANEL_AHEAD = 1.25
PANEL_BELOW_EYES = 0.15
PAGE = (1600, 900)
# The page's zoom on VR (VRPage): CSS pixels are page pixels over 1.2 ** 3.
ZOOM = 1.2 ** 3
HEADSET = (0.0, 1.6, 0.0)
CONTROLLER = (0.2, 1.4, -0.3)
TRIGGER = 1 << 33

# The page lives on between runs: its listeners go on once, the counts start again at every run.
LISTEN = """(() => {
  window.__panelPointer = {move: null, moves: 0, down: 0, up: 0, wheel: 0};
  if (!window.__panelPointerListening2) {
    window.__panelPointerListening2 = true;
    document.addEventListener('mousemove', e => { window.__panelPointer.move = [e.clientX, e.clientY]; window.__panelPointer.moves++; }, true);
    document.addEventListener('mousedown', () => window.__panelPointer.down++, true);
    document.addEventListener('mouseup', () => window.__panelPointer.up++, true);
    document.addEventListener('wheel', () => window.__panelPointer.wheel++, true);
  }
  return true;
})()"""


def aim_rotation(target, at=None):
    """The controller's rotation (rows) whose forward, -z, points from the controller (at CONTROLLER, or at) to a point in room space."""
    hand = at or CONTROLLER
    d = [target[i] - hand[i] for i in range(3)]
    length = math.sqrt(sum(v * v for v in d))
    d = [v / length for v in d]
    pitch = math.asin(max(-1.0, min(1.0, d[1])))
    yaw = math.atan2(-d[0], -d[2])
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    # Ry(yaw) * Rx(pitch)
    return [[cy, sy * sp, sy * cp], [0.0, cp, -sp], [-sy, cy * sp, cy * cp]]


def panel_point(page_x, page_y):
    """A page pixel's place in room space, for a panel opened with the headset at HEADSET looking ahead."""
    u, v = page_x / PAGE[0], page_y / PAGE[1]
    head = (-PANEL_WIDTH / 2 + u * PANEL_WIDTH, -PANEL_BELOW_EYES + PANEL_HEIGHT / 2 - v * PANEL_HEIGHT, -PANEL_AHEAD)
    return tuple(HEADSET[i] + head[i] for i in range(3))


def yaw_rotation(degrees):
    """The headset turned to the right by that many degrees, level (rows)."""
    yaw = math.radians(-degrees)
    cy, sy = math.cos(yaw), math.sin(yaw)
    return [[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]]


def perform(game, segments, read=None, read_at=None, head=None, at=None):
    """Plays one continuous sequence: the headset still at HEADSET (looking ahead, or turned as head says), the right
    controller held as each segment says.
    A segment is (rotation, pressed buttons, stick, seconds). DevBench puts the controller back at rest when a sequence ends,
    so what the page saw is read while the sequence still plays: read() at read_at seconds, its answer returned."""
    h = head or [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    hand = at or CONTROLLER
    base = game.tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
    frames = []
    for rotation, pressed, stick, seconds in segments:
        r = rotation
        for _ in range(int(seconds * 50)):
            f = json.loads(json.dumps(base))
            f.pop('seq', None)
            i = len(frames)
            f['tMs'] = i * 20
            f['hmd']['matrix'] = [h[0][0], h[0][1], h[0][2], HEADSET[0], h[1][0], h[1][1], h[1][2], HEADSET[1], h[2][0], h[2][1], h[2][2], HEADSET[2]]
            f['right']['matrix'] = [r[0][0], r[0][1], r[0][2], hand[0], r[1][0], r[1][1], r[1][2], hand[1], r[2][0], r[2][1], r[2][2], hand[2]]
            f['left']['matrix'] = [1, 0, 0, -0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
            for d in ('hmd', 'left', 'right'):
                f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
                if f[d].get('index') is None:
                    f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
                f[d]['velocity'] = [0.0, 0.0, 0.0]
                f[d]['angularVelocity'] = [0.0, 0.0, 0.0]
                if d != 'hmd' and not f[d].get('controller'):
                    f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
            for d in ('left', 'right'):
                f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
            f['right']['controller']['pressed'] = pressed
            f['right']['controller']['touched'] = pressed
            f['right']['controller']['axes'] = [list(stick)] + [[0.0, 0.0]] * 4
            frames.append(f)
    game.tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'live-menu-laser'}, timeout=15)
    total = len(frames) / 50.0
    answer = None
    if read and read_at is not None:
        time.sleep(read_at)
        answer = read()
        time.sleep(max(0.0, total - read_at) + 0.3)
    else:
        time.sleep(total + 0.3)
    return answer


def pitch_rotation(degrees):
    """The headset tilted up by that many degrees (rows)."""
    pitch = math.radians(degrees)
    cp, sp = math.cos(pitch), math.sin(pitch)
    return [[1.0, 0.0, 0.0], [0.0, cp, -sp], [0.0, sp, cp]]


def open_panel(game, pid, head=None):
    """Opens the menu on the panel while DevBench holds the headset at HEADSET looking ahead, so the panel is where panel_point
    puts it; it stays there after the sequence, when the headset goes back to rest.
    The controller points at the floor meanwhile, so the first aim of a check is a fresh move onto the panel."""
    down = aim_rotation((CONTROLLER[0], 0.0, CONTROLLER[2]))
    perform(game, [(down, 0, (0.0, 0.0), 1.5)], read=lambda: call(pid, 'urSovngarde_SetMenuOpen', 1), read_at=0.5, head=head)
    return shared.wait(lambda: call(pid, 'urSovngarde_IsMenuOpen'), 5)


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
    page = shared.DevTools()
    if not open_panel(game, pid):
        print('INVALID: the menu did not open on the panel')
        return 2
    page.evaluate(LISTEN)

    centre = aim_rotation(panel_point(PAGE[0] / 2, PAGE[1] / 2))
    seen = perform(game, [(centre, 0, (0.0, 0.0), 1.5)], read=lambda: page.evaluate('window.__panelPointer.move'), read_at=1.2)
    expected = (PAGE[0] / 2 / ZOOM, PAGE[1] / 2 / ZOOM)
    near = bool(seen) and abs(seen[0] - expected[0]) <= 40 and abs(seen[1] - expected[1]) <= 40
    check('aimed at the panel centre, the page gets the pointer near its centre', near, f'page saw {seen}, centre {expected[0]:.0f},{expected[1]:.0f}')

    # Off the panel and back to the very same spot: the page lost track of the pointer, so it is told again.
    page.evaluate('window.__panelPointer.moves = 0; true')
    down = aim_rotation((CONTROLLER[0], 0.0, CONTROLLER[2]))
    moves = perform(game, [(centre, 0, (0.0, 0.0), 0.8), (down, 0, (0.0, 0.0), 0.5), (centre, 0, (0.0, 0.0), 0.8)],
                    read=lambda: page.evaluate('window.__panelPointer.moves'), read_at=2.0)
    check('off the panel and back to the same spot, the page is told again', (moves or 0) >= 2, f'{moves} moves')

    page.evaluate('window.__panelPointer.down = 0; window.__panelPointer.up = 0; true')
    perform(game, [(centre, 0, (0.0, 0.0), 0.6), (centre, TRIGGER, (0.0, 0.0), 0.4), (centre, 0, (0.0, 0.0), 0.5)])
    clicks = page.evaluate('[window.__panelPointer.down, window.__panelPointer.up]')
    check('a trigger pull is one click', clicks == [1, 1], f'down, up {clicks}')

    perform(game, [(centre, 0, (0.0, 0.0), 0.4), (centre, 0, (0.0, 1.0), 0.8), (centre, 0, (0.0, 0.0), 0.3)])
    wheel = page.evaluate('window.__panelPointer.wheel')
    check('the stick scrolls', (wheel or 0) >= 3, f'{wheel} wheel steps')

    find_button = "Array.from(document.querySelectorAll('.app-root-menu button')).find(b => b.textContent.includes('Join a friend'))"
    rect = page.evaluate(f'(() => {{ const r = {find_button}?.getBoundingClientRect(); return r ? [r.left + r.width / 2, r.top + r.height / 2] : null; }})()')
    if check('the menu has "Join a friend"', bool(rect)):
        button = aim_rotation(panel_point(rect[0] * ZOOM, rect[1] * ZOOM))
        perform(game, [(button, 0, (0.0, 0.0), 0.8), (button, TRIGGER, (0.0, 0.0), 0.3), (button, 0, (0.0, 0.0), 0.6)])
        opened = shared.wait(lambda: page.evaluate("!!OPEN_JOIN_JS"), 5)
        check('aimed at "Join a friend", a pull opens the join panel', opened)
        page.evaluate("Array.from((OPEN_JOIN_JS?.querySelectorAll('button') ?? [])).find(b => b.textContent.includes('Close'))?.click()")

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
