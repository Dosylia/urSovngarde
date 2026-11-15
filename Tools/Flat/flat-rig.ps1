# The flat (non-VR) test game: Skyrim Special Edition with our flat client, started through a Mod Organizer instance of its own.
#
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 up        # server, game, start cell, connected
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 down      # quits the game, and the server if this script started it
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 status
#
# The instance is a portable Mod Organizer for Skyrim Special Edition, in the folder the SKYRIM_FLAT_ROOT user variable names.
# Its profile "Rig" keeps its own saves and INIs, so the rig never touches a player's saves.
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

    if ($Save) {
        Invoke-Tool 'game' @{ action = 'load'; name = $Save } | Out-Null
        Wait-Until { $h = Get-Health; $h -and $h.lastLifecycle -eq 'postLoadGame' } $LoadTimeout 'the save to load' -NeedsGame
    } else {
        try { Invoke-Tool 'console' @{ action = 'exec'; command = $StartCommand } | Out-Null } catch { Write-Host "The start command gave no answer yet: $($_.Exception.Message)" }
        Start-Sleep -Seconds 5
        Wait-Until { -not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -match '^(Main Menu|Loading Menu)$') } $LoadTimeout 'the start cell to load' -NeedsGame
    }
    Start-Sleep -Seconds 5

    if ($NoConnect) {
        Write-Host 'Up, not connected (no server was started).' -ForegroundColor Green
        Show-Status
        return
    }

    # The client reads F6 with GetAsyncKeyState, so it is pressed at the Windows level.
    if (-not ('FlatRigKeys' -as [type])) {
        Add-Type -Name FlatRigKeys -Namespace '' -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);'
    }
    [FlatRigKeys]::keybd_event(0x75, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [FlatRigKeys]::keybd_event(0x75, 0, 2, [UIntPtr]::Zero)
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
