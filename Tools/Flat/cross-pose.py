"""A VR player's arms on a flat screen: the VR player raises both hands, and the flat game's copy raises its arms.

    python Tools/Flat/cross-pose.py
    python Tools/Flat/cross-pose.py --expect-fail     on a build without the fix: must be RED
    python Tools/Flat/cross-pose.py --frames 60       also looks for frames that drop the pose (flicker)
    python Tools/Flat/cross-pose.py --fingers         the VR game sends its fingers and the flat game writes them on the copy, with close pictures of its hand

Both rigs up and connected: Tools/Flat/flat-rig.ps1 up first, then Tools/VR/headless.ps1 up -AutoConnect.
The VR rig's headset is held at standing height and its hands at the sides, then above the head (DevBench input).
The flat game takes pictures from one free camera looking at the copy: twice with the hands down, a second apart, then with the hands up.
The arms must change the picture above the copy's head far more than the two hands-down pictures differ.
With --frames, more pictures follow with the hands still up; the flat game's own count of frame ends it left unposed must be 0, and its posed-copy lines must show it posed the copy in that time.
Each picture is one frame the flat game drew, taken at no fixed moment, so a pose dropped every other frame shows in about half of them.
Those that differ above the head are listed for the eye: the rig's VR body flips an elbow or the head now and then, and the copy shows it.
No picture of the copy unposed can be made beside them: the game's view check follows DevBench's free camera, so turning the flat player away does not unpose it.
Only DevBench over HTTP: no window is raised and no key is pressed at the Windows level.
"""
import argparse
import json
import math
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.request

from PIL import Image, ImageChops

FLAT_PORT, VR_PORT = 8922, 8921
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SHOTS = os.path.join(ROOT, 'build', 'windows', 'x64', 'release', 'logs', 'shots')
# Above the copy's head in the picture, where raised arms show and hanging ones do not.
ABOVE_HEAD = (0.38, 0.0, 0.62, 0.22)
# The whole copy in the picture, head to feet.
# How much more the arms must change that area than the noise does, and at least how much.
FACTOR = 3.0
FLOOR = 10.0


def tool(port, name, body, timeout=10):
    request = urllib.request.Request('http://127.0.0.1:%d/api/tool/%s' % (port, name), data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as answer:
            return json.loads(answer.read().decode())
    except urllib.error.HTTPError as error:
        return {'error': 'HTTP %d' % error.code}
    except Exception as error:
        return {'error': type(error).__name__}


def papyrus(port, script, function, args, self_form='0x14'):
    body = {'action': 'call', 'script': script, 'function': function, 'args': args}
    if self_form:
        body['self'] = {'form': self_form}
    return tool(port, 'papyrus', body).get('returned')


def position(port):
    at = [papyrus(port, 'ObjectReference', 'GetPosition' + axis, []) for axis in 'XYZ']
    return at if all(isinstance(v, (int, float)) for v in at) else None


def log_connected(root_variable, folder):
    root = os.environ.get(root_variable)
    path = os.path.join(root, 'tools', folder, 'logs', 'tp_client.log') if root else ''
    if not path or not os.path.isfile(path):
        return False
    with open(path, 'rb') as f:
        text = f.read().decode('utf-8', errors='replace')
    return text.rfind('Connected: the server accepted this player') > text.rfind('Disconnected from server')


class Hands:
    """Holds the VR rig's headset at 1.6 m and both hands at a height, in 5 s sequences, until stopped.

    reach is how far ahead of the headset the hands are held."""

    def __init__(self):
        self.height = 1.0
        self.current = 1.0
        self.reach = 0.2
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop.is_set():
            base = tool(VR_PORT, 'input', {'action': 'observe', 'device': 'vrTrackedSet'}).get('frame')
            if not base:
                self.stop.wait(1)
                continue
            frames = []
            for i in range(250):
                # At 1 m/s: a hand that jumps reads as a swing, and the VR game sends an attack.
                self.current += max(-0.02, min(0.02, self.height - self.current))
                f = json.loads(json.dumps(base))
                f.pop('seq', None)
                f['tMs'] = i * 20
                f['hmd']['matrix'] = [1, 0, 0, 0, 0, 1, 0, 1.6, 0, 0, 1, 0]
                f['left']['matrix'] = [1, 0, 0, -0.25, 0, 1, 0, self.current, 0, 0, 1, -self.reach]
                f['right']['matrix'] = [1, 0, 0, 0.25, 0, 1, 0, self.current, 0, 0, 1, -self.reach]
                for device in ('hmd', 'left', 'right'):
                    f[device].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
                    if f[device].get('index') is None:
                        f[device]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[device]
                    for key in ('velocity', 'angularVelocity'):
                        f[device].setdefault(key, [0.0, 0.0, 0.0])
                frames.append(f)
            tool(VR_PORT, 'input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'cross-pose'}, timeout=15)
            self.stop.wait(1.0)


def picture(name):
    answer = tool(FLAT_PORT, 'capture', {'kind': 'native', 'checkpointId': name, 'cleanup': True, 'outDir': SHOTS}, timeout=40)
    path = answer.get('path')
    return path if path and os.path.isfile(path) else None


def changed_share(path_a, path_b, area=ABOVE_HEAD):
    """The percentage of pixels in an area of the picture that changed by more than 24 of 255 in a colour."""
    a, b = Image.open(path_a).convert('RGB'), Image.open(path_b).convert('RGB')
    w, h = a.size
    box = tuple(int(v) for v in (area[0] * w, area[1] * h, area[2] * w, area[3] * h))
    pixels = list(ImageChops.difference(a.crop(box), b.crop(box)).getdata())
    return 100.0 * sum(1 for p in pixels if max(p) > 24) / len(pixels)


UNPOSED = re.compile(r'^\[(\S+ \S+)\].*PoseFlipDiag: copy \S+ posed at (\d+) of (\d+) frame ends in 10 s; not posed: (.*?); switched')


def unposed_frame_ends(since, until):
    """The flat game's own count of frame ends where it left the copy unposed between two times, and why.

    It writes one line per 10 s, and only for a 10 s where some frame end was not posed."""
    total, reasons = 0, []
    for line in flat_log_since_connect():
        match = UNPOSED.match(line)
        if not match:
            continue
        at = time.mktime(time.strptime(match.group(1).split('.')[0], '%Y-%m-%d %H:%M:%S'))
        # A line covers the 10 s before it.
        if at < since or at - 10 > until:
            continue
        total += int(match.group(3)) - int(match.group(2))
        reasons.append(match.group(4))
    return total, reasons


def vr_picture(name):
    answer = tool(VR_PORT, 'capture', {'kind': 'native', 'checkpointId': name, 'cleanup': True, 'outDir': SHOTS}, timeout=40)
    path = answer.get('path')
    return path if path and os.path.isfile(path) else None


def look_at(eye, aim):
    dx, dy, dz = aim[0] - eye[0], aim[1] - eye[1], aim[2] - eye[2]
    tool(FLAT_PORT, 'camera', {'action': 'freecam', 'on': True})
    tool(FLAT_PORT, 'camera', {'action': 'drive', 'x': eye[0], 'y': eye[1], 'z': eye[2], 'pitch': math.atan2(-dz, math.hypot(dx, dy)), 'yaw': math.atan2(dx, dy)})


def log_since_connect(root_variable, folder):
    """A client's log lines since its last connection.

    A log full at 5 MB moves to tp_client.1.log, so that older file is read first."""
    root = os.environ.get(root_variable)
    if not root:
        return []
    lines = []
    for name in ('tp_client.1.log', 'tp_client.log'):
        path = os.path.join(root, 'tools', folder, 'logs', name)
        if os.path.isfile(path):
            with open(path, 'rb') as f:
                lines += f.read().decode('utf-8', errors='replace').splitlines()
    starts = [i for i, line in enumerate(lines) if 'Connected: the server accepted this player' in line]
    return lines[starts[-1]:] if starts else []


def flat_log_since_connect():
    return log_since_connect('SKYRIM_FLAT_ROOT', 'Skyrim Together')


def line_time(line):
    match = re.match(r'^\[(\S+ \S+)\]', line)
    return time.mktime(time.strptime(match.group(1).split('.')[0], '%Y-%m-%d %H:%M:%S')) if match else None


def posed_between(since, until):
    """The flat game's posed-copy lines between two times: written every 5 s, and only while it poses the copy."""
    return [line for line in flat_log_since_connect()
            if 'VRBodySync: actor' in line and ' body -- ' in line and line_time(line) is not None and since <= line_time(line) <= until]


def fingers_check(heading, ahead, stamp, expect_fail):
    """The flat copy's fingers take the VR player's: found in the skeleton's bone array and written there.

    The rig's controllers cannot curl the VR player's fingers (no button or touch moves them), so nothing here changes them.
    The VR hand rests open and straight while the flat game's own animation keeps its fingers curled: the close pictures of the
    copy's right hand show which one the flat game draws, for the eye."""
    # The VR player faces the flat player, its hands at chest height between the two.
    papyrus(VR_PORT, 'ObjectReference', 'SetAngle', [0.0, 0.0, (heading + 180.0) % 360.0])
    hands = Hands()
    hands.height, hands.current, hands.reach = 1.25, 1.25, 0.35
    hands.thread.start()
    try:
        time.sleep(6)
        # Where the VR player stands once it has landed on the ground, which is not the height it was put at.
        stands = position(VR_PORT) or ahead
        # Toward the flat player is the copy's front; its right is a quarter turn from there.
        front = (-math.sin(math.radians(heading)), -math.cos(math.radians(heading)))
        right = (-math.cos(math.radians(heading)), math.sin(math.radians(heading)))
        hand = (stands[0] + 24.0 * front[0] + 17.0 * right[0], stands[1] + 24.0 * front[1] + 17.0 * right[1], stands[2] + 87.0)
        eye = (hand[0] + 70.0 * front[0], hand[1] + 70.0 * front[1], hand[2] + 20.0)
        look_at(eye, hand)
        time.sleep(1)
        shots = {'hand': picture('cross-fingers-%s-hand' % stamp)}
        time.sleep(1)
        shots['hand2'] = picture('cross-fingers-%s-hand2' % stamp)
        shots['vr-hand'] = vr_picture('cross-fingers-%s-vr-hand' % stamp)
    finally:
        tool(FLAT_PORT, 'camera', {'action': 'freecam', 'on': False})
        hands.stop.set()

    for name, path in shots.items():
        print('   %s: %s' % (name, path))
    log = flat_log_since_connect()
    posed = [line for line in log if 'VRBodySync: actor' in line and ' body -- ' in line]
    no_array = [line for line in log if 'no usable flattened bone array' in line]
    no_fingers = [line for line in log if 'finger bones not found by shape' in line]
    # Written once per copy, the first time the flat game writes its fingers.
    written = [line for line in log if 'VRBodySync: actor' in line and ' fingers posed ' in line]
    # Without its own finger bones the VR game sends no fingers, and the flat game has nothing to write.
    not_sent = [line for line in log_since_connect('SKYRIM_FUS_ROOT', 'Skyrim Together VR') if 'local finger bones not found' in line]
    if not posed:
        print('INVALID: the flat log shows no posed copy since the connection')
        return 2
    for line in (no_array + no_fingers + not_sent)[:4]:
        print('   log: %s' % line.strip()[:220])
    checks = [
        (not no_array and not no_fingers, 'the flat game finds the copy\'s bone array and its fingers (%d posed-copy lines, %d without an array, %d without fingers)' % (len(posed), len(no_array), len(no_fingers))),
        (not not_sent, 'the VR game sends its fingers (%d lines saying it has none)' % len(not_sent)),
        (bool(written), 'the flat game wrote the copy\'s fingers (%s)' % (written[-1].strip()[-60:] if written else 'no line')),
    ]
    for ok, text in checks:
        print(('ok    ' if ok else 'FAIL  ') + text)
    passed = all(ok for ok, _ in checks)
    print('%s: %d of %d checks' % ('PASS' if passed else 'FAIL', sum(ok for ok, _ in checks), len(checks)))
    if expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


def held_frames(stamp, count):
    """count pictures in a row with the hands held up; returns their paths and the seconds they took."""
    started = time.time()
    paths = [picture('cross-pose-%s-up-%02d' % (stamp, i)) for i in range(count)]
    return paths, time.time() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='on a build without the fix: the run must be red')
    parser.add_argument('--frames', type=int, default=0, help='pictures to take with the hands held up, looking for frames that drop the pose')
    parser.add_argument('--fingers', action='store_true', help='the hands closing instead of the arms rising')
    options = parser.parse_args()

    if not log_connected('SKYRIM_FLAT_ROOT', 'Skyrim Together') or not log_connected('SKYRIM_FUS_ROOT', 'Skyrim Together VR'):
        print('INVALID: both rig games must be up and connected')
        return 2
    # An open menu pauses a flat game.
    for port in (FLAT_PORT, VR_PORT):
        for menu in (tool(port, 'menu', {'action': 'list'}) or {}).get('openMenus', []):
            if menu not in ('HUD Menu', 'Cursor Menu'):
                tool(port, 'menu', {'action': 'close', 'name': menu})

    # The VR player stands 150 units ahead of the flat player, in the flat camera's view.
    here = position(FLAT_PORT)
    heading = papyrus(FLAT_PORT, 'ObjectReference', 'GetAngleZ', [])
    if not here or not isinstance(heading, (int, float)):
        print('INVALID: no position or heading for the flat player')
        return 2
    ahead = [here[0] + 150 * math.sin(math.radians(heading)), here[1] + 150 * math.cos(math.radians(heading)), here[2]]
    papyrus(VR_PORT, 'ObjectReference', 'SetPosition', ahead)

    # A drawn weapon swings as the hands rise or close; sheathed, the arms only lift.
    papyrus(VR_PORT, 'Actor', 'SheatheWeapon', [])
    os.makedirs(SHOTS, exist_ok=True)
    stamp = time.strftime('%H%M%S')
    if options.fingers:
        if options.frames:
            print('   --frames is not used with --fingers')
        return fingers_check(heading, ahead, stamp, options.expect_fail)

    hands = Hands()
    hands.thread.start()
    eye = (ahead[0] + 180.0, ahead[1] - 180.0, ahead[2] + 120.0)
    aim = (ahead[0], ahead[1], ahead[2] + 70.0)
    try:
        time.sleep(6)
        look_at(eye, aim)
        time.sleep(1)
        down = picture('cross-pose-%s-down' % stamp)
        time.sleep(1)
        down2 = picture('cross-pose-%s-down2' % stamp)
        hands.height = 2.0
        time.sleep(6)
        up = picture('cross-pose-%s-up' % stamp)
        held, held_seconds, held_since = [], 0.0, time.time()
        if options.frames > 0 and up:
            held, held_seconds = held_frames(stamp, options.frames)
            # The flat game reports a 10 s with an unposed frame end only once that 10 s is over, and its log is written each second.
            # The free camera stays on meanwhile, so the view check sees the copy as it did in the pictures.
            time.sleep(13)
    finally:
        tool(FLAT_PORT, 'camera', {'action': 'freecam', 'on': False})
        hands.stop.set()

    if not (down and down2 and up):
        print('INVALID: the flat game gave no picture (%s, %s, %s)' % (down, down2, up))
        return 2
    # The noise is read with the hands down: raised, the rig's VR body can settle its elbows two ways, a real change of pose.
    arms, noise = changed_share(down, up), changed_share(down, down2)
    print('   pictures: %s, %s, %s' % (down, down2, up))
    print('   above the copy\'s head: %.1f%% changed when the hands went up, %.1f%% between the two pictures with them down' % (arms, noise))
    checks = [arms >= FLOOR and arms >= FACTOR * noise]
    print(('ok    ' if checks[0] else 'FAIL  ') + 'the flat copy raises its arms with the VR player\'s hands')
    if options.frames > 0:
        if not held or None in held:
            print('INVALID: the flat game gave no picture for some frames')
            return 2
        held_until = held_since + held_seconds
        unposed, reasons = unposed_frame_ends(held_since, held_until)
        # A copy never posed in that time writes no unposed count at all, so the posed-copy lines (every 5 s) must show it was posed.
        posed = posed_between(held_since, held_until + 6)
        # Frames far from the hands-up picture above the head are for the eye: the rig's VR body flips an elbow or the head now and then.
        apart = [i for i, path in enumerate(held) if changed_share(path, up) > FLOOR]
        print('   %d frames with the hands up in %.1f s; %d of them differ above the head from the hands-up picture (look at them): %s' % (len(held), held_seconds, len(apart), ', '.join(os.path.basename(held[i]) for i in apart[:8])))
        print('   the flat game left the copy unposed at %d frame ends in that time%s' % (unposed, (': ' + ' | '.join(reasons)) if reasons else ''))
        steady = unposed == 0 and bool(posed)
        print(('ok    ' if steady else 'FAIL  ') + 'the flat game posed the copy at every frame end while the hands were up (%d posed-copy lines in that time)' % len(posed))
        checks.append(steady)
    passed = all(checks)
    print('%s: %d of %d checks' % ('PASS' if passed else 'FAIL', sum(checks), len(checks)))
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
