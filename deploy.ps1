param(
    [switch]$Release,
    [string]$MmdDir = (Join-Path $env:APPDATA "MikuMikuDanceE_v932x64_dx11_clean")
)

$ErrorActionPreference = "Stop"

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Point it at your vcpkg checkout."
}
if (-not $env:QTDIR) {
    throw "QTDIR is not set. Point it at a Qt 6 MSVC 2022 x64 kit (e.g. C:\Qt\6.12.0\msvc2022_64)."
}

$projectDir = $PSScriptRoot
$config = if ($Release) { "release" } else { "debug" }
$buildDir = Join-Path $projectDir "out\build\$config"

Write-Host "=== Building Hikari ($config) ===" -ForegroundColor Cyan
Push-Location $projectDir
try {
    cmake --preset $config
    cmake --build --preset $config
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $MmdDir)) {
    throw "MMD directory does not exist: $MmdDir"
}

$coreSrc = Join-Path $buildDir "src\hikari\core\hikari_core.dll"
$effectsSrc = Join-Path $buildDir "src\hikari\core\HikariEffects.dll"

foreach ($file in @($coreSrc, $effectsSrc)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Build artifact not found: $file"
    }
}

Get-Process -Name "MikuMikuDance*" -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Host "Deploying to $MmdDir..." -ForegroundColor Green
Copy-Item -LiteralPath $coreSrc -Destination (Join-Path $MmdDir "hikari_core.dll") -Force
Copy-Item -LiteralPath $effectsSrc -Destination (Join-Path $MmdDir "HikariEffects.dll") -Force

Get-ChildItem -Path $projectDir -Filter "*.fx" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $MmdDir $_.Name) -Force
}

Write-Host "Deploy complete! hikari_core.dll, HikariEffects.dll deployed to root of $MmdDir" -ForegroundColor Green
