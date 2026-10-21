# Builds and runs the test suite. Exits non-zero if the build or any test fails.
#   powershell -ExecutionPolicy Bypass -File scripts\test.ps1
$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$output   = Join-Path $buildDir 'ptagen_tests.exe'

$clang = Get-Command clang++ -ErrorAction SilentlyContinue
if (-not $clang) {
    Write-Error 'clang++ was not found on PATH. Install LLVM or add it to PATH.'
    exit 1
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Library sources under test (everything except src\main.cpp) plus the test files.
$sources = @(
    (Join-Path $root 'src\core\lexer.cpp'),
    (Join-Path $root 'src\core\parser.cpp'),
    (Join-Path $root 'src\core\ast_builder.cpp'),
    (Join-Path $root 'src\core\analyzer.cpp'),
    (Join-Path $root 'src\output\text_printer.cpp'),
    (Join-Path $root 'src\output\json_writer.cpp'),
    (Join-Path $root 'src\server\json_request.cpp'),
    (Join-Path $root 'src\server\http_server.cpp'),
    (Join-Path $root 'tests\test_runner.cpp'),
    (Join-Path $root 'tests\unit\lexer_tests.cpp'),
    (Join-Path $root 'tests\unit\parser_tests.cpp'),
    (Join-Path $root 'tests\unit\ast_tests.cpp'),
    (Join-Path $root 'tests\unit\diagnostic_tests.cpp'),
    (Join-Path $root 'tests\unit\diagnostic_message_tests.cpp'),
    (Join-Path $root 'tests\unit\json_tests.cpp'),
    (Join-Path $root 'tests\unit\server_tests.cpp')
)

$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic',
           '-I', (Join-Path $root 'src'), '-I', (Join-Path $root 'tests'))

Write-Host "Building tests: $output"
& $clang.Source @flags @sources -o $output
if ($LASTEXITCODE -ne 0) {
    Write-Error "Test build failed (clang++ exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

# The server tests serve the real web UI from this folder.
$env:PTAGEN_WEB_ROOT = Join-Path $root 'web'
& $output (Join-Path $root 'tests\cases')
exit $LASTEXITCODE
