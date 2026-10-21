"""How bright the game is drawn, with and without the mod: the same place, hour, weather and camera, measured.

Players reported the game darker with the mod. One run per setup, with the rig up in that setup:

    powershell -File Tools\\VR\\headless.ps1 up -Shortcut SKSE     then  python Tools\\VR\\light-compare.py vanilla
    powershell -File Tools\\VR\\headless.ps1 up -NoConnect         then  python Tools\\VR\\light-compare.py mod-offline
    powershell -File Tools\\VR\\headless.ps1 up                    then  python Tools\\VR\\light-compare.py mod --connected
    python Tools\\VR\\light-compare.py --report

Each run puts the game at noon in clear weather, hides the HUD, looks from a free camera at a fixed place above the
player's start (the rig puts the player in the same cell every time), lets the light settle, and takes the game's own
screenshots. Each picture is measured by its mean luminance (0-255) and its 10th and 90th percentiles; the numbers go to
logs/shots/light-<label>.json next to the pictures.

With --connected the game is connected when the run starts. The hour and the weather are set offline (F6), since a
connected game keeps the server's clock, and the game connects again before the pictures (label-connected). Then it goes
offline once more and takes the same pictures (label-offline), so the connection's own share shows apart from the mod's.
"""
import ctypes
import json
import os
import re
import shutil
import sys
import time
import urllib.request

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RELEASE = os.path.join(ROOT, 'build', 'windows', 'x64', 'release')
SHOTS = os.path.join(RELEASE, 'logs', 'shots')
FUS_ROOT = os.environ.get('SKYRIM_FUS_ROOT') or next(
    (path for path in ('C:/FUS', 'E:/FUS') if os.path.isfile(os.path.join(path, 'ModOrganizer.exe'))), 'E:/FUS')
CAPTURES = os.path.join(FUS_ROOT, 'overwrite', 'SKSE', 'Plugins', 'devbench', 'captures')
CLIENT_LOG = os.path.join(FUS_ROOT, 'tools', 'Skyrim Together VR', 'logs', 'tp_client.log')
DEVBENCH = 'http://127.0.0.1:8921/api/tool/'

CLEAR_WEATHER = '81A'      # SkyrimClear
PICTURES = 5
SETTLE_SECONDS = 10


def tool(name, body, timeout=10):
    req = urllib.request.Request(DEVBENCH + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def console(command):
    tool('console', {'action': 'exec', 'command': command})
    time.sleep(0.5)


def player_position():
    at = []
    for axis in 'XYZ':
        r = tool('papyrus', {'action': 'call', 'script': 'ObjectReference', 'function': 'GetPosition' + axis, 'self': {'form': '0x14'}})
        at.append(float(r.get('returned')))
    return at


def press_f6():
    # The client reads F6 with GetAsyncKeyState, which only a key pressed at the Windows level reaches.
    user32 = ctypes.windll.user32
    user32.keybd_event(0x75, 0, 0, 0)
    time.sleep(0.3)
    user32.keybd_event(0x75, 0, 2, 0)


def connected():
    """The last of the client's "Connected:" and "Disconnected from server" lines says which it is now."""
    with open(CLIENT_LOG, encoding='utf-8', errors='replace') as f:
        lines = f.readlines()[-3000:]
    state = None
    for line in lines:
        if 'Connected: the server accepted' in line:
            state = True
        elif 'Disconnected from server' in line:
            state = False
    return state


def wait_connection(wanted, seconds=45):
    end = time.time() + seconds
    while time.time() < end:
        if connected() is wanted:
            return True
        time.sleep(1)
    return False


def capture(name):
    """One of the game's own screenshots, moved from the mod manager's overwrite folder to logs/shots."""
    r = tool('capture', {'kind': 'native', 'checkpointId': name, 'cleanup': True}, timeout=40)
    if not r.get('ok'):
        return None
    os.makedirs(SHOTS, exist_ok=True)
    target = os.path.join(SHOTS, name + '.png')
    for base, _, files in os.walk(CAPTURES):
        if name + '.png' in files:
            shutil.copyfile(os.path.join(base, name + '.png'), target)
            os.remove(os.path.join(base, name + '.png'))
            return target
    return None


def measure(path):
    histogram = Image.open(path).convert('L').histogram()
    total = sum(histogram)

    def percentile(share):
        wanted, seen = share * total, 0
        for value, count in enumerate(histogram):
            seen += count
            if seen >= wanted:
                return value
        return 255

    mean = sum(value * count for value, count in enumerate(histogram)) / total
    return {'mean': round(mean, 2), 'p10': percentile(0.10), 'p90': percentile(0.90)}


def scene():
    """The hour and the weather the picture is taken in: a connected game keeps the server's clock, whatever was set."""
    hour = tool('papyrus', {'action': 'call', 'script': 'GlobalVariable', 'function': 'GetValue', 'self': {'form': '0x38'}}).get('returned')
    weather = tool('papyrus', {'action': 'call', 'script': 'Weather', 'function': 'GetCurrentWeather'}).get('returned')
    return {'hour': round(float(hour), 2) if isinstance(hour, (int, float)) else hour,
            'weather': weather.get('formId') if isinstance(weather, dict) else weather}


def pictures(label, eye):
    tool('camera', {'action': 'freecam', 'on': True})
    # North, a little down onto the land: the same view in every run.
    tool('camera', {'action': 'drive', 'x': eye[0], 'y': eye[1], 'z': eye[2], 'pitch': 0.15, 'yaw': 0.0})
    time.sleep(2)
    results = []
    try:
        for i in range(PICTURES):
            path = capture('light-%s-%d' % (label, i))
            if path:
                results.append(dict(measure(path), picture=os.path.basename(path), **scene()))
            time.sleep(1.5)
    finally:
        tool('camera', {'action': 'freecam', 'on': False})
    return results


def set_scene():
    console('set gamehour to 12')
    console('fw %s' % CLEAR_WEATHER)


def save(label, results, notes):
    os.makedirs(SHOTS, exist_ok=True)
    data = {'label': label, 'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'notes': notes, 'pictures': results}
    with open(os.path.join(SHOTS, 'light-%s.json' % label), 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    means = [r['mean'] for r in results]
    print('%s: %d pictures, mean luminance %s' % (label, len(results), ', '.join('%.1f' % m for m in means)), flush=True)


def run(label, with_connection):
    start = player_position()
    eye = (start[0], start[1], start[2] + 200.0)
    notes = []
    if with_connection:
        if not connected():
            sys.exit('--connected: the game is not connected; start the rig with a plain "up"')
        press_f6()
        if not wait_connection(False):
            sys.exit('F6 did not take the game offline')
    set_scene()
    console('tm')
    try:
        if with_connection:
            press_f6()
            if not wait_connection(True):
                sys.exit('F6 did not connect the game again')
            notes.append('connected again after the hour and weather were set')
        time.sleep(SETTLE_SECONDS)
        save(label + ('-connected' if with_connection else ''), pictures(label + ('-connected' if with_connection else ''), eye), notes)
        if with_connection:
            press_f6()
            wait_connection(False)
            # The clock and the weather go back to the game's own when it goes offline; set them again for the same scene.
            set_scene()
            time.sleep(SETTLE_SECONDS)
            save(label + '-offline', pictures(label + '-offline', eye), ['offline right after the connected pictures'])
    finally:
        console('tm')


def report():
    rows = []
    for name in sorted(os.listdir(SHOTS)) if os.path.isdir(SHOTS) else []:
        m = re.match(r'light-(.+)\.json$', name)
        if not m:
            continue
        with open(os.path.join(SHOTS, name), encoding='utf-8') as f:
            data = json.load(f)
        means = [p['mean'] for p in data['pictures']]
        if means:
            first = data['pictures'][0]
            rows.append((data['label'], data['time'], sum(means) / len(means), min(means), max(means),
                         sum(p['p10'] for p in data['pictures']) / len(means), sum(p['p90'] for p in data['pictures']) / len(means),
                         first.get('hour'), first.get('weather')))
    base = next((r[2] for r in rows if r[0] == 'vanilla'), None)
    print('%-22s %-19s %8s %8s %8s %6s %6s %9s %6s %s' % ('setup', 'taken', 'mean', 'lowest', 'highest', 'p10', 'p90', 'vs vanilla', 'hour', 'weather'))
    for label, taken, mean, low, high, p10, p90, hour, weather in rows:
        against = '%+.1f%%' % (100.0 * (mean - base) / base) if base else ''
        print('%-22s %-19s %8.1f %8.1f %8.1f %6.0f %6.0f %9s %6s %s' % (label, taken, mean, low, high, p10, p90, against, hour, weather))


def main():
    args = sys.argv[1:]
    if '--report' in args:
        report()
        return
    if not args:
        sys.exit(__doc__)
    run(args[0], '--connected' in args)


if __name__ == '__main__':
    main()
