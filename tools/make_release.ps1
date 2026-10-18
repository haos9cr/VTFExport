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

# Privacy check: the plug-in must not contain local paths or the Windows user name
# (they get in through the PDB path if /PDBALTPATH is missing from the project).
# The project URL (src\Version.h) is allowed: it contains the GitHub name, which may
# equal the Windows user name.
$bytes = [System.IO.File]::ReadAllBytes($plugin)
$texts = @([System.Text.Encoding]::UTF8.GetString($bytes), [System.Text.Encoding]::Unicode.GetString($bytes))
$versionText = Get-Content -LiteralPath (Join-Path $root 'src\Version.h') -Raw
if ($versionText -match 'VTFEXPORT_URL\s+"([^"]+)"') {
    $url = [regex]::Escape($Matches[1])
    $texts = $texts | ForEach-Object { [regex]::Replace($_, $url, '', 'IgnoreCase') }
}
$needles = @($root, '\Users\', $env:USERNAME) | Where-Object { $_ -and $_.Length -ge 3 }
foreach ($needle in $needles) {
    foreach ($text in $texts) {
        if ($text.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "VTFExport.8bi contains '$needle' (a local path or your user name). Rebuild with the current VTFExport.vcxproj (it uses /PDBALTPATH) and run this script again."
        }
    }
}
Write-Host 'Privacy check: no local paths or user name in VTFExport.8bi'

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
