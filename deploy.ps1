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
$coreSrc = Join-Path $targetDir "hikari_core.dll"
$effectsSrc = Join-Path $targetDir "HikariEffects.dll"

foreach ($file in @($coreSrc, $effectsSrc)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Build artifact not found: $file"
    }
}

# Stop any running MMD instance before copying
Get-Process -Name "MikuMikuDance*" -ErrorAction SilentlyContinue | Stop-Process -Force

# Copy new standalone binaries to MMD root
Write-Host "Deploying to $MmdDir..." -ForegroundColor Green
Copy-Item -LiteralPath $coreSrc -Destination (Join-Path $MmdDir "hikari_core.dll") -Force
Copy-Item -LiteralPath $effectsSrc -Destination (Join-Path $MmdDir "HikariEffects.dll") -Force

# Copy shader files to MMD root for testing
Get-ChildItem -Path $projectDir -Filter "*.fx" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $MmdDir $_.Name) -Force
}

Write-Host "Deploy complete! hikari_core.dll, HikariEffects.dll deployed to root of $MmdDir" -ForegroundColor Green
