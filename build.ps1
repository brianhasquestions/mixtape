param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
cmake -S . -B build -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built: $PSScriptRoot\build\$Config\mixtape.exe"
