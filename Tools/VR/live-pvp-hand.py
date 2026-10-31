"""Runs live-pvp-hand-aim: a real held spell, from the rig's virtual hands, at the bot.

    python Tools/VR/live-pvp-hand.py [spell hex, default 12FCD]

The rig must be up. It starts live-check on the script, waits for its cue, then holds the trigger with the hands
turned by play.py's body. The rig's spell mods turn a hand's aim by an amount that differs between modlists, so the
hand is steered by the aim the bot says it receives until that points at the bot.
A real hit on the bot's copy then shows that the aim the game sends is where the real spell goes.
The checks are in the script.
"""
import math
import os
import re
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import play  # noqa: E402

CUE = 'hand cast now'
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BOT_LOG = os.path.join(ROOT, 'build', 'windows', 'x64', 'release', 'logs', 'live-pvp-hand-aim-bot.log')
AIM = re.compile(r'Spell aim of the player at \w+: heading (-?[\d.]+) degrees, pitch (-?[\d.]+); this bot is at heading (-?[\d.]+)')


def nearest_other(player):
    me = player.position()
    best = None
    for r in player.actors():
        if r['formId'] == play.PLAYER_FORM:
            continue
        d = math.dist(me, r['position'])
        if d < 600 and (best is None or d < best[0]):
            best = (d, r)
    return best[1] if best else None


def last_aim():
    """The newest aim the bot read: heading, pitch and the bot's own heading, in degrees."""
    try:
        with open(BOT_LOG, encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except OSError:
        return None
    for line in reversed(lines):
        m = AIM.search(line)
        if m:
            return tuple(float(x) for x in m.groups())
    return None


def turn(a, b):
    """The short way from heading b to heading a, in degrees."""
    return (a - b + 180) % 360 - 180


def cast_at(player, spell, seconds):
    body = play.Body(player)
    body.start()
    time.sleep(1.5)
    room = player.heading()
    target = nearest_other(player)
    if not target:
        print('   hand: no copy near', flush=True)
        body.finish()
        return
    me = player.position()
    at = target['position']
    print('   hand: copy %s at %.0f units' % (target['formId'], math.dist(me, at)), flush=True)
    # The body presses both triggers, and on a shield that is a block, which stops the cast at a target in front.
    player.console('player.unequipitem 4F912')
    player.console('player.addspell %s' % spell)
    player.console('player.equipspell %s right' % spell)
    player.console('player.setav magicka 30000')
    player.console('player.restoreav magicka 100000')
    time.sleep(1)

    body.face(room, math.degrees(math.atan2(at[0] - me[0], at[1] - me[1])) % 360)
    flat = math.hypot(at[0] - me[0], at[1] - me[1])
    body.hand_yaw = body.yaw + play.SPELL_LEFT
    body.pitch = math.atan2((at[2] + 60) - (me[2] + 84), flat) + play.SPELL_UP
    time.sleep(1)
    body.cast = True

    # A hand's yaw turns its aim the opposite way to a heading, which grows clockwise.
    # The bot's log comes in bursts, so a step waits for a new reading.
    ends = time.time() + seconds
    last = None
    while time.time() < ends:
        time.sleep(1.6)
        aim = last_aim()
        if not aim or aim == last:
            continue
        last = aim
        off = turn(aim[0], aim[2])
        print('   hand: aim heading %.1f, pitch %.1f; the bot at %.1f; off by %.1f' % (aim[0], aim[1], aim[2], off), flush=True)
        if abs(off) > 2:
            body.hand_yaw += math.radians(off)
    body.cast = False
    time.sleep(1)
    body.finish()
    print('   hand: done', flush=True)


def main():
    spell = sys.argv[1] if len(sys.argv) > 1 else '12FCD'
    root = os.path.dirname(os.path.abspath(__file__))
    check = subprocess.Popen([sys.executable, os.path.join(root, 'live-check.py'), 'live-pvp-hand-aim'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, errors='replace')
    player = play.Player()
    started = False
    for line in check.stdout:
        print(line.rstrip()[:200], flush=True)
        if CUE in line and not started:
            started = True
            threading.Thread(target=cast_at, args=(player, spell, 22), daemon=True).start()
    return check.wait()


if __name__ == '__main__':
    raise SystemExit(main())
