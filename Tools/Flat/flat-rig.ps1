# The flat (non-VR) test game: Skyrim Special Edition with our flat client, started through a Mod Organizer instance of its own.
#
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 up        # server, game, start cell, connected
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 down      # quits the game, and the server if this script started it
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\flat-rig.ps1 status
#   ... up -KeepConnect     the game joins what the launcher's Join wrote into connect.txt (the launcher's flat join); that server must already run
#
# The instance is a portable Mod Organizer for Skyrim Special Edition, in the folder the SKYRIM_FLAT_ROOT user variable names.
# Its profile "Rig" keeps its own saves and INIs, so the rig never touches a player's saves.
# The first start begins a new game outside Mistwatch, answers its Survival Mode question with No, and saves it as FlatRigStart; later starts load that save.
# Its mods: the Script Extender for the game's version, the Address Library for that version, DevBench, and SkyrimTogether.esp.
# DevBench answers on port 8922 there (its config.json), so this game can run beside the VR rig, which uses 8921.
# Its executable "Skyrim Together" starts tools\Skyrim Together\urSovngardeSE.exe; deploy-flat-client.ps1 puts each build there.
# The game never needs its window in front, where it would take the mouse and keyboard of whoever uses the PC.
# It connects by itself to the server its connect.txt names (%LOCALAPPDATA%\SkyrimTogetherSE), which this script points at the local test server for the run.
# It is started with URSOVNGARDE_TEST_RIG set, so it reads DevBench's keys with its window in the background.
# Whenever its window takes the focus (when it opens), the script gives it back to the window that had it.
# With both rigs: this one first, then headless.ps1 up -AutoConnect.

param(
    [Parameter(Position = 0)][ValidateSet('up', 'down', 'status')][string]$Action = 'status',
    [string]$Save = '',                         # a save of the Rig profile to load; empty runs the start command at the main menu instead
    [string]$StartCommand = 'cow Tamriel 34 -9', # run at the main menu when no save is given: the VR rig's start, outside Mistwatch
    [switch]$NoConnect,                         # no server and no F6: the game alone, mod loaded
    [switch]$KeepConnect,                       # the connect file is left as it is: the game joins what the launcher's Join wrote there
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
$gamePattern = '^(urSovngardeSE|SkyrimTogether)$'
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
        if (Get-Command Restore-UserFocus -ErrorAction SilentlyContinue) { Restore-UserFocus }
        if ($ok) { return }
        Start-Sleep -Milliseconds 1500
    }
    throw "Timed out after $seconds s waiting for $what."
}

# Mod Organizer asks whether to start Steam before it starts a Steam game, and nobody would see that question.
# So Steam is started first, quietly, as the VR rig's start of SteamVR does.
function Start-SteamIfNeeded {
    if (Test-Running '^steam$') { return }
    $steamExe = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
    if (-not $steamExe -or -not (Test-Path -LiteralPath $steamExe)) { throw 'Steam is not running and its program was not found: start Steam first.' }
    Write-Host 'Steam is not running: starting it.'
    Start-Process -FilePath $steamExe -ArgumentList '-silent'
    Wait-Until { Test-Running '^steamwebhelper$' } 90 'Steam to start'
    Start-Sleep -Seconds 10
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

# The game's message boxes pause it; DevBench reads the open one and presses one of its buttons.
# Loading a save with mods asks whether to go on without achievements: Yes.
# A new game asks whether to turn Survival Mode on: No.
# Returns the answer given, or null when no box is open, the box is not one of those, its answer is not in -Allowed, or DevBench did not answer.
function Answer-MessageBox([string[]]$Allowed = @('Yes', 'No')) {
    try {
        $box = Invoke-Tool 'menu' @{ action = 'describe' }
        if (-not $box.messageBoxOpen) { return $null }
        $answer = if ($box.bodyText -match 'Mods are currently loaded') { 'Yes' } elseif ($box.bodyText -match 'Survival') { 'No' } else { $null }
        if ($answer -and $Allowed -notcontains $answer) { return $null }
        $index = if ($answer) { [array]::IndexOf([string[]]$box.buttons, $answer) } else { -1 }
        if ($index -lt 0) { Write-Host "A message box the rig does not know: $($box.bodyText)" -ForegroundColor Yellow; return $null }
        Invoke-Tool 'menu' @{ action = 'accept'; index = $index } | Out-Null
        Write-Host "Answered $answer to: $($box.bodyText)"
        return $answer
    }
    catch { return $null }
}

# Mod Organizer runs once per PC: a second start hands its shortcut to the first one started without --multiple, the only one that takes shortcuts.
# The Mod Organizers running, each with whether it is this instance's and whether it was started with --multiple.
function Get-Mo2Processes {
    $own = [IO.Path]::GetFullPath($mo2)
    Get-CimInstance Win32_Process -Filter "Name='ModOrganizer.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
        [pscustomobject]@{
            Id       = $_.ProcessId
            Own      = [bool]($_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath) -eq $own)
            Multiple = [bool]($_.CommandLine -match '--multiple')
        }
    }
}

# Closes them politely, and by force after 15 s.
function Close-Mo2([object[]]$processes) {
    if (-not $processes) { return }
    foreach ($entry in $processes) { $process = Get-Process -Id $entry.Id -ErrorAction SilentlyContinue; if ($process) { $process.CloseMainWindow() | Out-Null } }
    try { Wait-Until { -not ($processes | Where-Object { Get-Process -Id $_.Id -ErrorAction SilentlyContinue }) } 15 'Mod Organizer to close' }
    catch { $processes | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } }
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
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr window, out uint processId);
[DllImport("user32.dll")] public static extern bool AttachThreadInput(uint thread, uint toThread, bool attach);
[DllImport("user32.dll")] public static extern bool IsWindow(System.IntPtr window);
[DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
'@
}

# The window the person at the PC was using when the run started.
$userWindow = [FlatRigKeys]::GetForegroundWindow()

# The game's window takes the focus when it opens, and with it the mouse and keyboard: they go back to the person's window.
# Only the rig's own windows are taken from the front (the game, this instance's Mod Organizer); a window the person picked stays.
# Joined to the front window's input thread for the call, this script may hand the focus on without a key press of its own.
function Restore-UserFocus {
    if ($userWindow -eq [IntPtr]::Zero -or -not [FlatRigKeys]::IsWindow($userWindow)) { return }
    $front = [FlatRigKeys]::GetForegroundWindow()
    if ($front -eq [IntPtr]::Zero -or $front -eq $userWindow) { return }
    $frontProcessId = [uint32]0
    $frontThread = [FlatRigKeys]::GetWindowThreadProcessId($front, [ref]$frontProcessId)
    $process = Get-Process -Id $frontProcessId -ErrorAction SilentlyContinue
    if (-not $process) { return }
    $ownMo2 = $process.ProcessName -eq 'ModOrganizer' -and $process.Path -and $process.Path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)
    if ($process.ProcessName -notmatch $gamePattern -and -not $ownMo2) { return }
    $ownThread = [FlatRigKeys]::GetCurrentThreadId()
    [FlatRigKeys]::AttachThreadInput($ownThread, $frontThread, $true) | Out-Null
    [FlatRigKeys]::SetForegroundWindow($userWindow) | Out-Null
    [FlatRigKeys]::AttachThreadInput($ownThread, $frontThread, $false) | Out-Null
}

function Write-RigQuit([string]$what) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $what
    Write-Host $line
    try { Add-Content -LiteralPath (Join-Path $clientDir 'logs\rig-quits.log') -Value $line } catch { }
}

# The flat game's own folder; the VR game's is SkyrimTogetherVR, so the two rigs never share it.
$connectFile = Join-Path $env:LOCALAPPDATA 'SkyrimTogetherSE\connect.txt'
$connectBackup = "$connectFile.before-rig"

function Restore-Connect {
    if (-not (Test-Path -LiteralPath $connectBackup)) { return }
    # An empty backup means there was no file before the run.
    if ((Get-Item -LiteralPath $connectBackup).Length -eq 0) { Remove-Item -LiteralPath $connectFile -ErrorAction SilentlyContinue }
    else { Copy-Item -LiteralPath $connectBackup -Destination $connectFile -Force }
    Remove-Item -LiteralPath $connectBackup -Force
    Write-Host 'Connect file put back as it was.'
}

function Set-RigConnect {
    if (Test-Path -LiteralPath $connectBackup) { Restore-Connect }
    New-Item -ItemType Directory -Force -Path (Split-Path $connectFile) | Out-Null
    if (Test-Path -LiteralPath $connectFile) { Copy-Item -LiteralPath $connectFile -Destination $connectBackup -Force }
    else { Set-Content -LiteralPath $connectBackup -Value '' -NoNewline }
    $port = 10578
    $ini = Join-Path $release 'config\STServer.ini'
    if (Test-Path $ini) {
        $line = Select-String -LiteralPath $ini -Pattern '^\s*uPort\s*=\s*(\d+)' | Select-Object -First 1
        if ($line) { $port = [int]$line.Matches[0].Groups[1].Value }
    }
    # With -NoConnect an address nothing listens on: a server the owner hosts on this PC is not joined by a stray F6.
    # Otherwise the game connects by itself a few seconds after the load, as the launcher's "connect automatically" does.
    $text = if ($NoConnect) { '127.0.0.1:1' } else { "127.0.0.1:$port`r`nautoconnect=on" }
    Set-Content -LiteralPath $connectFile -Value $text -Encoding ascii
    Write-Host "Connect file set to $(($text -split "`r`n")[0]) for this run$(if (-not $NoConnect) { ', connecting by itself' })."
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
    # This instance's Mod Organizer is closed too: while it runs, a player's own Mod Organizer started by hand hands itself to it.
    Close-Mo2 @(Get-Mo2Processes | Where-Object { $_.Own })
    Restore-Connect
    Remove-Item $stateFile -ErrorAction SilentlyContinue
}

if ($Action -eq 'status') { Show-Status; return }
if ($Action -eq 'down') { Stop-Everything; Show-Status; return }

if (Test-Running $gamePattern) { throw 'The flat game is already running: run "down" first.' }
if (-not (Test-Path (Join-Path $clientDir 'urSovngardeSE.exe'))) { throw "No flat client in ${clientDir}: run Tools\Flat\deploy-flat-client.ps1 first." }
if ($KeepConnect -and $NoConnect) { throw 'Use -KeepConnect or -NoConnect, not both.' }
if ($KeepConnect) {
    # A backup left by an interrupted run would be put back over the launcher's file at "down", and the file may still be the rig's own.
    if (Test-Path -LiteralPath $connectBackup) { throw 'A previous run left the connect file changed: run "down" first, then press Join in the launcher again.' }
    if (-not (Test-Path -LiteralPath $connectFile)) { throw "With -KeepConnect, $connectFile must exist: press Join in the launcher first." }
    # Without that line the game waits for the player to connect, and nothing here presses it.
    if (-not (Select-String -LiteralPath $connectFile -Pattern '^\s*autoconnect=on\s*$' -Quiet)) { throw 'The connect file has no "autoconnect=on" line: turn on "connect automatically" in the launcher and press Join again.' }
}

$startedServer = $false
# With -KeepConnect the game joins the server the launcher named, which must already run; a test server here could hold the port a launcher Host needs.
if (-not $NoConnect -and -not $KeepConnect -and -not (Test-Running $serverPattern)) {
    Start-Process -FilePath (Join-Path $release 'urSovngardeServer.exe') -WorkingDirectory $release -WindowStyle Hidden
    $startedServer = $true
}
@{ startedServer = $startedServer } | ConvertTo-Json | Set-Content -LiteralPath $stateFile
# With -NoConnect too: the game then has nothing to join, never the player's own server.
# With -KeepConnect no backup is made, so "down" leaves the launcher's file as it is.
if ($KeepConnect) { Write-Host "Connect file kept: $((Get-Content -LiteralPath $connectFile -TotalCount 1)), as the launcher wrote it." }
else { Set-RigConnect }

try {
    # This instance's Mod Organizer is started anew, so the game inherits the variable below from it.
    # With another instance's running (the VR game's or the player's), this one starts beside it.
    Close-Mo2 @(Get-Mo2Processes | Where-Object { $_.Own })
    Start-SteamIfNeeded
    $mo2Running = @(Get-Mo2Processes)
    $mo2Options = if ($mo2Running.Count -gt 0) { '--multiple ' } else { '' }
    $env:URSOVNGARDE_TEST_RIG = '1'
    Start-Process -FilePath $mo2 -ArgumentList "${mo2Options}`"moshortcut://:Skyrim Together`""
    Remove-Item Env:URSOVNGARDE_TEST_RIG
    Wait-Until { Test-Running $gamePattern } 120 'the game to start'
    Wait-Until { Get-Health } $MenuTimeout 'DevBench to answer (the game to start)' -NeedsGame
    Wait-Until { (Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'Main Menu' } $MenuTimeout 'the main menu' -NeedsGame

    # The rig's own start save, once made, spares the new game and its Survival Mode question.
    if (-not $Save -and (Test-Path (Join-Path $root "profiles\Rig\saves\$startSave.ess"))) { $Save = $startSave }

    if ($Save) {
        Invoke-Tool 'game' @{ action = 'load'; name = $Save } | Out-Null
        Wait-Until { Answer-MessageBox -Allowed 'Yes' | Out-Null; $h = Get-Health; $h -and $h.lastLifecycle -eq 'postLoadGame' } $LoadTimeout 'the save to load' -NeedsGame
    } else {
        try { Invoke-Tool 'console' @{ action = 'exec'; command = $StartCommand } | Out-Null } catch { Write-Host "The start command gave no answer yet: $($_.Exception.Message)" }
        Start-Sleep -Seconds 5
        Wait-Until { -not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -match '^(Main Menu|Loading Menu)$') } $LoadTimeout 'the start cell to load' -NeedsGame
    }
    Start-Sleep -Seconds 5

    # A new game asks about Survival Mode some seconds after it has loaded, and a save made before the answer asks again.
    $answeredSurvival = $false
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if ((Answer-MessageBox) -eq 'No') { $answeredSurvival = $true; break }
        Start-Sleep -Seconds 1
    }
    # An answer is queued for the game's next frame, so the box closes a moment later.
    try { Wait-Until { -not ((Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'MessageBoxMenu') } 10 'the message box to close' -NeedsGame }
    catch { if (-not (Test-Running $gamePattern)) { throw }; throw 'A message box stayed open and pauses the game.' }

    # Made after a new game's question is answered, before any connection, so the next starts load it without the question.
    if (-not $Save -or ($Save -eq $startSave -and $answeredSurvival)) {
        try { Invoke-Tool 'console' @{ action = 'exec'; command = "save $startSave" } | Out-Null; Start-Sleep -Seconds 4; Write-Host "Saved the rig's start as $startSave." }
        catch { Write-Host "The start save was not made: $($_.Exception.Message)" -ForegroundColor Yellow }
    }

    if ($NoConnect) {
        Write-Host 'Up, not connected (no server was started).' -ForegroundColor Green
        Show-Status
        return
    }

    Wait-Until { Get-Connected } 60 'the connection (the game connects by itself after the load)' -NeedsGame
    Write-Host 'Up.' -ForegroundColor Green
    Show-Status
}
catch {
    Write-Host "Failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Cleaning up.'
    Stop-Everything
    exit 1
}
