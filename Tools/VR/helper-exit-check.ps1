# The game's browser helpers (TPProcess.exe) end with the game, even when the game ends without closing them, as in a crash.
# A helper left behind keeps Mod Organizer's virtual file system loaded and holds files the update installer must replace.
#
#   powershell -ExecutionPolicy Bypass -File Tools\VR\helper-exit-check.ps1 -StuckMain              must PASS on a build where helpers end with the game
#   powershell -ExecutionPolicy Bypass -File Tools\VR\helper-exit-check.ps1 -StuckMain -ExpectFail  on a build without it: must be RED
#
# Without -StuckMain the helpers usually quit by themselves when the game is killed (6 of 6 on a build without the fix),
# so only -StuckMain tells a build with the fix from one without it.
#
# It brings the rig up (headless.ps1 up -NoConnect), notes the helpers the game started (their "--pid=<game>" switch),
# ends the game process at once as a crash would, and checks them 15 s later. The rig's save copy keeps the saves as they were.

param(
    [switch]$ExpectFail,
    # Each helper's main thread suspended first, as a helper stuck in a call would be.
    # A helper then cannot quit by itself when the game is gone, which is what left helpers behind in play.
    [switch]$StuckMain,
    [int]$WaitSeconds = 15
)

$ErrorActionPreference = 'Stop'
$headless = Join-Path $PSScriptRoot 'headless.ps1'

# headless.ps1 defines its paths and helpers before it acts; "status" only prints one line.
. $headless status

# The same refusals as "up", made before the try: its finally runs "down", which quits whatever game runs and closes SteamVR.
# A refusal tests nothing: it ends as INVALID (2), never as a failure.
if (Test-Running '^(urSovngarde|SkyrimTogetherVR|SkyrimVR)$') { Write-Host '   INVALID  The game is already running. If that is a real session, leave it alone; otherwise run "headless.ps1 down" first.'; exit 2 }
if (Test-Running '^(vrserver|vrmonitor)$') { Write-Host '   INVALID  SteamVR is already running. If the headset is in use, leave it alone; otherwise close it first.'; exit 2 }

$verdict = 'INVALID'
$detail = ''
# The helpers this run suspended: a suspended helper never quits by itself, and "down" does not stop it.
$suspended = New-Object System.Collections.Generic.List[int]
try {
    $global:LASTEXITCODE = 0
    & $headless up -NoConnect
    $upExit = $LASTEXITCODE

    $game = Get-Process $gameProcess -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $game) { throw "The game is not running after ""up"" (headless.ps1 exit $upExit)." }

    $helpers = @(Get-CimInstance Win32_Process -Filter "Name = 'TPProcess.exe'" | Where-Object { $_.CommandLine -match "--pid=$($game.Id)(\s|$)" })
    if (-not $helpers.Count) {
        $detail = "the game (process $($game.Id)) has no helper with --pid=$($game.Id); nothing to test"
    }
    else {
        $kinds = ($helpers | ForEach-Object { if ($_.CommandLine -match '--type=([\w-]+)') { $Matches[1] } else { 'browser' } }) -join ', '
        Write-Host "The game ($($game.Id)) has $($helpers.Count) helper(s): $kinds. Ending the game process as a crash would."
        if ($StuckMain) {
            Add-Type -Namespace HelperExitCheck -Name Win32 -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenThread(uint access, bool inherit, uint id);
[DllImport("kernel32.dll")] public static extern uint SuspendThread(System.IntPtr thread);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr handle);
'@
            foreach ($helper in $helpers) {
                $main = (Get-Process -Id $helper.ProcessId).Threads | Sort-Object StartTime | Select-Object -First 1
                $thread = [HelperExitCheck.Win32]::OpenThread(0x0002, $false, [uint32]$main.Id)
                if ($thread -eq [IntPtr]::Zero) { throw "Could not open the main thread of helper $($helper.ProcessId)." }
                [HelperExitCheck.Win32]::SuspendThread($thread) | Out-Null
                [HelperExitCheck.Win32]::CloseHandle($thread) | Out-Null
                $suspended.Add([int]$helper.ProcessId)
            }
            Write-Host 'Each helper''s main thread is suspended.'
        }
        Stop-Process -Id $game.Id -Force
        Start-Sleep -Seconds $WaitSeconds

        $left = @($helpers | Where-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue })
        if ($left.Count) {
            $verdict = 'FAIL'
            $detail = "$($left.Count) of $($helpers.Count) helper(s) still running $WaitSeconds s after the game ended: " +
                (($left | ForEach-Object { if ($_.CommandLine -match '--type=([\w-]+)') { $Matches[1] } else { 'browser' } }) -join ', ')
            # Left behind by this test: ended here, so the machine is as it was.
            $left | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        }
        else {
            $verdict = 'PASS'
            $detail = "all $($helpers.Count) helper(s) ended with the game"
        }
    }
}
catch {
    # The rig did not come up, or the run broke off before its verdict: it proves nothing either way.
    $verdict = 'INVALID'
    $detail = "the run broke off: $_"
}
finally {
    $suspended | ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
    & $headless down
}

Write-Host ('   {0}  {1}' -f $verdict, $detail)
if ($verdict -eq 'INVALID') { exit 2 }
if ($ExpectFail) {
    if ($verdict -eq 'FAIL') { Write-Host 'RED as expected.' -ForegroundColor Green; exit 0 }
    Write-Host 'NOT RED: no helper was left behind on this build, so this run cannot show the fix.' -ForegroundColor Red; exit 1
}
if ($verdict -eq 'FAIL') { exit 1 }
