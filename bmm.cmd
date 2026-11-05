@echo off
setlocal
pushd "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0bmm.ps1" %*
set "BMM_EXIT=%ERRORLEVEL%"
popd
exit /b %BMM_EXIT%
