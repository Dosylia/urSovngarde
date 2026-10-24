# Zips everything needed for an urSovngarde bug report onto the Desktop.
# Run it from the mod's folder, next to urSovngarde.exe (collect-logs.bat does that), or pass -ClientFolder.
param(
    [string]$ClientFolder = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm'
$staging = Join-Path $env:TEMP "urSovngarde-logs-$stamp"
$zip = Join-Path ([Environment]::GetFolderPath('Desktop')) "urSovngarde-logs-$stamp.zip"
New-Item -ItemType Directory -Force $staging | Out-Null

function Add-File([string]$Path, [string]$Name = (Split-Path $Path -Leaf)) {
    if (Test-Path $Path) {
        Copy-Item $Path (Join-Path $staging $Name)
        Write-Host "  + $Name"
    }
}

Write-Host "Collecting urSovngarde logs..."

# Client logs (the rotated ones hold earlier sessions of the same day).
Get-ChildItem (Join-Path $ClientFolder 'logs') -Filter 'tp_client*.log' -ErrorAction SilentlyContinue | ForEach-Object { Add-File $_.FullName }
# CEF's log, its last 5 MB at most. On 2026-10-10 it grew to 8 GB in one session; copied whole it filled the disk and
# Compress-Archive, which cannot take a file over 2 GB, left the copy behind in %TEMP%.
$cef = Join-Path $ClientFolder 'logs\cef_debug.log'
$cefTail = 5MB
if ((Test-Path $cef) -and (Get-Item $cef).Length -gt $cefTail) {
    $in = [IO.File]::Open($cef, 'Open', 'Read', 'ReadWrite')
    try {
        $in.Seek(-$cefTail, 'End') | Out-Null
        $out = [IO.File]::Create((Join-Path $staging 'cef_debug.log'))
        try { $in.CopyTo($out) } finally { $out.Close() }
    } finally { $in.Close() }
    Write-Host "  + cef_debug.log (its last 5 MB of $([math]::Round((Get-Item $cef).Length / 1MB)) MB)"
} else {
    Add-File $cef
}
Add-File (Join-Path $ClientFolder 'logs\dashboard_frame.bmp')

# Every SKSE crash log (Crash Logger) from the last day, not just the newest.
#
# Only the most recent one is not enough: a player can crash several times, and a single dump may come from another session.
# A Crash Logger stack names modules and functions; the stack our own handler scans out of the raw stack memory does not.
# Several small text files beat one, and without them the only thing left to do is guess.
$skse = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Skyrim VR\SKSE'
Get-ChildItem $skse -Filter 'crash-*.log' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt (Get-Date).AddDays(-1) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 8 |
    ForEach-Object { Add-File $_.FullName }

# Newest crash dump from the last day. They are large, but compress well.
$dumps = @(Get-ChildItem $ClientFolder -Filter 'crash_*.dmp' -ErrorAction SilentlyContinue)
# Our crash handler writes its small dumps next to the client log.
$dumps += @(Get-ChildItem (Join-Path $ClientFolder 'logs') -Filter 'crash_*.dmp' -ErrorAction SilentlyContinue)
$overwrite = Join-Path (Split-Path (Split-Path $ClientFolder -Parent) -Parent) 'overwrite\Root'
$dumps += @(Get-ChildItem $overwrite -Filter 'crash_*.dmp' -ErrorAction SilentlyContinue)
# Two, not one: a crash and the one before it are often the pair that tells them apart. They are large, so not
# more than two. A crash leaves a small dump and a full one; the small ones come first, so two dumps are two crashes.
$dumps = @($dumps | Where-Object { $_.LastWriteTime -gt (Get-Date).AddDays(-1) })
$small = @($dumps | Where-Object { $_.Name -like '*.small.dmp' })
if ($small.Count -gt 0) { $dumps = $small }
# The freeze watchdog's dump of every thread (one file, the latest freeze), when it is from the last day.
$freeze = Get-Item (Join-Path $ClientFolder 'logs\freeze.small.dmp') -ErrorAction SilentlyContinue
if ($freeze -and $freeze.LastWriteTime -gt (Get-Date).AddDays(-1)) { Add-File $freeze.FullName }
$dumps |
    Sort-Object LastWriteTime -Descending | Select-Object -First 2 |
    ForEach-Object { Add-File $_.FullName }

# Build of the client, from the first log line that names it.
$log = Join-Path $ClientFolder 'logs\tp_client.log'
if (Test-Path $log) {
    $build = Select-String -Path $log -Pattern 'Skyrim Together client, build (.+)$' | Select-Object -Last 1
    if ($build) { Set-Content (Join-Path $staging 'build.txt') $build.Matches[0].Groups[1].Value }
}

if (-not (Get-ChildItem $staging)) {
    Write-Host "No logs found in $ClientFolder. Run this from the mod's folder, next to urSovngarde.exe."
    Remove-Item $staging -Recurse -Force
    exit 1
}

# What Windows itself recorded in the last day: crash events, Windows Error Reporting files, display driver resets.
# A heap corruption (0xc0000374) ends the game without our crash report; only Windows records it.
# Nothing here is required, so a missing log or a denied folder is skipped.
$since = (Get-Date).AddDays(-1)
# Hides the Windows user name in paths such as C:\Users\<name>\... and C:/Users/<name>/...
# The backslash is doubled: a single one in a character class only escapes the slash after it.
function Hide-UserName([string]$Text) { $Text -replace '([\\/]Users[\\/])[^\\/\r\n]+', '$1<user>' }

$events = @()
try {
    $events += @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000, 1001, 1002; StartTime = $since } -ErrorAction Stop |
        Where-Object { $_.Message -match 'urSovngarde\.exe|SkyrimVR\.exe' })
} catch { }
try {
    $events += @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 4101; StartTime = $since } -ErrorAction Stop)
} catch { }
if ($events.Count) {
    $text = $events | Sort-Object TimeCreated | ForEach-Object {
        "[$($_.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss'))] $($_.LogName) event $($_.Id), $($_.ProviderName)`r`n$($_.Message)`r`n"
    }
    Set-Content (Join-Path $staging 'windows-events.txt') (Hide-UserName ($text -join "`r`n")) -Encoding UTF8
    Write-Host "  + windows-events.txt ($($events.Count) events)"
}

# The three newest Windows Error Reporting files for the game; the rest of each report folder is large and not needed.
$wer = Join-Path $env:ProgramData 'Microsoft\Windows\WER'
$reports = @()
foreach ($folder in 'ReportArchive', 'ReportQueue') {
    try {
        $reports += @(Get-ChildItem (Join-Path $wer $folder) -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like 'AppCrash_urSovngarde*' -or $_.Name -like 'AppCrash_SkyrimVR*' } |
            ForEach-Object { Get-Item (Join-Path $_.FullName 'Report.wer') -ErrorAction SilentlyContinue })
    } catch { }
}
$reports | Where-Object { $_ -and $_.LastWriteTime -gt $since } | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object {
    $name = "WER-Report-$($_.LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')).txt"
    try {
        Set-Content (Join-Path $staging $name) (Hide-UserName (Get-Content $_.FullName -Raw -ErrorAction Stop)) -Encoding UTF8
        Write-Host "  + $name"
    } catch { }
}

# SteamVR's own logs, from the last day: when the game cannot start VR, they say why.
# vrclient_SkyrimVR is the game's side (VR_Init errors), vrserver the headset and its drivers, vrmonitor and vrcompositor SteamVR itself.
# Each is cut to its last 2 MB, with the Windows user name and the PC's identifiers hidden; SteamVR keeps them open, so they are read shared.
# vrserver lists the network adapters with their addresses, every device has a GUID, and a headset's serial number names it.
function Hide-DeviceIds([string]$Text) {
    # A MAC first: the IPv6 rule would take it for an address.
    $Text = $Text -replace '\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b', '<mac>'
    $Text = $Text -replace '\b(?:\d{1,3}\.){3}\d{1,3}\b', '<ip>'
    $Text = $Text -replace '\b(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}\b', '<ip>'
    $Text = $Text -replace '\b[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*::[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*\b', '<ip>'
    $Text = $Text -replace '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', '<guid>'
    # "tracked device with serial number '...'", "Active HMD set to lighthouse.LHR-...", "Using existing HMD lighthouse.LHR-...".
    $Text = $Text -replace "(serial number ')[^']*", '$1<serial>'
    $Text = $Text -replace '(HMD (?:set to )?[\w-]+\.)[^\r\n]+', '$1<serial>'
    $Text = $Text -replace '(SerialNumber\w*\s*[=:]\s*)[^\s,]+', '$1<serial>'
    $Text -replace '\bLHR-[0-9a-f]{8}\b', '<serial>'
}
$steamPath = $null
try { $steamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath } catch { }
if (-not $steamPath) { $steamPath = Join-Path ${env:ProgramFiles(x86)} 'Steam' }
$vrLogs = Join-Path $steamPath 'logs'
$vrTail = 2MB
foreach ($pattern in 'vrclient_SkyrimVR*.txt', 'vrclient_urSovngarde*.txt', 'vrserver*.txt', 'vrmonitor*.txt', 'vrcompositor*.txt') {
    Get-ChildItem $vrLogs -Filter $pattern -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $since } | ForEach-Object {
        try {
            $in = [IO.File]::Open($_.FullName, 'Open', 'Read', 'ReadWrite')
            try {
                if ($in.Length -gt $vrTail) { $in.Seek(-$vrTail, 'End') | Out-Null }
                $text = (New-Object IO.StreamReader($in)).ReadToEnd()
            } finally { $in.Close() }
            Set-Content (Join-Path $staging "SteamVR-$($_.Name)") (Hide-DeviceIds (Hide-UserName $text)) -Encoding UTF8
            Write-Host "  + SteamVR-$($_.Name)"
        } catch { }
    }
}

Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -Force
Remove-Item $staging -Recurse -Force
Write-Host ""
Write-Host "Done: $zip"
Write-Host "Send that file."
