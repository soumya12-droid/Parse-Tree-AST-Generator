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

# Later phases add more sources under src/.
$sources = @(
    (Join-Path $root 'src\main.cpp'),
    (Join-Path $root 'src\core\lexer.cpp'),
    (Join-Path $root 'src\core\parser.cpp'),
    (Join-Path $root 'src\core\ast_builder.cpp'),
    (Join-Path $root 'src\core\analyzer.cpp'),
    (Join-Path $root 'src\output\text_printer.cpp'),
    (Join-Path $root 'src\output\json_writer.cpp'),
    (Join-Path $root 'src\server\json_request.cpp'),
    (Join-Path $root 'src\server\http_server.cpp'),
    (Join-Path $root 'src\server\serve_command.cpp')
)

$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-O1', '-I', (Join-Path $root 'src'))

Write-Host "Compiler: $($clang.Source)"
Write-Host "Building: $output"

& $clang.Source @flags @sources -o $output
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed (clang++ exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

Write-Host 'Build succeeded.'
