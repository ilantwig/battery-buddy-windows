param(
    [string]$Compiler = (Join-Path $PSScriptRoot '.tools\llvm-mingw-20261117-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Compiler not found. Run scripts\setup-toolchain.ps1 first or specify -Compiler.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = Join-Path $OutputDirectory 'BatteryBuddy.exe'
& $Compiler -std=c++17 -O2 -municode '-Wl,--subsystem,windows' -static (Join-Path $PSScriptRoot 'BatteryBuddy.cpp') -o $output -luser32 -lgdi32 -lshell32 -ladvapi32 -lpowrprof
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
Write-Host "Built $output"

