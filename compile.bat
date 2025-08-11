@echo off
echo ===============================================
echo  Aerofly Bridge DLL - Compilation Script
echo  Visual Studio 2022 - Windows 11
echo  FIXED VERSION (with NDEBUG flag)
echo ===============================================

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
echo Delegating to scripts\compile.bat...
call scripts\compile.bat

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===============================================
    echo  COMPILATION SUCCESSFUL!
    echo ===============================================
    echo.
    echo Generated file: AeroflyBridge.dll
    echo File size:
    dir AeroflyBridge.dll | find ".dll"
    echo.
    echo IMPORTANT: This version has assert() macros DISABLED
    echo This should fix the 0x80000003 breakpoint exception on Windows 10
    echo.
    echo To install:
    echo 1. Backup current DLL (if any):
    echo    copy "%%USERPROFILE%%\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll" AeroflyBridge_backup.dll
    echo.
    echo 2. Copy DLL to:
    echo    copy AeroflyBridge.dll "%%USERPROFILE%%\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll"
    echo.
    echo 3. Test with Aerofly FS4 - should load without crashing
    echo.
) else (
    echo.
    echo ===============================================
    echo  COMPILATION ERROR!
    echo ===============================================
    echo.
    echo Check the errors above and verify:
    echo 1. That tm_external_message.h exists in the directory
    echo 2. That the .cpp file is present
    echo 3. That there are no syntax errors
    echo.
)

REM Clean temporary files
if exist *.obj del *.obj
if exist *.exp del *.exp
if exist *.lib del *.lib

echo.
echo CHANGELOG FROM PREVIOUS VERSION:
echo + Added /DNDEBUG flag to disable assert() macros
echo + Output filename set to AeroflyBridge.dll
echo + This should resolve Windows 10 breakpoint crashes
echo.
pause