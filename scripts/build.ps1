# Full FlowSpace build: configures CMake (MinGW toolchain), builds app + diagnostic + tests.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-BuildType Debug|Release]
param(
    [string]$BuildType = 'Debug',
    [switch]$NoTests,
    [string]$MingwBin = 'C:\msys64\ucrt64\bin'
)

$ErrorActionPreference = 'Stop'
$requiredTools = @('g++.exe', 'gcc.exe', 'ninja.exe', 'windres.exe')
foreach ($tool in $requiredTools) {
    if (-not (Test-Path (Join-Path $MingwBin $tool))) {
        Write-Error "Required tool '$tool' not found at $MingwBin"
    }
}

$env:PATH = "$MingwBin;$env:PATH"

$source = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $source 'build\cmake'

# First-run icon generation.
if (-not (Test-Path (Join-Path $source 'build\flowspace.ico'))) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'make-icon.ps1') | Out-Null
}

Write-Output "=== Configuring ($BuildType) ==="
# NOTE: pass args as an array. The backtick-continuation form interacted badly with the
# console layer and once delivered a literal '$BuildType' into the CMake cache.
$cmakeMingwBin = $MingwBin.Replace('\', '/')
$cmakeArgs = @('-S', $source, '-B', $buildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$BuildType",
"-DCMAKE_CXX_COMPILER=$cmakeMingwBin/g++.exe",
"-DCMAKE_C_COMPILER=$cmakeMingwBin/gcc.exe",
"-DCMAKE_RC_COMPILER=$cmakeMingwBin/windres.exe",
"-DCMAKE_MAKE_PROGRAM=$cmakeMingwBin/ninja.exe")
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error 'CMake configure failed' }

Write-Output '=== Building ==='
$targets = @('flowspace', 'flowspace-diag')
if (-not $NoTests) { $targets += 'flowspace_tests' }
foreach ($t in $targets) {
    cmake --build $buildDir --target $t
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed for target $t" }
}

Write-Output '=== BUILD OK ==='
Get-ChildItem $buildDir -Filter '*.exe' | ForEach-Object { Write-Output ("  " + $_.FullName) }
exit 0
