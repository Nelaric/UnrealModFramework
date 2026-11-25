@rem Copyright (c) 2026 Nelaric Contributors
@echo off
setlocal DisableDelayedExpansion

for %%D in ("%~dp0..") do set "root=%%~fD"
set "source=%root%\UnrealModFramework\Plugins\ModRuntime"
set "tools=%source%\Tools\build"
set "arch=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "arch=%PROCESSOR_ARCHITEW6432%"
if /i "%arch%"=="AMD64" set "arch=x64"
if /i "%arch%"=="ARM64" set "arch=arm64"
set "receipt=libclang-Windows-%arch%.path"

if not exist "%source%\ModRuntime.uplugin" (
    echo ModRuntime.uplugin is missing from this checkout. 1>&2
    exit /b 1
)
for %%T in (ModHeaderTool ModBuildTool ModSourceTool ModSDKTool) do (
    if not exist "%tools%\%%T\%%T.exe" goto setup_required
)
if not exist "%tools%\%receipt%" goto setup_required
set /p "library=" < "%tools%\%receipt%"
if not defined library goto setup_required
set "library=%library:/=\%"
if not exist "%tools%\%library%" goto setup_required
for %%F in ("%tools%\%library%") do set "clang_bin=%%~dpF"
for %%D in ("%clang_bin%..") do (
    set "clang_root=%%~fD"
    set "clang_name=%%~nxD"
)

set "dist=%root%\Dist"
set "output=%dist%\ModRuntime"
set "name=.ModRuntime-%RANDOM%-%RANDOM%"
set "plugin=%dist%\%name%"
set "previous=%dist%\%name%-previous"
set "previous_moved="
set "output_tools=%plugin%\Tools\build"
mkdir "%plugin%" || exit /b 1

robocopy "%source%" "%plugin%" /E /XD "%tools%" Binaries Intermediate Saved DerivedDataCache obj .vs .git /XF *.ubtplugin.csproj.props *.modsource-lock /R:0 /W:0 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 exit /b 1
copy /y "%root%\LICENSE" "%plugin%\LICENSE" >nul || exit /b 1
copy /y "%root%\NOTICE.md" "%plugin%\NOTICE.md" >nul || exit /b 1

for %%T in (ModHeaderTool ModBuildTool ModSourceTool ModSDKTool) do (
    mkdir "%output_tools%\%%T" || exit /b 1
    copy /y "%tools%\%%T\%%T.exe" "%output_tools%\%%T\%%T.exe" >nul || exit /b 1
)
robocopy "%tools%\ModSDKTool\Licenses" "%output_tools%\ModSDKTool\Licenses" /E /R:0 /W:0 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 exit /b 1
robocopy "%clang_root%" "%output_tools%\LibClang\%clang_name%" /E /R:0 /W:0 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 exit /b 1
>"%output_tools%\%receipt%" echo LibClang/%clang_name%/bin/libclang.dll
if exist "%output%" (
    if not exist "%output%\ModRuntime.uplugin" (
        echo Cannot replace "%output%" because it is missing ModRuntime.uplugin. 1>&2
        exit /b 1
    )
    ren "%output%" "%name%-previous" || exit /b 1
    set "previous_moved=1"
)
ren "%plugin%" ModRuntime || (
    if defined previous_moved if not exist "%output%" ren "%previous%" ModRuntime
    exit /b 1
)
if defined previous_moved rmdir /s /q "%previous%"
echo "%output%"
exit /b 0

:setup_required
echo Run Setup.bat in this checkout first. 1>&2
exit /b 1
