@rem Copyright (c) 2026 Nelaric Contributors
@echo off
setlocal DisableDelayedExpansion
set "UMF_SCRIPT_PATH=%~dp0Build.cmake"
if /i "%~1"=="--setup" (
    set "UMF_SCRIPT_PATH=%~dp0Setup.cmake"
    shift
)

if not defined UMF_CMAKE_EXECUTABLE (
    if exist "%~dp0..\..\..\..\Setup\FindCMake.bat" (
        call "%~dp0..\..\..\..\Setup\FindCMake.bat"
        if errorlevel 1 exit /b 1
    ) else (
        where cmake.exe >nul 2>&1
        if errorlevel 1 (
            echo CMake 3.20 or newer is required on PATH. 1>&2
            exit /b 1
        )
        set "UMF_CMAKE_EXECUTABLE=cmake.exe"
    )
)
set "UMF_ARGUMENT_DIR="
set "UMF_TARGET=all"
set "UMF_JOBS="
set "UMF_OUTPUT_DIR="
set "UMF_GENERATOR="

:parse
if "%~1"=="" goto discover
if /i "%~1"=="--help" goto help
if /i "%~1"=="--target" (
    if "%~2"=="" goto usage
    set "UMF_TARGET=%~2"
    shift
    shift
    goto parse
)
if /i "%~1"=="--jobs" (
    if "%~2"=="" goto usage
    set "UMF_JOBS=%~2"
    shift
    shift
    goto parse
)
if /i "%~1"=="--build-dir" (
    if "%~2"=="" goto usage
    set "UMF_OUTPUT_DIR=%~2"
    shift
    shift
    goto parse
)
if /i "%~1"=="--generator" (
    if "%~2"=="" goto usage
    set "UMF_GENERATOR=%~2"
    shift
    shift
    goto parse
)
set "UMF_ARGUMENT=%~1"
if "%UMF_ARGUMENT:~0,1%"=="-" goto usage
if defined UMF_ARGUMENT_DIR goto usage
set "UMF_ARGUMENT_DIR=%~1"
shift
goto parse

:discover
if defined UMF_ARGUMENT_DIR (
    if not exist "%UMF_ARGUMENT_DIR%\" (
        echo The toolchain directory does not exist: %UMF_ARGUMENT_DIR% 1>&2
        exit /b 1
    )
    call :prepare "%UMF_ARGUMENT_DIR%"
    if errorlevel 1 (
        echo No usable MSVC toolchain was found in: %UMF_ARGUMENT_DIR% 1>&2
        exit /b 1
    )
    goto build
)

where cl.exe >nul 2>&1
if not errorlevel 1 goto build
for %%D in ("%TOOLCHAIN_DIR%" "%MSVC_ROOT%" "%VCToolsInstallDir%" "%VCINSTALLDIR%" "%VSINSTALLDIR%") do (
    call :prepare "%%~D"
    if not errorlevel 1 goto build
)
if defined CXX (
    for %%D in ("%CXX%") do call :prepare "%%~dpD"
    if not errorlevel 1 goto build
)
call :discover_visual_studio
goto build

:discover_visual_studio
set "UMF_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%UMF_VSWHERE%" set "UMF_VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%UMF_VSWHERE%" exit /b 1
set "UMF_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "UMF_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "UMF_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"
for /f "usebackq delims=" %%D in (`"%UMF_VSWHERE%" -latest -products * -requires %UMF_VC_COMPONENT% -property installationPath`) do (
    call :prepare "%%D"
    if not errorlevel 1 exit /b 0
)
exit /b 1

:prepare
if "%~1"=="" exit /b 1
if not exist "%~1\" exit /b 1
set "UMF_ENV_ROOT=%~f1"
set "UMF_ENV_SCRIPT="
for /l %%N in (1,1,8) do call :find_environment_script
if defined UMF_ENV_SCRIPT goto activate
set "PATH=%~f1;%~f1\bin;%PATH%"
where cl.exe >nul 2>&1
exit /b %errorlevel%

:find_environment_script
if defined UMF_ENV_SCRIPT exit /b 0
if exist "%UMF_ENV_ROOT%\vcvarsall.bat" set "UMF_ENV_SCRIPT=%UMF_ENV_ROOT%\vcvarsall.bat"
if exist "%UMF_ENV_ROOT%\Auxiliary\Build\vcvarsall.bat" set "UMF_ENV_SCRIPT=%UMF_ENV_ROOT%\Auxiliary\Build\vcvarsall.bat"
if exist "%UMF_ENV_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" set "UMF_ENV_SCRIPT=%UMF_ENV_ROOT%\VC\Auxiliary\Build\vcvarsall.bat"
for %%P in ("%UMF_ENV_ROOT%\..") do set "UMF_ENV_ROOT=%%~fP"
exit /b 0

:activate
set "UMF_HOST_ARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "UMF_HOST_ARCH=%PROCESSOR_ARCHITEW6432%"
set "UMF_VCVARS_ARCH=amd64"
if /i "%UMF_HOST_ARCH%"=="ARM64" set "UMF_VCVARS_ARCH=arm64"
if /i "%UMF_HOST_ARCH%"=="x86" set "UMF_VCVARS_ARCH=x86"
call "%UMF_ENV_SCRIPT%" %UMF_VCVARS_ARCH%
exit /b %errorlevel%

:build
set "UMF_OUTPUT_OPTION="
set "UMF_GENERATOR_OPTION="
if defined UMF_OUTPUT_DIR set UMF_OUTPUT_OPTION="-DBUILD_DIR=%UMF_OUTPUT_DIR%"
if defined UMF_GENERATOR set UMF_GENERATOR_OPTION="-DGENERATOR=%UMF_GENERATOR%"
if defined UMF_JOBS (
    "%UMF_CMAKE_EXECUTABLE%" "-DTOOLCHAIN_DIR=%UMF_ARGUMENT_DIR%" "-DTARGET=%UMF_TARGET%" "-DJOBS=%UMF_JOBS%" %UMF_OUTPUT_OPTION% %UMF_GENERATOR_OPTION% -P "%UMF_SCRIPT_PATH%"
) else (
    "%UMF_CMAKE_EXECUTABLE%" "-DTOOLCHAIN_DIR=%UMF_ARGUMENT_DIR%" "-DTARGET=%UMF_TARGET%" %UMF_OUTPUT_OPTION% %UMF_GENERATOR_OPTION% -P "%UMF_SCRIPT_PATH%"
)
if errorlevel 1 exit /b 1
exit /b 0

:usage
echo Usage: Build.bat [toolchain-directory] [--target name] [--jobs count] [--build-dir directory] [--generator name] 1>&2
exit /b 1

:help
"%UMF_CMAKE_EXECUTABLE%" -DHELP=ON -P "%UMF_SCRIPT_PATH%"
exit /b %errorlevel%
