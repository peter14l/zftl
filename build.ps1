# PowerShell build & test helper for zFTL / Storage Simulator
param(
    [switch]$RunTests = $true,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$cmakePaths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\CMake\bin\cmake.exe"
)

$cmakeExe = $null
foreach ($p in $cmakePaths) {
    if (Test-Path $p) {
        $cmakeExe = $p
        break
    }
}

if (-not $cmakeExe) {
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($found) { $cmakeExe = $found.Source }
}

if (-not $cmakeExe) {
    Write-Error "CMake could not be found. Please ensure Visual Studio C++ CMake tools are installed."
    exit 1
}

Write-Host "Using CMake: $cmakeExe" -ForegroundColor Cyan

if (-not (Test-Path "build")) {
    & $cmakeExe -B build -G "Visual Studio 17 2022" -A x64
}

Write-Host "Building project in $Config mode..." -ForegroundColor Cyan
& $cmakeExe --build build --config $Config

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit $LASTEXITCODE
}

if ($RunTests) {
    $testExe = "build\$Config\zftl_tests.exe"
    if (-not (Test-Path $testExe)) {
        $testExe = "build\$Config\hyper_ram_tests.exe"
    }
    if (Test-Path $testExe) {
        Write-Host "`nRunning Test Suite ($testExe)..." -ForegroundColor Green
        & $testExe
    }
}
