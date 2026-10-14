@echo off
rem Builds BeginnerBot.dll (32-bit, Release) with Visual Studio + CMake.
rem Usage: scripts\build_windows.bat [BWAPI folder]
rem The BWAPI folder defaults to %USERPROFILE%\BWAPI (BWAPI 4.4.0 installer default).
setlocal
cd /d "%~dp0\.."

set "BWAPI_DIR=%~1"
if "%BWAPI_DIR%"=="" set "BWAPI_DIR=%USERPROFILE%\BWAPI"
if not exist "%BWAPI_DIR%\include\BWAPI.h" (
  echo [error] "%BWAPI_DIR%\include\BWAPI.h" not found.
  echo         Install BWAPI 4.4.0 or pass the folder: scripts\build_windows.bat C:\path\to\BWAPI
  exit /b 1
)

cmake -S . -B build -A Win32 -DBWAPI_DIR="%BWAPI_DIR%" || exit /b 1
cmake --build build --config Release || exit /b 1
ctest --test-dir build -C Release --output-on-failure || exit /b 1

echo.
echo Built: %CD%\build\Release\BeginnerBot.dll
echo        %CD%\build\Release\BeginnerBotConfig.exe
echo Next:  scripts\install_to_starcraft.bat "C:\path\to\StarCraft"
