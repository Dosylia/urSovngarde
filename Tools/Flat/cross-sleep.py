"""Shared sleep and wait: time moves only once every player has confirmed the Sleep/Wait menu, and then by the same hours in both games.

    python Tools/Flat/cross-sleep.py                 both players confirm: both games rest, close the menu and end at the same hour
    python Tools/Flat/cross-sleep.py --midnight      both confirm a rest past midnight: each player's own date moves to the next day
    python Tools/Flat/cross-sleep.py --one           only the flat player confirms: no clock moves, and the vote runs out after a minute
    python Tools/Flat/cross-sleep.py --hold          the flat player waits in the vote's paused menu for 40 s: the cow its game runs stays its game's
    python Tools/Flat/cross-sleep.py --probe-hud     the VR player confirms alone: a picture of the VR game, for a person to see whether the vote's HUD line shows over the menu
    python Tools/Flat/cross-sleep.py --menu-line     the VR player confirms alone: both games' open menus show the vote in place of their question, read back from the menu
    python Tools/Flat/cross-sleep.py --one --expect-fail     on a build without the vote: must be RED
    python Tools/Flat/cross-sleep.py --sleep-off [--vr]     Gameplay:bEnableSleep off, as on the play servers: the flat (or VR) E press rests at once as in the game, the hour goes back
    ... --server-ini <path>     the config of the server the rig runs, when it is not the release folder's

Both rigs up and connected, the flat one first: Tools/Flat/flat-rig.ps1 up, then Tools/VR/headless.ps1 up -AutoConnect.
The rig server must run with Gameplay:bEnableSleep=true (build\\windows\\x64\\release\\config\\STServer.ini, set while it is stopped), or false for --sleep-off.
--menu-line reads the menu's text through the client's own log line, which reads the field back from the menu's movie after writing it;
whether a player sees it in the headset is for a play session to tell (the rig's pictures leave the menu out).
--sleep-off checks that what the player sees is unchanged where sleep is off: no red run exists for it, and it reads the new build's log lines.
Each game opens its Sleep/Wait menu through DevBench and presses the menu's OK with the E key, so each picks the menu's own default length.
The hour each player sees is the game's GameHour global, read through Papyrus; the date its GameDay and GameMonth, the days passed its GameDaysPassed.
After a shared rest the days passed must have grown by the agreed hours, which the build before the clock change lost.
--midnight needs the shared hour between 23:00 and 23:50 before it starts: move it there first, as a plugin's hour move does.
--hold places a cow by the flat player, so the flat game runs it and the VR game shows it; a paused game sends no movement, and the server hands
a silent game's actors to another game after 15 s. A player waiting in the vote must keep them: the server log's Handoff lines say who lost what.
It has never been red: both games keep sending movement while the menu pauses them, flat and VR (--vr), so no hand-off happens.
It stays as a guard in case that changes; it proves nothing about a fix.
A paused menu stops a game's clock, and the other game's clock runs on in real time, so a check allows the time the test itself takes.
With --one, a build without the vote moved both clocks as soon as the flat player confirmed: that is what goes red there.
"""
import argparse
import os
import re
import sys
import time

from cross_games import close_menus, connected, flat_game, form_id, log_since, log_size, papyrus, tool, vr_game

GAME_HOUR = '0x00000038'
GAME_DAY = '0x00000037'
GAME_MONTH = '0x00000036'
GAME_DAYS_PASSED = '0x00000039'
# The game's months, from the first (GameMonth 0).
DAYS_IN_MONTH = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
# The hours --midnight starts between, so that the menu's one hour crosses midnight.
MIDNIGHT_START = (23.0, 23.83)
SLEEP_WAIT_MENU = 'Sleep/Wait Menu'
# Game hours one real second adds at the default time scale of 20.
GAME_HOURS_PER_SECOND = 20.0 / 3600.0
# Two clocks count as one hour when they are this close, in game hours (six game minutes).
SAME_HOUR = 0.1
# The vote runs out a minute after the first confirmation; the notice may take a few seconds more.
VOTE_TIMEOUT = 60.0
TIMEOUT_GRACE = 8.0
# How long both games are given to rest an agreed hour and report it.
REST_WAIT = 25.0
# A rest that begins within this long of the press began at once, as in the game, in seconds.
AT_ONCE = 1.0
RELEASE_INI = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'windows', 'x64', 'release', 'config', 'STServer.ini')
SHOTS = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'windows', 'x64', 'release', 'logs', 'shots')
SERVER_LOG = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'windows', 'x64', 'release', 'logs', 'STServerOut.log')
# Skyrim.esm's cow: peaceful, so the Sleep/Wait menu still opens beside it.
COW = '0x00023A90'
# How long the flat player waits in the vote: past the server's 15 s silence and two of its 2 s hand-off sweeps, inside the vote's minute.
HOLD = 40.0
# How long the VR game is given to show the flat game's cow, in seconds.
COPY_WAIT = 15.0


def server_setting(ini, name):
    """One of the rig server's Gameplay switches from its config: True, False, or None when the config cannot be read."""
    try:
        with open(ini, encoding='utf-8', errors='replace') as f:
            found = re.search(r'^\s*' + name + r'\s*=\s*(true|false)\s*$', f.read(), re.MULTILINE | re.IGNORECASE)
    except OSError:
        return None
    return found.group(1).lower() == 'true' if found else None


def global_value(target, form):
    """One of that game's global variables, or None when it cannot be read."""
    value = papyrus(target, 'GlobalVariable', 'GetValue', [], form)
    return value if isinstance(value, (int, float)) else None


def game_hour(target):
    """The hour that game shows, 0 to 24, or None when it cannot be read."""
    return global_value(target, GAME_HOUR)


def game_date(target):
    """That game's date as (day, month), or None when it cannot be read."""
    day, month = global_value(target, GAME_DAY), global_value(target, GAME_MONTH)
    return (int(day), int(month)) if day is not None and month is not None else None


def next_day(date):
    """The date the day after (day, month), into the next month and the next year."""
    day, month = date
    if day < DAYS_IN_MONTH[month % 12]:
        return (day + 1, month)
    return (1, (month + 1) % 12)


def hours_between(before, after, at_least=0.0):
    """How far a clock moved from one hour to a later one, across midnight too.

    A move of a whole day reads as none on a clock; a caller that expects at least some hours passes them, and a whole day is added back."""
    moved = (after - before) % 24.0
    return moved + 24.0 if at_least > 12.0 and moved < at_least - 12.0 else moved


def menu_open(target):
    """Whether that game has its Sleep/Wait menu open."""
    return SLEEP_WAIT_MENU in (tool(target, 'menu', {'action': 'list'}) or {}).get('openMenus', [])


def press_ok(target):
    """That game's player presses the open menu's OK with the E key."""
    tool(target, 'input', {'action': 'down', 'device': 'keyboard', 'key': 'e', 'maxHoldMs': 200, 'owner': 'cross-sleep'})
    time.sleep(1.0)


def confirm_in_menu(target):
    """That game's player opens the Sleep/Wait menu and presses its OK with the E key; returns whether the menu opened.

    Each run ends with a withdrawal, after which the player waits half a minute: a press refused as too soon is pressed again once that wait is over."""
    tool(target, 'menu', {'action': 'open', 'name': SLEEP_WAIT_MENU})
    time.sleep(1.5)
    if not menu_open(target):
        return False
    offset = log_size(target)
    press_ok(target)
    refused = re.search(r'SleepVote: confirmed again too soon, (\d+) s to wait', log_since(target, offset))
    if refused:
        print('   (%s: pressed too soon after the last run, pressing again in %s s)' % (target['name'], refused.group(1)), flush=True)
        time.sleep(int(refused.group(1)) + 1.0)
        press_ok(target)
    return True


def line_time(text, pattern):
    """The time of the first log line matching a pattern, in seconds of the day, or None."""
    found = re.search(r'^\[\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d\.\d+)\][^\n]*' + pattern, text, re.MULTILINE)
    return int(found.group(1)) * 3600 + int(found.group(2)) * 60 + float(found.group(3)) if found else None


def seconds_between_lines(text, first, second):
    """How long after the first line matching one pattern the first line matching another came, or None."""
    start, end = line_time(text, first), line_time(text, second)
    return end - start if start is not None and end is not None else None


def server_log_size():
    """How far the rig server's log goes now, to read only what comes after; 0 when there is none yet."""
    return os.path.getsize(SERVER_LOG) if os.path.isfile(SERVER_LOG) else 0


def server_log_since(offset):
    """The rig server's log from an offset on."""
    with open(SERVER_LOG, 'rb') as f:
        f.seek(offset)
        return f.read().decode('utf-8', errors='replace')


def watch_rest(targets, offsets, patterns, seconds):
    """Waits until each game's log has every pattern since its offset, reading each game's hour about twice a second meanwhile.

    Returns the patterns still missing per game, and per game the largest step its hour took backwards, in hours: a clock that snaps back."""
    deadline = time.time() + seconds
    last = {t['name']: game_hour(t) for t in targets}
    backwards = {t['name']: 0.0 for t in targets}
    while True:
        missing = {t['name']: [p for p in patterns if not re.search(p, log_since(t, offsets[t['name']]))] for t in targets}
        if not any(missing.values()) or time.time() > deadline:
            return missing, backwards
        time.sleep(0.5)
        for target in targets:
            name, hour = target['name'], game_hour(target)
            if hour is None:
                continue
            if last[name] is not None:
                # A move forward reads as most of a day backwards, so only steps of under half a day count.
                step_back = (last[name] - hour) % 24.0
                if step_back < 12.0:
                    backwards[name] = max(backwards[name], step_back)
            last[name] = hour


def wait_for_lines(target, offset, patterns, seconds):
    """Waits until that game's log has every pattern since an offset, or the time is up; returns the patterns still missing."""
    deadline = time.time() + seconds
    while True:
        text = log_since(target, offset)
        missing = [p for p in patterns if not re.search(p, text)]
        if not missing or time.time() > deadline:
            return missing
        time.sleep(1.0)


def both_confirm(flat, vr, across_midnight=False):
    """Both players confirm: each game presses its own OK for the agreed hours, rests, ends at the same hour, and counts the days passed.

    Across midnight, each player's own date must move to the next day."""
    hours_before = {t['name']: game_hour(t) for t in (flat, vr)}
    if None in hours_before.values():
        return 'a game hour could not be read'
    if across_midnight and not all(MIDNIGHT_START[0] <= hour <= MIDNIGHT_START[1] for hour in hours_before.values()):
        return 'the shared hour must be between 23:00 and 23:50 first (flat %.2f, VR %.2f)' % (hours_before['flat'], hours_before['VR'])
    days_before = {t['name']: global_value(t, GAME_DAYS_PASSED) for t in (flat, vr)}
    dates_before = {t['name']: game_date(t) for t in (flat, vr)}
    if None in days_before.values() or None in dates_before.values():
        return 'a game date could not be read'
    offsets = {t['name']: log_size(t) for t in (flat, vr)}
    started = time.time()

    if not confirm_in_menu(flat):
        return 'the flat game did not open its Sleep/Wait menu'
    time.sleep(2.0)
    # The press is held: the menu stays open until the other player confirms.
    flat_held = menu_open(flat)
    if not confirm_in_menu(vr):
        return 'the VR game did not open its Sleep/Wait menu'

    rest_lines = [r'SleepVote: confirmed [\d.]+ h', r'SleepVote: everyone confirmed, skipping [\d.]+ h', r"SleepVote: pressing the game's own OK for [\d.]+ h",
                  r"SleepVote: the game's own rest began", r"SleepVote: slept, the game's own rest is over", r'SleepVote: time resync after the skip',
                  r'SleepVote: the clock follows the server again']
    missing, backwards = watch_rest((flat, vr), offsets, rest_lines, REST_WAIT)
    time.sleep(2.0)
    elapsed = time.time() - started
    hours_after = {t['name']: game_hour(t) for t in (flat, vr)}
    days_after = {t['name']: global_value(t, GAME_DAYS_PASSED) for t in (flat, vr)}
    dates_after = {t['name']: game_date(t) for t in (flat, vr)}
    if None in hours_after.values() or None in days_after.values() or None in dates_after.values():
        return 'a game hour or date could not be read after the rest'

    agreed = re.search(r'SleepVote: everyone confirmed, skipping ([\d.]+) h', log_since(flat, offsets['flat']))
    agreed_hours = float(agreed.group(1)) if agreed else None
    checks = []
    checks.append(('the flat menu stayed open while its player waited for the other', flat_held, 'menu list before the VR press'))
    for target in (flat, vr):
        name = target['name']
        checks.append(('the %s game confirmed, rested the agreed hours and woke' % name, not missing[name], 'missing: %s' % ', '.join(missing[name]) if missing[name] else 'all lines'))
        checks.append(('the %s game closed its Sleep/Wait menu' % name, not menu_open(target), 'menu list'))
        checks.append(('the %s clock never went back during the rest' % name, backwards[name] <= SAME_HOUR, 'largest step back %.2f h' % backwards[name]))
        moved = hours_between(hours_before[name], hours_after[name], agreed_hours or 0.0)
        # The agreed hours, and at most the test's own real time on top.
        expected = agreed_hours or 0.0
        checks.append(('the %s clock moved by the agreed %s h' % (name, agreed_hours), agreed_hours is not None and expected - SAME_HOUR <= moved <= expected + elapsed * GAME_HOURS_PER_SECOND + SAME_HOUR,
                       'moved %.2f h in %.0f s' % (moved, elapsed)))
        passed_hours = (days_after[name] - days_before[name]) * 24.0
        checks.append(('the %s days passed grew by the agreed hours' % name, agreed_hours is not None and expected - SAME_HOUR <= passed_hours <= expected + elapsed * GAME_HOURS_PER_SECOND + SAME_HOUR,
                       'grew %.2f h' % passed_hours))
        if across_midnight:
            checks.append(('the %s date moved to the next day' % name, dates_after[name] == next_day(dates_before[name]),
                           'day %d of month %d, then day %d of month %d' % (dates_before[name] + dates_after[name])))
    gap = hours_between(hours_after['VR'], hours_after['flat'])
    gap = min(gap, 24.0 - gap)
    checks.append(('both players see the same hour after', gap <= SAME_HOUR, 'flat %.2f, VR %.2f' % (hours_after['flat'], hours_after['VR'])))
    return checks


def only_one_confirms(flat, vr):
    """Only the flat player confirms: neither clock moves, the vote runs out and names the VR player, and the flat menu is left open."""
    hours_before = {t['name']: game_hour(t) for t in (flat, vr)}
    if None in hours_before.values():
        return 'a game hour could not be read'
    offset = log_size(flat)
    started = time.time()

    if not confirm_in_menu(flat):
        return 'the flat game did not open its Sleep/Wait menu'
    time.sleep(10.0)
    vr_early = game_hour(vr)
    early_elapsed = time.time() - started

    missing = wait_for_lines(flat, offset, [r'SleepVote: timed out, did not confirm: .+'], VOTE_TIMEOUT + TIMEOUT_GRACE)
    flat_after = game_hour(flat)
    menu_left_open = menu_open(flat)
    close_menus(flat)

    text = log_since(flat, offset)
    checks = []
    checks.append(('the flat game confirmed', re.search(r'SleepVote: confirmed [\d.]+ h', text) is not None, 'SleepVote: confirmed'))
    vr_moved = hours_between(hours_before['VR'], vr_early or hours_before['VR'])
    checks.append(('the VR clock did not jump while only the flat player confirmed', vr_early is not None and vr_moved <= early_elapsed * GAME_HOURS_PER_SECOND + SAME_HOUR,
                   'moved %.2f h in %.0f s' % (vr_moved, early_elapsed)))
    flat_moved = hours_between(hours_before['flat'], flat_after or hours_before['flat'])
    # The flat menu pauses that clock; the rest of the time it follows the shared one, which runs in real time.
    flat_elapsed = time.time() - started
    checks.append(('the flat clock did not jump', flat_after is not None and flat_moved <= flat_elapsed * GAME_HOURS_PER_SECOND + SAME_HOUR,
                   'moved %.2f h in %.0f s' % (flat_moved, flat_elapsed)))
    checks.append(('the vote ran out and named who did not confirm', not missing, 'timed out line' if not missing else 'no timed out line'))
    checks.append(('the flat game pressed no OK of its own', "pressing the game's own OK" not in text, 'no own OK'))
    checks.append(('the flat menu was released, not closed', menu_left_open, 'menu list before closing it'))
    return checks


def hold_in_vote(waiter, other):
    """One player waits in the vote's paused menu longer than a silent game keeps its actors: no actor may leave the waiting game meanwhile."""
    other_offset = log_size(other)
    placed = form_id(papyrus(waiter, 'ObjectReference', 'PlaceAtMe', [{'form': COW}, 1, False, False], '0x14'))
    if not placed:
        return 'the %s game placed no cow' % waiter['name']
    # The other game logs the copy it builds: a temporary actor (an FF id) of that name.
    if wait_for_lines(other, other_offset, [r'Spawn Actor: FF[0-9A-F]+, and NPC Cow'], COPY_WAIT):
        return "the %s game shows no copy of the %s game's cow" % (other['name'], waiter['name'])

    waiter_offset, server_offset = log_size(waiter), server_log_size()
    if not confirm_in_menu(waiter):
        return 'the %s game did not open its Sleep/Wait menu' % waiter['name']
    time.sleep(HOLD)
    held = menu_open(waiter)
    handoffs = [line for line in server_log_since(server_offset).splitlines() if 'Handoff:' in line and 'silent' in line]
    waiter_text = log_since(waiter, waiter_offset)
    close_menus(waiter)
    papyrus(waiter, 'ObjectReference', 'Delete', [], placed)

    # The client logs a gap in its movement sends once it sends again, so a waiting game that went quiet says so.
    gaps = [line for line in log_since(waiter, waiter_offset).splitlines() if 'Silence: sent nothing for' in line]
    name = waiter['name']
    checks = []
    checks.append(('the %s game confirmed' % name, re.search(r'SleepVote: confirmed [\d.]+ h', waiter_text) is not None, 'SleepVote: confirmed'))
    checks.append(('the %s menu held the vote for %.0f s' % (name, HOLD), held, 'menu list at the end of the wait'))
    checks.append(('no actor left the waiting %s game' % name, not handoffs, '%d silent hand-offs: %s' % (len(handoffs), handoffs[0].strip()[-160:]) if handoffs else 'no silent hand-off'))
    print('   (%s game while waiting: %s)' % (name, gaps[-1].strip()[-120:] if gaps else 'no gap in its movement sends'), flush=True)
    return checks


def probe_hud(vr):
    """The VR player confirms alone and waits: a picture of the VR game while its line about the vote should be up, for a person to judge.

    The script cannot tell whether the line is seen over the open menu; the picture says, as far as the game's own capture shows the headset's view."""
    offset = log_size(vr)
    if not confirm_in_menu(vr):
        return 'the VR game did not open its Sleep/Wait menu'
    # The HUD line comes with the server's answer to the confirmation and stays up about two seconds.
    shown = not wait_for_lines(vr, offset, [r"SleepVote: '.+' confirmed, \d+/\d+ ready"], 3.0)
    os.makedirs(SHOTS, exist_ok=True)
    answer = tool(vr, 'capture', {'kind': 'native', 'checkpointId': 'cross-sleep-hud', 'cleanup': True, 'outDir': os.path.abspath(SHOTS)}, timeout=40)
    close_menus(vr)
    path = answer.get('path') if isinstance(answer, dict) else None

    checks = []
    checks.append(('the VR game confirmed and heard the vote\'s answer', shown, 'waiting line'))
    checks.append(('a picture of the VR game was taken for a person to look at', bool(path) and os.path.isfile(path), path or str(answer)[:160]))
    return checks


def menu_line(flat, vr):
    """The VR player confirms alone: its menu shows whom the vote waits for, the flat player's open menu shows the count, and the question comes back once the vote closes."""
    offsets = {t['name']: log_size(t) for t in (flat, vr)}
    if not confirm_in_menu(vr):
        return 'the VR game did not open its Sleep/Wait menu'
    vr_missing = wait_for_lines(vr, offsets['VR'], [r"SleepVote: the menu shows 'Waiting for .+ \(1/2\)'"], 5.0)

    # The flat player opens the menu without confirming.
    tool(flat, 'menu', {'action': 'open', 'name': SLEEP_WAIT_MENU})
    flat_missing = wait_for_lines(flat, offsets['flat'], [r"SleepVote: the menu shows '1/2 ready to rest [\d.]+ h'"], 5.0)

    # The VR player closes the menu: its vote is withdrawn, nobody is ready, and the flat menu shows its question again.
    close_menus(vr)
    back_missing = wait_for_lines(flat, offsets['flat'], [r"SleepVote: the menu's question is back, '.+'"], 5.0)
    close_menus(flat)

    shown = {t['name']: re.findall(r"SleepVote: the menu shows '([^']*)'", log_since(t, offsets[t['name']])) for t in (flat, vr)}
    back = re.findall(r"SleepVote: the menu's question is back, '([^']*)'", log_since(flat, offsets['flat']))
    checks = []
    checks.append(('the VR menu shows whom the vote waits for', not vr_missing, shown['VR'][-1] if shown['VR'] else 'nothing read back'))
    checks.append(('the flat menu shows the count to a player not yet in the vote', not flat_missing, shown['flat'][-1] if shown['flat'] else 'nothing read back'))
    checks.append(('the flat menu shows its question again once nobody is ready', not back_missing, back[-1] if back else 'nothing read back'))
    return checks


def sleep_switched_off(presser, other):
    """Gameplay:bEnableSleep off: one player's E press rests at once as in the game, with no wait on a vote, the menu closes, and the hour goes back to the shared one."""
    hours_before = {t['name']: game_hour(t) for t in (presser, other)}
    if None in hours_before.values():
        return 'a game hour could not be read'
    offset = log_size(presser)
    started = time.time()

    if not confirm_in_menu(presser):
        return 'the %s game did not open its Sleep/Wait menu' % presser['name']
    missing = wait_for_lines(presser, offset, [r"SleepVote: the game's own rest began", r"SleepVote: slept, the game's own rest is over"], REST_WAIT)
    time.sleep(3.0)
    elapsed = time.time() - started
    hours_after = {t['name']: game_hour(t) for t in (presser, other)}
    if None in hours_after.values():
        return 'a game hour could not be read after the rest'

    text = log_since(presser, offset)
    # A wait keeps the menu open while its hours count down, as in the game; it closes when the rest ends.
    delay = seconds_between_lines(text, r'SleepVote: confirmed [\d.]+ h', r"SleepVote: the game's own rest began")
    checks = []
    checks.append(('the rest began at once, with no wait on a vote', delay is not None and delay <= AT_ONCE, 'began %s s after the press' % ('%.2f' % delay if delay is not None else '?')))
    checks.append(('the %s game rested on its own and woke' % presser['name'], not missing, 'missing: %s' % ', '.join(missing) if missing else 'began and slept lines'))
    checks.append(('the %s menu closed when the rest ended' % presser['name'], not menu_open(presser), 'menu list after the rest'))
    checks.append(('no vote was agreed or ran out', 'SleepVote: everyone confirmed' not in text and 'SleepVote: timed out' not in text, 'no vote lines'))
    drift = elapsed * GAME_HOURS_PER_SECOND + SAME_HOUR
    for target in (presser, other):
        name = target['name']
        moved = hours_between(hours_before[name], hours_after[name])
        checks.append(('the %s clock is back on the shared hour' % name, moved <= drift, 'moved %.2f h in %.0f s' % (moved, elapsed)))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--one', action='store_true', help='only the flat player confirms: no clock moves, and the vote runs out')
    parser.add_argument('--hold', action='store_true', help='the flat player waits in the vote\'s paused menu for 40 s: the cow its game runs stays its game\'s')
    parser.add_argument('--menu-line', action='store_true', help='the VR player confirms alone: both open menus show the vote, read back from the menu')
    parser.add_argument('--probe-hud', action='store_true', help='the VR player confirms alone: a picture of the VR game for a person to judge the HUD line')
    parser.add_argument('--midnight', action='store_true', help='both confirm a rest past midnight: each player\'s own date moves to the next day')
    parser.add_argument('--sleep-off', action='store_true', help='Gameplay:bEnableSleep off: the E press rests at once as in the game, and the hour goes back')
    parser.add_argument('--vr', action='store_true', help='with --sleep-off or --hold: the VR player presses or waits instead of the flat one')
    parser.add_argument('--expect-fail', action='store_true', help='on the build without the vote: the run must be red')
    parser.add_argument('--server-ini', default=RELEASE_INI, help='the config of the server the rig runs')
    options = parser.parse_args()

    wanted = not options.sleep_off
    if server_setting(options.server_ini, 'bEnableSleep') is not wanted:
        print('INVALID: the rig server needs Gameplay:bEnableSleep=%s in %s' % ('true' if wanted else 'false', os.path.normpath(options.server_ini)))
        return 2
    if options.midnight:
        # With a shared calendar the server sends the date, so only personal calendars show a lost day.
        shared = server_setting(options.server_ini, 'bSyncPlayerCalendar')
        print('Calendar: %s (Gameplay:bSyncPlayerCalendar=%s)' % ('shared' if shared else 'personal', 'true' if shared else 'false'), flush=True)
    flat, vr = flat_game(), vr_game()
    for target in (flat, vr):
        if not connected(target):
            print('INVALID: the %s game is not connected (%s)' % (target['name'], target['log']))
            return 2
        close_menus(target)
    time.sleep(1)

    if options.sleep_off:
        checks = sleep_switched_off(vr, flat) if options.vr else sleep_switched_off(flat, vr)
    elif options.one:
        checks = only_one_confirms(flat, vr)
    elif options.hold:
        checks = hold_in_vote(vr, flat) if options.vr else hold_in_vote(flat, vr)
    elif options.probe_hud:
        checks = probe_hud(vr)
    elif options.menu_line:
        checks = menu_line(flat, vr)
    else:
        checks = both_confirm(flat, vr, options.midnight)
    for target in (flat, vr):
        close_menus(target)
    if isinstance(checks, str):
        print('INVALID: %s' % checks)
        return 2

    for name, ok, detail in checks:
        print(('ok    ' if ok else 'FAIL  ') + name + ' (%s)' % detail, flush=True)
    passed = all(ok for _, ok, _ in checks)
    print('PASS' if passed else 'FAIL')
    if options.expect_fail:
        print('RED as expected' if not passed else 'NOT RED: the unfixed build passes, so this tests nothing')
        return 0 if not passed else 1
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
