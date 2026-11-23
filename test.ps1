param([string]$Compiler = (Join-Path $PSScriptRoot '.tools\llvm-mingw-20261117-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Compiler not found. Run scripts\setup-toolchain.ps1 first or specify -Compiler.' }
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'dist') -Force | Out-Null
$testExe = Join-Path $PSScriptRoot 'dist\battery-tests.exe'
& $Compiler -std=c++17 -O2 -static (Join-Path $PSScriptRoot 'test.cpp') -o $testExe -luser32 -lgdi32 -lshell32 -ladvapi32 -lpowrprof
if ($LASTEXITCODE -ne 0) { throw "Test compilation failed with exit code $LASTEXITCODE." }
& $testExe
if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }

