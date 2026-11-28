# This PC's folders, as the hub keeps them for its runner, read from the file the runner writes.
# Dot-source it: . (Join-Path $PSScriptRoot 'PcFolders.ps1')
# Get-PcFolder returns a full folder, or nothing when the file does not name it; the callers then keep their own search.

function Get-PcFolder([string]$Key) {
    $file = Join-Path $env:LOCALAPPDATA 'urSovngarde-tools\folders.json'
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    try { $value = (Get-Content -LiteralPath $file -Raw -Encoding UTF8 | ConvertFrom-Json).folders.$Key } catch { return $null }
    if ($value -is [string] -and $value) { return $value }
    return $null
}

# The FUS install: SKYRIM_FUS_ROOT (it picks the modlist for one run), else this PC's list, else C:\FUS or E:\FUS where Mod Organizer is.
function Get-FusRoot {
    if ($env:SKYRIM_FUS_ROOT) { return $env:SKYRIM_FUS_ROOT }
    $mine = Get-PcFolder 'fus'
    if ($mine) { return $mine }
    return @('C:\FUS', 'E:\FUS') | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'ModOrganizer.exe') } | Select-Object -First 1
}

# The client's folder of an install: this PC's list's when the install is its FUS or MGO, else the tool folder holding urSovngarde.exe.
# 'Skyrim Together VR' on the host's PC, 'urSovngarde' on another player's.
function Get-ClientFolder([string]$Root) {
    foreach ($name in 'fus', 'mgo') {
        $install = Get-PcFolder $name
        $client = Get-PcFolder "${name}Client"
        if ($install -and $client -and ($install.TrimEnd('\', '/') -ieq $Root.TrimEnd('\', '/'))) { return $client }
    }
    $holding = @('Skyrim Together VR', 'urSovngarde') | ForEach-Object { Join-Path $Root "tools\$_" } | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'urSovngarde.exe') } | Select-Object -First 1
    if ($holding) { return $holding }
    return Join-Path $Root 'tools\Skyrim Together VR'
}
