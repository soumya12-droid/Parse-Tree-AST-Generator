# Builds the development fixture exporter and (re)generates web\fixtures\*.js
# from the real C++ pipeline, or exports one source file as contract JSON.
#
#   powershell -ExecutionPolicy Bypass -File scripts\export-fixtures.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\export-fixtures.ps1 -File my.src -Out result.json
param(
    [string]$File,
    [string]$Out,
    [string]$FixturesDir
)
$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$exe      = Join-Path $buildDir 'export_fixtures.exe'

$clang = Get-Command clang++ -ErrorAction SilentlyContinue
if (-not $clang) {
    Write-Error 'clang++ was not found on PATH.'
    exit 1
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$sources = @(
    (Join-Path $root 'tools\export_fixtures.cpp'),
    (Join-Path $root 'src\core\lexer.cpp'),
    (Join-Path $root 'src\core\parser.cpp'),
    (Join-Path $root 'src\core\ast_builder.cpp'),
    (Join-Path $root 'src\core\analyzer.cpp'),
    (Join-Path $root 'src\output\json_writer.cpp')
)
$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-O1', '-I', (Join-Path $root 'src'))

& $clang.Source @flags @sources -o $exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed (clang++ exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

if ($File) {
    if ($Out) { & $exe --file $File --out $Out } else { & $exe --file $File }
    exit $LASTEXITCODE
}

if (-not $FixturesDir) { $FixturesDir = Join-Path $root 'web\fixtures' }
New-Item -ItemType Directory -Force -Path $FixturesDir | Out-Null
& $exe --fixtures-dir $FixturesDir
exit $LASTEXITCODE
