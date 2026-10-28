param(
    [switch]$Release,
    [string]$MmdDir = (Join-Path $env:APPDATA "MikuMikuDanceE_v932x64_dx11_clean")
)

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$config = if ($Release) { "release" } else { "debug" }
$targetDir = Join-Path $projectDir "target\$config"

Write-Host "=== Building Hikari ($config) ===" -ForegroundColor Cyan
if ($Release) {
    cargo build --workspace --release
} else {
    cargo build --workspace
}

if (-not (Test-Path -LiteralPath $MmdDir)) {
    throw "MMD directory does not exist: $MmdDir"
}

# Source binaries
$d3d9Src = Join-Path $targetDir "d3d9.dll"
$coreSrc = Join-Path $targetDir "hikari_core.dll"
$effectsSrc = Join-Path $targetDir "HikariEffects.dll"

foreach ($file in @($d3d9Src, $coreSrc, $effectsSrc)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Build artifact not found: $file"
    }
}

# Backup original d3d9.dll in MMD dir if not already backed up
$targetD3D9 = Join-Path $MmdDir "d3d9.dll"
$backupD3D9 = Join-Path $MmdDir "d3d9.dll.orig"
if ((Test-Path -LiteralPath $targetD3D9) -and (-not (Test-Path -LiteralPath $backupD3D9))) {
    Write-Host "Backing up existing d3d9.dll to d3d9.dll.orig..." -ForegroundColor Yellow
    Copy-Item -LiteralPath $targetD3D9 -Destination $backupD3D9
}

# Disable legacy MMDPlugin Hikari if present
$legacyPluginHikari = Join-Path $MmdDir "plugin\Hikari"
$disabledPluginHikari = Join-Path $MmdDir "plugin\Hikari.disabled"
if (Test-Path -LiteralPath $legacyPluginHikari) {
    Write-Host "Disabling legacy MMDPlugin Hikari..." -ForegroundColor Yellow
    if (Test-Path -LiteralPath $disabledPluginHikari) {
        Remove-Item -LiteralPath $disabledPluginHikari -Recurse -Force
    }
    Rename-Item -LiteralPath $legacyPluginHikari -NewName "Hikari.disabled"
}

# Stop any running MMD instance before copying
Get-Process -Name "MikuMikuDance*" -ErrorAction SilentlyContinue | Stop-Process -Force

# Copy new standalone binaries to MMD root
Write-Host "Deploying to $MmdDir..." -ForegroundColor Green
Copy-Item -LiteralPath $d3d9Src -Destination $targetD3D9 -Force
Copy-Item -LiteralPath $coreSrc -Destination (Join-Path $MmdDir "hikari_core.dll") -Force
Copy-Item -LiteralPath $effectsSrc -Destination (Join-Path $MmdDir "HikariEffects.dll") -Force

# Copy shader files to MMD root for testing
Get-ChildItem -Path $projectDir -Filter "*.fx" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $MmdDir $_.Name) -Force
}

Write-Host "Deploy complete! d3d9.dll, hikari_core.dll, HikariEffects.dll deployed to root of $MmdDir" -ForegroundColor Green
