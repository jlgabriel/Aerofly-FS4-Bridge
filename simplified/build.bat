@echo off
REM ============================================================================
REM Aerofly FS4 Reader DLL - Build Script for Windows 11 + VS2022
REM ============================================================================
REM
REM Requisitos:
REM   - Visual Studio 2022 con C++ desktop development
REM   - CMake 3.15+ (incluido con VS2022)
REM   - tm_external_message.h en este directorio
REM
REM Uso:
REM   build.bat           - Compila en Release
REM   build.bat debug     - Compila en Debug
REM   build.bat clean     - Limpia el directorio build
REM   build.bat install   - Compila e instala en Aerofly
REM
REM ============================================================================

setlocal enabledelayedexpansion

REM Colores para output
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "RESET=[0m"

REM Configuracion
set "BUILD_TYPE=Release"
set "BUILD_DIR=build"
set "DLL_NAME=AeroflyReader.dll"
set "AEROFLY_DIR=%USERPROFILE%\Documents\Aerofly FS 4\external_dll"

REM Procesar argumentos
if /i "%1"=="debug" set "BUILD_TYPE=Debug"
if /i "%1"=="clean" goto :clean
if /i "%1"=="install" set "DO_INSTALL=1"

echo.
echo %GREEN%============================================================================%RESET%
echo %GREEN%  Aerofly FS4 Reader DLL - Build Script%RESET%
echo %GREEN%============================================================================%RESET%
echo.

REM Verificar que existe tm_external_message.h
if not exist "tm_external_message.h" (
    echo %RED%ERROR: No se encontro tm_external_message.h%RESET%
    echo.
    echo Por favor, copia el archivo tm_external_message.h del SDK de Aerofly
    echo a este directorio antes de compilar.
    echo.
    goto :error
)

REM Buscar CMake
where cmake >nul 2>&1
if errorlevel 1 (
    REM Intentar con la ruta de VS2022
    set "CMAKE_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if not exist "!CMAKE_PATH!" (
        set "CMAKE_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    if not exist "!CMAKE_PATH!" (
        set "CMAKE_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    if not exist "!CMAKE_PATH!" (
        echo %RED%ERROR: No se encontro CMake%RESET%
        echo.
        echo Instala CMake o usa el que viene con Visual Studio 2022.
        goto :error
    )
) else (
    set "CMAKE_PATH=cmake"
)

echo %YELLOW%Configuracion:%RESET%
echo   Build Type: %BUILD_TYPE%
echo   CMake: %CMAKE_PATH%
echo.

REM Crear directorio build si no existe
if not exist "%BUILD_DIR%" (
    echo %YELLOW%Creando directorio build...%RESET%
    mkdir "%BUILD_DIR%"
)

REM Configurar con CMake
echo %YELLOW%Configurando proyecto con CMake...%RESET%
"%CMAKE_PATH%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo %RED%ERROR: Fallo la configuracion de CMake%RESET%
    goto :error
)
echo.

REM Compilar
echo %YELLOW%Compilando en modo %BUILD_TYPE%...%RESET%
"%CMAKE_PATH%" --build "%BUILD_DIR%" --config %BUILD_TYPE%
if errorlevel 1 (
    echo %RED%ERROR: Fallo la compilacion%RESET%
    goto :error
)
echo.

REM Verificar que se creo el DLL
set "DLL_PATH=%BUILD_DIR%\%BUILD_TYPE%\%DLL_NAME%"
if not exist "%DLL_PATH%" (
    echo %RED%ERROR: No se encontro el DLL compilado%RESET%
    goto :error
)

echo %GREEN%Compilacion exitosa!%RESET%
echo   DLL: %DLL_PATH%
echo.

REM Instalar si se solicito
if defined DO_INSTALL (
    echo %YELLOW%Instalando en Aerofly FS 4...%RESET%

    REM Crear directorio si no existe
    if not exist "%AEROFLY_DIR%" (
        mkdir "%AEROFLY_DIR%"
    )

    REM Copiar DLL
    copy /Y "%DLL_PATH%" "%AEROFLY_DIR%\" >nul
    if errorlevel 1 (
        echo %RED%ERROR: No se pudo copiar el DLL%RESET%
        echo Asegurate de que Aerofly FS 4 no este corriendo.
        goto :error
    )

    echo %GREEN%Instalado en: %AEROFLY_DIR%\%DLL_NAME%%RESET%
    echo.
)

echo %GREEN%============================================================================%RESET%
echo %GREEN%  Build completado exitosamente!%RESET%
echo %GREEN%============================================================================%RESET%
echo.

if not defined DO_INSTALL (
    echo Para instalar, ejecuta: build.bat install
    echo O copia manualmente:
    echo   %DLL_PATH%
    echo   a: %AEROFLY_DIR%\
    echo.
)

goto :end

:clean
echo %YELLOW%Limpiando directorio build...%RESET%
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo %GREEN%Directorio build eliminado.%RESET%
) else (
    echo Directorio build no existe.
)
goto :end

:error
echo.
echo %RED%Build fallido.%RESET%
exit /b 1

:end
endlocal
