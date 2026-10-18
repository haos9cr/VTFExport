# make_release.ps1 - packs a built plug-in into dist\VTFExport-<version>.zip
#
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Configuration Release]
#
# Run after building Release | x64. The version comes from src\Version.h.
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "bin\$Configuration"
$plugin = Join-Path $bin 'VTFExport.8bi'
if (-not (Test-Path -LiteralPath $plugin)) { throw "Not found: $plugin - build $Configuration | x64 first." }

$versionHeader = Get-Content -LiteralPath (Join-Path $root 'src\Version.h') -Raw
if ($versionHeader -notmatch 'VTFEXPORT_VERSION_STR\s+"([^"]+)"') { throw 'Cannot read the version from src\Version.h' }
$version = $Matches[1]

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist "VTFExport-$version"
$zip = "$stage.zip"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Path (Join-Path $stage 'VTFExport_lang') -Force | Out-Null

Copy-Item -LiteralPath $plugin -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'lang\template.lang.txt') -Destination (Join-Path $stage 'VTFExport_lang')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')
$readme = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release_readme.txt') -Raw -Encoding UTF8) -replace '\{version\}', $version
[System.IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, (New-Object System.Text.UTF8Encoding $true))

# ZipFile writes '/' separators (Compress-Archive in Windows PowerShell 5.1 may write '\').
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip)
Remove-Item -LiteralPath $stage -Recurse -Force

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
Write-Host "Created $zip"
Write-Host "SHA-256: $hash"
