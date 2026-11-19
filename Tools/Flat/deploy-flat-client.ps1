# Copies the freshly built flat client into the flat rig's Mod Organizer instance.
#
#   powershell -ExecutionPolicy Bypass -File Tools\Flat\deploy-flat-client.ps1
#
# The instance is the folder the SKYRIM_FLAT_ROOT user variable names; the client goes to its tools\Skyrim Together.
# That folder also needs the menu's browser files and UI, which a release package carries; this copies only what the build makes.
# The binary is checked before it is copied: its size, and the build's version string inside it.

param([int]$MinimumBytes = 5000000)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$release = Join-Path $repo 'build\windows\x64\release'
$root = $env:SKYRIM_FLAT_ROOT
if (-not $root -or -not (Test-Path (Join-Path $root 'ModOrganizer.exe'))) {
    throw 'No flat Mod Organizer instance: set the SKYRIM_FLAT_ROOT user variable to its folder.'
}
$destination = Join-Path $root 'tools\Skyrim Together'
if (-not (Test-Path $destination)) { New-Item -ItemType Directory -Path $destination | Out-Null }

# The flat client was SkyrimTogether.exe, upstream's name, before it became urSovngardeSE.exe.
if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in 'urSovngardeSE', 'SkyrimTogether' }) {
    Write-Host 'The flat game is running; its files are in use. Not copied.' -ForegroundColor Yellow
    exit 2
}

$exe = Join-Path $release 'urSovngardeSE.exe'
if (-not (Test-Path $exe)) { throw "No flat client in ${release}: build first." }
$size = (Get-Item $exe).Length
if ($size -lt $MinimumBytes) { throw "urSovngardeSE.exe is $size bytes, smaller than a real build: a cut link." }

# The build's version string inside the binary says the file is whole; the longest match, as a cut piece of it can be there too.
# It must be the server's version as well, so the client and the server were built together.
function Get-BuildVersion($path) {
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($path))
    $all = [regex]::Matches($text, 'v\d+\.\d+\.\d+(-\d+-g[0-9a-f]+)?(-dirty\.[0-9a-f]+)?') | ForEach-Object { $_.Value }
    if ($all) { return ($all | Sort-Object Length -Descending | Select-Object -First 1) }
    return $null
}
$version = Get-BuildVersion $exe
if (-not $version) { throw "No version string inside ${exe}: it is not a complete binary." }
$server = Join-Path $release 'STServer.dll'
if ((Test-Path $server) -and (Get-BuildVersion $server) -ne $version) {
    throw "urSovngardeSE.exe is $version but STServer.dll is $(Get-BuildVersion $server): build again."
}

. (Join-Path $PSScriptRoot '..\VR\binary-check.ps1')
foreach ($name in @('urSovngardeSE.exe', 'TPProcess.exe', 'EarlyLoad.dll')) {
    $source = Join-Path $release $name
    if (Test-Path $source) { Assert-CompleteBinary $source }
}
foreach ($name in @('urSovngardeSE.exe', 'urSovngardeSE.pdb', 'TPProcess.exe', 'EarlyLoad.dll')) {
    $source = Join-Path $release $name
    if (Test-Path $source) { Copy-Item $source (Join-Path $destination $name) -Force }
}
# A client under the old name would still start from an old shortcut; it goes, so only this build can run.
foreach ($old in 'SkyrimTogether.exe', 'SkyrimTogether.pdb') {
    $path = Join-Path $destination $old
    if (Test-Path $path) { Remove-Item $path -Force }
}
$hash = (Get-FileHash (Join-Path $destination 'urSovngardeSE.exe') -Algorithm SHA256).Hash.Substring(0, 16)
Write-Host "Deployed $version to $destination"
Write-Host "  $size bytes, sha256 $hash..."
