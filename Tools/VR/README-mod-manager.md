# urSovngarde with Mod Organizer 2 or a Wabbajack list

The urSovngarde launcher does all of this for you (install, update, play). By hand:

1. Install the folder `urSovngarde mod` as a mod in MO2 (drag it onto the mod list, or zip it and
   use "Install a new mod"). Tick it. Tick `SkyrimTogether.esp` in the plugin list on the right.
2. Copy the folder `urSovngarde` into your list's `tools\` folder (or anywhere you like).
3. In MO2, add an executable: title `urSovngarde`, binary `urSovngarde.exe` from that folder.
   Start the game with it from now on, not with SKSE. Your mods load exactly as before.
4. Run `setup-connect.bat` once in the `urSovngarde` folder and type the host's address.
   If you host yourself, type `127.0.0.1:10578`.
5. Start the game through MO2, load a save, then press F6 to connect (the game shows "press F6 to connect to ..." once it is ready); with "Connect automatically" on in the launcher's settings it connects by itself a few seconds after the load. F6 again disconnects.

`uGridsToLoad` must be 5, which is the default of every list. The server refuses other values.

Updating: close the game and drag the update zip onto `update.bat` in the mod's folder. It swaps the files without
closing MO2. (By hand: replace `urSovngarde.exe` and `urSovngarde.pdb` there.)

Coming from a build before the rename (the program was `SkyrimTogetherVR.exe`, the folder `Skyrim Together VR`):
point your MO2 executable at `urSovngarde.exe`. The launcher does that for you; `update.bat` keeps the old name
working with the new build in the meantime.

## Shipping a server with a mod

A server can come as a small mod, so a list can ship its own and anyone can hand theirs out as a zip.

1. Make a text file called `connect.txt`, with the server's address on the first line:

       play.example.org:10578
       autoconnect=on

   Leave out the port and the game uses 10578. With `autoconnect=on` the game connects by itself a few seconds
   after a load; without it, players press F6. A password goes on the line after the address, and anyone who has
   the mod can read it.
2. Put the file in `SKSE\Plugins\urSovngarde\` and zip the `SKSE` folder.
3. Drag the zip onto MO2's mod list and tick it. In a Wabbajack list it is one of your own mods (No Match Include).

Several server mods? The lowest one ticked in the mod list wins. Changing that takes a game restart.

A server you set yourself always wins: `setup-connect.bat` and joining a server both write
`%LOCALAPPDATA%\SkyrimTogetherVR\connect.txt`. Delete that file to use the mod's server.
