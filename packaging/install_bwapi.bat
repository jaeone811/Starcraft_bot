@echo off
rem Copies BWAPI 4.4.0's runtime files into a StarCraft 1.16.1 folder.
rem Only files that are missing are copied; nothing of StarCraft is changed.
rem Usage: install_bwapi.bat "C:\path\to\StarCraft"
setlocal
set "SC=%~1"
if "%SC%"=="" (
  echo Usage: %~nx0 "C:\path\to\StarCraft"
  exit /b 1
)
if not exist "%SC%\StarCraft.exe" (
  echo [error] StarCraft.exe not found in the given folder.
  exit /b 1
)
set "SRC=%~dp0BWAPI-4.4.0"
if not exist "%SC%\bwapi-data\data" mkdir "%SC%\bwapi-data\data" 2>nul
if not exist "%SC%\bwapi-data\data" exit /b 1
for %%F in (bwapi-data\BWAPI.dll bwapi-data\bwapi.ini bwapi-data\data\Broodwar.map SNP_DirectIP.snp) do (
  if exist "%SC%\%%F" (
    echo   kept     %%F
  ) else (
    copy "%SRC%\%%F" "%SC%\%%F" >nul || exit /b 1
    echo   copied   %%F
  )
)
exit /b 0
