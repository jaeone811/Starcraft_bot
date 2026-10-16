@echo off
rem Copies JaeTankBot into a StarCraft 1.16.1 folder that already has BWAPI 4.4.0.
rem Only adds files under bwapi-data\AI; it never touches StarCraft's own files.
rem bwapi.ini is NOT changed here: JaeTankBotPlay.exe starts games without changing it
rem (for Chaoslauncher use tools\JaeTankBotConfig.exe --apply-bwapi-ini).
rem Usage: install_to_starcraft.bat "C:\path\to\StarCraft"
setlocal
set "SC=%~1"
if "%SC%"=="" (
  echo Usage: %~nx0 "C:\path\to\StarCraft"
  exit /b 1
)
if not exist "%SC%\bwapi-data\BWAPI.dll" (
  echo [error] "%SC%\bwapi-data\BWAPI.dll" not found. Install BWAPI 4.4.0 into this StarCraft folder first.
  exit /b 1
)

rem Works from the repository (build\Release) and from the packaged zip (bwapi-data\AI).
set "SRC=%~dp0"
set "DLL=%SRC%bwapi-data\AI\JaeTankBot.dll"
if not exist "%DLL%" set "DLL=%SRC%..\build\Release\JaeTankBot.dll"
set "INI=%SRC%bwapi-data\AI\JaeTankBot.ini"
if not exist "%INI%" set "INI=%SRC%..\config\JaeTankBot.ini"
if not exist "%DLL%" (
  echo [error] JaeTankBot.dll not found. Build it first: scripts\build_windows.bat
  exit /b 1
)

if not exist "%SC%\bwapi-data\AI" mkdir "%SC%\bwapi-data\AI"
copy /Y "%DLL%" "%SC%\bwapi-data\AI\JaeTankBot.dll" >nul || exit /b 1
echo Copied JaeTankBot.dll to "%SC%\bwapi-data\AI"
rem The game launcher (JaeTankBotPlay.exe) finds the StarCraft folder from bwapi-data\AI.
set "PLAY=%SRC%bwapi-data\AI\JaeTankBotPlay.exe"
if not exist "%PLAY%" set "PLAY=%SRC%..\build\Release\JaeTankBotPlay.exe"
if exist "%PLAY%" (
  copy /Y "%PLAY%" "%SC%\bwapi-data\AI\JaeTankBotPlay.exe" >nul || exit /b 1
  echo Copied JaeTankBotPlay.exe ^(game launcher^) to "%SC%\bwapi-data\AI"
)
if exist "%SC%\bwapi-data\AI\JaeTankBot.ini" (
  echo Kept the existing "%SC%\bwapi-data\AI\JaeTankBot.ini"
) else (
  copy "%INI%" "%SC%\bwapi-data\AI\JaeTankBot.ini" >nul || exit /b 1
  echo Copied default JaeTankBot.ini ^(difficulty 3^)
)
echo.
echo Play: "%SC%\bwapi-data\AI\JaeTankBotPlay.exe" ^(no Chaoslauncher needed^)
echo With Chaoslauncher: set "ai = bwapi-data/AI/JaeTankBot.dll" in "%SC%\bwapi-data\bwapi.ini"
echo       or run: JaeTankBotConfig.exe --starcraft "%SC%" --apply-bwapi-ini
