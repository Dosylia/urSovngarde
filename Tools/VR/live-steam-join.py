"""Joins a Steam friend's server from the game, no launcher, on the rig, with the rig's own Steam account as the friend.

    python Tools/VR/live-steam-join.py

The rig must be up and connected to its local server (headless.ps1 up), with no launcher writing launcher.json.
The rig has one Steam account, so it joins itself: its game shows its own server in Steam's rich presence, and the
join reads that back from Steam, picks the address this PC reaches it on and connects there. That is the whole chain
a friend's game runs, from one PC; only the friends list itself needs two accounts.

Checks: the game shows its own server to Steam friends; the join leaves the server and connects at once by the home
network; the panel without a launcher has the Steam friends section and says codes need the launcher;
the menu's setting takes the server out of Steam and puts it back.
"""
import importlib.util
import os

import sys
import time
import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('launcher_join', os.path.join(HERE, 'live-launcher-join.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)

SHOWN = 'SteamFriends: Steam friends see this player\'s own server'
HIDDEN = 'SteamFriends: Steam friends no longer see where this player plays'


def own_steam_id():
    """The signed-in account, as a 64-bit Steam id; 0 when Steam has none."""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam\ActiveProcess') as key:
            account = winreg.QueryValueEx(key, 'ActiveUser')[0]
    except OSError:
        return 0
    return 76561197960265728 + account if account else 0


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(bool(ok))
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    if os.path.isfile(shared.STATE):
        print('INVALID: launcher.json is there; this test is for a player without the launcher')
        return 2
    whole_log = shared.log_since(0)
    if whole_log.rfind('Connected: the server accepted this player') <= whole_log.rfind('Disconnected from server'):
        print('INVALID: the game is not connected to the rig server now')
        return 2
    steam_id = own_steam_id()
    if not steam_id:
        print('INVALID: no Steam account signed in (HKCU\\Software\\Valve\\Steam\\ActiveProcess\\ActiveUser)')
        return 2
    try:
        page = shared.DevTools()
    except (OSError, StopIteration, ConnectionError) as error:
        print(f'INVALID: no page on the debugging port ({error})')
        return 2

    if not check('the game shows its own server to Steam friends', shared.wait(lambda: SHOWN in shared.log_since(0), 20)):
        return 1

    offset = shared.log_size()
    try:
        page.evaluate(f"skyrimtogether.joinSteamFriend('{steam_id}', '')")
    except RuntimeError as error:
        check('the menu can join a Steam friend', False, str(error))
        return 1

    # In order: the friend's server read from Steam, by which route, the old server left, one attempt, accepted.
    # The address itself stays out of the log; on one PC the route must be the home network.
    def rejoined():
        text = shared.log_since(offset)
        if "SteamFriends: a Steam friend's server can't be reached from here" in text:
            return 'unreachable'
        start = text.find("SteamFriends: joining a Steam friend's server by ")
        left = text.find('Disconnected from server', start)
        attempt = text.find("VRConnectService: connecting to a Steam friend's server (attempt 1)", max(left, 0))
        accepted = text.find('Connected: the server accepted this player', max(attempt, 0))
        if min(start, left, attempt, accepted) < 0:
            return None
        if 'trying again in' in text[start:accepted] or 'trying its next address' in text[start:accepted]:
            return 'retried'
        return text[start:].splitlines()[0].rsplit(' by ', 1)[1].strip()
    outcome = shared.wait(rejoined, 40)
    check('the join leaves the server and connects at once, by the home network', outcome == 'lan', outcome or 'nothing within 40 s')

    find_button = "Array.from(document.querySelectorAll('.app-root-menu button')).find(b => b.textContent.includes('Join a friend'))"
    page.evaluate(f'{find_button}?.click()')
    text = shared.wait(lambda: page.evaluate("OPEN_JOIN_JS?.textContent ?? ''"), 10) or ''
    check('the panel without a launcher has the Steam friends section', 'Steam friends playing urSovngarde' in text)
    check('the panel says codes need the launcher', 'needs the urSovngarde launcher' in text and not page.evaluate("!!OPEN_JOIN_JS?.querySelector('input[type=text]')"))
    page.evaluate("Array.from((OPEN_JOIN_JS?.querySelectorAll('button') ?? [])).find(b => b.textContent.includes('Close'))?.click()")

    offset = shared.log_size()
    page.evaluate('skyrimtogether.setShareWithSteamFriends(false)')
    check('the setting off takes the server out of Steam', shared.wait(lambda: HIDDEN in shared.log_since(offset), 15))
    offset = shared.log_size()
    page.evaluate('skyrimtogether.setShareWithSteamFriends(true)')
    check('the setting on puts it back', shared.wait(lambda: SHOWN in shared.log_since(offset), 15))

    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
