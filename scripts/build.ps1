#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build script for Aerofly FS4 Bridge using CMake

.DESCRIPTION
    This script provides a convenient wrapper around CMake for building the
    Aerofly FS4 Bridge DLL. It supports Debug and Release configurations.

.PARAMETER Config
    Build configuration: Debug or Release (default: Release)

.PARAMETER Clean
    Clean build directory before building

.PARAMETER Install
    Install the DLL after building

.PARAMETER Test
    Run tests after building

.PARAMETER Rebuild
    Clean and rebuild from scratch

.EXAMPLE
    .\build.ps1
    Build Release configuration

.EXAMPLE
    .\build.ps1 -Config Debug -Test
    Build Debug configuration and run tests

.EXAMPLE
    .\build.ps1 -Rebuild -Install
    Clean rebuild and install to Aerofly FS4 directory
#>

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [switch]$Clean,
    [switch]$Install,
    [switch]$Test,
    [switch]$Rebuild
)

# Change to repository root
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
Push-Location $repoRoot

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Aerofly FS4 Bridge - Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$buildDir = "build"
$generator = "Visual Studio 17 2022"

# Check if Visual Studio 2022 is available, fallback to 2019
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vs2022 = & $vsWhere -version "[17.0,18.0)" -property installationPath
    if (-not $vs2022) {
        $vs2019 = & $vsWhere -version "[16.0,17.0)" -property installationPath
        if ($vs2019) {
            $generator = "Visual Studio 16 2019"
            Write-Host "Using Visual Studio 2019" -ForegroundColor Yellow
        }
    } else {
        Write-Host "Using Visual Studio 2022" -ForegroundColor Green
    }
}

# Rebuild = Clean + Build
if ($Rebuild) {
    $Clean = $true
}

# Clean build directory if requested
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# Configure CMake project
if (-not (Test-Path "$buildDir/CMakeCache.txt")) {
    Write-Host "Configuring CMake project..." -ForegroundColor Cyan
    Write-Host "Generator: $generator" -ForegroundColor Gray
    Write-Host ""

    cmake -B $buildDir -G $generator -A x64

    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ CMake configuration failed!" -ForegroundColor Red
        Pop-Location
        exit 1
    }
    Write-Host ""
}

# Build the project
Write-Host "Building $Config configuration..." -ForegroundColor Cyan
cmake --build $buildDir --config $Config

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host "✅ Build successful!" -ForegroundColor Green
Write-Host ""

# Show output DLL location
$dllPath = "$buildDir\$Config\AeroflyBridge.dll"
if (Test-Path $dllPath) {
    $dllInfo = Get-Item $dllPath
    Write-Host "Output DLL:" -ForegroundColor Cyan
    Write-Host "  Location: $($dllInfo.FullName)" -ForegroundColor Gray
    Write-Host "  Size: $([math]::Round($dllInfo.Length / 1KB, 2)) KB" -ForegroundColor Gray
    Write-Host ""
}

# Run tests if requested
if ($Test) {
    Write-Host "Running tests..." -ForegroundColor Cyan
    cmake --build $buildDir --config $Config --target RUN_TESTS

    if ($LASTEXITCODE -ne 0) {
        Write-Host "⚠️  Some tests failed!" -ForegroundColor Yellow
    } else {
        Write-Host "✅ All tests passed!" -ForegroundColor Green
    }
    Write-Host ""
}

# Install if requested
if ($Install) {
    Write-Host "Installing DLL..." -ForegroundColor Cyan

    $installPath = "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll"
    if (-not (Test-Path $installPath)) {
        Write-Host "Creating installation directory: $installPath" -ForegroundColor Yellow
        New-Item -ItemType Directory -Path $installPath -Force | Out-Null
    }

    if (Test-Path $dllPath) {
        Copy-Item $dllPath $installPath -Force
        Write-Host "✅ Installed to: $installPath\AeroflyBridge.dll" -ForegroundColor Green
    } else {
        Write-Host "❌ DLL not found at: $dllPath" -ForegroundColor Red
    }
    Write-Host ""
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

Pop-Location
