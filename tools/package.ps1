param()
$ErrorActionPreference = 'Stop'

# Explicit allowlist: never bundle flash backups, caches or previous archives.
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$distPath = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $distPath -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$archivePath = Join-Path $distPath "AIRSPEED-source-$stamp.zip"
if (Test-Path -LiteralPath $archivePath) { throw 'Archive already exists; retry in a second.' }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$files = @()
foreach ($name in @('AGENTS.md', 'README.md', 'platformio.ini', '.gitignore')) {
    $files += Get-Item -LiteralPath (Join-Path $projectRoot $name)
}
foreach ($folder in @('src', 'docs', 'tools', 'lib')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $projectRoot $folder) -File -Recurse -Force
}
$files = @($files | Sort-Object FullName)
$hashes = [ordered]@{}
$zip = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
        $entryName = "AIRSPEED/$relative"
        $hashes[$entryName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $manifest = $zip.CreateEntry('AIRSPEED/SHA256SUMS.txt')
    $writer = [IO.StreamWriter]::new($manifest.Open(), [Text.UTF8Encoding]::new($false))
    try {
        foreach ($name in $hashes.Keys) {
            $writer.WriteLine('{0}  {1}', $hashes[$name], $name.Substring(9))
        }
    } finally { $writer.Dispose() }
} finally { $zip.Dispose() }

# Reopen and hash the decompressed bytes, not just ZIP metadata.
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    if ($zip.Entries.Count -ne $files.Count + 1) { throw 'Unexpected ZIP entry count.' }
    foreach ($name in $hashes.Keys) {
        $entry = $zip.GetEntry($name)
        if ($null -eq $entry) { throw "Missing entry: $name" }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
            if ($actual -ne $hashes[$name]) { throw "ZIP content mismatch: $name" }
        } finally { $sha.Dispose(); $stream.Dispose() }
    }
} finally { $zip.Dispose() }

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archivePath.sha256", "$archiveHash  $([IO.Path]::GetFileName($archivePath))`n", [Text.Encoding]::ASCII)
[pscustomobject]@{
    Archive = $archivePath
    SourceFiles = $files.Count
    Bytes = (Get-Item -LiteralPath $archivePath).Length
    SHA256 = $archiveHash
    Verified = $true
} | ConvertTo-Json
