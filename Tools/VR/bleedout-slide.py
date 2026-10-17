"""Does a downed essential NPC slide along the floor? Run with the rig up (headless.ps1 up), nobody playing.

The player's own NPC, not a copy: a bandit is placed next to the player, made essential, knocked into bleedout and
watched while it lies there. Measured connected first, then again after F6 disconnects, so the two can be compared.
A body lying in bleedout should stay where it fell; travel while down is the slide players reported. Each state is
measured twice: lying still, and while the player's sword sweeps across it every 2.5 s.

    python Tools/VR/bleedout-slide.py [seconds per run, default 15]
"""
import ctypes
import importlib.util
import json
import math
import os
import sys
import time
import urllib.request

DEVBENCH = 'http://127.0.0.1:8921/api/tool/'
BANDIT_REF = 'C00F8'  # a Mistwatch bandit near the rig's start; only its base is used


def tool(name, body, timeout=6):
    req = urllib.request.Request(DEVBENCH + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def papyrus(script, function, form, args=None):
    body = {'action': 'call', 'script': script, 'function': function, 'self': {'form': '0x' + form}}
    if args is not None:
        body['args'] = args
    try:
        return tool('papyrus', body).get('returned')
    except Exception as e:
        return 'ERR ' + type(e).__name__


def console(command):
    tool('console', {'action': 'exec', 'command': command})


def position(form):
    p = [papyrus('ObjectReference', f, form) for f in ('GetPositionX', 'GetPositionY', 'GetPositionZ')]
    return p if all(isinstance(v, (int, float)) for v in p) else None


def pelvis(form):
    """Where the body is drawn: a ragdoll or an animation moves the pelvis without moving the reference."""
    p = []
    for axis in 'XYZ':
        body = {'action': 'call', 'script': 'NetImmerse', 'function': 'GetNodeWorldPosition' + axis,
                'args': [{'form': '0x' + form}, 'NPC Pelvis [Pelv]', False]}
        try:
            p.append(tool('papyrus', body).get('returned'))
        except Exception:
            return None
    return p if all(isinstance(v, (int, float)) for v in p) and any(p) else None


def screenshot(name):
    # live-check's own, which moves the picture out of the mod manager's overwrite folder.
    spec = importlib.util.spec_from_file_location('live_check', os.path.join(os.path.dirname(__file__), 'live-check.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.screenshot(name)


def press_f6():
    # The client reads F6 with GetAsyncKeyState, which only a key press at the Windows level reaches.
    user32 = ctypes.windll.user32
    user32.keybd_event(0x75, 0, 0, 0)
    time.sleep(0.1)
    user32.keybd_event(0x75, 0, 2, 0)


def form_id(returned):
    return returned.get('formId', '').replace('0x', '') if isinstance(returned, dict) else None


def place_essential_bandit():
    base = form_id(papyrus('ObjectReference', 'GetBaseObject', BANDIT_REF))
    if not base:
        sys.exit('no base for %s: is the rig at Mistwatch?' % BANDIT_REF)
    ref = form_id(papyrus('ObjectReference', 'PlaceAtMe', '14', [{'form': '0x' + base}, 1, False, False]))
    if not ref:
        sys.exit('PlaceAtMe of %s failed' % base)
    placed_base = form_id(papyrus('ObjectReference', 'GetBaseObject', ref)) or base
    papyrus('ActorBase', 'SetEssential', placed_base, [True])
    time.sleep(3)
    # Straight ahead of the player, within a sword's reach of a sweep.
    heading = math.radians(papyrus('ObjectReference', 'GetAngleZ', '14'))
    papyrus('ObjectReference', 'MoveTo', ref, [{'form': '0x14'}, 90.0 * math.sin(heading), 90.0 * math.cos(heading), 0.0, False])
    time.sleep(1)
    return ref


def swing_across(ref, ms=120):
    """The right controller sweeps right to left across the body at knee height, 45 cm short of it, so the blade's
    middle crosses it (DevBench vrTrackedSet, as live-check's "DO swing ... through copy")."""
    def pos(form):
        return [papyrus('ObjectReference', 'Get' + a, form) for a in ('PositionX', 'PositionY', 'PositionZ', 'AngleZ')]
    bx, by, bz, _ = pos(ref)
    px, py, pz, pa = pos('14')
    pa = math.radians(pa)
    dx, dy = bx - px, by - py
    right = dx * math.cos(pa) - dy * math.sin(pa)
    forward = dx * math.sin(pa) + dy * math.cos(pa)
    rx, rz = right / 70.0, -forward / 70.0
    x0, x1, y, z = rx + 0.45, rx - 0.45, 0.35, min(-0.2, rz + 0.45)
    base = tool('input', {'action': 'observe', 'device': 'vrTrackedSet'})['frame']
    steps = max(2, ms // 20)
    frames = []
    for i in range(steps + 1):
        f = json.loads(json.dumps(base))
        f.pop('seq', None)
        f['tMs'] = int(i * ms / steps)
        f['hmd']['matrix'] = [1, 0, 0, 0, 0, 1, 0, 1.6, 0, 0, 1, 0]
        # Turned half a turn about the vertical: unturned, the blade points back at the player.
        f['right']['matrix'] = [-1, 0, 0, x0 + (x1 - x0) * i / steps, 0, 1, 0, y, 0, 0, -1, z]
        f['left']['matrix'] = [1, 0, 0, -0.25, 0, 1, 0, 1.0, 0, 0, 1, -0.1]
        for d in ('hmd', 'left', 'right'):
            f[d].update({'available': True, 'connected': True, 'valid': True, 'trackingResult': 200})
            if f[d].get('index') is None:
                f[d]['index'] = {'hmd': 0, 'left': 2, 'right': 3}[d]
            for key in ('velocity', 'angularVelocity'):
                f[d].setdefault(key, [0.0, 0.0, 0.0])
            if d != 'hmd' and not f[d].get('controller'):
                f[d]['controller'] = {'axes': [[0.0, 0.0]] * 5, 'packetNumber': 1, 'pressed': 0, 'touched': 0}
        for d in ('left', 'right'):
            f[d]['controller']['packetNumber'] = (base[d].get('controller') or {}).get('packetNumber', 1) + i + 1
        frames.append(f)
    tool('input', {'action': 'sequence', 'device': 'vrTrackedSet', 'frames': frames, 'owner': 'bleedout-slide'}, timeout=15)


def ensure(command, wanted):
    """A console toggle put in a known state: run, read the answer, run once more if it went the wrong way."""
    for _ in range(2):
        tool('console', {'action': 'exec', 'command': command, 'capture': True})
        time.sleep(1.2)
        answer = ' '.join(tool('console', {'action': 'read'}).get('lines', []))
        if wanted.lower() in answer.lower():
            return True
    return False


def run(ref, seconds, label, hits=False):
    # Kept down: no health comes back and no recovery, so the game never stands it up during the measurement.
    papyrus('Actor', 'ForceActorValue', ref, ['HealRate', 0.0])
    papyrus('Actor', 'ForceActorValue', ref, ['CombatHealthRegenMult', 0.0])
    papyrus('Actor', 'SetNoBleedoutRecovery', ref, [True])
    papyrus('Actor', 'ResetHealthAndLimbs', ref)
    time.sleep(3)
    papyrus('Actor', 'DamageActorValue', ref, ['Health', 100000.0])
    for _ in range(20):
        if papyrus('Actor', 'IsBleedingOut', ref) is True:
            break
        time.sleep(0.25)
    # Bleedout starts with a fall; the measurement starts once the body has settled on the floor.
    time.sleep(2.5)

    # Only samples taken while it is still down count: a body getting up walks, which is not a slide.
    last = pelvis(ref)
    first = last
    first_ref = position(ref)
    travelled, top_step, down_samples, up_samples = 0.0, 0.0, 0, 0
    shot = None
    end = time.time() + seconds
    next_swing = time.time() + 1.0
    while time.time() < end:
        time.sleep(0.3)
        # The downed NPCs players saw slide were being hit.
        if hits and time.time() >= next_swing:
            swing_across(ref)
            next_swing = time.time() + 2.5
            if not shot:
                time.sleep(0.06)
                shot = screenshot('bleedout-slide-' + label.replace(', ', '-'))
        at = pelvis(ref)
        down = papyrus('Actor', 'IsBleedingOut', ref) is True
        if not down:
            up_samples += 1
        elif at and last:
            step = math.dist(at, last)
            travelled += step
            top_step = max(top_step, step)
            down_samples += 1
        last = at or last
    papyrus('Actor', 'SetNoBleedoutRecovery', ref, [False])
    net = math.dist(first, last) if first and last else -1.0
    last_ref = position(ref)
    ref_net = math.dist(first_ref, last_ref) if first_ref and last_ref else -1.0
    print('%-14s %d samples down, %d up: pelvis travelled %.0f units while down, largest step %.0f, %.0f from where it settled; '
          'the reference moved %.0f%s' % (label, down_samples, up_samples, travelled, top_step, net, ref_net, '; ' + shot if shot else ''))
    return travelled


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
    # The NPCs players saw slide were in a fight: combat AI on (the rig turns it off), the player out of harm's way.
    ensure('tgm', 'enabled')
    ensure('tcai', 'is on')
    ref = place_essential_bandit()
    print('bandit %s placed and made essential' % ref)
    papyrus('Actor', 'DrawWeapon', '14')
    time.sleep(2)
    connected = run(ref, seconds, 'connected')
    connected_hit = run(ref, seconds, 'connected, hit', hits=True)
    press_f6()
    time.sleep(4)
    offline = run(ref, seconds, 'offline')
    offline_hit = run(ref, seconds, 'offline, hit', hits=True)
    press_f6()
    ensure('tcai', 'is off')
    ensure('tgm', 'disabled')
    papyrus('ObjectReference', 'Disable', ref, [False])
    print('travelled while down: connected %.0f (hit %.0f), offline %.0f (hit %.0f); F6 pressed again to reconnect' %
          (connected, connected_hit, offline, offline_hit))


if __name__ == '__main__':
    main()
