# Build script for Stellaris MCP Bridge DLL
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RootDir "build"

Write-Host "=== Building Stellaris Bridge DLL ===" -ForegroundColor Cyan
Write-Host "Root Directory: $RootDir"
Write-Host "Build Directory: $BuildDir"

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

$VcvarsPath = "F:\vss\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $VcvarsPath)) {
    $VcvarsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
}

if (-not (Test-Path $VcvarsPath)) {
    Write-Error "Could not find vcvars64.bat!"
    exit 1
}

# Avoid LNK1104 if game has the DLL loaded: rename the existing DLL to .old
$DllPath = Join-Path $BuildDir "stellaris_bridge\Release\stellaris_bridge.dll"
if (Test-Path $DllPath) {
    $OldPath = "$DllPath.old"
    Remove-Item -Path $OldPath -Force -ErrorAction SilentlyContinue
    Move-Item -Path $DllPath -Destination $OldPath -Force -ErrorAction SilentlyContinue
}

$CmakeCmd = @"
call "$VcvarsPath"
cd /d "$BuildDir"
cmake -G "Visual Studio 17 2022" -A x64 "$RootDir"
cmake --build . --config Release
"@

$BatFile = Join-Path $BuildDir "run_build.bat"
Set-Content -Path $BatFile -Value $CmakeCmd

cmd.exe /c $BatFile
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "=== Build Completed Successfully ===" -ForegroundColor Green
if (Test-Path $DllPath) {
    Write-Host "Artifact: $DllPath" -ForegroundColor Green
}
