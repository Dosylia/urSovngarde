"""What the cross tests share: the flat and the VR rig game, reached only through DevBench over HTTP, and their client logs.

No window is raised and no key is pressed at the Windows level.
"""
import json
import os
import sys
import urllib.error
import urllib.request


def game(name, port, root_variable, client_folder):
    """One rig game: its DevBench address and its client log, found through a user variable naming its Mod Organizer folder."""
    root = os.environ.get(root_variable)
    if not root:
        sys.exit('INVALID: set the %s user variable' % root_variable)
    return {'name': name, 'devbench': 'http://127.0.0.1:%d/api/tool/' % port,
            'log': os.path.join(root, 'tools', client_folder, 'logs', 'tp_client.log')}


def flat_game():
    """The flat rig game (Tools/Flat/flat-rig.ps1), DevBench on port 8922."""
    return game('flat', 8922, 'SKYRIM_FLAT_ROOT', 'Skyrim Together')


def vr_game():
    """The VR rig game (Tools/VR/headless.ps1), DevBench on port 8921."""
    return game('VR', 8921, 'SKYRIM_FUS_ROOT', 'Skyrim Together VR')


def tool(target, name, body, timeout=8):
    """Calls one DevBench tool and returns its answer, or {'error': ...} when the call failed."""
    request = urllib.request.Request(target['devbench'] + name, data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as answer:
            return json.loads(answer.read().decode())
    except urllib.error.HTTPError as error:
        return {'error': 'HTTP %d %s' % (error.code, error.read().decode(errors='replace')[:200])}
    except Exception as error:
        return {'error': type(error).__name__}


def papyrus(target, script, function, args, self_form=None):
    """Calls a Papyrus function in that game and returns what it returned.

    DevBench needs every argument, the function's defaults included."""
    body = {'action': 'call', 'script': script, 'function': function, 'args': args}
    if self_form:
        body['self'] = {'form': self_form}
    return tool(target, 'papyrus', body).get('returned')


def form_id(returned):
    """The form id ('0x...') of a form a Papyrus call returned, or None."""
    return returned.get('formId') if isinstance(returned, dict) and returned.get('formId') else None


def position(target, form='0x14'):
    """A reference's position in that game, or None when it cannot be read."""
    at = [papyrus(target, 'ObjectReference', 'GetPosition' + axis, [], form) for axis in 'XYZ']
    return at if all(isinstance(v, (int, float)) for v in at) else None


def nearest_temporary_actor(target, at, within, leave_out=()):
    """The form id of the temporary actor (an 0xFF id, as copies are) standing nearest a place in that game, within a distance."""
    answer = tool(target, 'inspect', {'kind': 'refs', 'formType': 'Actor', 'limit': 300})
    best, best_distance = None, within
    for ref in answer.get('refs', []) if isinstance(answer, dict) else []:
        form, there = str(ref.get('formId', '')), ref.get('position')
        if not form.upper().startswith('0XFF') or not there or form.upper() in {f.upper() for f in leave_out}:
            continue
        distance = sum((a - b) ** 2 for a, b in zip(there, at)) ** 0.5
        if distance < best_distance:
            best, best_distance = form, distance
    return best


def log_size(target):
    """How far that game's client log goes now, to read only what comes after."""
    return os.path.getsize(target['log'])


def log_since(target, offset):
    """That game's client log from an offset on."""
    with open(target['log'], 'rb') as f:
        f.seek(offset)
        return f.read().decode('utf-8', errors='replace')


def connected(target):
    """Whether that game's client log says it is connected now.

    The log starts a new file at 5 MB (tp_client.log, the one before it tp_client.1.log); one that says neither is read with the file before it."""
    stem, extension = os.path.splitext(target['log'])
    for path in (target['log'], stem + '.1' + extension):
        if not os.path.isfile(path):
            continue
        with open(path, 'rb') as f:
            text = f.read().decode('utf-8', errors='replace')
        accepted, dropped = text.rfind('Connected: the server accepted this player'), text.rfind('Disconnected from server')
        if accepted >= 0 or dropped >= 0:
            return accepted > dropped
    return False


def close_menus(target):
    """Closes the menus open in that game: an open menu pauses a flat game, and a paused game neither casts nor moves."""
    for menu in (tool(target, 'menu', {'action': 'list'}) or {}).get('openMenus', []):
        if menu not in ('HUD Menu', 'Cursor Menu'):
            tool(target, 'menu', {'action': 'close', 'name': menu})
            print('   (%s: closed %s)' % (target['name'], menu), flush=True)
