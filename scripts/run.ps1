# Builds ptagen.exe and starts the local server in the foreground.
# Stop it with Ctrl+C. The server only listens on 127.0.0.1.
#
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -Port 9000 -Open
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -NoBuild
param(
    [int]$Port = 8080,
    [switch]$Open,      # open the UI in your default browser once the server is up
    [switch]$NoBuild    # skip the build step
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'build\ptagen.exe'

if (-not $NoBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path $exe)) {
    Write-Error "$exe not found. Run without -NoBuild first."
    exit 1
}

if ($Open) {
    # Open the browser shortly after the server starts listening.
    Start-Job -ArgumentList $Port -ScriptBlock {
        param($p)
        Start-Sleep -Seconds 1
        Start-Process "http://127.0.0.1:$p/"
    } | Out-Null
}

Write-Host "Starting the server on http://127.0.0.1:$Port/  (Ctrl+C to stop)"
& $exe serve --port $Port
exit $LASTEXITCODE
