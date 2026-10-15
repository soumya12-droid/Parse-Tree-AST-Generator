# Builds ptagen.exe with clang++ (C++17). Run from any directory:
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1
$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$output   = Join-Path $buildDir 'ptagen.exe'

$clang = Get-Command clang++ -ErrorAction SilentlyContinue
if (-not $clang) {
    Write-Error 'clang++ was not found on PATH. Install LLVM or add it to PATH.'
    exit 1
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Phase 0 compiles main.cpp only; later phases add more sources under src/.
$sources = @(Join-Path $root 'src\main.cpp')

$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-O1', '-I', (Join-Path $root 'src'))

Write-Host "Compiler: $($clang.Source)"
Write-Host "Building: $output"

& $clang.Source @flags @sources -o $output
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed (clang++ exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

Write-Host 'Build succeeded.'
