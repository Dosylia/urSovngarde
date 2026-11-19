"""Types with the page's own keyboard on the in-game menu panel, aiming and pulling the trigger as a player does.

    python Tools/VR/live-menu-keyboard.py

The VR rig must be up (headless.ps1 up) in a loaded save, with the page that has the keyboard deployed.
Checks: a pull on the chat field gives it the focus and brings up the keyboard; pulls on h and i type "hi" there and the
field keeps the focus; backspace takes the i away; the close key lets the field go and hides the keyboard.
"""
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from plugin_call import call  # noqa: E402
import play  # noqa: E402

spec = importlib.util.spec_from_file_location('menu_laser', os.path.join(HERE, 'live-menu-laser.py'))
laser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(laser)
shared = laser.shared

CHAT = "document.querySelector('.app-chat-input input')"


def centre_of(page, selector_js):
    """The element's centre in page pixels, or None."""
    rect = page.evaluate(f'(() => {{ const r = {selector_js}?.getBoundingClientRect(); return r && r.width ? [r.left + r.width / 2, r.top + r.height / 2] : null; }})()')
    return (rect[0] * laser.ZOOM, rect[1] * laser.ZOOM) if rect else None


def pull_at(game, point):
    """Aims at a page pixel and pulls the trigger once, in one sequence."""
    rotation = laser.aim_rotation(laser.panel_point(*point))
    laser.perform(game, [(rotation, 0, (0.0, 0.0), 0.6), (rotation, laser.TRIGGER, (0.0, 0.0), 0.3), (rotation, 0, (0.0, 0.0), 0.4)])


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    pid = laser.menu.game_pid()
    if not pid:
        print('INVALID: urSovngarde.exe is not running')
        return 2
    game = play.Player(8921)
    page = shared.DevTools()
    if not laser.open_panel(game, pid):
        print('INVALID: the menu did not open on the panel')
        return 2
    page.evaluate(f'(() => {{ const i = {CHAT}; if (i) {{ i.value = ""; i.dispatchEvent(new Event("input")); i.blur(); }} return true; }})()')
    time.sleep(0.5)

    chat = centre_of(page, CHAT)
    if not check('the panel has the chat field', bool(chat)):
        return 1
    pull_at(game, chat)
    focused = shared.wait(lambda: page.evaluate(f'document.activeElement === {CHAT}'), 3)
    check('a pull on the chat field gives it the focus', focused)
    shown = shared.wait(lambda: page.evaluate("!!document.querySelector('.app-keyboard')"), 3)
    if not check('the keyboard comes up for it', shown):
        return 1

    for key in ('h', 'i'):
        spot = centre_of(page, f'document.querySelector(\'.app-keyboard-key[data-key="{key}"]\')')
        if spot:
            pull_at(game, spot)
    value = page.evaluate(f'{CHAT}.value')
    check('pulls on h and i type "hi"', value == 'hi', repr(value))
    check('the field keeps the focus while typing', page.evaluate(f'document.activeElement === {CHAT}'))

    backspace = centre_of(page, 'document.querySelector(\'.app-keyboard-key[data-key="backspace"]\')')
    if backspace:
        pull_at(game, backspace)
    value = page.evaluate(f'{CHAT}.value')
    check('backspace takes the last letter away', value == 'h', repr(value))

    close = centre_of(page, 'document.querySelector(\'.app-keyboard-key[data-key="close"]\')')
    if close:
        pull_at(game, close)
    gone = shared.wait(lambda: page.evaluate("!document.querySelector('.app-keyboard')"), 3)
    check('the close key hides the keyboard and lets the field go', gone and not page.evaluate(f'document.activeElement === {CHAT}'))

    call(pid, 'urSovngarde_SetMenuOpen', 0)
    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
