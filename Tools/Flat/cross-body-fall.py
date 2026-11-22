"""A body that died out of this game's sight, shown here by a stand-in: its ragdoll must stay on the ground when the player comes near.

    python Tools/Flat/cross-body-fall.py
    python Tools/Flat/cross-body-fall.py --expect-fail     to show the fall where it happens: must be RED

Both rigs up and connected, the flat one first: Tools/Flat/flat-rig.ps1 up, then Tools/VR/headless.ps1 up -AutoConnect. One run per rig start.
The VR player first goes far away, so that the flat game alone has the body while it moves.
The flat game then kills Skyrim.esm's rabbit 105985 at its home and carries the body to the rigs' start, far from where the VR game has that rabbit.
Then the VR player comes to 9000 units from it: the VR game, which has no rabbit loaded there, makes a stand-in copy for the dead body the flat game runs.
After 20 s the VR player comes to 3000 units, near enough for the body's physics to run.
The copy's ragdoll root (NPC Root) is read for 12 s: it must stay within 512 units of the flat game's body, and the copy must stay switched on and dead.
A stand-in that reaches its 3D step already dead, with nothing played, can have a ragdoll with no ground under it once the player is near; it then falls and keeps falling.
That failing state comes and goes: the run reproduces the fall when it comes, and passes when the stand-in dies at its 3D step as usual.
A SpawnDeathDiag line names what killed the stand-in early; a FallStop line means the guard switched a falling copy off (the frame rate is saved, but the body still fell).
"""
import argparse
import re
import sys
import time

from cross_games import close_menus, connected, flat_game, log_since, log_size, papyrus, position, vr_game

RABBIT = '0x00105985'
# Where the flat player finds the rabbit, and the rigs' start, flat ground where bodies lie still.
HOME = [27000.0, -36700.0, 200.0]
SPOT = [140924.0, -32908.0, -10350.0]
FAR = 9000.0
# Where the VR player waits while the flat game carries the body: out of reach of both places.
AWAY = [-32724.0, -96882.0, 400.0]
NEAR = 3000.0
WAIT_FAR = 20.0
WATCH_NEAR = 12.0
# A settled ragdoll root lies within this of the owner's body; one that fell is thousands of units under it.
HELD_WITHIN = 512.0


def root_z(target, form):
    """The height of an actor's NPC Root node in that game, or None when it cannot be read (no 3D, or switched off)."""
    z = papyrus(target, 'NetImmerse', 'GetNodeWorldPositionZ', [{'form': form}, 'NPC Root [Root]', False])
    return z if isinstance(z, (int, float)) and z != 0.0 else None


def wait_for(test, seconds):
    """Waits until a test returns something true or the time runs out; returns its last result."""
    end = time.time() + seconds
    result = test()
    while not result and time.time() < end:
        time.sleep(0.5)
        result = test()
    return result


def bring_dead_rabbit(flat):
    """The flat player finds the rabbit at its home, kills it and carries the body to the rigs' start; returns where it lies, or the reason it could not."""
    papyrus(flat, 'ObjectReference', 'SetPosition', HOME, '0x14')
    if not wait_for(lambda: papyrus(flat, 'ObjectReference', 'Is3DLoaded', [], RABBIT) is True, 20.0):
        return 'the flat game has no rabbit 105985 near its home (killed by an earlier run? one run per rig start)'
    if papyrus(flat, 'Actor', 'IsDead', [], RABBIT) is not True:
        papyrus(flat, 'Actor', 'DamageActorValue', ['Health', 10000.0], RABBIT)
        time.sleep(4)
    # The body moves while its cell is loaded; then the flat player follows it.
    papyrus(flat, 'ObjectReference', 'SetPosition', SPOT, RABBIT)
    time.sleep(2)
    papyrus(flat, 'ObjectReference', 'SetPosition', [SPOT[0] - 200.0, SPOT[1], SPOT[2] + 50.0], '0x14')
    at = wait_for(lambda: position(flat, RABBIT), 15.0)
    if not at or papyrus(flat, 'Actor', 'IsDead', [], RABBIT) is not True:
        return 'the dead rabbit did not reach the rigs\' start in the flat game'
    return at


def stand_in_from(vr, offset):
    """The form id of the newest copy the VR game made to stand in for the rabbit since an offset in its log that has its 3D, or None.

    A stand-in made while the flat game still carries the body is removed before it loads; only one that has its 3D counts."""
    copies = re.findall(r'Stand-in: (?:reference 105985 is not loaded here;|the copy for reference 105985 \(server id [0-9A-F]+\) was gone;) copy (FF[0-9A-F]+)', log_since(vr, offset))
    copies = ['0x' + copy for copy in copies]
    for copy in reversed(copies):
        if papyrus(vr, 'ObjectReference', 'Is3DLoaded', [], copy) is True:
            return copy
    return None


def took_over(vr, offset, copy):
    """Whether the VR game took the body over since an offset in its log, which makes it that game's own body, not a copy."""
    return re.search(r'GAINED OWNERSHIP OF ACTOR \S+ \(FORM %s\)' % copy.upper().replace('0X', ''), log_since(vr, offset).upper()) is not None


def say_lines(vr, offset, copy):
    """Prints the VR log lines since an offset about the copy: how it came, its fall, the guard."""
    short = copy.upper().replace('0X', '')
    for line in log_since(vr, offset).splitlines():
        if short in line.upper() and any(word in line for word in ('came', 'Stand-in', 'BodyStand', 'CorpseFall', 'FallStop', 'CopyOff', 'left there', 'SpawnDeathDiag')):
            print('   VR: ' + line.strip()[:220], flush=True)


def watch_root(vr, copy, seconds, label):
    """Reads the copy's ragdoll root height every half second for some seconds; returns the readings."""
    readings = []
    end = time.time() + seconds
    while time.time() < end:
        readings.append(root_z(vr, copy))
        time.sleep(0.5)
    print('   %s: %s' % (label, ' '.join('-' if z is None else '%.0f' % z for z in readings)), flush=True)
    return readings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='to show the fall where it happens: the run must be red')
    options = parser.parse_args()

    flat, vr = flat_game(), vr_game()
    for target in (flat, vr):
        if not connected(target):
            print('INVALID: the %s game is not connected (%s)' % (target['name'], target['log']))
            return 2
        close_menus(target)

    papyrus(vr, 'ObjectReference', 'SetPosition', AWAY, '0x14')
    time.sleep(5)
    at = bring_dead_rabbit(flat)
    if isinstance(at, str):
        print('INVALID: ' + at)
        return 2
    time.sleep(5)
    offset = log_size(vr)
    papyrus(vr, 'ObjectReference', 'SetPosition', [at[0] + FAR, at[1], at[2] + 300.0], '0x14')
    copy = wait_for(lambda: stand_in_from(vr, offset), 45.0)
    if not copy:
        print('INVALID: the VR game made no stand-in for the dead rabbit (it may have the rabbit loaded itself)')
        return 2
    if took_over(vr, offset, copy):
        print('INVALID: the VR game took the body over; the flat game must run it')
        return 2
    print('   flat body at z %.0f; VR stand-in %s, dead %s, health %s' % (at[2], copy, papyrus(vr, 'Actor', 'IsDead', [], copy),
                                                                      papyrus(vr, 'Actor', 'GetActorValue', ['Health'], copy)), flush=True)
    far = watch_root(vr, copy, WAIT_FAR, 'ragdoll root, the player %.0f units off' % FAR)
    if not any(z is not None for z in far):
        print('INVALID: the stand-in\'s ragdoll root could not be read while the player waited off')
        return 2

    papyrus(vr, 'ObjectReference', 'SetPosition', [at[0] + NEAR, at[1], at[2] + 300.0], '0x14')
    near = watch_root(vr, copy, WATCH_NEAR, 'ragdoll root, the player %.0f units off' % NEAR)
    switched_off = papyrus(vr, 'ObjectReference', 'IsDisabled', [], copy) is not False
    say_lines(vr, offset, copy)

    read = [z for z in near if z is not None]
    if len(read) < len(near) // 2 and not switched_off:
        print('INVALID: the ragdoll root was read in only %d of %d looks' % (len(read), len(near)))
        return 2
    off_ground = [z for z in read if abs(z - at[2]) > HELD_WITHIN]
    lowest = min(read or [at[2]])
    dead = papyrus(vr, 'Actor', 'IsDead', [], copy)
    checks = [
        ('its ragdoll stays on the ground once the player is near', bool(read) and not off_ground,
         'lowest %.0f against the flat body at %.0f; %d of %d readings off the ground, %d unread' % (lowest, at[2], len(off_ground), len(read), len(near) - len(read))),
        ('the copy stays switched on', not switched_off, 'switched off' if switched_off else 'on'),
        ('the copy is dead, as in its owner\'s game', switched_off or dead is True, 'dead' if dead is True else 'not dead (%s)' % dead),
    ]
    for name, ok, detail in checks:
        print(('ok    ' if ok else 'FAIL  ') + name + ' (%s)' % detail, flush=True)
    passed = all(ok for _, ok, _ in checks)
    print('PASS' if passed else 'FAIL')
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the fall did not happen in this run')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
