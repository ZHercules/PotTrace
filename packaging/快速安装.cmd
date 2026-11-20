@echo off
setlocal
set "PSModulePath="
if "%~1"=="" goto pick
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0QuickSetup.ps1" -Action Install -PotPlayerPath "%~1"
goto done
:pick
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0QuickSetup.ps1" -Action Install
:done
set "result=%errorlevel%"
pause
exit /b %result%
