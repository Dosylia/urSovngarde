"""The extra test, not part of the suite: two real games on one PC, on one server, watched side by side.

    powershell -File Tools/VR/headless.ps1 up -AutoConnect -Twin     with %LOCALAPPDATA%/SkyrimTogetherVR/two-games.on present
    python Tools/VR/two-games.py --minutes 10

It asks the anchor for the second game, takes it into the world, then watches both: are they alive, do frames still come,
does either show an error box, and what do they disagree on. Results: build/desync/two-games/.
Every minute one player walks for 5 seconds, and it measures how far the other game's copy of that player is behind.
The second game joins SteamVR as an overlay, because SteamVR kills the scene application before a new one; nothing paces it, so it runs at about 120 fps.
"""
import argparse
import ctypes
import ctypes.wintypes as w
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import play  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'build', 'desync', 'two-games')
ASK_FILE = os.path.join(tempfile.gettempdir(), 'two-games-second')
LOGS = r'C:\Fus\tools\urSovngarde\logs'


def log(*args):
    print(time.strftime('%H:%M:%S'), *args, flush=True)


def game_pids():
    t = subprocess.run('tasklist /FI "IMAGENAME eq urSovngarde.exe" /FO CSV /NH', shell=True, capture_output=True, text=True).stdout
    return {int(line.split('","')[1]) for line in t.splitlines() if 'urSovngarde' in line}


def error_boxes(pids):
    """The text of every dialog box the games show."""
    u = ctypes.WinDLL('user32')
    ENUM = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    u.GetClassNameW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    u.GetWindowTextW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    u.EnumWindows.argtypes = (ENUM, w.LPARAM)
    u.EnumChildWindows.argtypes = (w.HWND, ENUM, w.LPARAM)
    u.GetWindowThreadProcessId.argtypes = (w.HWND, ctypes.POINTER(w.DWORD))
    u.IsWindowVisible.argtypes = (w.HWND,)
    found = []

    def text(h):
        b = ctypes.create_unicode_buffer(1500)
        u.GetWindowTextW(h, b, 1500)
        return b.value

    def each(h, _):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(pid))
        b = ctypes.create_unicode_buffer(64)
        u.GetClassNameW(h, b, 64)
        if pid.value in pids and b.value == '#32770' and u.IsWindowVisible(h):
            parts = []

            def child(c, _):
                t = text(c)
                if t:
                    parts.append(t)
                return True
            u.EnumChildWindows(h, ENUM(child), 0)
            found.append('%d %s: %s' % (pid.value, text(h), ' | '.join(parts)))
        return True
    u.EnumWindows(ENUM(each), 0)
    return found


def wait(test, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if test():
                log(what)
                return True
        except Exception:
            pass
        time.sleep(2)
    log('TIMED OUT:', what)
    return False


def client_log(index):
    return os.path.join(LOGS, 'tp_client.log' if index == 1 else 'tp_client_instance_%d.log' % index)


def connected(index):
    try:
        with open(client_log(index), encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return False
    session = text[text.rfind('Skyrim Together client, build'):]
    connects = session.rfind('Connected: the server accepted')
    return connects >= 0 and connects > session.rfind('Disconnected from server')


def bring_up_second(first_pid):
    for f in (client_log(2),):
        if os.path.exists(f) and first_pid:
            try:
                os.remove(f)
            except OSError:
                pass
    open(ASK_FILE, 'w').close()
    second = play.Player(8922)
    if not wait(lambda: second.alive(), 180, 'the second game answers on 8922'):
        return None
    if not wait(lambda: 'Main Menu' in second.tool('menu', {'action': 'list'}).get('openMenus', []), 480, 'the second game is at the main menu'):
        return None
    log('load:', str(second.tool('game', {'action': 'loadLast'}, 60))[:120])
    if not wait(lambda: (second.alive() or {}).get('lastLifecycle') == 'postLoadGame', 240, 'it loaded the save'):
        return None
    time.sleep(3)
    second.console('cow Tamriel 34 -9')
    time.sleep(3)
    wait(lambda: 'Loading Menu' not in second.tool('menu', {'action': 'list'}).get('openMenus', []), 180, 'it arrived')
    wait(lambda: connected(2), 90, 'it is connected')
    return second


def copy_gap(watcher, player):
    """How far the watcher's copy of the other player is from where that player is now."""
    refs = watcher.actors()
    me = next(r for r in refs if r['formId'] == play.PLAYER_FORM)
    copy = next(r for r in refs if r['formId'] != play.PLAYER_FORM and r.get('name') == me.get('name'))
    return math.dist(copy['position'], player.position())


def middle_and_most(values):
    return 'none' if not values else '%d units typically, %d at most, %d samples' % (sorted(values)[len(values) // 2], max(values), len(values))


def view(game):
    refs = game.actors()
    me = next((r for r in refs if r['formId'] == play.PLAYER_FORM), None)
    return me, {r['formId']: r for r in refs if r['formId'] != play.PLAYER_FORM}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--minutes', type=float, default=10)
    args = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)
    first = play.Player(8921)
    if not first.alive():
        raise SystemExit('The first game does not answer.')
    start_pids = game_pids()
    log('first game', start_pids, 'frame', first.alive().get('frame'), 'connected', connected(1))
    second = bring_up_second(start_pids)
    log('both up' if second else 'NO second game', 'games', game_pids(), 'boxes', error_boxes(game_pids()))
    bodies = []
    if second:
        for label, game in (('first', first), ('second', second)):
            bodies.append(play.Body(game))
            bodies[-1].start()
    end = time.time() + args.minutes * 60
    last = {}
    report = []
    stalls = {'first': 0, 'second': 0}
    still_gaps, walking_gaps = [], []
    samples = 0
    while time.time() < end:
        pids = game_pids()
        line = {'t': time.strftime('%H:%M:%S'), 'games': len(pids)}
        for label, game in (('first', first), ('second', second)):
            health = game.alive() if game else None
            frame = (health or {}).get('frame')
            line[label] = frame
            if frame is not None and frame == last.get(label):
                stalls[label] += 1
            last[label] = frame
        boxes = error_boxes(pids)
        if boxes:
            line['boxes'] = boxes
            log('ERROR BOX', boxes)
        report.append(line)
        if not first.alive() or (second and not second.alive()):
            log('A GAME IS GONE:', line)
            break
        samples += 1
        if not second:
            time.sleep(10)
            continue
        walk = samples // 6 if samples % 6 == 0 else None
        try:
            if walk is None:
                still_gaps.append(copy_gap(first, second))
                time.sleep(10)
                continue
            walker, watcher, body = (second, first, bodies[1]) if walk % 2 else (first, second, bodies[0])
            # Each player turns round before each walk, so it comes back to where it was.
            body.yaw = body.hand_yaw = math.pi if walk % 4 < 2 else 0.0
            time.sleep(1.5)
            walker.hold_key('w', 5000)
            time.sleep(2.5)
            walking_gaps.append(copy_gap(watcher, walker))
            time.sleep(6)
        except Exception as e:
            log('no gap measured:', type(e).__name__, e)
            time.sleep(5)
    log('frames: first %s, second %s; samples with no new frame: %s; samples: %d' % (last.get('first'), last.get('second'), stalls, len(report)))
    log('copy behind the player: standing %s; walking %s' % (middle_and_most(still_gaps), middle_and_most(walking_gaps)))
    for index in (1, 2):
        try:
            with open(client_log(index), encoding='utf-8', errors='replace') as f:
                text = f.read()
            session = text[text.rfind('Skyrim Together client, build'):]
            perf = re.findall(r'Perf last \d+ s: \d+ frames, avg [\d.]+ ms \((\d+) fps\)', session)
            pings = [int(p) for p in re.findall(r'Connection: ping (\d+) ms', session)]
            quality = [float(q) for q in re.findall(r'Connection: ping \d+ ms, quality ([\d.]+) here', session)]
            if pings:
                log('game %d: ping %d to %d ms, typically %d; lowest quality %.2f; %d readings' % (index, min(pings), max(pings), sorted(pings)[len(pings) // 2], min(quality or [0]), len(pings)))
            log('game %d: fps %s; crash blocks %d; connects %d, disconnects %d; players it spawned %d' % (index, perf[-6:], session.count('crash occurred'), session.count('Connected: the server accepted'),
                                                                                                         session.count('Disconnected from server'), session.count('CharacterSpawnRequest')))
        except OSError as e:
            log('game %d: no log: %s' % (index, e))
    if second and first.alive() and second.alive():
        me1, a1 = view(first)
        me2, a2 = view(second)
        common = set(a1) & set(a2)
        differ = [f for f in common if a1[f]['actor'].get('alive') != a2[f]['actor'].get('alive')]
        apart = sorted((math.dist(a1[f]['position'], a2[f]['position']), f) for f in common)[-5:]
        log('actors: first %d, second %d, same form in both %d, life differs on %d: %s; furthest apart: %s' % (len(a1), len(a2), len(common), len(differ), differ[:6], [(round(g), f) for g, f in apart]))
        log('copies of a player seen: first %s, second %s' % ([f for f, r in a1.items() if r.get('name') == me1.get('name')], [f for f, r in a2.items() if r.get('name') == me2.get('name')]))
        for label, game in (('first', first), ('second', second)):
            try:
                log(label, 'picture', game.shot('two-games-' + label, os.path.join(OUT, label + '.png')))
            except Exception as e:
                log(label, 'no picture', type(e).__name__)
    for body in bodies:
        body.finish()
    with open(os.path.join(OUT, 'watch.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f)
    log('finished')


if __name__ == '__main__':
    sys.exit(main())
