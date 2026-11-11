@rem Copyright (c) 2026 Nelaric Contributors
@echo off
setlocal DisableDelayedExpansion
call "%~dp0Setup\FindCMake.bat"
if errorlevel 1 exit /b 1
call "%~dp0UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat" --setup %*
exit /b %errorlevel%
