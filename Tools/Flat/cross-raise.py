"""A body raised by the player whose game does not run it: both games must show the raised creature standing, with health, through a hit.

    python Tools/Flat/cross-raise.py [--tries <n>] [--hit-after <seconds>] [--own-body [--caster vr|flat]]
    python Tools/Flat/cross-raise.py --strong [--tries <n>]     a body too strong for the spell: raised in neither game
    python Tools/Flat/cross-raise.py --expect-fail     once a fix exists, on the build without it: must be RED

Both rigs up and connected, the flat one first so that it leads the party: Tools/Flat/flat-rig.ps1 up, then Tools/VR/headless.ps1 up -AutoConnect.
The flat game places a skeever ahead of its player and kills it, so the flat game runs that body and the VR game shows a copy of it.
The VR player then raises that copy with Raise Zombie, as a player who joined raises a body the host's game runs.
The spell is launched by RemoteCast from a marker just above the body, with the VR player as its caster.
A VR spell cast by the player flies where the rig's hand points, which is not at a body on the ground.
For the next seconds both games are read: raised (alive and commanded by the player), up (alive), dead (lying, even when commanded), or gone (switched off: a raised body that ends turns to ash).
A body counts as standing only when Papyrus has it alive: one commanded but dead lies on the ground, as a copy did when it ran the raise itself.
Only the game that runs the body raises it; a copy gets up when that game says it lives, so it reads up, not raised.
The skeever is Skyrim.esm's own, not levelled, so both games show the same creature; Raise Zombie raises one that weak.
Its health is read in both games: a raised body left at 0 health dies at its first hit and turns to ash in that game only.
After the watch the VR player's game hits it once, lightly, and it must still stand in both games.
At the end of the watch it prints whether each game has it hostile to that game's player (Papyrus IsHostileToActor), without judging it.
A copy commanded by this game's player still reads hostile, where a creature raised in that game does not; what that means in play is not shown.
A body is raised again in a new try with --tries, as what takes its health may catch only some raises.
With --strong the flat game's body is a bear (EncBear, level 12), too strong for Raise Zombie (level 6).
The VR game must refuse the raise, as the game refuses it on a body it runs, and send nothing; the bear must stay dead in both games.
"""
import argparse
import math
import re
import sys
import time

from cross_games import close_menus, connected, flat_game, form_id, log_since, log_size, nearest_temporary_actor, papyrus, position, vr_game

SKEEVER = '0x00023AB7'
# Skyrim.esm's EncBear, a fixed level 12, above the level 6 Raise Zombie raises.
BEAR = '0x00023A8A'
RAISE_ZOMBIE = '0x0007E8E1'
XMARKER = '0x0000003B'
# How long the raised skeever is watched; the spell lasts 60 s.
WATCH = 12.0
# The first moments after the cast, while the body gets up and its copy follows, are not judged.
SETTLE = 4.0
# Fewer looks than this after SETTLE judge nothing: DevBench stalled (a look takes about a second, so a watch has about 9).
FEWEST_JUDGED = 6
# The hit after the watch, well under the skeever's health of about 15, and how long both games are watched after it.
HIT = 5.0
AFTER_HIT = 4.0
# The looks that have the body on its feet.
STANDING = ('raised', 'up')


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


def place_dead_creature(target, base=SKEEVER):
    """A creature (a skeever unless told) that game places 200 units ahead of its player, at that spot from the start, and kills; returns its form id, or None.

    Placed where it stays, its copy in the other game falls where this game's body lies."""
    spot = ahead_of(target, 200.0)
    marker = marker_at(target, spot) if spot else None
    creature = form_id(papyrus(target, 'ObjectReference', 'PlaceAtMe', [{'form': base}, 1, False, False], marker)) if marker else None
    if not creature:
        return None
    time.sleep(4)
    papyrus(target, 'Actor', 'DamageActorValue', ['Health', 10000.0], creature)
    papyrus(target, 'ObjectReference', 'Disable', [False], marker)
    return creature


def state(target, form):
    """'raised', 'dead', 'up' or 'gone' for an actor in that game; gone when it is switched off, has no 3D, or is no longer there; dead when it lies, commanded or not."""
    if papyrus(target, 'ObjectReference', 'IsDisabled', [], form) is not False:
        return 'gone'
    if papyrus(target, 'ObjectReference', 'Is3DLoaded', [], form) is False:
        return 'gone'
    dead = papyrus(target, 'Actor', 'IsDead', [], form)
    if dead is True:
        return 'dead'
    if dead is not False:
        return 'gone'
    return 'raised' if papyrus(target, 'Actor', 'IsCommandedActor', [], form) is True else 'up'


def hostile_to_player(target, form):
    """Whether an actor in that game is hostile to that game's player, or None when it cannot be read."""
    hostile = papyrus(target, 'Actor', 'IsHostileToActor', [{'form': '0x14'}], form)
    return hostile if isinstance(hostile, bool) else None


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


def say_log_lines(target, offset, forms):
    """Prints the log lines since an offset that speak of a death or a life, or of one of the forms."""
    wanted = [f.upper().replace('0X', '') for f in forms]
    for line in log_since(target, offset).splitlines():
        if 'Death sync' in line or 'Reanimat' in line or 'Respawn' in line or any(w and w in line.upper() for w in wanted):
            print('   %s: %s' % (target['name'], line.strip()[:230]), flush=True)


def base_of(target, form):
    """The form id of an actor's base in that game, or None."""
    return form_id(papyrus(target, 'Actor', 'GetActorBase', [], form))


def dead_copy_near(target, at, owner, body):
    """Waits up to 10 s for that game's dead copy of a body another game runs, near where that game has it; returns (form id or None, whether it is dead).

    The owner's player's copy is left out, and so are actors switched off nearby or of another creature: bodies of earlier tries.
    A copy that has no 3D yet is waited for."""
    body_base = base_of(owner, body)
    owner_player_copy = nearest_temporary_actor(target, position(owner) or [0, 0, 0], 150.0)
    leave_out = [owner_player_copy] if owner_player_copy else []
    found = None
    end = time.time() + 10.0
    while time.time() < end:
        found = nearest_temporary_actor(target, at, 300.0, leave_out)
        look = state(target, found) if found else None
        if look and base_of(target, found) != body_base:
            leave_out.append(found)
        elif look == 'dead':
            return found, True
        elif look == 'gone' and papyrus(target, 'ObjectReference', 'IsDisabled', [], found) is True:
            leave_out.append(found)
        time.sleep(0.5)
    return found, False


def raise_flat_body(flat, vr, base=SKEEVER):
    """Places a dead creature (a skeever unless told) in the flat game and raises its copy from the VR game.

    Returns (the flat game's body, copy, cast time), or the reason the try is invalid."""
    skeever = place_dead_creature(flat, base)
    at = position(flat, skeever) if skeever else None
    if not at:
        return 'the flat game placed no skeever'
    vr_offset = log_size(vr)
    copy, dead = dead_copy_near(vr, at, flat, skeever)
    if not dead:
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
    skeever = place_dead_creature(caster)
    at = position(caster, skeever) if skeever else None
    if not at:
        return 'the %s game placed no skeever' % caster['name']
    copy, dead = dead_copy_near(other, at, caster, skeever)
    if not dead:
        return 'the %s game shows no dead copy of the %s game\'s skeever %s (copy %s)' % (other['name'], caster['name'], skeever, copy)
    print('   %s skeever %s, its copy in %s %s, both dead' % (caster['name'], skeever, other['name'], copy), flush=True)

    papyrus(caster, 'ObjectReference', 'SetPosition', [at[0] + 250.0, at[1], at[2]], '0x14')
    time.sleep(1)
    if not raise_body(caster, skeever):
        return 'the %s game could not place the marker to cast from' % caster['name']
    return skeever, copy, time.time()


def judge(looks, after_hit, owner_is_vr):
    """The checks on one try's looks: the reason when they judge nothing, else a list of (name, ok, detail).

    owner_is_vr: the VR game runs the body, else the flat game does."""
    if not any(v in STANDING or f in STANDING for _, v, f, _, _ in looks):
        return 'the skeever got up in neither game: the spell did not take'
    judged = [look for look in looks if look[0] >= SETTLE]
    if len(judged) < FEWEST_JUDGED:
        return 'only %d looks after the first %.0f s: DevBench stalled' % (len(judged), SETTLE)
    low = [(t, hv, hf) for t, _, _, hv, hf in judged if (hv is not None and hv <= 0) or (hf is not None and hf <= 0)]
    looks_after_hit = sorted({'%s/%s' % (v, f) for _, v, f, _, _ in after_hit})
    lying = [(t, v, f) for t, v, f, _, _ in judged if v not in STANDING or f not in STANDING]
    return [
        ('the raised skeever stands in both games', not lying, ('%d of %d looks; first at %.1f s: VR %s, flat %s' % ((len(lying), len(judged)) + lying[0])) if lying else 'standing in %d looks' % len(judged)),
        ('the game that runs it has it raised', any((v if owner_is_vr else f) == 'raised' for _, v, f, _, _ in judged),
         'the %s game, which runs it: %s' % ('VR' if owner_is_vr else 'flat', ', '.join(sorted({(v if owner_is_vr else f) for _, v, f, _, _ in judged})))),
        ('the raised skeever keeps health in both games', not low, ('first at %.1f s: VR %s, flat %s' % low[0]) if low else 'above 0 in %d looks' % len(judged)),
        ('one light hit does not end it in either game', bool(after_hit) and all(v in STANDING and f in STANDING for _, v, f, _, _ in after_hit),
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
        # Timed from the cast: the looks before the body got up say nothing of the hit.
        after_hit = [look for look in after_hit if look[0] >= SETTLE]
    hostile = (hostile_to_player(vr, bodies[1]), hostile_to_player(flat, bodies[0]))

    say_looks('after the raise, VR/flat (VR health, flat health)', looks)
    say_looks('after a hit of %.0f in the %s game' % (HIT, hit_game['name']), after_hit)
    print('   hostile to that game\'s player at the end of the watch (not judged): VR %s, flat %s' % hostile, flush=True)
    say_log_lines(flat, flat_offset, [skeever])
    say_log_lines(vr, vr_offset, [copy])

    # Cleanup: the game that runs the skeever switches it off, and its copy goes with it.
    if own_body and not caster_is_flat:
        papyrus(vr, 'ObjectReference', 'Disable', [False], copy)
    else:
        papyrus(flat, 'ObjectReference', 'Disable', [False], skeever)
    return judge(looks, after_hit, own_body and not caster_is_flat)


def strong_try(flat, vr, number):
    """The VR player raises the copy of a bear the flat game runs, too strong for Raise Zombie: the reason when the try is invalid, else its checks."""
    print('-- try %d: a bear (level 12) against Raise Zombie (level 6)' % number, flush=True)
    vr_offset = log_size(vr)
    raised = raise_flat_body(flat, vr, BEAR)
    if isinstance(raised, str):
        return raised
    bear, copy, cast_at = raised
    looks = watch(flat, vr, [bear, copy], cast_at, WATCH)
    say_looks('after the raise, VR/flat (VR health, flat health)', looks)
    vr_log = log_since(vr, vr_offset).upper()
    short = copy.upper().replace('0X', '')
    refused = ('RAISE ON COPY %s IS REFUSED BY THE GAME HERE' % short) in vr_log
    sent = ('RAISE ON COPY %s IS SENT TO ITS OWNER' % short) in vr_log
    papyrus(flat, 'ObjectReference', 'Disable', [False], bear)
    judged = [look for look in looks if look[0] >= SETTLE]
    # A build without these log lines shows the raise only by the bear getting up.
    if not refused and not sent and not any(v != 'dead' or f != 'dead' for _, v, f, _, _ in judged):
        return 'no sign that the spell reached the copy %s' % copy
    risen = [(t, v, f) for t, v, f, _, _ in judged if v != 'dead' or f != 'dead']
    return [
        ('the VR game refuses the raise and sends nothing', refused and not sent, 'refused here' if refused else 'sent to the flat game' if sent else 'no log line of the refusal'),
        ('the bear stays dead in both games', not risen, ('first at %.1f s: VR %s, flat %s' % risen[0]) if risen else 'dead in %d looks' % len(judged)),
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true', help='once a fix exists, on the build without it: the run must be red')
    parser.add_argument('--tries', type=int, default=1, help='how many bodies are raised, one after the other')
    parser.add_argument('--hit-after', type=float, default=None, help='seconds after the cast for the hit; by default it comes once the watch is over')
    parser.add_argument('--own-body', action='store_true', help='the caster\'s game runs the body it raises')
    parser.add_argument('--caster', choices=('vr', 'flat'), default='vr', help='with --own-body: which game\'s player raises its own body')
    parser.add_argument('--strong', action='store_true', help='the body is a bear, too strong for the spell: raised in neither game')
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
        if options.strong:
            checks = strong_try(flat, vr, number)
        else:
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
