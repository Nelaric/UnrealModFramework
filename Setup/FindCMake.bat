@rem Copyright (c) 2026 Nelaric Contributors
@echo off
setlocal DisableDelayedExpansion

set "UMF_CMAKE_ARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "UMF_CMAKE_ARCH=%PROCESSOR_ARCHITEW6432%"
set "UMF_CMAKE_PLATFORM="
if /i "%UMF_CMAKE_ARCH%"=="AMD64" set "UMF_CMAKE_PLATFORM=windows-x86_64"
if /i "%UMF_CMAKE_ARCH%"=="ARM64" set "UMF_CMAKE_PLATFORM=windows-arm64"
if /i "%UMF_CMAKE_ARCH%"=="x86" set "UMF_CMAKE_PLATFORM=windows-i386"
set "UMF_CMAKE_VERSION="
set "UMF_CMAKE_SHA256="
for /f "usebackq eol=# tokens=1,2" %%K in ("%~dp0CMakePackages.txt") do (
    if "%%K"=="version" set "UMF_CMAKE_VERSION=%%L"
    if "%%K"=="%UMF_CMAKE_PLATFORM%" set "UMF_CMAKE_SHA256=%%L"
)

for %%D in ("%~dp0..\.tools\cmake") do set "UMF_CMAKE_CACHE=%%~fD"
set "UMF_CMAKE_PACKAGE=cmake-%UMF_CMAKE_VERSION%-%UMF_CMAKE_PLATFORM%"
set "UMF_CMAKE_BINARY=%UMF_CMAKE_CACHE%\%UMF_CMAKE_PACKAGE%\bin\cmake.exe"
if exist "%UMF_CMAKE_BINARY%" (
    call :usable "%UMF_CMAKE_BINARY%"
    if not errorlevel 1 goto found
)
for /f "delims=" %%C in ('where.exe cmake.exe 2^>nul') do (
    call :usable "%%C"
    if not errorlevel 1 (
        endlocal & set "UMF_CMAKE_EXECUTABLE=%%C"
        exit /b 0
    )
)
if not defined UMF_CMAKE_SHA256 goto no_package
if not defined UMF_CMAKE_VERSION goto no_package

if not exist "%UMF_CMAKE_CACHE%\" mkdir "%UMF_CMAKE_CACHE%"
if not exist "%UMF_CMAKE_CACHE%\" exit /b 1
:create_download_directory
set "UMF_CMAKE_DOWNLOAD_NAME=.download-%RANDOM%-%RANDOM%-%RANDOM%"
set "UMF_CMAKE_WORK=%UMF_CMAKE_CACHE%\%UMF_CMAKE_DOWNLOAD_NAME%"
if exist "%UMF_CMAKE_WORK%" goto create_download_directory
mkdir "%UMF_CMAKE_WORK%" || exit /b 1

set "UMF_CMAKE_ARCHIVE=%UMF_CMAKE_WORK%\%UMF_CMAKE_PACKAGE%.zip"
for /f "tokens=1,2 delims=." %%M in ("%UMF_CMAKE_VERSION%") do set "UMF_CMAKE_RELEASE=%%M.%%N"
echo Downloading CMake %UMF_CMAKE_VERSION% for %UMF_CMAKE_PLATFORM%...
curl.exe --fail --location --silent --show-error --retry 2 --connect-timeout 20 --max-time 300 --output "%UMF_CMAKE_ARCHIVE%" "https://cmake.org/files/v%UMF_CMAKE_RELEASE%/%UMF_CMAKE_PACKAGE%.zip"
if errorlevel 1 goto download_failed
certutil.exe -hashfile "%UMF_CMAKE_ARCHIVE%" SHA256 | findstr.exe /i /x /c:"%UMF_CMAKE_SHA256%" >nul
if errorlevel 1 (
    echo CMake archive SHA-256 verification failed. 1>&2
    goto download_failed
)
tar.exe -xf "%UMF_CMAKE_ARCHIVE%" -C "%UMF_CMAKE_WORK%"
if errorlevel 1 goto download_failed
call :usable "%UMF_CMAKE_WORK%\%UMF_CMAKE_PACKAGE%\bin\cmake.exe"
if errorlevel 1 goto download_failed

rem Publish the complete package on the same filesystem. A concurrent run may have published it first.
for %%D in ("%UMF_CMAKE_WORK%\%UMF_CMAKE_PACKAGE%") do (
    if /i not "%%~dpD"=="%UMF_CMAKE_WORK%\" goto download_failed
    move /y "%%~fD" "%UMF_CMAKE_CACHE%\" >nul 2>&1
)
call :usable "%UMF_CMAKE_BINARY%"
if errorlevel 1 goto download_failed
call :cleanup
if errorlevel 1 exit /b 1

:found
endlocal & set "UMF_CMAKE_EXECUTABLE=%UMF_CMAKE_BINARY%"
exit /b 0

:no_package
echo No portable CMake package is available for this Windows architecture. Put CMake 3.20 or newer on PATH. 1>&2
exit /b 1

:download_failed
echo Could not prepare workspace CMake. Retry Setup or put CMake 3.20 or newer on PATH. 1>&2
call :cleanup
exit /b 1

:cleanup
for %%D in ("%UMF_CMAKE_WORK%") do (
    if /i not "%%~dpD"=="%UMF_CMAKE_CACHE%\" exit /b 1
    if /i not "%%~nxD"=="%UMF_CMAKE_DOWNLOAD_NAME%" exit /b 1
    rd /s /q "%%~fD"
)
if exist "%UMF_CMAKE_WORK%" exit /b 1
exit /b 0

:usable
if not exist "%~1" exit /b 1
for /f "tokens=1-4 delims=. " %%A in ('call "%~1" --version 2^>nul') do (
    if "%%A %%B"=="cmake version" (
        if %%C GTR 3 exit /b 0
        if %%C EQU 3 if %%D GEQ 20 exit /b 0
        exit /b 1
    )
)
exit /b 1
