"""This PC's folders, as the hub keeps them for its runner, read from the file the runner writes.

pc_folder(key) is a full folder, or None when the file does not name it; the callers then keep their own search.
"""
import json
import os

FILE = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'urSovngarde-tools', 'folders.json')
# The client's folder among Mod Organizer's tools: 'Skyrim Together VR' on the host's PC, 'urSovngarde' on another player's.
CLIENT_FOLDERS = ('Skyrim Together VR', 'urSovngarde')


def pc_folder(key):
    """One folder of this PC's list (fus, fusClient, fusServer, mgo, mgoClient, mgoServer, skyrimVR, steam, toolbox, mod), or None."""
    try:
        with open(FILE, encoding='utf-8-sig') as f:
            folders = json.load(f).get('folders') or {}
        value = folders.get(key)
    except (OSError, ValueError, AttributeError):
        return None
    return value if isinstance(value, str) and value else None


def fus_root(fallback='E:/FUS'):
    """The FUS install: SKYRIM_FUS_ROOT (it picks the modlist for one run), else this PC's list, else C:/FUS or E:/FUS where Mod Organizer is."""
    given = os.environ.get('SKYRIM_FUS_ROOT') or pc_folder('fus')
    if given:
        return given
    return next((root for root in ('C:/FUS', 'E:/FUS') if os.path.isfile(os.path.join(root, 'ModOrganizer.exe'))), fallback)


def same_folder(a, b):
    """Two Windows folders are the same, whatever their case, slashes and trailing separator."""
    return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))


def client_dir(root, fallback=None):
    """The client's folder of an install: this PC's list's when the install is its FUS or MGO, else the tool folder holding urSovngarde.exe."""
    for name in ('fus', 'mgo'):
        install, client = pc_folder(name), pc_folder(name + 'Client')
        if install and client and same_folder(root, install):
            return client
    holding = [os.path.join(root, 'tools', name) for name in CLIENT_FOLDERS if os.path.isfile(os.path.join(root, 'tools', name, 'urSovngarde.exe'))]
    if holding:
        return holding[0]
    return fallback if fallback is not None else os.path.join(root, 'tools', CLIENT_FOLDERS[0])
