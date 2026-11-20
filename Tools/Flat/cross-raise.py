"""A body raised by the player whose game does not run it: both games must show the raised creature standing.

    python Tools/Flat/cross-raise.py
    python Tools/Flat/cross-raise.py --expect-fail     on a build without the fix: must be RED

Both rigs up and connected, the flat one first so that it leads the party: Tools/Flat/flat-rig.ps1 up, then Tools/VR/headless.ps1 up -AutoConnect.
The flat game places a skeever ahead of its player and kills it, so the flat game runs that body and the VR game shows a copy of it.
The VR player then raises that copy with Raise Zombie, as a player who joined raises a body the host's game runs.
The spell is launched by RemoteCast from a marker just above the body, with the VR player as its caster.
A VR spell cast by the player flies where the rig's hand points, which is not at a body on the ground.
For the next seconds both games are read: raised (a creature the player commands), dead, up, or gone (switched off: a raised body that ends turns to ash).
Papyrus counts a body as dead for the first moment of its raising, so a raised one is told by the command, not by IsDead.
The skeever is Skyrim.esm's own, not levelled, so both games show the same creature; Raise Zombie raises one that weak.
"""
import argparse
import math
import re
import sys
import time

from cross_games import close_menus, connected, flat_game, form_id, log_since, log_size, nearest_temporary_actor, papyrus, position, vr_game

SKEEVER = '0x00023AB7'
RAISE_ZOMBIE = '0x0007E8E1'
XMARKER = '0x0000003B'
# How long the raised skeever is watched; the spell lasts 60 s.
WATCH = 12.0
# The first moments after the cast, while the body gets up, are not judged.
SETTLE = 3.0


def ahead_of(target, distance):
    """The point a distance ahead of that game's player, at its height, or None."""
    here = position(target)
    heading = papyrus(target, 'ObjectReference', 'GetAngleZ', [], '0x14')
    if not here or not isinstance(heading, (int, float)):
        return None
    return [here[0] + distance * math.sin(math.radians(heading)), here[1] + distance * math.cos(math.radians(heading)), here[2]]


def marker_at(target, spot):
    """An invisible marker that game places at a spot; returns its form id, or None."""
    marker = form_id(papyrus(target, 'ObjectReference', 'PlaceAtMe', [{'form': XMARKER}, 1, False, False], '0x14'))
    if marker:
        papyrus(target, 'ObjectReference', 'SetPosition', spot, marker)
    return marker


def place_dead_skeever(flat):
    """A skeever the flat game places 200 units ahead of its player, at that spot from the start, and kills; returns its form id, or None.

    Placed where it stays, its copy in the other game falls where the flat game's body lies."""
    spot = ahead_of(flat, 200.0)
    marker = marker_at(flat, spot) if spot else None
    skeever = form_id(papyrus(flat, 'ObjectReference', 'PlaceAtMe', [{'form': SKEEVER}, 1, False, False], marker)) if marker else None
    if not skeever:
        return None
    time.sleep(4)
    papyrus(flat, 'Actor', 'DamageActorValue', ['Health', 10000.0], skeever)
    papyrus(flat, 'ObjectReference', 'Disable', [False], marker)
    return skeever


def state(target, form):
    """'raised', 'dead', 'up' or 'gone' for an actor in that game; gone when it is switched off or no longer there."""
    if papyrus(target, 'ObjectReference', 'IsDisabled', [], form) is not False:
        return 'gone'
    if papyrus(target, 'Actor', 'IsCommandedActor', [], form) is True:
        return 'raised'
    dead = papyrus(target, 'Actor', 'IsDead', [], form)
    return 'dead' if dead is True else 'up' if dead is False else 'gone'


def look_away(target, seconds):
    """Turns that game's player away for a while: a copy's body waits until its player looks away to be moved where its owner has it."""
    heading = papyrus(target, 'ObjectReference', 'GetAngleZ', [], '0x14')
    if not isinstance(heading, (int, float)):
        return
    papyrus(target, 'ObjectReference', 'SetAngle', [0.0, 0.0, (heading + 180.0) % 360.0], '0x14')
    time.sleep(seconds)
    papyrus(target, 'ObjectReference', 'SetAngle', [0.0, 0.0, heading], '0x14')


def raise_body(vr, copy):
    """The VR player raises a body: Raise Zombie by RemoteCast from a marker 40 units above it, the player its caster."""
    at = position(vr, copy)
    marker = marker_at(vr, [at[0], at[1], at[2] + 40.0]) if at else None
    if not marker:
        return False
    time.sleep(0.5)
    papyrus(vr, 'Spell', 'RemoteCast', [{'form': marker}, {'form': '0x14'}, {'form': copy}], RAISE_ZOMBIE)
    return True


def wait_for(test, seconds):
    """Waits until a test holds or the time runs out; returns whether it held."""
    end = time.time() + seconds
    while time.time() < end:
        if test():
            return True
        time.sleep(0.5)
    return False


def say_log_lines(target, offset, forms):
    """Prints the log lines since an offset that speak of a death or a life, or of one of the forms."""
    wanted = [f.upper().replace('0X', '') for f in forms]
    for line in log_since(target, offset).splitlines():
        if 'Death sync' in line or 'Reanimat' in line or 'Respawn' in line or any(w and w in line.upper() for w in wanted):
            print('   %s: %s' % (target['name'], line.strip()[:230]), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='on a build without the fix: the run must be red')
    options = parser.parse_args()

    flat, vr = flat_game(), vr_game()
    for target in (flat, vr):
        if not connected(target):
            print('INVALID: the %s game is not connected (%s)' % (target['name'], target['log']))
            return 2
        close_menus(target)
    time.sleep(1)

    flat_offset, vr_offset = log_size(flat), log_size(vr)
    skeever = place_dead_skeever(flat)
    at = position(flat, skeever) if skeever else None
    if not at:
        print('INVALID: the flat game placed no skeever')
        return 2
    flat_player_copy = nearest_temporary_actor(vr, position(flat) or [0, 0, 0], 150.0)
    copy = None

    def copy_is_dead():
        nonlocal copy
        copy = copy or nearest_temporary_actor(vr, at, 300.0, [flat_player_copy] if flat_player_copy else [])
        return bool(copy) and state(vr, copy) == 'dead'

    if not wait_for(copy_is_dead, 10.0):
        print('INVALID: the VR game shows no dead copy of the flat game\'s skeever %s (copy %s)' % (skeever, copy))
        return 2
    if re.search(r'GAINED OWNERSHIP OF ACTOR \S+ \(FORM %s\)' % copy.upper().replace('0X', ''), log_since(vr, vr_offset).upper()):
        print('INVALID: the VR game took the skeever over; the flat game must run it, as the host\'s game did')
        return 2
    print('   flat skeever %s, its copy in VR %s, both dead' % (skeever, copy), flush=True)

    # The VR player stands near the body, looks away while its copy is laid where the flat game has it, and raises it.
    papyrus(vr, 'ObjectReference', 'SetPosition', [at[0] + 250.0, at[1], at[2]], '0x14')
    look_away(vr, 3.0)
    if not raise_body(vr, copy):
        print('INVALID: the VR game could not place the marker to cast from')
        return 2
    cast_at = time.time()

    timeline = []
    while time.time() - cast_at < WATCH:
        health = papyrus(vr, 'Actor', 'GetActorValue', ['Health'], copy)
        timeline.append((time.time() - cast_at, state(vr, copy), state(flat, skeever), health))
        time.sleep(0.5)

    print('   VR raised its copy; every half second after, VR / flat (and the VR copy\'s health):', flush=True)
    print('   ' + ' '.join('%.1f:%s/%s(%s)' % (t, v, f, '%.0f' % h if isinstance(h, (int, float)) else '-') for t, v, f, h in timeline), flush=True)
    say_log_lines(flat, flat_offset, [skeever])
    say_log_lines(vr, vr_offset, [copy])

    # Cleanup: the flat game's skeever goes, and its copy with it.
    papyrus(flat, 'ObjectReference', 'Disable', [False], skeever)

    if not any(v in ('raised', 'up') or f in ('raised', 'up') for _, v, f, _ in timeline):
        print('INVALID: the skeever got up in neither game: the spell did not take')
        return 2

    judged = [(v, f) for t, v, f, _ in timeline if t >= SETTLE]
    checks = [
        ('the raised skeever stays up in the caster\'s game', all(v == 'raised' for v, _ in judged), ', '.join(sorted({v for v, _ in judged}))),
        ('both games show the raised skeever the same way', all(v == f for v, f in judged), '%d of %d looks differ' % (sum(v != f for v, f in judged), len(judged))),
    ]
    for name, ok, detail in checks:
        print(('ok    ' if ok else 'FAIL  ') + name + ' (%s)' % detail, flush=True)
    passed = all(ok for _, ok, _ in checks)
    print('%s: %d of %d checks' % ('PASS' if passed else 'FAIL', sum(ok for _, ok, _ in checks), len(checks)))
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
