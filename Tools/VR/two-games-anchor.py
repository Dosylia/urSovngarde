"""Started once by Mod Organizer (headless.ps1 up -Twin): starts both games inside its one virtual folder.

Every launch through Mod Organizer copies the Root files again and remounts the virtual folder under a game already running, which crashed Mod Organizer.
A process Mod Organizer started passes the virtual folder on to what it starts, so this one starts the first game at once and a second when asked.
It ends when no game is left.
"""
import os
import re
import subprocess
import tempfile
import time

ASK_FILE = os.path.join(tempfile.gettempdir(), 'two-games-second')


def fus_root():
    for root in (os.environ.get('SKYRIM_FUS_ROOT') or '', 'C:/FUS', 'E:/FUS'):
        if root and os.path.isfile(os.path.join(root, 'ModOrganizer.exe')):
            return root
    raise SystemExit('Mod Organizer not found')


def game_exe(root):
    for name in ('Skyrim Together VR', 'urSovngarde'):
        path = os.path.join(root, 'tools', name, 'urSovngarde.exe')
        if os.path.isfile(path):
            return path
    raise SystemExit('urSovngarde.exe not found')


def game_folder(root):
    with open(os.path.join(root, 'ModOrganizer.ini'), encoding='utf-8', errors='replace') as f:
        found = re.search(r'^gamePath=@ByteArray\((.*)\)\s*$', f.read(), re.M)
    return found.group(1).replace('\\\\', '\\') if found else None


def main():
    root = fus_root()
    exe, folder = game_exe(root), game_folder(root)
    if os.path.exists(ASK_FILE):
        os.remove(ASK_FILE)
    games = [subprocess.Popen([exe], cwd=folder)]
    while any(game.poll() is None for game in games):
        if os.path.exists(ASK_FILE):
            os.remove(ASK_FILE)
            games.append(subprocess.Popen([exe], cwd=folder, env=dict(os.environ, TWO_GAMES_SECOND='1')))
        time.sleep(0.5)


if __name__ == '__main__':
    main()
