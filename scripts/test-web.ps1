# Frontend tests: Node's built-in test runner (no npm packages).
#  1. builds ptagen.exe (the live end-to-end tests start the real server);
#  2. checks that the committed fixtures are exactly what the current C++
#     compiler emits;
#  3. runs tests/web/*.test.js, including the live tests against ptagen serve.
#   powershell -ExecutionPolicy Bypass -File scripts\test-web.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Error 'node was not found on PATH (needed only to run the frontend tests).'
    exit 1
}

# 1. Build the real application (server included).
& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error 'build failed'; exit $LASTEXITCODE }
$env:PTAGEN_EXE = Join-Path $root 'build\ptagen.exe'

# 2. Fixtures must be fresh: regenerate into a temp dir and compare bytes.
$tmp = Join-Path $root 'build\fixtures-check'
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'export-fixtures.ps1') -FixturesDir $tmp | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error 'fixture export failed'; exit $LASTEXITCODE }

$stale = $false
foreach ($name in 'fixtures.js', 'fixtures-stress.js') {
    $a = (Get-FileHash (Join-Path $tmp $name)).Hash
    $b = (Get-FileHash (Join-Path $root "web\fixtures\$name")).Hash
    if ($a -ne $b) {
        Write-Host "STALE  web\fixtures\$name differs from the current compiler output" -ForegroundColor Red
        $stale = $true
    } else {
        Write-Host "FRESH  web\fixtures\$name"
    }
}
if ($stale) {
    Write-Host 'Run scripts\export-fixtures.ps1 to regenerate them.'
    exit 1
}

# 3. Unit and live tests.
$files = Get-ChildItem (Join-Path $root 'tests\web') -Filter '*.test.js' | ForEach-Object { $_.FullName }
& node --test @files
exit $LASTEXITCODE
