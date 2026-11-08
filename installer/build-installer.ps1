# =============================================================================
#  build-installer.ps1 -- stage the payload and compile the Inno Setup installer
#
#  ASCII-only on purpose: Windows PowerShell 5.1 reads a UTF-8 file without BOM
#  as ANSI, so non-ASCII bytes in here can corrupt the parser. Chinese prose
#  lives in installer\BEFORE-INSTALL.txt and installer\TOIRE-2A.iss (both are
#  UTF-8 WITH a BOM, which is what Inno Setup requires).
#
#  What it does
#    1. copies the RELEASE VST3 bundle into installer\payload\TOIRE-2A.vst3;
#       the bundle already carries Contents\webui\ (index.html, style.css,
#       app.js, juce-bridge.js, fonts\) so nothing extra is staged;
#    2. refuses to continue if any required payload file is missing;
#    3. runs ISCC.exe on TOIRE-2A.iss, which writes
#       ..\dist\TOIRE-2A-<version>-win64.exe
#    4. publishes a SHA256 manifest next to the installer.
#
#  Usage:
#    powershell -NoProfile -ExecutionPolicy Bypass -File installer\build-installer.ps1
#    ... -Version 1.0.1        (only if you bumped the version)
# =============================================================================

[CmdletBinding()]
param(
    [string]$Version = '1.0.0',
    [string]$Iscc    = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $here

$bundleSrc     = Join-Path $proj 'build\TOIRE2A_artefacts\Release\VST3\TOIRE-2A.vst3'
$payload       = Join-Path $here 'payload'
$payloadBundle = Join-Path $payload 'TOIRE-2A.vst3'

Write-Host ('Project  : ' + $proj)
Write-Host ('Bundle   : ' + $bundleSrc)

if (-not (Test-Path -LiteralPath $bundleSrc)) {
    throw ('Missing input: ' + $bundleSrc + '  (build Release first)')
}

# --- 1. stage the payload ----------------------------------------------------
if (Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload -Recurse -Force }
New-Item -ItemType Directory -Path $payloadBundle -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $bundleSrc 'Contents') -Destination $payloadBundle -Recurse -Force

# The CMake POST_BUILD copies WebUI\* into the bundle and never purges stale
# files, so drop editor leftovers before packaging.
Get-ChildItem -LiteralPath $payloadBundle -Recurse -File |
    Where-Object { $_.Name -match '\.(bak|before-scale|orig)$' } |
    ForEach-Object { Write-Host ('  dropping stale: ' + $_.Name); Remove-Item -LiteralPath $_.FullName -Force }

# --- 2. the bundle is useless if any of these are missing --------------------
$required = @(
    'Contents\x86_64-win\TOIRE-2A.vst3',
    'Contents\x86_64-win\WebView2Loader.dll',
    'Contents\Resources\moduleinfo.json',
    'Contents\webui\index.html',
    'Contents\webui\app.js',
    'Contents\webui\style.css',
    'Contents\webui\juce-bridge.js',
    'Contents\webui\fonts\CinzelDecorative-700.woff2',
    'Contents\webui\fonts\CinzelDecorative-900.woff2'
)
foreach ($rel in $required) {
    $full = Join-Path $payloadBundle $rel
    if (-not (Test-Path -LiteralPath $full)) { throw ('Payload incomplete, missing: ' + $rel) }
    Write-Host ('  payload OK: ' + $rel + '  (' + (Get-Item -LiteralPath $full).Length + ' bytes)')
}

# --- dependencies ------------------------------------------------------------
$depsMissing = @()
foreach ($d in @('deps\MicrosoftEdgeWebview2Setup.exe', 'deps\ChineseSimplified.isl')) {
    if (-not (Test-Path -LiteralPath (Join-Path $here $d))) { $depsMissing += $d }
}
if ($depsMissing.Count -gt 0) {
    Write-Warning ('missing: ' + ($depsMissing -join ', '))
    Write-Warning 'copy them from another plugin installer project, otherwise the installer loses its Chinese UI and/or the WebView2 auto-install.'
    Write-Warning 'continuing anyway (the .iss falls back to an English UI).'
}

# --- 3. compile --------------------------------------------------------------
if (-not (Test-Path -LiteralPath $Iscc)) { throw ('ISCC.exe not found: ' + $Iscc) }
Write-Host ''
Write-Host ('Compiling with: ' + $Iscc)

& $Iscc (Join-Path $here 'TOIRE-2A.iss')
if ($LASTEXITCODE -ne 0) { throw ('ISCC failed with exit code ' + $LASTEXITCODE) }

$out = Join-Path $proj ('dist\TOIRE-2A-' + $Version + '-win64.exe')
if (-not (Test-Path -LiteralPath $out)) { throw ('Expected output missing: ' + $out) }

# --- 4. publish checksums ----------------------------------------------------
$sha = @()
$sha += '# SHA256 - TOIRE-2A-' + $Version + '-win64.exe  (' + (Get-Date -Format 'yyyy-MM-dd HH:mm') + ')'
$sha += (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLower() + '  TOIRE-2A-' + $Version + '-win64.exe'
foreach ($rel in $required) {
    $full = Join-Path $payloadBundle $rel
    $sha += (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLower() + '  TOIRE-2A.vst3\' + $rel
}
Set-Content -LiteralPath (Join-Path $proj 'dist\SHA256-installer.txt') -Value $sha -Encoding ASCII

Write-Host ''
Write-Host ('Installer : ' + $out)
Write-Host ('Size      : ' + (Get-Item -LiteralPath $out).Length + ' bytes')
Write-Host ('MD5       : ' + (Get-FileHash -LiteralPath $out -Algorithm MD5).Hash.ToLower())
Write-Host ('SHA256    : ' + (Join-Path $proj 'dist\SHA256-installer.txt'))
