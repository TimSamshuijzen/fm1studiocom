@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Copyright (C) 2026 Tim Samshuijzen
rem
rem build.cmd - on Windows: a project zip of FM-1 Studio to a .fwsc package
rem for a real M-VAVE FM-1. It starts firmware/build.sh in WSL.
rem
rem   firmware\build.cmd src\examples\neonreef\neonreef.zip [NAME]
rem
rem The zip is a path in this repository, or a full path. See firmware\README.md.
setlocal
if "%~1"=="" (
    echo usage: firmware\build.cmd PROJECT.zip [NAME]
    exit /b 2
)
where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo This computer has no WSL. See firmware\README.md, "One time: WSL".
    exit /b 3
)
cd /d "%~dp0.."
set "ZIP=%~1"
set "ZIP=%ZIP:\=/%"
if "%~2"=="" (
    wsl.exe bash firmware/build.sh "%ZIP%"
) else (
    wsl.exe bash firmware/build.sh "%ZIP%" "%~2"
)
exit /b %errorlevel%
