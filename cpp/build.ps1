param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$out = Join-Path $base 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($tool in @('bench','collect','install_sdk','tests')) {
    & $Compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-I' (Join-Path $base 'vendor') (Join-Path $base "src/$tool.cpp") '-o' (Join-Path $out "servo_$tool.exe")
    if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed: $tool" }
}
& (Join-Path $out 'servo_tests.exe') (Join-Path $base '../config.json')
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
