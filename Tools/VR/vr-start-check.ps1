# The game started while SteamVR has no headset.
# Without VRStart the game runs on without VR and crashes at its first loading screen (SkyrimVR.exe+0xC53E1B).
# With it, the game waits in a dialog, and "Try again" starts VR once a headset is there.
#
#   powershell -ExecutionPolicy Bypass -File Tools\VR\vr-start-check.ps1              on a build with VRStart: must PASS
#   powershell -ExecutionPolicy Bypass -File Tools\VR\vr-start-check.ps1 -ExpectFail  on a build without it: must be RED
#   powershell -ExecutionPolicy Bypass -File Tools\VR\vr-start-check.ps1 -Close       presses "Close the game" instead: the game must end
#                                                                                   with code 4, an "Exit:" line and no crash on the way out
#
# It starts the game the way players do (Mod Organizer's entry) with SteamVR closed and the rig's null headset off.
# Then it switches the null headset on, as a player would connect theirs, and presses "Try again" in the dialog.
# Needs: the game, SteamVR and the rig all closed, and the owner not playing. The game never gets past the main menu, and the saves are copied aside and put back as "up" and "down" do.

param(
    [switch]$ExpectFail,
    [switch]$Close,
    [int]$Timeout = 180
)

$ErrorActionPreference = 'Stop'

# headless.ps1 defines its paths and helpers before it acts; "status" only prints one line.
. (Join-Path $PSScriptRoot 'headless.ps1') status

# A refusal tests nothing: it ends as INVALID (2), never as a failure.
if (Test-Running $gamePattern) { Write-Host 'INVALID: the game is running. If that is a real session, leave it alone.' -ForegroundColor Yellow; exit 2 }
if (Test-Running '^(vrserver|vrmonitor|vrcompositor)$') { Write-Host 'INVALID: SteamVR is running: this check needs it closed, so that the game starts with no headset.' -ForegroundColor Yellow; exit 2 }
if (([System.IO.File]::ReadAllText($settingsPath) | ConvertFrom-Json).PSObject.Properties.Name -contains 'driver_null') {
    Write-Host 'INVALID: the rig null headset is still switched on (driver_null in steamvr.vrsettings); run "headless.ps1 down" first.' -ForegroundColor Yellow; exit 2
}

# As "up" does: the profile MO2 starts the game in, written into the state file, then its saves aside.
# Stop-Everything puts them back however the run ends.
# Engine Fixes deletes co-saves that have no save at every start, so without this a run changes the saves folder.
Switch-ToSelectedProfile
@{ startedServer = $false; save = ''; profileDir = $profileDir } | ConvertTo-Json | Set-Content $stateFile
Copy-SavesAside

$results = New-Object System.Collections.Generic.List[string]
$failed = 0
function Check([string]$what, [bool]$ok, [string]$detail) {
    $script:results.Add(('   {0}  {1,-48} {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $what, $detail))
    if (-not $ok) { $script:failed++ }
}

# Only the lines this run writes: the client log can roll over at start, so lines are taken by their time.
$started = Get-Date
function Get-RunLines {
    if (-not (Test-Path -LiteralPath $clientLog)) { return @() }
    $stamp = $started.ToString('yyyy-MM-dd HH:mm:ss')
    @(Get-Content -LiteralPath $clientLog | Where-Object { $_.Length -gt 20 -and $_.Substring(1, 19) -ge $stamp })
}
function Find-Line([string]$pattern) { Get-RunLines | Where-Object { $_ -match $pattern } | Select-Object -First 1 }

$savesBefore = @(Get-ChildItem -LiteralPath $savesDir -File | ForEach-Object { '{0}|{1}|{2}' -f $_.Name, $_.Length, $_.LastWriteTimeUtc.Ticks })

Add-Type -Namespace VRStartCheck -Name Win32 -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr hwnd, System.IntPtr param);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, System.IntPtr param);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr hwnd);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(System.IntPtr hwnd, System.Text.StringBuilder text, int max);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(System.IntPtr hwnd, System.Text.StringBuilder text, int max);
[DllImport("user32.dll")] public static extern System.IntPtr SendMessageW(System.IntPtr hwnd, uint msg, System.IntPtr wParam, System.IntPtr lParam);
'@

# The game's visible top-level windows, with their titles and classes, so a run shows what was there.
function Get-GameWindows {
    $gameIds = @(Get-Process $gameProcess -ErrorAction SilentlyContinue | ForEach-Object { [uint32]$_.Id })
    if (-not $gameIds.Count) { return @() }
    $found = New-Object System.Collections.Generic.List[object]
    $callback = [VRStartCheck.Win32+EnumProc] {
        param($hwnd, $param)
        $windowPid = [uint32]0
        [VRStartCheck.Win32]::GetWindowThreadProcessId($hwnd, [ref]$windowPid) | Out-Null
        if ($gameIds -contains $windowPid -and [VRStartCheck.Win32]::IsWindowVisible($hwnd)) {
            $title = New-Object System.Text.StringBuilder 256
            $class = New-Object System.Text.StringBuilder 256
            [VRStartCheck.Win32]::GetWindowTextW($hwnd, $title, 256) | Out-Null
            [VRStartCheck.Win32]::GetClassNameW($hwnd, $class, 256) | Out-Null
            $found.Add([pscustomobject]@{ Handle = $hwnd; Title = $title.ToString(); Class = $class.ToString() })
        }
        return $true
    }
    [VRStartCheck.Win32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
    return $found
}

# The dialog: the game's window titled urSovngarde, a dialog-class window first.
function Find-StartDialog {
    $windows = @(Get-GameWindows)
    Write-Host ('   (the game''s windows: {0})' -f (($windows | ForEach-Object { "'{0}' {1}" -f $_.Title, $_.Class }) -join ', '))
    $named = @($windows | Where-Object { $_.Title -eq 'urSovngarde' })
    $dialog = $named | Where-Object { $_.Class -eq '#32770' } | Select-Object -First 1
    if (-not $dialog) { $dialog = $named | Select-Object -First 1 }
    if (-not $dialog) { $dialog = $windows | Where-Object { $_.Class -eq '#32770' } | Select-Object -First 1 }
    if ($dialog) { return $dialog.Handle }
    return [IntPtr]::Zero
}

try {
    Write-Host "Starting the game with no headset ($mo2Shortcut)."
    Start-Process -FilePath $mo2 -ArgumentList "`"moshortcut://:$mo2Shortcut`""

    # Which comes first: a crash, the dialog, or a game that started anyway.
    # DevBench cannot tell: it is an SKSE plugin and answers before the game starts VR.
    $outcome = $null
    $deadline = (Get-Date).AddSeconds($Timeout)
    while (-not $outcome -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        if (Find-Line 'VectoredExceptionHandler: in ') { $outcome = 'crash' }
        elseif (Find-Line 'VRStart: OpenVR did not start') { $outcome = 'dialog' }
        elseif (Find-Line 'VRStart: OpenVR started|Menu queued: Main Menu') { $outcome = 'started' }
    }

    if ($outcome -eq 'crash') {
        $crash = Find-Line 'VectoredExceptionHandler: in '
        Check 'no crash at the first loading screen' $false ($crash -replace '^.*VectoredExceptionHandler: ', '')
    }
    elseif ($outcome -eq 'started') {
        Write-Host 'INVALID: the game started VR on its own, so a headset was found; nothing was tested.' -ForegroundColor Yellow
        $failed = -1
    }
    elseif ($outcome -ne 'dialog') {
        Write-Host "INVALID: neither a crash nor the dialog within $Timeout s." -ForegroundColor Yellow
        $failed = -1
    }
    else {
        Check 'the game waits instead of starting without VR' $true ((Find-Line 'VRStart: OpenVR did not start') -replace '^.*VRStart: ', '')

        # A handle taken before the game ends keeps its exit code readable afterwards.
        $games = @(Get-Process $gameProcess -ErrorAction SilentlyContinue)
        $games | ForEach-Object { $null = $_.Handle }

        if ($Close) {
            # Pressed as soon as the dialog shows: on this PC something cancelled it about 8 s in, before a later look found it.
            $dialog = [IntPtr]::Zero
            foreach ($look in 1..15) {
                $dialog = Find-StartDialog
                if ($dialog -ne [IntPtr]::Zero) { break }
                Start-Sleep -Seconds 1
            }
        }
        else {
            # The dialog holds the game: no crash, no loading screen.
            Start-Sleep -Seconds 20
            Check 'no crash while it waits' (-not (Find-Line 'VectoredExceptionHandler: in ')) ''
            Check 'no loading screen while it waits' (-not (Find-Line 'Menu queued: Loading Menu')) ''
            $dialog = Find-StartDialog
        }
        Check 'the dialog is open' ($dialog -ne [IntPtr]::Zero) ''

        if ($Close) {
            # The player gives up: "Close the game" (id 102).
            if ($dialog -ne [IntPtr]::Zero) { [VRStartCheck.Win32]::SendMessageW($dialog, 0x466, [IntPtr]102, [IntPtr]::Zero) | Out-Null }
            $ended = $true
            foreach ($game in $games) { if (-not $game.WaitForExit(30000)) { $ended = $false } }
            Check 'Close the game ends the game' ($ended -and $games.Count -gt 0) ''
            $codes = @($games | Where-Object { $_.HasExited } | ForEach-Object { '0x{0:X}' -f $_.ExitCode })
            Check 'the game ends with code 4' ($codes.Count -gt 0 -and @($codes | Where-Object { $_ -ne '0x4' }).Count -eq 0) ($codes -join ', ')
            Check 'an Exit: line marks a normal close' ([bool](Find-Line 'Exit: the game is closing')) ''
            $crash = Find-Line 'VectoredExceptionHandler: in |crash occurred!|SevereException:'
            Check 'no crash on the way out' (-not $crash) ($crash -replace '^.*\] ', '')
        }
        else {
            # The player connects a headset: SteamVR, which the failed start launched, is closed, then started with the null headset.
            foreach ($attempt in 1..4) {
                foreach ($name in 'vrmonitor', 'vrstartup', 'vrdashboard', 'vrwebhelper', 'vrcompositor', 'vrserver', 'steamvr_room_setup') {
                    Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
                }
                Start-Sleep -Seconds 3
                if (-not (Test-Running '^(vrserver|vrmonitor|vrcompositor|vrdashboard|vrwebhelper|vrstartup)$')) { break }
            }
            Set-NullDriver $true
            Start-Process 'steam://rungameid/250820'
            Wait-Until { Test-Running '^vrserver$' } 90 'SteamVR to start'
            Start-Sleep -Seconds 12

            # Try again (TDM_CLICK_BUTTON on the button's id).
            $dialog = Find-StartDialog
            if ($dialog -ne [IntPtr]::Zero) { [VRStartCheck.Win32]::SendMessageW($dialog, 0x466, [IntPtr]101, [IntPtr]::Zero) | Out-Null }

            try { Wait-Until { Find-Line 'VRStart: OpenVR started' } 60 'OpenVR to start' -NeedsGame } catch { }
            Check 'Try again starts VR' ([bool](Find-Line 'VRStart: OpenVR started \(attempt [2-9]')) ((Find-Line 'VRStart: OpenVR started') -replace '^.*VRStart: ', '')
            try { Wait-Until { (Invoke-Tool 'menu' @{ action = 'list' }).openMenus -contains 'Main Menu' } 180 'the main menu' -NeedsGame } catch { }
            $menus = try { (Invoke-Tool 'menu' @{ action = 'list' }).openMenus -join ', ' } catch { 'DevBench not answering' }
            Check 'the game reaches its main menu' ($menus -match 'Main Menu') $menus
            Check 'no crash after VR started' (-not (Find-Line 'VectoredExceptionHandler: in ')) ''
        }
    }
}
catch {
    # The run broke off (SteamVR never started, a step threw): unless a check already failed, it proves nothing.
    Write-Host "INVALID: the run broke off: $_" -ForegroundColor Yellow
    if ($failed -eq 0) { $failed = -1 }
}
finally {
    Stop-Everything
    $savesAfter = @(Get-ChildItem -LiteralPath $savesDir -File | ForEach-Object { '{0}|{1}|{2}' -f $_.Name, $_.Length, $_.LastWriteTimeUtc.Ticks })
    $same = -not (Compare-Object $savesBefore $savesAfter)
    Write-Host ("Saves: {0} files before, {1} after, {2}." -f $savesBefore.Count, $savesAfter.Count, $(if ($same) { 'unchanged' } else { 'CHANGED' }))
}

$results | ForEach-Object { Write-Host $_ }
if ($failed -lt 0) { exit 2 }
if ($ExpectFail) {
    if ($failed -gt 0) { Write-Host 'RED as expected.' -ForegroundColor Green; exit 0 }
    Write-Host 'NOT RED: the build passed, so this run tested nothing.' -ForegroundColor Red; exit 1
}
if ($failed -gt 0) { Write-Host "FAILED: $failed check(s)." -ForegroundColor Red; exit 1 }
Write-Host 'PASS' -ForegroundColor Green
