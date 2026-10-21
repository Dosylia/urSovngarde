# Runs the real game with nobody in the headset, for tests a bot alone cannot do.
#
#   powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 up        # SteamVR (null headset), server, game, last save, connected
#   powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 up -NoConnect   # the same without a server: the game alone
#   powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 status
#   powershell -ExecutionPolicy Bypass -File Tools\VR\headless.ps1 down      # quit the game, stop what 'up' started, headset setting off
#
# First done by hand on 2026-10-01; this is those steps, in the order that worked. What it relies on:
#   - SteamVR's 'null' driver with two virtual controllers (the SkyrimVR Devkit's source, built and copied in by hand).
#     Valve's stock null driver has no controllers, and skyrimvrtools.dll crashes six seconds after a load without them.
#     The driver is only active while a 'driver_null' block is in steamvr.vrsettings, which 'up' adds and 'down' removes.
#     SteamVR must be closed for either, because it rewrites that file when it exits.
#   - DevBench (an SKSE plugin, http://127.0.0.1:8921) to load the save and to quit.
#   - The client connecting by itself five seconds after a load (%LOCALAPPDATA%\SkyrimTogetherVR\connect.txt).
#
# 'down' closes SteamVR by force, because it ignores a polite close.
# It never touches MO2.

param(
    [Parameter(Position = 0)][ValidateSet('up', 'down', 'status')][string]$Action = 'status',
    [string]$Save = '',                 # a save name; empty loads the fixed rig save below, or the most recent one if it is gone
    [switch]$NoConnect,                 # no server and no waiting for a connection: the game alone, mod loaded
    [switch]$AutoConnect,               # the launcher's "connect automatically" setting on: the game connects by itself, F6 is not pressed
    [int]$LoadTimeout = 180,
    # Where the player is put once the save has loaded: the tests start outside Mistwatch (Tamriel, cell 34 -9).
    # The most recent save is wherever the player last played. Empty leaves the player where the save has them.
    [string]$StartCell = 'Tamriel 34 -9',
    # Another MO2 executable to start instead of Skyrim Together, e.g. 'SKSE' to see whether something happens
    # without the mod at all. Implies -NoConnect.
    [string]$Shortcut = ''
)

if ($Shortcut) { $NoConnect = $true }
# The game's process: the launcher's own, or the game itself when another executable starts it.
# The game runs inside the mod's program: urSovngarde.exe, SkyrimTogetherVR.exe before the rename of 2026-10-09.
$gameProcess = if ($Shortcut) { @('SkyrimVR') } else { @('urSovngarde', 'SkyrimTogetherVR') }
$gamePattern = '^(' + ($gameProcess -join '|') + ')$'

$ErrorActionPreference = 'Stop'

$steamRoot = $env:SKYRIM_STEAM_ROOT
if (-not $steamRoot) {
    $steamRoot = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath
}
if (-not $steamRoot) { $steamRoot = 'C:\Program Files (x86)\Steam' }
$settingsPath = Join-Path $steamRoot 'config\steamvr.vrsettings'
$fusRoot = $env:SKYRIM_FUS_ROOT
if (-not $fusRoot) {
    $fusRoot = @('C:\FUS', 'E:\FUS') | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'ModOrganizer.exe') } | Select-Object -First 1
}
if (-not $fusRoot) { throw 'FUS installation not found. Set SKYRIM_FUS_ROOT to its directory.' }
$mo2 = Join-Path $fusRoot 'ModOrganizer.exe'
$mo2Ini = Join-Path $fusRoot 'ModOrganizer.ini'
# The MO2 entry that runs the mod's program, found by its program: the launcher renames its title ("urSovngarde"), never its program.
function Get-ModEntryTitle([string]$ini) {
    if (-not (Test-Path -LiteralPath $ini)) { return $null }
    $titles = @{}
    $entry = $null
    $inside = $false
    foreach ($line in Get-Content -LiteralPath $ini) {
        if ($line -match '^\[') { $inside = $line -eq '[customExecutables]'; continue }
        if (-not $inside -or $line -notmatch '^(\d+)\\(binary|title)=(.*)$') { continue }
        $index, $field, $value = $Matches[1], $Matches[2], $Matches[3]
        if ($field -eq 'title') { $titles[$index] = $value }
        elseif ($value -match '(^|[\\/])(urSovngarde|SkyrimTogetherVR)\.exe$') { $entry = $index }
    }
    if ($entry) { return $titles[$entry] }
    return $null
}
$mo2Shortcut = if ($Shortcut) { $Shortcut } else { Get-ModEntryTitle $mo2Ini }
if (-not $mo2Shortcut) { $mo2Shortcut = 'SkyrimTogetherVR' }
$release = (Resolve-Path (Join-Path $PSScriptRoot '..\..\build\windows\x64\release')).Path
$clientLog = Join-Path $fusRoot 'tools\Skyrim Together VR\logs\tp_client.log'
$stateFile = Join-Path $env:TEMP 'st-headless-state.json'
# The game plays in the player's own MO2 profile, so whatever it saves lands among the player's saves.
# A test autosave (going through a door) becomes the newest save, and the player's next "Continue" would load it.
# It also carries another character id, which hides the real saves from the Load menu.
# So the saves folder is copied aside before the game starts and put back exactly as it was once the game has gone.
# Whatever the test game wrote or changed is moved to saves-made-by-rig\<time>; whatever it overwrote comes back from the copy.
# A run that died before putting it back is put right by the next "up" or "down".
$profileDir = Join-Path $fusRoot 'profiles\FUS RO DAH (Basic + Appearance + Gameplay)'
$savesDir = Join-Path $profileDir 'saves'
$savesCopy = Join-Path $profileDir 'saves-before-rig'
$savesCopyDone = Join-Path $profileDir 'saves-before-rig.complete'
$rigSaves = Join-Path $profileDir 'saves-made-by-rig'
$devbench = 'http://127.0.0.1:8921'

function Test-Running([string]$pattern) {
    return [bool](Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match $pattern })
}

function Invoke-Tool([string]$tool, [hashtable]$body, [int]$timeout = 15) {
    $json = $body | ConvertTo-Json -Compress -Depth 6
    return Invoke-RestMethod -Uri "$devbench/api/tool/$tool" -Method Post -ContentType 'application/json' -Body $json -TimeoutSec $timeout
}

function Get-Health {
    try { return Invoke-RestMethod -Uri "$devbench/api/health" -TimeoutSec 3 } catch { return $null }
}

# Drivers that hang SteamVR's start next to the null driver, switched off for the run where the owner has no setting of their own.
# With activateMultipleDrivers, SteamVR 2.18.4 loads the PlayStation VR2 add-on again once the null headset is active, and that second cleanup never returns.
$driversOffForRun = @('driver_playstation_vr2')
# Which of them 'up' switched off, so that 'down' takes out those blocks and no others.
$driversOffMarker = Join-Path $env:TEMP 'urSovngarde-headless-drivers-off.txt'

# The settings file is JSON that SteamVR writes itself; it is read and written whole, and only the rig's keys are touched.
function Set-NullDriver([bool]$on) {
    if (Test-Running '^(vrserver|vrmonitor|vrcompositor)$') { throw 'SteamVR is running; its settings cannot be changed until it is closed.' }
    $text = [System.IO.File]::ReadAllText($settingsPath)
    $settings = $text | ConvertFrom-Json
    $names = @($settings.PSObject.Properties.Name)
    $changed = $false
    if ($on) {
        $block = [pscustomobject]@{ enable = $true; windowWidth = 1280; windowHeight = 720; renderWidth = 1024; renderHeight = 1024; displayFrequency = 90.0 }
        if ($names -contains 'driver_null') { $settings.driver_null = $block } else { $settings | Add-Member -NotePropertyName 'driver_null' -NotePropertyValue $block }
        $changed = $true
        $added = @($driversOffForRun | Where-Object { $names -notcontains $_ })
        foreach ($name in $added) { $settings | Add-Member -NotePropertyName $name -NotePropertyValue ([pscustomobject]@{ enable = $false }) }
        if ($added.Count) { Set-Content -LiteralPath $driversOffMarker -Value $added }
    }
    else {
        if ($names -contains 'driver_null') { $settings.PSObject.Properties.Remove('driver_null'); $changed = $true }
        if (Test-Path -LiteralPath $driversOffMarker) {
            foreach ($name in @(Get-Content -LiteralPath $driversOffMarker)) {
                if ($names -contains $name) { $settings.PSObject.Properties.Remove($name); $changed = $true }
            }
            Remove-Item -LiteralPath $driversOffMarker
        }
    }
    if (-not $changed) { return }
    $out = $settings | ConvertTo-Json -Depth 20
    [System.IO.File]::WriteAllText($settingsPath, $out + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

# A test that throws (a request timing out while the game is busy loading) counts as "not yet". With -NeedsGame the
# wait ends at once if the game has gone, instead of sitting out its timeout on a process that crashed.
function Wait-Until([scriptblock]$test, [int]$seconds, [string]$what, [switch]$NeedsGame) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ($NeedsGame -and -not (Test-Running $gamePattern)) {
            throw "The game closed while waiting for $what; see the newest crash log in Documents\My Games\Skyrim VR\SKSE."
        }
        $ok = $false
        try { $ok = [bool](& $test) } catch { $ok = $false }
        if ($ok) { return }
        Start-Sleep -Milliseconds 1500
    }
    throw "Timed out after $seconds s waiting for $what."
}

# The rig's frame rate: SteamVR's null headset draws into a desktop window, so the game runs at the monitors' refresh
# rate (the fastest one), not at the headset's 90 Hz. HIGGS splits Havok's step by it: two steps a frame below 70 fps,
# as for the players near 45, and one above. A monitor left at 100 Hz makes a rig run that physics and timing tests
# cannot trust, so it is said before the run (and live-check keeps each run's frame rate in its history).
function Get-MonitorRates {
    Add-Type -AssemblyName System.Windows.Forms
    if (-not ('RigMonitorModes' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RigMonitorModes
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool EnumDisplaySettings(string name, int mode, ref DEVMODE devmode);
    public static int RefreshRate(string device)
    {
        var d = new DEVMODE();
        d.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        return EnumDisplaySettings(device, -1, ref d) ? d.dmDisplayFrequency : 0;
    }
}
'@
    }
    foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
        [pscustomobject]@{ Name = $screen.DeviceName; Hz = [RigMonitorModes]::RefreshRate($screen.DeviceName) }
    }
}

function Show-Status {
    $health = Get-Health
    $game = Test-Running $gamePattern
    $connected = 'unknown'
    if ($game -and (Test-Path $clientLog)) {
        # The last of the client's "Connected:" and "Disconnected from server" lines says which it is now.
        $last = Get-Content $clientLog -Tail 2000 | Select-String -Pattern '(Connected: the server accepted|Disconnected from server)' | Select-Object -Last 1
        if ($last) { $connected = $(if ($last.Matches[0].Groups[1].Value -like 'Connected*') { 'yes' } else { 'no' }) }
    }
    Write-Host ("game {0} | devbench {1} | connected {2} | SteamVR {3} | server {4}" -f `
        $(if ($game) { 'running' } else { 'not running' }), `
        $(if ($health) { "frame $($health.frame), $($health.lastLifecycle)" } else { 'no answer' }), `
        $connected, `
        $(if (Test-Running '^vrserver$') { 'running' } else { 'closed' }), `
        $(if (Test-Running '^(urSovngardeServer|SkyrimTogetherServer)$') { 'running' } else { 'stopped' }))
}

function Copy-SavesAside {
    if (Test-Path -LiteralPath $savesCopy) { Restore-Saves }
    if (Test-Path -LiteralPath $savesCopy) { throw "The saves from an earlier run could not be put back ($savesCopy); not starting the game." }
    Remove-Item -LiteralPath $savesCopyDone -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $savesCopy | Out-Null
    $files = @(Get-ChildItem -LiteralPath $savesDir -File)
    foreach ($f in $files) { Copy-Item -LiteralPath $f.FullName -Destination $savesCopy -Force }
    $copied = @(Get-ChildItem -LiteralPath $savesCopy -File)
    if ($copied.Count -ne $files.Count) { throw "Only $($copied.Count) of $($files.Count) saves were copied aside; not starting the game." }
    # Only a complete copy may ever be used to decide what the test game wrote.
    Set-Content -LiteralPath $savesCopyDone -Value $files.Count
    Write-Host "Saves copied aside ($($files.Count) files); they are put back when the game is closed."
}

function Restore-Saves {
    if (-not (Test-Path -LiteralPath $savesCopy)) { return }
    if (Test-Running '^(urSovngarde|SkyrimTogetherVR|SkyrimVR)$') { Write-Host 'The game is still running; saves NOT put back yet.' -ForegroundColor Red; return }
    if (-not (Test-Path -LiteralPath $savesCopyDone)) {
        # Copying aside stopped halfway, before the game was started: the saves were never touched.
        Remove-Item -LiteralPath $savesCopy -Recurse -Force
        Write-Host 'An incomplete copy of the saves was thrown away; the saves themselves were not touched.' -ForegroundColor Yellow
        return
    }
    $moved = 0; $restored = 0
    $quarantine = Join-Path $rigSaves (Get-Date -Format 'yyyyMMdd-HHmmss')
    $before = @{}
    foreach ($f in Get-ChildItem -LiteralPath $savesCopy -File) { $before[$f.Name] = $f }
    foreach ($f in Get-ChildItem -LiteralPath $savesDir -File) {
        $old = $before[$f.Name]
        if ($old -and $old.Length -eq $f.Length -and $old.LastWriteTimeUtc -eq $f.LastWriteTimeUtc) { continue }
        New-Item -ItemType Directory -Force -Path $quarantine | Out-Null
        Move-Item -LiteralPath $f.FullName -Destination $quarantine -Force
        $moved++
    }
    foreach ($name in $before.Keys) {
        if (-not (Test-Path -LiteralPath (Join-Path $savesDir $name))) {
            Copy-Item -LiteralPath $before[$name].FullName -Destination $savesDir -Force
            $restored++
        }
    }
    Remove-Item -LiteralPath $savesCopy -Recurse -Force
    Remove-Item -LiteralPath $savesCopyDone -ErrorAction SilentlyContinue
    if ($moved -or $restored) { Write-Host "Saves put back: $moved file(s) the test game wrote moved to $quarantine, $restored restored from the copy." -ForegroundColor Yellow }
    else { Write-Host 'Saves put back: the test game wrote nothing.' }
}

# The game connects by itself to whatever %LOCALAPPDATA%\SkyrimTogetherVR\connect.txt names, and that is the player's own file.
# If it names the public server, the test game would join the public server with the player's character.
# For the run it names the local test server (with -NoConnect too: then there is nothing to join).
# The player's file is put back by "down".
$connectFile = Join-Path $env:LOCALAPPDATA 'SkyrimTogetherVR\connect.txt'
$connectBackup = "$connectFile.before-rig"

function Set-RigConnect {
    if (Test-Path -LiteralPath $connectBackup) { Restore-Connect }
    if (Test-Path -LiteralPath $connectFile) { Copy-Item -LiteralPath $connectFile -Destination $connectBackup -Force }
    else { Set-Content -LiteralPath $connectBackup -Value '' -NoNewline } # "there was none": removed again on restore
    $port = 10578
    $ini = Join-Path $release 'config\STServer.ini'
    if (Test-Path $ini) {
        $line = Select-String -LiteralPath $ini -Pattern '^\s*uPort\s*=\s*(\d+)' | Select-Object -First 1
        if ($line) { $port = [int]$line.Matches[0].Groups[1].Value }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $connectFile) | Out-Null
    $text = "127.0.0.1:$port"
    if ($AutoConnect) { $text += "`r`nautoconnect=on" }
    Set-Content -LiteralPath $connectFile -Value $text -Encoding ascii
    Write-Host "Connect file set to the local test server (127.0.0.1:$port) for this run."
}

function Restore-Connect {
    if (-not (Test-Path -LiteralPath $connectBackup)) { return }
    if ((Get-Item -LiteralPath $connectBackup).Length -eq 0) { Remove-Item -LiteralPath $connectFile -ErrorAction SilentlyContinue }
    else { Copy-Item -LiteralPath $connectBackup -Destination $connectFile -Force }
    Remove-Item -LiteralPath $connectBackup -Force
    Write-Host 'Connect file put back as it was.'
}

function Stop-Everything {
    $state = $null
    if (Test-Path $stateFile) { $state = Get-Content $stateFile -Raw | ConvertFrom-Json }

    if (Test-Running $gamePattern) {
        # The game's own quit. A kill would leave a crash report that reads like a real crash later.
        try { Invoke-Tool 'console' @{ action = 'exec'; command = 'qqq' } 8 | Out-Null } catch { }
        try { Wait-Until { -not (Test-Running $gamePattern) } 40 'the game to quit' }
        catch {
            Write-Host 'The game did not quit on its own; stopping it.' -ForegroundColor Yellow
            Get-Process $gameProcess -ErrorAction SilentlyContinue | Stop-Process -Force
        }
    }

    if ($state -and $state.startedServer) { Get-Process urSovngardeServer, SkyrimTogetherServer -ErrorAction SilentlyContinue | Stop-Process -Force }

    Restore-Saves
    if (-not (Test-Running $gamePattern)) { Restore-Connect }

    # More than once if need be. SteamVR's parts restart one another: on 2026-10-02 a single pass left all five
    # running, the setting could not be removed, and the fake headset stayed switched on until somebody noticed.
    $closed = $false
    foreach ($attempt in 1..4) {
        foreach ($name in 'vrmonitor', 'vrstartup', 'vrdashboard', 'vrwebhelper', 'vrcompositor', 'vrserver', 'steamvr_room_setup') {
            Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds 3
        if (-not (Test-Running '^(vrserver|vrmonitor|vrcompositor|vrdashboard|vrwebhelper|vrstartup)$')) {
            # Still gone a moment later, or it was only between two restarts.
            Start-Sleep -Seconds 3
            if (-not (Test-Running '^(vrserver|vrmonitor|vrcompositor|vrdashboard|vrwebhelper|vrstartup)$')) { $closed = $true; break }
        }
    }
    if (-not $closed) { Write-Host 'SteamVR is still running after four attempts to close it.' -ForegroundColor Red }

    $settingOff = $false
    try { Set-NullDriver $false; $settingOff = $true; Write-Host 'Headset setting off: the Quest starts normally again.' }
    catch { Write-Host "COULD NOT REMOVE the driver_null setting: $_" -ForegroundColor Red }

    Remove-Item $stateFile -ErrorAction SilentlyContinue
    if (-not $settingOff) {
        Write-Host 'THE FAKE HEADSET IS STILL SWITCHED ON. Close SteamVR and run "headless.ps1 down" again before playing.' -ForegroundColor Red
        $script:downFailed = $true
    }
}

if ($Action -eq 'status') { Show-Status; return }
if ($Action -eq 'down') { Stop-Everything; Show-Status; if ($script:downFailed) { exit 3 }; return }

# ---- up ----
if (Test-Running '^(urSovngarde|SkyrimTogetherVR|SkyrimVR)$') { throw 'The game is already running. If that is a real session, leave it alone; otherwise run "down" first.' }
if (Test-Running '^(vrserver|vrmonitor)$') { throw 'SteamVR is already running. If the headset is in use, leave it alone; otherwise close it first.' }

$rates = @(Get-MonitorRates)
$fastest = ($rates | Measure-Object -Property Hz -Maximum).Maximum
Write-Host ('Monitors: ' + (($rates | ForEach-Object { '{0} {1} Hz' -f $_.Name, $_.Hz }) -join ', ') + "; the rig will run near $fastest fps.")
if ($fastest -gt 60) {
    Write-Host "A monitor runs above 60 Hz: the game will run near $fastest fps with one physics step a frame, while the players run near 45 fps with two. Timing and physics results of this run do not stand for play; set the monitors to 60 Hz for those." -ForegroundColor Yellow
}

$startedServer = $false
# The tests run on one fixed save, since the newest one changes with every play session and changes the results with it.
# The bot clones the player's equipment, and a save's effects (a cloak spell, a companion) can kill the bot's creatures.
# The newest save is used only when the fixed one is gone.
$rigSave = 'Save46_C232C981_0_517565656E20456D6D61_Tamriel_011038_20261011155852_30_1'
if (-not $Save -and (Test-Path -LiteralPath (Join-Path $savesDir "$rigSave.ess"))) {
    $Save = $rigSave
    Write-Host "Loading the rig's fixed save $rigSave (pass -Save to load another)."
}
try {
    Copy-SavesAside
    Set-RigConnect
    Set-NullDriver $true
    @{ startedServer = $false; save = $Save } | ConvertTo-Json | Set-Content $stateFile

    Start-Process 'steam://rungameid/250820'
    Wait-Until { Test-Running '^vrserver$' } 90 'SteamVR to start'
    Start-Sleep -Seconds 12

    $vrLog = Join-Path $steamRoot 'logs\vrserver.txt'
    $loaded = Select-String -Path $vrLog -Pattern "Driver 'null' finished adding tracked device with serial number 'CTRL2Serial'" -Quiet
    if (-not $loaded) { throw "SteamVR is up, but the null driver did not add its controllers (see $vrLog). Is the built driver_null.dll still in place?" }

    if (-not $NoConnect -and -not (Test-Running '^(urSovngardeServer|SkyrimTogetherServer)$')) {
        Start-Process -FilePath (Join-Path $release 'urSovngardeServer.exe') -WorkingDirectory $release -WindowStyle Hidden
        $startedServer = $true
        @{ startedServer = $true; save = $Save } | ConvertTo-Json | Set-Content $stateFile
        Start-Sleep -Seconds 4
    }

    Start-Process -FilePath $mo2 -ArgumentList "`"moshortcut://:$mo2Shortcut`""
    Wait-Until { Get-Health } 180 'DevBench to answer (the game to start)'
    Wait-Until { (Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'Main Menu' } 180 'the main menu' -NeedsGame

    if ($Save) { Invoke-Tool 'game' @{ action = 'load'; name = $Save } | Out-Null }
    else { Invoke-Tool 'game' @{ action = 'loadLast' } | Out-Null }
    Wait-Until { $h = Get-Health; $h -and $h.lastLifecycle -eq 'postLoadGame' } $LoadTimeout 'the save to load' -NeedsGame

    if ($StartCell) {
        Start-Sleep -Seconds 3
        Invoke-Tool 'console' @{ action = 'exec'; command = "cow $StartCell" } | Out-Null
        Start-Sleep -Seconds 3
        Wait-Until { -not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'Loading Menu') } $LoadTimeout 'the start cell to load' -NeedsGame
        Start-Sleep -Seconds 5
        Write-Host "Player put at the start: cow $StartCell"
    }

    if ($NoConnect) {
        Write-Host 'Up, not connected (no server was started).' -ForegroundColor Green
        Show-Status
        return
    }

    # Without the auto-connect setting the game connects only when F6 is pressed.
    # The client reads the key with GetAsyncKeyState, so it is pressed at the Windows level.
    # DevBench's keyboard input goes to the game's own input, which GetAsyncKeyState does not see.
    Start-Sleep -Seconds 6
    if (-not $AutoConnect) {
        if (-not ('RigKeys' -as [type])) {
            Add-Type -Name RigKeys -Namespace '' -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);'
        }
        [RigKeys]::keybd_event(0x75, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 300
        [RigKeys]::keybd_event(0x75, 0, 2, [UIntPtr]::Zero)
        Write-Host 'Pressed F6 to connect.'
    }

    # Connected: in this game session's part of the log (from its "Skyrim Together client, build" line), the last of
    # "Connected: the server accepted" and "Disconnected from server" is the first.
    Wait-Until {
        # The empty message box the game shows at the end of a load (SkyrimVR.exe+0x168507) is dropped by the client
        # while the loading screen is up; once it came a few milliseconds after (2026-10-03 11:18, loading
        # STTest_Mistwatch), paused the game, and nobody was there to click it.
        try {
            if ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'MessageBoxMenu') {
                Invoke-Tool 'menu' @{ action = 'close'; name = 'MessageBoxMenu' } | Out-Null
                Write-Host 'Closed a message box left open after the load.' -ForegroundColor Yellow
            }
        } catch { }
        $lines = @(Get-Content $clientLog)
        $start = ($lines | Select-String -Pattern 'Skyrim Together client, build' | Select-Object -Last 1).LineNumber
        $last = $lines | Select-Object -Skip ([int]$start) | Select-String -Pattern '(Connected: the server accepted|Disconnected from server)' | Select-Object -Last 1
        [bool]($last -and $last.Matches[0].Groups[1].Value -like 'Connected*')
    } 90 'the client to connect' -NeedsGame

    Write-Host 'Up.' -ForegroundColor Green
    Show-Status
}
catch {
    Write-Host "Failed: $_" -ForegroundColor Red
    Write-Host 'Cleaning up.' -ForegroundColor Yellow
    Stop-Everything
    exit 1
}
