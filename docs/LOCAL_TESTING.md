# 로컬 Claude Code로 실전 테스트 맡기기

StarCraft 1.16.1 + BWAPI 4.4.0이 설치된 Windows PC에서 Claude Code를 실행해 테스트를 진행하는 방법.
Claude Code는 이 저장소의 `CLAUDE.md`를 자동으로 읽는다.

## 준비 (사람이 할 일)

1. StarCraft: Brood War 1.16.1, BWAPI 4.4.0, Visual Studio(C++ 데스크톱, x86), CMake, Git, Python 3, 최신 VC++ 재배포 패키지 **x86** 설치
2. 저장소 받기
   ```bat
   git clone https://github.com/jaeone811/Starcraft_bot.git
   cd Starcraft_bot
   git checkout claude/beginner-terran-bot
   ```
3. 이 폴더에서 `claude` 실행 → 아래 프롬프트 붙여넣기 (경로 두 개는 본인 환경으로 수정)

## 첫 프롬프트 (그대로 복사)

```
이 저장소는 StarCraft: Brood War 1.16.1용 BWAPI 4.4.0 AI 봇(JaeTankBot)이다.
CLAUDE.md, README.md, docs/VALIDATION.md 를 먼저 읽어라.

내 환경:
- StarCraft 폴더: C:\Games\StarCraft        (수정)
- BWAPI 설치 폴더: %USERPROFILE%\BWAPI       (수정)

해야 할 일:
1. README 2절 체크리스트로 OS, Visual Studio, CMake, BWAPI 설치, StarCraft 버전(1.16.1)을 확인하고 결과를 표로 보고해라.
   1.16.1이 아니거나 BWAPI가 없으면 거기서 멈추고 알려줘라.
2. scripts\build_windows.bat 로 빌드하고 ctest 결과를 보여줘라. 실패하면 원인을 고쳐라.
3. scripts\install_to_starcraft.bat 로 설치하고, JaeTankBotConfig.exe 로 난이도 3, 내 StarCraft의 maps 폴더에 있는
   2인용 맵 하나, 상대 Protoss 로 bwapi.ini 를 설정해라(백업 자동). bwapi-data\AI\JaeTankBot.ini 의 local_speed 는 0.
4. 준비되면 나한테 "Chaoslauncher 관리자 실행 → BWAPI Injector [RELEASE] 체크 → Start" 를 요청하고 기다려라.
5. 경기가 끝났다고 하면 최신 로그와 results.csv 를 docs/VALIDATION.md 3절 체크리스트대로 점검하고,
   항목별로 "확인됨 / 문제 있음 / 판단 불가" 와 근거 로그 줄을 보고해라.
6. 문제가 있으면 원인을 코드에서 찾아 최소한으로 고치고, 빌드+ctest 통과 후 다시 설치하고 다음 경기를 요청해라.
   난이도 수치를 억지로 바꾸지 말고 로직을 고쳐라. 고친 내용은 브랜치에 커밋만 하고 push 는 나한테 물어봐라.
게임 안에서 확인되지 않은 것은 "검증되지 않음"이라고 써라.
```

## 반복 대전 단계 (1차가 정상 동작한 뒤)

```
이제 난이도별 비교를 하자. docs/VALIDATION.md 4절을 따른다.
bwapi.ini [auto_menu] 에 auto_restart = ON 을 설정해도 되는지 나한테 물어보고, 허락하면 백업 후 설정해라.
난이도 1~5 각각 같은 맵/상대로 최소 10경기씩 돌릴 거다. 난이도를 바꿀 때마다 JaeTankBotConfig 로 바꾸고
나한테 게임 시작을 요청해라. 각 난이도가 끝나면 summarize_results.py 결과를 보여주고,
1~2단계가 고장 없이 쉬운지, 4~5단계가 3단계보다 나은지 판단해라. 결과는 docs/VALIDATION.md 5절 표에 기록해라.
```

## 알아둘 것

- **게임 시작은 사람이 해야 한다.** Chaoslauncher는 관리자 권한 GUI라 Claude가 Start를 누를 수 없다.
  `auto_menu = SINGLE_PLAYER` 와 `auto_restart = ON` 이면 한 번 시작한 뒤에는 메뉴 진행과 다음 경기가 자동으로 넘어간다.
  반복을 멈추려면 게임을 직접 종료한다.
- 경기마다 새 로그 파일(`JaeTankBot_날짜_시간.log`)이 생기고 `results.csv`에 한 줄씩 추가된다.
- StarCraft가 `Program Files` 아래에 있으면 로그 쓰기가 막힐 수 있다 (게임 화면에 `[log file could not be opened]`).
- 봇이 크래시하면 `StarCraft\Errors\` 폴더의 BWAPI 오류 로그도 Claude에게 읽게 한다.
- 3단계와 기본 AI의 실력이 비슷한지는 여러 맵과 종족으로 반복 대전한 결과로만 판단한다.
