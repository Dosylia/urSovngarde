"""A body raised by the player whose game does not run it: both games must show the raised creature standing, with health, through a hit.

    python Tools/Flat/cross-raise.py [--tries <n>] [--hit-after <seconds>] [--own-body [--caster vr|flat]]
    python Tools/Flat/cross-raise.py --expect-fail     once a fix exists, on the build without it: must be RED

Both rigs up and connected, the flat one first so that it leads the party: Tools/Flat/flat-rig.ps1 up, then Tools/VR/headless.ps1 up -AutoConnect.
The flat game places a skeever ahead of its player and kills it, so the flat game runs that body and the VR game shows a copy of it.
The VR player then raises that copy with Raise Zombie, as a player who joined raises a body the host's game runs.
The spell is launched by RemoteCast from a marker just above the body, with the VR player as its caster.
A VR spell cast by the player flies where the rig's hand points, which is not at a body on the ground.
For the next seconds both games are read: raised (a creature the player commands), dead, up, or gone (switched off: a raised body that ends turns to ash).
Papyrus counts a body as dead for the first moment of its raising, so a raised one is told by the command, not by IsDead.
The skeever is Skyrim.esm's own, not levelled, so both games show the same creature; Raise Zombie raises one that weak.
Its health is read in both games: a raised body left at 0 health dies at its first hit and turns to ash in that game only.
After the watch the VR player's game hits it once, lightly, and both games must still show it raised.
A body is raised again in a new try with --tries, as what takes its health may catch only some raises.
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
# Fewer looks than this after SETTLE judge nothing: DevBench stalled (a look takes about a second, so a watch has about 9).
FEWEST_JUDGED = 6
# The hit after the watch, well under the skeever's health of about 15, and how long both games are watched after it.
HIT = 5.0
AFTER_HIT = 4.0


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


def place_dead_skeever(target):
    """A skeever that game places 200 units ahead of its player, at that spot from the start, and kills; returns its form id, or None.

    Placed where it stays, its copy in the other game falls where this game's body lies."""
    spot = ahead_of(target, 200.0)
    marker = marker_at(target, spot) if spot else None
    skeever = form_id(papyrus(target, 'ObjectReference', 'PlaceAtMe', [{'form': SKEEVER}, 1, False, False], marker)) if marker else None
    if not skeever:
        return None
    time.sleep(4)
    papyrus(target, 'Actor', 'DamageActorValue', ['Health', 10000.0], skeever)
    papyrus(target, 'ObjectReference', 'Disable', [False], marker)
    return skeever


def state(target, form):
    """'raised', 'dead', 'up' or 'gone' for an actor in that game; gone when it is switched off, has no 3D, or is no longer there."""
    if papyrus(target, 'ObjectReference', 'IsDisabled', [], form) is not False:
        return 'gone'
    if papyrus(target, 'ObjectReference', 'Is3DLoaded', [], form) is False:
        return 'gone'
    if papyrus(target, 'Actor', 'IsCommandedActor', [], form) is True:
        return 'raised'
    dead = papyrus(target, 'Actor', 'IsDead', [], form)
    return 'dead' if dead is True else 'up' if dead is False else 'gone'


def health_of(target, form):
    """An actor's health in that game, or None when it cannot be read."""
    health = papyrus(target, 'Actor', 'GetActorValue', ['Health'], form)
    return health if isinstance(health, (int, float)) else None


def follow_replacement(target, form, other, other_form):
    """The actor that now stands for a body in that game: the same one, or a new copy made in its place near where the other game has the body."""
    if state(target, form) != 'gone':
        return form
    at = position(other, other_form)
    newer = nearest_temporary_actor(target, at, 150.0) if at else None
    if newer and newer != form:
        print('   (%s replaced its copy %s with %s)' % (target['name'], form, newer), flush=True)
        return newer
    return form


def watch(flat, vr, bodies, since, until):
    """Looks at both games every half second until some seconds after a start; returns (seconds, VR look, flat look, VR health, flat health) per look.

    bodies: a list [flat form, VR form], updated when a game replaces its copy with a new one."""
    looks = []
    while time.time() - since < until:
        bodies[0] = follow_replacement(flat, bodies[0], vr, bodies[1])
        bodies[1] = follow_replacement(vr, bodies[1], flat, bodies[0])
        skeever, copy = bodies
        looks.append((time.time() - since, state(vr, copy), state(flat, skeever), health_of(vr, copy), health_of(flat, skeever)))
        time.sleep(0.5)
    return looks


def say_looks(title, looks):
    """Prints looks as seconds:VR/flat(VR health,flat health)."""
    def number(health):
        return '%.0f' % health if health is not None else '-'
    print('   %s: %s' % (title, ' '.join('%.1f:%s/%s(%s,%s)' % (t, v, f, number(hv), number(hf)) for t, v, f, hv, hf in looks)), flush=True)


def look_away(target, seconds):
    """Turns that game's player away for a while: a copy's body waits until its player looks away to be moved where its owner has it."""
    heading = papyrus(target, 'ObjectReference', 'GetAngleZ', [], '0x14')
    if not isinstance(heading, (int, float)):
        return
    papyrus(target, 'ObjectReference', 'SetAngle', [0.0, 0.0, (heading + 180.0) % 360.0], '0x14')
    time.sleep(seconds)
    papyrus(target, 'ObjectReference', 'SetAngle', [0.0, 0.0, heading], '0x14')


def raise_body(caster, body):
    """That game's player raises a body: Raise Zombie by RemoteCast from a marker 40 units above it, the player its caster."""
    at = position(caster, body)
    marker = marker_at(caster, [at[0], at[1], at[2] + 40.0]) if at else None
    if not marker:
        return False
    time.sleep(0.5)
    papyrus(caster, 'Spell', 'RemoteCast', [{'form': marker}, {'form': '0x14'}, {'form': body}], RAISE_ZOMBIE)
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


def raise_flat_body(flat, vr):
    """Places a dead skeever in the flat game and raises its copy from the VR game.

    Returns (skeever, copy, cast time), or the reason the try is invalid."""
    skeever = place_dead_skeever(flat)
    at = position(flat, skeever) if skeever else None
    if not at:
        return 'the flat game placed no skeever'
    flat_player_copy = nearest_temporary_actor(vr, position(flat) or [0, 0, 0], 150.0)
    copy = None

    def copy_is_dead():
        nonlocal copy
        copy = copy or nearest_temporary_actor(vr, at, 300.0, [flat_player_copy] if flat_player_copy else [])
        return bool(copy) and state(vr, copy) == 'dead'

    vr_offset = log_size(vr)
    if not wait_for(copy_is_dead, 10.0):
        return 'the VR game shows no dead copy of the flat game\'s skeever %s (copy %s)' % (skeever, copy)
    if re.search(r'GAINED OWNERSHIP OF ACTOR \S+ \(FORM %s\)' % copy.upper().replace('0X', ''), log_since(vr, vr_offset).upper()):
        return 'the VR game took the skeever over; the flat game must run it, as the host\'s game did'
    print('   flat skeever %s, its copy in VR %s, both dead' % (skeever, copy), flush=True)

    # The VR player stands near the body, looks away while its copy is laid where the flat game has it, and raises it.
    papyrus(vr, 'ObjectReference', 'SetPosition', [at[0] + 250.0, at[1], at[2]], '0x14')
    look_away(vr, 3.0)
    if not raise_body(vr, copy):
        return 'the VR game could not place the marker to cast from'
    return skeever, copy, time.time()


def raise_own_body(caster, other):
    """Places a dead skeever in the caster's game, which then runs that body while the other game shows a copy, and the caster's player raises it.

    Returns (the caster's skeever, the other game's copy, cast time), or the reason the try is invalid."""
    skeever = place_dead_skeever(caster)
    at = position(caster, skeever) if skeever else None
    if not at:
        return 'the %s game placed no skeever' % caster['name']
    caster_copy = nearest_temporary_actor(other, position(caster) or [0, 0, 0], 150.0)
    copy = None

    def copy_is_dead():
        nonlocal copy
        copy = copy or nearest_temporary_actor(other, at, 300.0, [caster_copy] if caster_copy else [])
        return bool(copy) and state(other, copy) == 'dead'

    if not wait_for(copy_is_dead, 10.0):
        return 'the %s game shows no dead copy of the %s game\'s skeever %s (copy %s)' % (other['name'], caster['name'], skeever, copy)
    print('   %s skeever %s, its copy in %s %s, both dead' % (caster['name'], skeever, other['name'], copy), flush=True)

    papyrus(caster, 'ObjectReference', 'SetPosition', [at[0] + 250.0, at[1], at[2]], '0x14')
    time.sleep(1)
    if not raise_body(caster, skeever):
        return 'the %s game could not place the marker to cast from' % caster['name']
    return skeever, copy, time.time()


def judge(looks, after_hit):
    """The checks on one try's looks: the reason when they judge nothing, else a list of (name, ok, detail)."""
    if not any(v in ('raised', 'up') or f in ('raised', 'up') for _, v, f, _, _ in looks):
        return 'the skeever got up in neither game: the spell did not take'
    judged = [look for look in looks if look[0] >= SETTLE]
    if len(judged) < FEWEST_JUDGED:
        return 'only %d looks after the first %.0f s: DevBench stalled' % (len(judged), SETTLE)
    low = [(t, hv, hf) for t, _, _, hv, hf in judged if (hv is not None and hv <= 0) or (hf is not None and hf <= 0)]
    looks_after_hit = sorted({'%s/%s' % (v, f) for _, v, f, _, _ in after_hit})
    return [
        ('the raised skeever stays up in the caster\'s game', all(v == 'raised' for _, v, _, _, _ in judged), ', '.join(sorted({v for _, v, _, _, _ in judged}))),
        ('both games show the raised skeever the same way', all(v == f for _, v, f, _, _ in judged), '%d of %d looks differ' % (sum(v != f for _, v, f, _, _ in judged), len(judged))),
        ('the raised skeever keeps health in both games', not low, ('first at %.1f s: VR %s, flat %s' % low[0]) if low else 'above 0 in %d looks' % len(judged)),
        ('one light hit does not end it in either game', bool(after_hit) and all(v == 'raised' and f == 'raised' for _, v, f, _, _ in after_hit),
         ', '.join(looks_after_hit) or 'no look after the hit'),
    ]


def one_try(flat, vr, number, hit_after, own_body, caster_is_flat):
    """One raise, watched, hit once and watched again: the reason when the try is invalid, else its checks.

    hit_after: seconds after the cast for the hit, within the watch; None hits once the watch is over.
    own_body: the caster's game runs the body it raises; caster_is_flat: that caster is the flat game's player, else the VR player's.
    Without own_body, the flat game runs the body and the VR player raises its copy."""
    print('-- try %d' % number, flush=True)
    flat_offset, vr_offset = log_size(flat), log_size(vr)
    if not own_body:
        raised = raise_flat_body(flat, vr)
    elif caster_is_flat:
        raised = raise_own_body(flat, vr)
    else:
        raised = raise_own_body(vr, flat)
        raised = raised if isinstance(raised, str) else (raised[1], raised[0], raised[2])
    if isinstance(raised, str):
        return raised
    skeever, copy, cast_at = raised
    # The hit lands in the caster's game, on the body as that game has it.
    hit_game, hit_form = (flat, skeever) if own_body and caster_is_flat else (vr, copy)
    bodies = [skeever, copy]

    if hit_after is None:
        looks = watch(flat, vr, bodies, cast_at, WATCH)
        papyrus(hit_game, 'Actor', 'DamageActorValue', ['Health', HIT], hit_form)
        after_hit = watch(flat, vr, bodies, time.time(), AFTER_HIT)
    else:
        before_hit = watch(flat, vr, bodies, cast_at, hit_after)
        papyrus(hit_game, 'Actor', 'DamageActorValue', ['Health', HIT], hit_form)
        after_hit = watch(flat, vr, bodies, cast_at, WATCH)
        looks = before_hit + after_hit

    say_looks('after the raise, VR/flat (VR health, flat health)', looks)
    say_looks('after a hit of %.0f in the %s game' % (HIT, hit_game['name']), after_hit)
    say_log_lines(flat, flat_offset, [skeever])
    say_log_lines(vr, vr_offset, [copy])

    # Cleanup: the game that runs the skeever switches it off, and its copy goes with it.
    if own_body and not caster_is_flat:
        papyrus(vr, 'ObjectReference', 'Disable', [False], copy)
    else:
        papyrus(flat, 'ObjectReference', 'Disable', [False], skeever)
    return judge(looks, after_hit)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='once a fix exists, on the build without it: the run must be red')
    parser.add_argument('--tries', type=int, default=1, help='how many bodies are raised, one after the other')
    parser.add_argument('--hit-after', type=float, default=None, help='seconds after the cast for the hit; by default it comes once the watch is over')
    parser.add_argument('--own-body', action='store_true', help='the caster\'s game runs the body it raises')
    parser.add_argument('--caster', choices=('vr', 'flat'), default='vr', help='with --own-body: which game\'s player raises its own body')
    options = parser.parse_args()

    flat, vr = flat_game(), vr_game()
    for target in (flat, vr):
        if not connected(target):
            print('INVALID: the %s game is not connected (%s)' % (target['name'], target['log']))
            return 2
        close_menus(target)
    time.sleep(1)

    results = []
    for number in range(1, options.tries + 1):
        checks = one_try(flat, vr, number, options.hit_after, options.own_body, options.caster == 'flat')
        if isinstance(checks, str):
            print('   try %d INVALID: %s' % (number, checks), flush=True)
            continue
        for name, ok, detail in checks:
            print(('ok    ' if ok else 'FAIL  ') + name + ' (%s)' % detail, flush=True)
        results.append(all(ok for _, ok, _ in checks))
        time.sleep(3)

    if not results:
        print('INVALID: no try could be judged')
        return 2
    passed = all(results)
    print('%s: %d of %d tries passed every check (%d invalid)' % ('PASS' if passed else 'FAIL', sum(results), len(results), options.tries - len(results)))
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
