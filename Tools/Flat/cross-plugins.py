"""Between a flat and a VR player the NPC-plugin warning leaves out each game's own plugins: SkyrimVR.esm, and flat's free Creation Club files.

    python Tools/Flat/cross-plugins.py
    python Tools/Flat/cross-plugins.py --expect-fail     on a server without the rule: must be RED

Both rigs up and connected on a freshly started server: Tools/Flat/flat-rig.ps1 up first, then Tools/VR/headless.ps1 up -AutoConnect.
The server warns a pair once per run, so a server still running from an earlier run says nothing new.

The comparison runs once, when the second game connects.
Each game's line is read only after its own newest connection, and the server's lines must be from the same seconds; otherwise the run is INVALID.

What each player sees: the warning their game shows (its log line holds the same text and counts).
The flat player has no plugin that only flat Skyrim has beside the game's own, so after the rule nothing is "only you have" there, and nothing "only there" for the VR player.
"""
import argparse
import datetime
import os
import re
import sys

GAME_OWN = ['SkyrimVR.esm', 'ccBGSSSE001-Fish.esm', 'ccBGSSSE025-AdvDSGS.esm', 'ccBGSSSE037-Curios.esl', 'ccQDRSSE001-SurvivalMode.esl', '_ResourcePack.esl']
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SERVER_LOG = os.path.join(REPO, 'build', 'windows', 'x64', 'release', 'logs', 'STServerOut.log')
CONNECTED = 'Connected: the server accepted this player'
WARNED_HERE = 'has other NPC plugins'
WARNED_SERVER = 'do not have the same plugins adding or changing NPCs'
LEFT_OUT = 'play different games; left out as their games\' own plugins'
# The server's lines and both games' lines come from one comparison.
SAME_MOMENT = datetime.timedelta(seconds=5)


def client_log(root_variable, client_folder):
    root = os.environ.get(root_variable)
    if not root:
        sys.exit('INVALID: set the %s user variable' % root_variable)
    return os.path.join(root, 'tools', client_folder, 'logs', 'tp_client.log')


def lines(path):
    # A log full at 5 MB moves to its .1 file, so the lines just written can be there; the older file is read first.
    stem, extension = os.path.splitext(path)
    for each in (stem + '.1' + extension, path):
        if os.path.exists(each):
            with open(each, encoding='utf-8', errors='replace') as file:
                for line in file:
                    yield line.rstrip()


def newest(all_lines, pattern):
    found = None
    for line in all_lines:
        if pattern in line:
            found = line
    return found


def after_last(all_lines, start, pattern):
    """The newest line with the pattern written after the newest start line, or None."""
    found = None
    for line in all_lines:
        if start in line:
            found = None
        elif pattern in line:
            found = line
    return found


def when(line):
    try:
        return datetime.datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f')
    except (TypeError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--expect-fail', action='store_true')
    arguments = parser.parse_args()

    flat_lines = list(lines(client_log('SKYRIM_FLAT_ROOT', 'Skyrim Together')))
    vr_lines = list(lines(client_log('SKYRIM_FUS_ROOT', 'Skyrim Together VR')))
    server_lines = list(lines(SERVER_LOG))

    flat_seen = after_last(flat_lines, CONNECTED, WARNED_HERE)
    vr_seen = after_last(vr_lines, CONNECTED, WARNED_HERE)
    warned = newest(server_lines, WARNED_SERVER)
    left_out = newest(server_lines, LEFT_OUT)
    for label, line in (('server warning', warned), ('server left out', left_out), ('flat sees', flat_seen), ('VR sees', vr_seen)):
        print('   %-16s %s' % (label, line[1:24] if line else 'none'))

    # Every line of this comparison is the warning's moment; a line from another moment is from another run.
    if not (flat_seen and vr_seen and warned):
        print('INVALID: no warning since the newest connection in one of the logs (both games connected to a fresh server?)')
        return 2
    moment = when(warned)
    if any(when(line) is None or abs(when(line) - moment) > SAME_MOMENT for line in (flat_seen, vr_seen)):
        print('INVALID: the games\' warnings are not from the server\'s newest comparison')
        return 2
    left_out_now = bool(left_out) and when(left_out) is not None and abs(when(left_out) - moment) <= SAME_MOMENT

    named = [name for name in GAME_OWN if name.lower() in warned.lower()]
    if arguments.expect_fail:
        if named:
            print('RED as expected: the warning names %s' % ', '.join(named))
            return 0
        print('NOT RED: the warning names no game\'s own plugin')
        return 1

    checks = [
        ('the server\'s warning names no game\'s own plugin%s' % (' (it names %s)' % ', '.join(named) if named else ''), not named),
        ('the server says which own plugins it left out', left_out_now and 'SkyrimVR.esm' in left_out and 'ccBGSSSE001-Fish.esm' in left_out),
    ]
    flat_counts = re.search(r'(\d+) only there, (\d+) only here', flat_seen)
    checks.append(('the flat player is told of nothing only they have (%s)' % (flat_counts.group(0) if flat_counts else 'no counts'),
                   bool(flat_counts) and flat_counts.group(2) == '0' and 'Only you have' not in flat_seen))
    vr_counts = re.search(r'(\d+) only there, (\d+) only here', vr_seen)
    checks.append(('the VR player is told of nothing only the flat player has (%s)' % (vr_counts.group(0) if vr_counts else 'no counts'),
                   bool(vr_counts) and vr_counts.group(1) == '0'))

    passed = 0
    for text, ok in checks:
        print('%-5s %s' % ('ok' if ok else 'FAIL', text))
        passed += ok
    print('%s: %d of %d checks' % ('PASS' if passed == len(checks) else 'FAIL', passed, len(checks)))
    return 0 if passed == len(checks) else 1


if __name__ == '__main__':
    sys.exit(main())
