"""Joins a friend from the dashboard's "Join a friend" panel on the rig, with a stand-in launcher.

    python Tools/VR/live-launcher-join.py

The rig must be up and connected to its local server (headless.ps1 up).
The script plays the launcher's side of the contract in %LOCALAPPDATA%\\SkyrimTogetherVR: it writes launcher.json with
no friends (the game reads Steam itself), answers the page's launcher-request.json, and writes connect.txt as the launcher does once a code
is looked up. connect.txt then names the rig's own server by this PC's network address instead of 127.0.0.1, so the
log shows the game read the new file, and the join is a real leave and connect.
The page is driven through its real buttons, over CEF's debugging port (127.0.0.1:8384).

Checks: the menu has the button; the panel offers a code field; Join sends the typed code; the game leaves its server
and connects to what connect.txt names; the panel closes. Then a code no server has shows the panel's error, and the
game stays on its server.
The script removes the two launcher files at the end; headless.ps1 down puts connect.txt back.
"""
import base64
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.request

FUS_ROOT = os.environ.get('SKYRIM_FUS_ROOT') or next(
    (path for path in ('C:/FUS', 'E:/FUS') if os.path.isfile(os.path.join(path, 'ModOrganizer.exe'))),
    'E:/FUS',
)
CLIENT_DIR = next(
    (path for path in (os.path.join(FUS_ROOT, 'tools', name) for name in ('Skyrim Together VR', 'urSovngarde')) if os.path.isfile(os.path.join(path, 'urSovngarde.exe'))),
    os.path.join(FUS_ROOT, 'tools', 'Skyrim Together VR'),
)
CLIENT_LOG = os.path.join(CLIENT_DIR, 'logs', 'tp_client.log')
FOLDER = os.path.join(os.environ['LOCALAPPDATA'], 'SkyrimTogetherVR')
STATE = os.path.join(FOLDER, 'launcher.json')
REQUEST = os.path.join(FOLDER, 'launcher-request.json')
CONNECT = os.path.join(FOLDER, 'connect.txt')
STAND_IN = 'rig stand-in'
FRIEND = {'code': 'RIG-J01'}
# The panel still open. The rig's page is hidden, so a closed popup never ends its closing animation and stays in the
# page; Angular marks it as being removed (an internal mark, so this is the rig's test only, never the menu's code).
OPEN_JOIN = "Array.from(document.querySelectorAll('app-join')).find(j => !j.closest('app-popup')?.__ng_removed)"


def normalised(code):
    return code.replace('-', '').replace(' ', '').upper()


def network_address():
    """This PC's address on its network, the way a friend's join names a host; None without one."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(('192.0.2.1', 9))  # sends nothing: it only picks the interface
        address = probe.getsockname()[0]
    except OSError:
        return None
    finally:
        probe.close()
    return None if address.startswith('127.') else address


class DevTools:
    """Just enough of a WebSocket client for Runtime.evaluate on the page."""

    def __init__(self):
        with urllib.request.urlopen('http://127.0.0.1:8384/json', timeout=5) as answer:
            pages = json.load(answer)
        url = next(p['webSocketDebuggerUrl'] for p in pages if p.get('type') == 'page')
        address, path = url[len('ws://'):].split('/', 1)
        host, port = address.split(':')
        self.sock = socket.create_connection((host, int(port)), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            f'GET /{path} HTTP/1.1\r\nHost: {address}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
            f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode())
        reply = b''
        while b'\r\n\r\n' not in reply:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('the page closed the debugging connection')
            reply += chunk
        if b' 101 ' not in reply.split(b'\r\n', 1)[0]:
            raise ConnectionError('the page refused the debugging connection')
        self.buffer = reply.split(b'\r\n\r\n', 1)[1]
        self.last_id = 0

    def _send(self, text):
        data = text.encode()
        header = bytearray([0x81])
        if len(data) < 126:
            header.append(0x80 | len(data))
        elif len(data) < 65536:
            header.append(0x80 | 126)
            header += struct.pack('>H', len(data))
        else:
            header.append(0x80 | 127)
            header += struct.pack('>Q', len(data))
        mask = os.urandom(4)
        self.sock.sendall(bytes(header) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('the page closed the debugging connection')
            self.buffer += chunk
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def _receive(self):
        message = b''
        while True:
            first, second = self._read(2)
            size = second & 0x7F
            if size == 126:
                size = struct.unpack('>H', self._read(2))[0]
            elif size == 127:
                size = struct.unpack('>Q', self._read(8))[0]
            payload = self._read(size)
            opcode = first & 0x0F
            if opcode == 8:
                raise ConnectionError('the page closed the debugging connection')
            if opcode in (9, 10):
                continue
            message += payload
            if first & 0x80:
                return message.decode()

    def evaluate(self, expression):
        expression = expression.replace('OPEN_JOIN_JS', f'({OPEN_JOIN})')
        self.last_id += 1
        self._send(json.dumps({'id': self.last_id, 'method': 'Runtime.evaluate', 'params': {'expression': expression, 'returnByValue': True}}))
        while True:
            reply = json.loads(self._receive())
            if reply.get('id') == self.last_id:
                result = reply.get('result', {})
                if 'exceptionDetails' in result:
                    raise RuntimeError(result['exceptionDetails'].get('text', 'the page threw'))
                return result.get('result', {}).get('value')


class StandIn(threading.Thread):
    """The launcher's side: its state every second, and an answer to each request, as the contract says."""

    def __init__(self, connect_text):
        super().__init__(daemon=True)
        self.connect_text = connect_text
        self.requests = []
        self.answer = None
        self.stopping = False

    def write_state(self):
        state = {'contract': 1, 'written': int(time.time()), 'launcher': STAND_IN, 'hosting': None, 'join': self.answer}
        temporary = STATE + '.tmp'
        with open(temporary, 'w', encoding='utf8') as handle:
            json.dump(state, handle)
        try:
            os.replace(temporary, STATE)
        except PermissionError:
            pass  # the game is reading it; the next beat writes it

    def handle(self, request):
        self.requests.append(request)
        code = normalised(request.get('code', ''))
        answer = {'request': request['request'], 'code': code, 'address': None, 'reason': None, 'detail': None}
        if code != normalised(FRIEND['code']):
            self.answer = dict(answer, state='failed', reason='noSuchCode', detail='no host has this code')
            return
        with open(CONNECT, 'w', encoding='utf8', newline='\r\n') as handle:
            handle.write(self.connect_text)
        self.answer = dict(answer, state='ready', address=self.connect_text.splitlines()[0].strip())

    def run(self):
        next_beat = 0
        while not self.stopping:
            if os.path.isfile(REQUEST):
                try:
                    with open(REQUEST, encoding='utf8') as handle:
                        request = json.load(handle)
                    os.remove(REQUEST)
                    self.handle(request)
                    next_beat = 0
                except (OSError, ValueError):
                    pass
            if time.time() >= next_beat:
                self.write_state()
                next_beat = time.time() + 1
            time.sleep(0.2)


def log_size():
    return os.path.getsize(CLIENT_LOG) if os.path.isfile(CLIENT_LOG) else 0


def log_since(offset):
    with open(CLIENT_LOG, 'rb') as handle:
        handle.seek(offset)
        return handle.read().decode('utf8', errors='replace')


def wait(condition, seconds):
    end = time.time() + seconds
    while time.time() < end:
        value = condition()
        if value:
            return value
        time.sleep(0.25)
    return condition()


def main():
    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(('ok    ' if ok else 'FAIL  ') + name + (f' ({detail})' if detail else ''))
        return ok

    if os.path.isfile(STATE):
        with open(STATE, encoding='utf8') as handle:
            text = handle.read()
        if STAND_IN not in text:
            print('INVALID: a real launcher wrote launcher.json; close it before this test')
            return 2
    if not os.path.isfile(CONNECT):
        print('INVALID: no connect.txt; bring the rig up with headless.ps1 up')
        return 2
    with open(CONNECT, encoding='utf8') as handle:
        connect_lines = handle.read().splitlines()
    whole_log = log_since(0)
    if whole_log.rfind('Connected: the server accepted this player') <= whole_log.rfind('Disconnected from server'):
        print('INVALID: the game is not connected to the rig server now')
        return 2
    lan = network_address()
    if not lan or not connect_lines or not connect_lines[0].startswith('127.0.0.1:'):
        print(f'INVALID: no network address, or connect.txt does not name the local server ({lan}, {connect_lines[:1]})')
        return 2
    friend_address = lan + connect_lines[0][len('127.0.0.1'):]
    connect_text = '\n'.join([friend_address] + connect_lines[1:]) + '\n'
    try:
        page = DevTools()
    except (OSError, StopIteration, ConnectionError) as error:
        print(f'INVALID: no page on the debugging port ({error})')
        return 2

    stand_in = StandIn(connect_text)
    stand_in.start()
    try:
        find_button = "Array.from(document.querySelectorAll('.app-root-menu button')).find(b => b.textContent.includes('Join a friend'))"
        if not check('the menu has "Join a friend"', wait(lambda: page.evaluate(f'!!{find_button}'), 10)):
            return 1
        page.evaluate(f'{find_button}.click()')
        offered = wait(lambda: page.evaluate("!!OPEN_JOIN_JS?.querySelector('input[type=text]')"), 10)
        if not check('the panel offers a code field with the launcher open', offered):
            return 1

        offset = log_size()
        page.evaluate(
            "(() => { const i = OPEN_JOIN_JS?.querySelector('input[type=text]'); i.value = '" + FRIEND['code'] + "'; i.dispatchEvent(new Event('input')); })()")
        page.evaluate("OPEN_JOIN_JS?.querySelector('app-action-buttons button.primary').click()")
        sent = wait(lambda: stand_in.requests, 5)
        request = stand_in.requests[0] if sent else {}
        check('Join sends the typed code', request.get('code') == FRIEND['code'] and request.get('name') == '', json.dumps({k: request.get(k) for k in ('code', 'name', 'action')}))
        check('the request has no password', request.get('password') == '')

        # In order after the join: the old server left, one attempt at the new address, accepted, and no retry between.
        def rejoined():
            text = log_since(offset)
            start = text.find('VRConnectService: joining the server the launcher just set')
            left = text.find('Disconnected from server', start)
            attempt = text.find(f'VRConnectService: connecting to {friend_address} (attempt 1)', left)
            accepted = text.find('Connected: the server accepted this player', attempt)
            if min(start, left, attempt, accepted) < 0:
                return None
            return 'trying again in' not in text[start:accepted]
        outcome = wait(rejoined, 40)
        check('the game leaves its server and connects at once to the address connect.txt now names', outcome is True, friend_address)
        check('the panel closes once connected', wait(lambda: page.evaluate("!OPEN_JOIN_JS"), 10))

        offset = log_size()
        page.evaluate(f'{find_button}.click()')
        wait(lambda: page.evaluate("!!OPEN_JOIN_JS?.querySelector('input')"), 10)
        page.evaluate(
            "(() => { const i = OPEN_JOIN_JS?.querySelector('input'); i.value = 'NO-CODE'; i.dispatchEvent(new Event('input')); })()")
        page.evaluate("OPEN_JOIN_JS?.querySelector('app-action-buttons button.primary').click()")
        error = wait(lambda: page.evaluate("OPEN_JOIN_JS?.querySelector('.join-status-error')?.textContent ?? ''"), 10)
        check('a code no server has shows the panel\'s error', 'No server has this code' in (error or ''), (error or '').strip())
        time.sleep(3)
        check('the game stays on its server after a refused code', 'Disconnected from server' not in log_since(offset))
        page.evaluate("Array.from((OPEN_JOIN_JS?.querySelectorAll('button') ?? [])).find(b => b.textContent.includes('Close'))?.click()")
    finally:
        stand_in.stopping = True
        stand_in.join(3)
        for path in (STATE, STATE + '.tmp', REQUEST):
            try:
                os.remove(path)
            except OSError:
                pass

    passed = all(results)
    print(f'{"PASS" if passed else "FAIL"}: {sum(results)} of {len(results)} checks')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
