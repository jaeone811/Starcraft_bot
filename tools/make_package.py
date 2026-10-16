"""Makes the JaeTankBot distribution zip (dist/JaeTankBot-<version>-win32.zip).

    python tools/make_package.py [--bwapi "C:/Users/me/BWAPI"]

Build first (scripts/build_windows.bat). The zip holds the bot, its launcher and
settings tool, and BWAPI 4.4.0's runtime files (LGPL-3.0, included unmodified with
its license). StarCraft itself is NOT included: StarCraft: Brood War 1.16.1 is
Blizzard's game and every player installs their own copy.
"""
import argparse
import os
import re
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = os.path.join(ROOT, 'build', 'Release')

README = """JaeTankBot {version} - 스타크래프트: 브루드워 테란 AI (난이도 1~5)
================================================================

이 압축 파일에는 스타크래프트 게임 파일이 들어 있지 않습니다.
StarCraft: Brood War 1.16.1 은 각자 설치한 것을 사용하세요
(BWAPI 는 1.16.1 에서만 동작합니다. 리마스터 / 1.18 이상은 안 됩니다).

준비물
  - StarCraft: Brood War 1.16.1 이 설치된 폴더 (StarCraft.exe 가 있는 곳)
  - Microsoft Visual C++ 재배포 패키지 2015-2022 (x86, 32비트)

설치
  1. BWAPI 가 없다면: install_bwapi.bat 을 StarCraft 폴더에 대해 실행
       install_bwapi.bat "C:\\StarCraft"
     (BWAPI-4.4.0 폴더의 BWAPI.dll, bwapi.ini, SNP_DirectIP.snp 를 없는 것만 복사합니다)
  2. install.bat "C:\\StarCraft"
     (bwapi-data\\AI 에 JaeTankBot.dll, JaeTankBot.ini, JaeTankBotPlay.exe 를 복사합니다)

플레이
  StarCraft\\bwapi-data\\AI\\JaeTankBotPlay.exe 를 실행 (바탕화면 바로가기 추천)
  - 사람 대 JaeTankBot: 내 화면은 일반 스타크래프트로 뜹니다.
      Multiplayer → Brood War → Local PC → 아이디 → 게임 목록의 JaeTankBot 게임에 참가
  - JaeTankBot 대 다른 봇 / 대 컴퓨터: 자동으로 시작합니다.
  - 봇 쪽 스타크래프트는 기본적으로 소리가 꺼져 있습니다.
  Chaoslauncher 는 필요 없습니다. 백신이 JaeTankBotPlay.exe 를 막으면
  (StarCraft 에 BWAPI.dll 을 불러오는 방식이 Chaoslauncher 와 같아서) 예외로 등록하세요.

설정
  tools\\JaeTankBotConfig.exe --list    난이도 설명
  bwapi-data\\AI\\JaeTankBot.ini         기본 난이도, 로그 등
  로그와 결과: StarCraft\\bwapi-data\\write\\JaeTankBot\\

라이선스
  BWAPI 4.4.0: LGPL-3.0 (BWAPI-4.4.0\\LICENSE-BWAPI.txt, 소스 https://github.com/bwapi/bwapi)
"""

INSTALL_BWAPI = r"""@echo off
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
  echo [error] "%SC%\StarCraft.exe" not found. Give the StarCraft 1.16.1 folder.
  exit /b 1
)
set "SRC=%~dp0BWAPI-4.4.0"
if not exist "%SC%\bwapi-data\data" mkdir "%SC%\bwapi-data\data"
for %%F in (bwapi-data\BWAPI.dll bwapi-data\bwapi.ini bwapi-data\data\Broodwar.map SNP_DirectIP.snp) do (
  if exist "%SC%\%%F" (
    echo kept     %%F
  ) else (
    copy "%SRC%\%%F" "%SC%\%%F" >nul && echo copied   %%F
  )
)
echo.
echo Next: install.bat "%SC%"
"""


def version():
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    return re.search(r'project\(JaeTankBot VERSION ([0-9.]+)', text).group(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bwapi', default=os.path.join(os.environ.get('USERPROFILE', ''), 'BWAPI'),
                        help='BWAPI 4.4.0 install folder (has Starcraft/bwapi-data/BWAPI.dll and LICENSE.txt)')
    args = parser.parse_args()
    v = version()
    top = 'JaeTankBot-%s' % v
    files = {
        'bwapi-data/AI/JaeTankBot.dll': os.path.join(RELEASE, 'JaeTankBot.dll'),
        'bwapi-data/AI/JaeTankBotPlay.exe': os.path.join(RELEASE, 'JaeTankBotPlay.exe'),
        'bwapi-data/AI/JaeTankBot.ini': os.path.join(ROOT, 'config', 'JaeTankBot.ini'),
        'tools/JaeTankBotConfig.exe': os.path.join(RELEASE, 'JaeTankBotConfig.exe'),
        'install.bat': os.path.join(ROOT, 'scripts', 'install_to_starcraft.bat'),
        'jaetankbot.ico': os.path.join(ROOT, 'resources', 'jaetankbot.ico'),
    }
    bw = os.path.join(args.bwapi, 'Starcraft')
    bwapi_files = {
        'BWAPI-4.4.0/bwapi-data/BWAPI.dll': os.path.join(bw, 'bwapi-data', 'BWAPI.dll'),
        'BWAPI-4.4.0/bwapi-data/bwapi.ini': os.path.join(bw, 'bwapi-data', 'bwapi.ini'),
        'BWAPI-4.4.0/bwapi-data/data/Broodwar.map': os.path.join(bw, 'bwapi-data', 'data', 'Broodwar.map'),
        'BWAPI-4.4.0/SNP_DirectIP.snp': os.path.join(bw, 'SNP_DirectIP.snp'),
        'BWAPI-4.4.0/LICENSE-BWAPI.txt': os.path.join(args.bwapi, 'LICENSE.txt'),
    }
    missing = [src for src in files.values() if not os.path.isfile(src)]
    if missing:
        raise SystemExit('missing (build first?): ' + ', '.join(missing))
    if any(not os.path.isfile(src) for src in bwapi_files.values()):
        print('BWAPI files not found under', args.bwapi, '- the zip will not include BWAPI')
        bwapi_files = {}
    os.makedirs(os.path.join(ROOT, 'dist'), exist_ok=True)
    out = os.path.join(ROOT, 'dist', top + '-win32.zip')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, src in list(files.items()) + list(bwapi_files.items()):
            z.write(src, top + '/' + name)
        z.writestr(top + '/README.txt', README.format(version=v).replace('\n', '\r\n').encode('utf-8-sig'))
        if bwapi_files:
            z.writestr(top + '/install_bwapi.bat', INSTALL_BWAPI.replace('\n', '\r\n'))
    print('wrote', out)
    with zipfile.ZipFile(out) as z:
        for info in z.infolist():
            print('  %8d  %s' % (info.file_size, info.filename))


if __name__ == '__main__':
    main()
