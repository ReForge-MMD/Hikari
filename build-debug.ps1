$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$preset = "debug"
$buildDir = Join-Path $projectDir "out\build\$preset"
$cacheFile = Join-Path $buildDir "CMakeCache.txt"

function Get-CacheValue([string]$name) {
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        return $null
    }
    $pattern = "^{0}:[^=]*=(.*)$" -f [regex]::Escape($name)
    $line = Select-String -Path $cacheFile -Pattern $pattern | Select-Object -First 1
    if ($null -eq $line) {
        return $null
    }
    return $line.Matches[0].Groups[1].Value
}

if (-not $env:VCPKG_ROOT) {
    $toolchain = Get-CacheValue "CMAKE_TOOLCHAIN_FILE"
    if ($toolchain -match '^(.*)[/\\]scripts[/\\]buildsystems[/\\]vcpkg\.cmake$') {
        $env:VCPKG_ROOT = $Matches[1]
    }
}
if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Point it at your vcpkg checkout."
}

if (-not $env:QTDIR) {
    $env:QTDIR = Get-CacheValue "CMAKE_PREFIX_PATH"
}
if (-not $env:QTDIR) {
    $qt6 = Get-CacheValue "Qt6_DIR"
    if ($qt6 -match '^(.*)[/\\]lib[/\\]cmake[/\\]Qt6$') {
        $env:QTDIR = $Matches[1]
    }
}
if (-not $env:QTDIR) {
    throw "QTDIR is not set. Point it at a Qt 6 MSVC 2022 x64 kit."
}

$vcvars = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $install = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $install = @($install | Where-Object { $_ }) | Select-Object -First 1
    if ($install) {
        $candidate = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path -LiteralPath $candidate) {
            $vcvars = $candidate
        }
    }
}
if (-not $vcvars -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "MSVC compiler not found. Run this script from a Developer PowerShell or install the C++ build tools."
}

Push-Location $projectDir
try {
    Write-Host "=== Building Hikari ($preset) ===" -ForegroundColor Cyan
    if ($vcvars) {
        $line = 'call "{0}" >nul && cmake --preset {1} && cmake --build --preset {1}' -f $vcvars, $preset
        cmd.exe /d /c $line
    } else {
        cmake --preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed."
        }
        cmake --build --preset $preset
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

Write-Host "Build complete: $(Join-Path $buildDir 'src\hikari\studio\hikari_studio.exe')" -ForegroundColor Green
