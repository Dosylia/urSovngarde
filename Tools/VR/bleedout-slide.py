"""Does a downed essential NPC slide along the floor? Run with the rig up (headless.ps1 up), nobody playing.

The player's own NPC, not a copy: a bandit is placed next to the player, made essential, knocked into bleedout and
watched while it lies there. Measured connected first, then again after F6 disconnects, so the two can be compared.
A body lying in bleedout should stay where it fell; travel while down is the slide players reported.

    python Tools/VR/bleedout-slide.py [seconds per run, default 12]
"""
import ctypes
import json
import math
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
    return ref


def run(ref, seconds, label):
    papyrus('Actor', 'ResetHealthAndLimbs', ref)
    time.sleep(2)
    papyrus('Actor', 'DamageActorValue', ref, ['Health', 100000.0])
    # Bleedout takes a moment to start; the measurement begins once the game says so.
    for _ in range(20):
        if papyrus('Actor', 'IsBleedingOut', ref) is True:
            break
        time.sleep(0.25)
    down = papyrus('Actor', 'IsBleedingOut', ref)
    start = last = position(ref)
    travelled, top_step, samples = 0.0, 0.0, 0
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.3)
        at = position(ref)
        if at and last:
            step = math.dist(at, last)
            travelled += step
            top_step = max(top_step, step)
            samples += 1
        last = at or last
    still_down = papyrus('Actor', 'IsBleedingOut', ref)
    net = math.dist(start, last) if start and last else -1.0
    print('%-12s bleeding out %s -> %s; %d samples: travelled %.0f units, %.0f from where it fell, largest step %.0f' %
          (label, down, still_down, samples, travelled, net, top_step))
    return travelled


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
    console('tgm')  # the bandit fights back; the player must not die mid-run
    ref = place_essential_bandit()
    print('bandit %s placed and made essential' % ref)
    connected = run(ref, seconds, 'connected')
    press_f6()
    time.sleep(4)
    offline = run(ref, seconds, 'offline')
    press_f6()
    console('tgm')
    papyrus('ObjectReference', 'Disable', ref, [False])
    print('connected travelled %.0f, offline %.0f (F6 pressed again to reconnect)' % (connected, offline))


if __name__ == '__main__':
    main()
