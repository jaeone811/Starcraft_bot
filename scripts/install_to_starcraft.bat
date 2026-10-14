@echo off
rem Copies BeginnerBot into a StarCraft 1.16.1 folder that already has BWAPI 4.4.0.
rem Only adds files under bwapi-data\AI; it never touches StarCraft's own files.
rem bwapi.ini is NOT changed here - use tools\BeginnerBotConfig.exe --apply-bwapi-ini for that.
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
set "DLL=%SRC%bwapi-data\AI\BeginnerBot.dll"
if not exist "%DLL%" set "DLL=%SRC%..\build\Release\BeginnerBot.dll"
set "INI=%SRC%bwapi-data\AI\BeginnerBot.ini"
if not exist "%INI%" set "INI=%SRC%..\config\BeginnerBot.ini"
if not exist "%DLL%" (
  echo [error] BeginnerBot.dll not found. Build it first: scripts\build_windows.bat
  exit /b 1
)

if not exist "%SC%\bwapi-data\AI" mkdir "%SC%\bwapi-data\AI"
copy /Y "%DLL%" "%SC%\bwapi-data\AI\BeginnerBot.dll" >nul || exit /b 1
echo Copied BeginnerBot.dll to "%SC%\bwapi-data\AI"
if exist "%SC%\bwapi-data\AI\BeginnerBot.ini" (
  echo Kept the existing "%SC%\bwapi-data\AI\BeginnerBot.ini"
) else (
  copy "%INI%" "%SC%\bwapi-data\AI\BeginnerBot.ini" >nul || exit /b 1
  echo Copied default BeginnerBot.ini ^(difficulty 3^)
)
echo.
echo Next: set "ai = bwapi-data/AI/BeginnerBot.dll" in "%SC%\bwapi-data\bwapi.ini"
echo       or run: BeginnerBotConfig.exe --starcraft "%SC%" --apply-bwapi-ini
