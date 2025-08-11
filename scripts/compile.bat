@echo off
setlocal enabledelayedexpansion

echo ===============================================
echo  Aerofly Bridge DLL - Compilation Script
echo  Visual Studio 2022 - Windows 10/11
echo  Output: dist\AeroflyBridge.dll
echo ===============================================

REM Ensure we run from repo root
cd /d "%~dp0.."

REM Detect Visual Studio 2022
set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022"
if exist "%VS_PATH%\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%VS_PATH%\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%VS_PATH%\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%VS_PATH%\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%VS_PATH%\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%VS_PATH%\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo ERROR: Visual Studio 2022 not found
    echo Manually locate vcvars64.bat and update the path
    pause
    exit /b 1
)

echo Setting up Visual Studio 2022 environment...
call "%VCVARS%"

echo.
echo Skipping SDK header pre-check (compiler will report missing includes if needed)...
echo.

REM Create output directory
if not exist dist mkdir dist

echo.
echo Compiling AeroflyBridge.dll (RELEASE MODE - NDEBUG)...
echo.

REM Compile with NDEBUG to disable assert() macros
cl.exe /LD /EHsc /O2 /DNDEBUG /std:c++17 /DWIN32 /D_WINDOWS /D_USRDLL ^
    /I . /I src ^
    aerofly_bridge_dll.cpp ^
    /Fe:dist\AeroflyBridge.dll ^
    /link ws2_32.lib kernel32.lib user32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===============================================
    echo  COMPILATION SUCCESSFUL!
    echo ===============================================
    echo.
    echo Generated: dist\AeroflyBridge.dll
    dir dist\AeroflyBridge.dll | find ".dll"
    echo.
    echo To install:
    echo  copy dist\AeroflyBridge.dll "%USERPROFILE%\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll"
) else (
    echo.
    echo ===============================================
    echo  COMPILATION ERROR!
    echo ===============================================
    echo.
)

endlocal

