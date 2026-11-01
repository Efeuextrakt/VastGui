# Packages a release: release\VastGui-<version>-win64\ + .zip + release notes, from an already built VastGui.exe.
# Called by `build.bat package` (which builds and runs the tests first). Fails if the versions disagree or the
# changelog has no section for the version, so a release never ships with stale metadata.
param(
    [Parameter(Mandatory = $true)][string]$Exe
)
$ErrorActionPreference = 'Stop'
trap { Write-Host "ERROR: $_"; exit 1 }
$root = Split-Path -Parent $PSScriptRoot

$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(VastGui VERSION (\d+\.\d+\.\d+)') { throw 'version not found in CMakeLists.txt' }
$version = $Matches[1]

$appHpp = Get-Content (Join-Path $root 'src\App.hpp') -Raw
if ($appHpp -notmatch 'kAppVersion = "([^"]+)"') { throw 'kAppVersion not found in src\App.hpp' }
if ($Matches[1] -ne $version) { throw "src\App.hpp kAppVersion $($Matches[1]) != CMakeLists.txt $version" }

$fileVersion = (Get-Item $Exe).VersionInfo.ProductVersion
if (-not $fileVersion.StartsWith("$version.")) { throw "VastGui.exe ProductVersion $fileVersion != $version (res\app.rc)" }

$changelog = Get-Content (Join-Path $root 'CHANGELOG.md') -Raw
$section = [regex]::Match($changelog, "(?ms)^## $([regex]::Escape($version)) - [^\r\n]+\r?\n(.*?)(?=^## |\z)")
if (-not $section.Success) { throw "CHANGELOG.md has no '## $version - <date>' section" }

$name = "VastGui-$version-win64"
$out = Join-Path $root 'release'
$stage = Join-Path $out $name
$zip = Join-Path $out "$name.zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
New-Item -ItemType Directory $stage | Out-Null

Copy-Item $Exe (Join-Path $stage 'VastGui.exe')
foreach ($f in 'README.md', 'LICENSE.md', 'THIRD_PARTY_LICENSES.txt', 'CHANGELOG.md') {
    Copy-Item (Join-Path $root $f) $stage
}
# The README links into docs/ (features, configuration, screenshot, icon) - ship them so the links work offline.
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory $docs, (Join-Path $docs 'images'), (Join-Path $docs 'screenshots') | Out-Null
Copy-Item (Join-Path $root 'docs\*.md') $docs
Copy-Item (Join-Path $root 'docs\images\*') (Join-Path $docs 'images')
Copy-Item (Join-Path $root 'docs\screenshots\*') (Join-Path $docs 'screenshots')

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
$sha = [System.Security.Cryptography.SHA256]::Create()
$stream = [System.IO.File]::OpenRead($zip)
try { $hash = -join ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) } finally { $stream.Dispose(); $sha.Dispose() }

# Body for the GitHub release page.
$notes = @"
## VastGui $version

$($section.Groups[1].Value.Trim())

### Download

``$name.zip`` - unzip and run ``VastGui.exe`` (64-bit Windows 10/11, no installer, no runtime).

SHA-256: ``$hash``

VastGui is free for non-commercial use (PolyForm Noncommercial 1.0.0, see ``LICENSE.md`` in the zip).
"@
Set-Content -Path (Join-Path $out "RELEASE_NOTES-$version.md") -Value $notes -Encoding utf8

Write-Host ""
Write-Host "Packaged $name"
Write-Host "  $zip  ($([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB)"
Write-Host "  SHA-256 $hash"
Write-Host "  release notes: release\RELEASE_NOTES-$version.md"
