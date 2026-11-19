# Whether a built program file is whole, for the deploy scripts (dot-sourced).
# A link that was stopped can leave a file of the right name that is not a program: zeros, or headers whose sections run past its end.
# Copied, a broken TPProcess.exe closes the game at start, as the menu's browser cannot start; the deploy refuses such a file instead.

function Assert-CompleteBinary([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $name = Split-Path -Leaf $path
    if ($bytes.Length -lt 0x200 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "${name} is not a whole program file (no MZ header): build again." }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) { throw "${name} is not a whole program file (no PE header): build again." }
    $sections = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $table = $pe + 24 + [BitConverter]::ToUInt16($bytes, $pe + 20)
    for ($i = 0; $i -lt $sections; ++$i) {
        $entry = $table + 40 * $i
        $end = [BitConverter]::ToUInt32($bytes, $entry + 20) + [BitConverter]::ToUInt32($bytes, $entry + 16)
        if ($end -gt $bytes.Length) { throw "${name} is cut short: a section ends at $end, the file at $($bytes.Length): build again." }
    }
}
