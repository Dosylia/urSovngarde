# The flat (non-VR) test game: Skyrim Special Edition with our flat client, started through a Mod Organizer instance of its own.
#
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 up        # server, game, start cell, connected
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 down      # quits the game, and the server if this script started it
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 status
#
# The instance is a portable Mod Organizer for Skyrim Special Edition, in the folder the SKYRIM_FLAT_ROOT user variable names.
# Its profile "Rig" keeps its own saves and INIs, so the rig never touches a player's saves.
# The first start begins a new game outside Mistwatch, answers its Survival Mode question with No, and saves it as FlatRigStart; later starts load that save.
# Its mods: the Script Extender for the game's version, the Address Library for that version, DevBench, and SkyrimTogether.esp.
# DevBench answers on port 8922 there (its config.json), so this game can run beside the VR rig, which uses 8921.
# Its executable "Skyrim Together" starts tools\Skyrim Together\SkyrimTogether.exe; deploy-flat-client.ps1 puts each build there.
# The flat client connects to 127.0.0.1:10578 when F6 is pressed at the Windows level, as on the VR rig.
# With both rigs up, that F6 reaches both games.

param(
    [Parameter(Position = 0)][ValidateSet('up', 'down', 'status')][string]$Action = 'status',
    [string]$Save = '',                         # a save of the Rig profile to load; empty runs the start command at the main menu instead
    [string]$StartCommand = 'cow Tamriel 34 -9', # run at the main menu when no save is given: the VR rig's start, outside Mistwatch
    [switch]$NoConnect,                         # no server and no F6: the game alone, mod loaded
    [int]$Port = 8922,
    [int]$MenuTimeout = 300,
    [int]$LoadTimeout = 180
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$release = Join-Path $repo 'build\windows\x64\release'
$root = $env:SKYRIM_FLAT_ROOT
if (-not $root -or -not (Test-Path (Join-Path $root 'ModOrganizer.exe'))) {
    throw 'No flat Mod Organizer instance: set the SKYRIM_FLAT_ROOT user variable to its folder.'
}
$mo2 = Join-Path $root 'ModOrganizer.exe'
$clientDir = Join-Path $root 'tools\Skyrim Together'
$clientLog = Join-Path $clientDir 'logs\tp_client.log'
$stateFile = Join-Path $root 'flat-rig.state.json'
$devbench = "http://127.0.0.1:$Port"
$startSave = 'FlatRigStart'
# The flat client maps the game into its own process, so the game runs under the client's name.
$gamePattern = '^SkyrimTogether$'
$serverPattern = '^(urSovngardeServer|SkyrimTogetherServer)$'

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

# A test that throws (a request timing out while the game loads) counts as "not yet".
# With -NeedsGame the wait ends at once if the game has gone.
function Wait-Until([scriptblock]$test, [int]$seconds, [string]$what, [switch]$NeedsGame) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ($NeedsGame -and -not (Test-Running $gamePattern)) {
            throw "The game closed while waiting for $what; see the newest crash log in Documents\My Games\Skyrim Special Edition\SKSE."
        }
        $ok = $false
        try { $ok = [bool](& $test) } catch { $ok = $false }
        if ($ok) { return }
        Start-Sleep -Milliseconds 1500
    }
    throw "Timed out after $seconds s waiting for $what."
}

# The connection state of this game session: the last of the two lines after the session's first line.
function Get-Connected {
    if (-not (Test-Path $clientLog)) { return $false }
    $lines = Get-Content -LiteralPath $clientLog -Tail 4000
    $start = 0
    for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ($lines[$i] -match 'Skyrim Together client, build') { $start = $i; break } }
    for ($i = $lines.Count - 1; $i -ge $start; $i--) {
        if ($lines[$i] -match 'Connected: the server accepted') { return $true }
        if ($lines[$i] -match 'Disconnected from server') { return $false }
    }
    return $false
}

function Show-Status {
    $h = Get-Health
    '{0} | {1} | connected {2} | server {3}' -f `
        $(if (Test-Running $gamePattern) { 'game running' } else { 'game not running' }),
        $(if ($h) { "devbench frame $($h.frame), $($h.lastLifecycle)" } else { 'devbench no answer' }),
        $(if (Test-Running $gamePattern) { Get-Connected } else { 'no' }),
        $(if (Test-Running $serverPattern) { 'running' } else { 'stopped' })
}

if (-not ('FlatRigKeys' -as [type])) {
    Add-Type -Name FlatRigKeys -Namespace '' -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindow(string windowClass, string title);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr window);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr window, int command);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
'@
}

# The flat client reads keys only while the game's window has the focus.
# The process's main window is the client's console, so the game's is found by the class the client gives it.
# Windows lets a background process bring a window to the front only just after a key press of its own, hence the Alt press.
function Set-GameFocus {
    $window = [FlatRigKeys]::FindWindow('Skyrim Together', 'Skyrim Together')
    if ($window -eq [IntPtr]::Zero) { return $false }
    [FlatRigKeys]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [FlatRigKeys]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [FlatRigKeys]::ShowWindow($window, 9) | Out-Null
    [FlatRigKeys]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 600
    return [FlatRigKeys]::GetForegroundWindow() -eq $window
}

function Send-Key([byte]$vk, [int]$holdMs = 150) {
    [FlatRigKeys]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $holdMs
    [FlatRigKeys]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 600
}

function Write-RigQuit([string]$what) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $what
    Write-Host $line
    try { Add-Content -LiteralPath (Join-Path $clientDir 'logs\rig-quits.log') -Value $line } catch { }
}

function Stop-Everything {
    $state = $null
    if (Test-Path $stateFile) { $state = Get-Content $stateFile -Raw | ConvertFrom-Json }
    if (Test-Running $gamePattern) {
        # The game's own quit: a kill would leave a crash report that reads like a real crash later.
        Write-RigQuit 'the rig sent qqq to the game'
        try { Invoke-Tool 'console' @{ action = 'exec'; command = 'qqq' } 8 | Out-Null } catch { }
        try { Wait-Until { -not (Test-Running $gamePattern) } 40 'the game to quit' }
        catch {
            Write-Host 'The game did not quit on its own; stopping it.' -ForegroundColor Yellow
            Write-RigQuit 'the game did not quit on its own; the rig stopped it'
            Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match $gamePattern } | Stop-Process -Force
        }
    }
    # The server is shared with the VR rig: it is stopped only if this script started it and the VR game is not running.
    if ($state -and $state.startedServer -and -not (Test-Running '^(urSovngarde|SkyrimVR)$')) {
        Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match $serverPattern } | Stop-Process -Force
    }
    Remove-Item $stateFile -ErrorAction SilentlyContinue
}

if ($Action -eq 'status') { Show-Status; return }
if ($Action -eq 'down') { Stop-Everything; Show-Status; return }

if (Test-Running $gamePattern) { throw 'The flat game is already running: run "down" first.' }
if (-not (Test-Path (Join-Path $clientDir 'SkyrimTogether.exe'))) { throw "No flat client in ${clientDir}: run Tools\Flat\deploy-flat-client.ps1 first." }

$startedServer = $false
if (-not $NoConnect -and -not (Test-Running $serverPattern)) {
    Start-Process -FilePath (Join-Path $release 'urSovngardeServer.exe') -WorkingDirectory $release -WindowStyle Hidden
    $startedServer = $true
}
@{ startedServer = $startedServer } | ConvertTo-Json | Set-Content -LiteralPath $stateFile

try {
    Start-Process -FilePath $mo2 -ArgumentList '"moshortcut://:Skyrim Together"'
    Wait-Until { Test-Running $gamePattern } 120 'the game to start'
    Wait-Until { Get-Health } $MenuTimeout 'DevBench to answer (the game to start)' -NeedsGame
    Wait-Until { (Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'Main Menu' } $MenuTimeout 'the main menu' -NeedsGame

    # The rig's own start save, once made, spares the new game and its Survival Mode question.
    if (-not $Save -and (Test-Path (Join-Path $root "profiles\Rig\saves\$startSave.ess"))) { $Save = $startSave }

    if ($Save) {
        Invoke-Tool 'game' @{ action = 'load'; name = $Save } | Out-Null
        Wait-Until { $h = Get-Health; $h -and $h.lastLifecycle -eq 'postLoadGame' } $LoadTimeout 'the save to load' -NeedsGame
    } else {
        try { Invoke-Tool 'console' @{ action = 'exec'; command = $StartCommand } | Out-Null } catch { Write-Host "The start command gave no answer yet: $($_.Exception.Message)" }
        Start-Sleep -Seconds 5
        Wait-Until { -not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -match '^(Main Menu|Loading Menu)$') } $LoadTimeout 'the start cell to load' -NeedsGame
    }
    Start-Sleep -Seconds 5

    # A new game asks whether to turn Survival Mode on, in a message box that pauses it: the answer is No.
    # DevBench's keyboard does not reach the flat game's menus, so the keys go in at the Windows level.
    foreach ($attempt in 1..5) {
        if (-not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'MessageBoxMenu')) { break }
        if (Set-GameFocus) {
            Send-Key 0x27
            Send-Key 0x0D
            Write-Host 'Answered No to a message box.'
        }
        Start-Sleep -Seconds 2
    }
    if ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'MessageBoxMenu') { throw 'A message box stayed open and pauses the game.' }

    # Made once, after a new game's question is answered: the next starts load it.
    if (-not $Save) {
        try { Invoke-Tool 'console' @{ action = 'exec'; command = "save $startSave" } | Out-Null; Start-Sleep -Seconds 4; Write-Host "Saved the rig's start as $startSave." }
        catch { Write-Host "The start save was not made: $($_.Exception.Message)" -ForegroundColor Yellow }
    }

    if ($NoConnect) {
        Write-Host 'Up, not connected (no server was started).' -ForegroundColor Green
        Show-Status
        return
    }

    # The client reads F6 with GetAsyncKeyState, so it is pressed at the Windows level, with the game's window in front.
    if (-not (Set-GameFocus)) { Write-Host 'The game window could not be brought to the front; F6 may go unheard.' -ForegroundColor Yellow }
    Send-Key 0x75 300
    Write-Host 'Pressed F6 to connect.'
    Wait-Until { Get-Connected } 60 'the connection' -NeedsGame
    Write-Host 'Up.' -ForegroundColor Green
    Show-Status
}
catch {
    Write-Host "Failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Cleaning up.'
    Stop-Everything
    exit 1
}
