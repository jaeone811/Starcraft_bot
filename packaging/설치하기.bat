@echo off
chcp 65001 >nul
rem JaeTankBot setup: BWAPI 4.4.0 (missing files only), the bot, a desktop shortcut.
rem Usage: double-click, or 설치하기.bat "C:\StarCraft"
setlocal
cd /d "%~dp0"
echo ============================================
echo  JaeTankBot 설치
echo ============================================
echo.
set "SC=%~1"
if not "%SC%"=="" goto have
echo 스타크래프트 1.16.1 폴더(StarCraft.exe 가 있는 폴더)를
echo 이 창으로 끌어다 놓거나 경로를 입력하고 Enter 를 누르세요.
set /p "SC=> "
:have
set "SC=%SC:"=%"
if "%SC:~-1%"=="\" set "SC=%SC:~0,-1%"
if exist "%SC%\StarCraft.exe" goto found
echo.
echo [오류] 이 폴더에 StarCraft.exe 가 없습니다:
echo   %SC%
goto fail

:found
echo.
echo 스타크래프트 폴더: %SC%
if exist "%SystemRoot%\SysWOW64\msvcp140.dll" goto runtime_ok
if exist "%SystemRoot%\System32\msvcp140.dll" goto runtime_ok
echo.
echo [안내] Visual C++ 재배포 패키지(x86)가 없는 것 같습니다. 설치 후 게임을 실행하세요:
echo   https://aka.ms/vs/17/release/vc_redist.x86.exe
:runtime_ok

echo.
echo [1/3] BWAPI 4.4.0
call "%~dp0install_bwapi.bat" "%SC%"
if errorlevel 1 goto noaccess

echo.
echo [2/3] JaeTankBot
call "%~dp0install.bat" "%SC%"
if errorlevel 1 goto noaccess

echo.
echo [3/3] 바탕화면 바로가기
powershell -NoProfile -ExecutionPolicy Bypass -Command "$w = New-Object -ComObject WScript.Shell; $s = $w.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\JaeTankBot.lnk'); $s.TargetPath = $env:SC + '\bwapi-data\AI\JaeTankBotPlay.exe'; $s.WorkingDirectory = $env:SC + '\bwapi-data\AI'; $s.Description = 'JaeTankBot launcher'; $s.Save()"
if errorlevel 1 echo   바로가기를 만들지 못했습니다. %SC%\bwapi-data\AI\JaeTankBotPlay.exe 를 직접 실행하세요.

echo.
echo ============================================
echo  설치 완료. 바탕화면의 JaeTankBot 을 실행하세요.
echo  자세한 방법: 실행방법.txt
echo ============================================
pause
exit /b 0

:noaccess
echo.
echo [오류] 스타크래프트 폴더에 파일을 복사하지 못했습니다.
echo   이 파일을 마우스 오른쪽 클릭 - "관리자 권한으로 실행" 으로 다시 실행해 보세요.
:fail
echo.
pause
exit /b 1
