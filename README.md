# BeginnerBot — 스타크래프트: 브루드워 초보 AI 봇 (Terran, 난이도 1~5)

BWAPI 4.4.0으로 게임 상태를 읽고 유닛에 명령을 내리는 C++ AI 모듈(DLL)이다. 화면 인식/마우스 조작은 쓰지 않는다.
BWAPI 기본 설정(전장의 안개 적용, 보이지 않는 유닛 정보 차단)을 그대로 사용하며 `CompleteMapInformation` 같은 치트 플래그를 켜지 않는다.

> **상태 요약**
> - 빌드: Windows MSVC 32비트(Win32, Release)와 Linux g++에서 빌드 성공, 단위 테스트/모듈 로드 테스트 통과 (GitHub Actions)
> - **실제 게임 실행: 아직 검증되지 않음.** 개발 환경(Linux 클라우드 컨테이너)에 StarCraft가 없었다. 자세한 내용은 [docs/VALIDATION.md](docs/VALIDATION.md)
> - 로컬 Claude Code로 실전 테스트하기: [docs/LOCAL_TESTING.md](docs/LOCAL_TESTING.md)

---

## 1. 호환성 (먼저 확인할 것)

| 구성 요소 | 필요 버전 | 근거 |
|---|---|---|
| 게임 | **StarCraft: Brood War 1.16.1** | BWAPI 공식 README Quick Start, `bwapi/include/starcraftver.h` (`STARCRAFT_VER "1.16.1"`) |
| BWAPI | **4.4.0** (공식 최신 릴리스) | https://github.com/bwapi/bwapi/releases |
| 컴파일러 | Visual Studio 2017 이상 (C++ 데스크톱 개발, x86) | CI는 VS 2026(MSVC 19.51)으로 확인 |
| CMake | 3.16 이상 | |
| 런타임 | **최신 Visual C++ 재배포 패키지 x86 (v14)** | 아래 참고 |

**StarCraft: Remastered(1.18 이상)는 BWAPI 4.4.0이 지원하지 않는다.** BWAPI는 게임 메모리 구조에 의존하므로 리마스터에 주입해도 동작하지 않는다.
선택지:
1. 1.16.1 클라이언트를 별도 폴더에 설치해서 사용 (리마스터와 공존 가능). 1.16.1은 현재 Blizzard 공식 다운로드로 제공되지 않으므로, 기존 CD/설치본 + 공식 1.16.1 패치 등 **합법적으로 확보한 사본**을 사용할 것. 커뮤니티 튜토리얼(SSCAIT, JBWAPI)이 배포처를 안내하지만 라이선스 확인은 사용자 책임이다.
2. 리마스터만 있다면 이 봇은 실행할 수 없다 (게임 화면 인식 방식은 이 프로젝트 범위 밖).

**런타임 주의:** BWAPI 설치본에는 VC++ 2017 재배포 패키지가 들어 있다. 이 봇을 새 툴셋(VS 2022/2026)으로 빌드했다면 *빌드 툴셋 버전 이상*의 런타임이 필요하므로
Microsoft의 최신 **x86** "Visual C++ v14 Redistributable"을 설치한다. (MSVC 2015~2026은 바이너리 호환이라 VS2017로 빌드된 BWAPI.dll과 함께 쓸 수 있다.)

## 2. 개발 환경 점검 체크리스트 (Windows PC)

PowerShell에서:
```powershell
[Environment]::OSVersion.Version                     # Windows 10/11
cmake --version                                        # 3.16+
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property displayName
Test-Path "$env:USERPROFILE\BWAPI\include\BWAPI.h"    # BWAPI 4.4.0 설치 여부 (기본 설치 경로)
Test-Path "C:\경로\StarCraft\bwapi-data\BWAPI.dll"     # StarCraft 폴더에 BWAPI가 설치됐는지
```
StarCraft 버전 확인: `StarCraft.exe` 속성 → 자세히 → 파일 버전이 `1.16.1.x` 이어야 한다. Chaoslauncher에서도 "Starcraft 1.16.1"로 표시된다.

## 3. 설치

1. Visual Studio(“C++를 사용한 데스크톱 개발” 워크로드)와 CMake 설치
2. StarCraft: Brood War 1.16.1 설치 (Remastered와 다른 폴더)
3. [BWAPI 4.4.0](https://github.com/bwapi/bwapi/releases/tag/v4.4.0) 설치 프로그램 실행 → StarCraft 폴더 지정. 기본 컴포넌트(헤더 + BWAPILIB 소스 포함)를 그대로 둔다.
4. 최신 Visual C++ v14 재배포 패키지 **x86** 설치

## 4. 빌드

```bat
git clone https://github.com/jaeone811/Starcraft_bot.git
cd Starcraft_bot
scripts\build_windows.bat                      :: BWAPI가 %USERPROFILE%\BWAPI 에 있을 때
scripts\build_windows.bat "D:\BWAPI"           :: 다른 경로
```
직접 실행하려면:
```bat
cmake -S . -B build -A Win32 -DBWAPI_DIR="%USERPROFILE%\BWAPI"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
- 반드시 **Win32(32비트)** + **Release** (BWAPI.dll이 32비트 Release, `/MD` 런타임)
- `BWAPI_DIR` 은 `include\BWAPI.h` 와 `BWAPILIB\` 가 있는 폴더. BWAPI git 저장소를 쓸 경우 `<저장소>\bwapi` 폴더 (`git clone --branch v4.4.0 https://github.com/bwapi/bwapi.git`)
- 결과물: `build\Release\BeginnerBot.dll`, `build\Release\BeginnerBotConfig.exe`

빌드 없이 쓰려면 GitHub Actions의 `BeginnerBot-win32` 아티팩트(zip)를 받는다.

## 5. 게임에 설치하고 실행

```bat
scripts\install_to_starcraft.bat "C:\Games\StarCraft"
build\Release\BeginnerBotConfig.exe --starcraft "C:\Games\StarCraft"
```
- 설치 스크립트는 `bwapi-data\AI\` 에 `BeginnerBot.dll`(과 없을 때만 `BeginnerBot.ini`)을 **추가**할 뿐 게임 파일이나 `bwapi.ini`를 건드리지 않는다.
- 설정 도구에서 난이도/종족을 고르고, 원하면 `bwapi.ini`도 수정한다(수정 전 `bwapi.ini.beginnerbot-날짜.bak` 백업 생성, `[ai] ai`와 `[auto_menu]`의 몇 개 키만 변경).

수동으로 할 경우 `StarCraft\bwapi-data\bwapi.ini`:
```ini
[ai]
ai = bwapi-data/AI/BeginnerBot.dll

[auto_menu]            ; 기본 AI와 자동 대전 (선택)
auto_menu = SINGLE_PLAYER
race = Terran
enemy_count = 1
enemy_race = Protoss
game_type = MELEE
map = maps/(2)Boxer.scm
```
실행: BWAPI 설치 폴더의 `Chaoslauncher\Chaoslauncher.exe`를 **관리자 권한**으로 실행 → `BWAPI 4.4.0 Injector [RELEASE]` 체크 (창 모드는 `W-MODE`) → Start.
`auto_menu = OFF`이면 Single Player → Expansion → Play Custom에서 Terran으로 직접 게임을 만든다.
게임이 시작되면 화면 왼쪽 위에 `BeginnerBot 0.6.2 - difficulty 3 (Normal)` 문구가 나온다.

## 6. 난이도 / 종족 / 맵 설정

`StarCraft\bwapi-data\AI\BeginnerBot.ini` (없으면 `bwapi-data\read\BeginnerBot.ini`, 그다음 DLL과 같은 폴더, 모두 없으면 기본값 3).
StarCraft 폴더는 작업 폴더에 `bwapi-data`가 있으면 그 폴더, 아니면 DLL 위치(`…\bwapi-data\AI`)의 두 단계 위로 판단하며 로그 첫 부분에 기록된다:
```ini
[bot]
difficulty = 3      ; 1 매우 쉬움, 2 쉬움, 3 기본(기본값), 4 어려움, 5 매우 어려움
race = Terran       ; Terran만 구현
```
| 단계 | 이름 | 설명 |
|---|---|---|
| 1 | 매우 쉬움 (VeryEasy) | 단순한 빌드, 늦은 정찰과 공격, 집중 공격/후퇴 판단 제한 |
| 2 | 쉬움 (Easy) | 안정적인 운영, 기본 정찰/공격, 최적화되지 않은 조합과 타이밍 |
| 3 | 기본 (Normal) | 기본 AI 수준 **목표**: 표준 빌드, 기본 정찰, 집결 후 공격, 기본 집중 공격 |
| 4 | 어려움 (Hard) | 높은 가동률, 보급 예측, 정찰 정보 기반 조합/공격 시점, 효율적인 집중 공격과 후퇴 |
| 5 | 매우 어려움 (VeryHard) | 이른 다중 확장, 업그레이드, 위협 우선순위 집중 공격, 손실 기반 재판단 |

모든 수치와 코드 위치: [docs/DIFFICULTY.md](docs/DIFFICULTY.md). `BeginnerBotConfig.exe --list`로도 볼 수 있다.

- **종족**: 실제 플레이 종족은 로비 또는 `bwapi.ini [auto_menu] race`가 결정한다. Terran이 아니면 봇은 로그에 ERROR를 남기고 일꾼 채취만 한다.
- **맵**: `bwapi.ini [auto_menu] map` (자동 시작 시). 처음에는 2인용 맵 하나로만 테스트할 것.

## 7. 로그

| 파일 | 내용 |
|---|---|
| `StarCraft\bwapi-data\write\BeginnerBot\BeginnerBot_YYYYMMDD_HHMMSS.log` | 게임별 로그 |
| `StarCraft\bwapi-data\write\BeginnerBot\results.csv` | 경기마다 한 줄: 결과, 시간, 일꾼/자원/보급 막힘/생산 유휴/웨이브/막힘/예외 수 |

경로는 `[log] directory`로 바꿀 수 있다 (StarCraft 폴더 기준 상대 경로). 형식:
```
[f=1234 t=00:51] INFO  [Build] task #3 Terran_Barracks started at 54,12
```
- 시작 부분 `APPLIED difficulty=…` 줄 = 실제 적용된 난이도 설정 전체
- 약 30초마다 `Snapshot` 줄 = 보급/자원/일꾼 역할/병력/전투 상태/생산 계획
- 끝에 `metrics:` 줄과 `game over: WIN/LOSS`
- 요약: `python tools\summarize_results.py "C:\Games\StarCraft\bwapi-data\write\BeginnerBot\results.csv"`

## 8. 문제 해결

| 증상 | 확인/해결 |
|---|---|
| Chaoslauncher에 BWAPI Injector가 없음 / 주입 실패 | StarCraft가 1.16.1인지 확인(리마스터 불가). Chaoslauncher를 관리자 권한으로 실행 |
| 게임 시작 직후 "Failed to load AI module" 류 메시지 | `bwapi.ini`의 `ai =` 경로 확인, DLL이 **32비트**인지(`-A Win32`), 최신 **x86** VC++ 재배포 패키지 설치 여부 |
| DLL은 로드되는데 아무것도 안 함 | 로그의 `race=` 확인(Terran 아님 → 일꾼만), 리플레이인지 확인 |
| 로그 파일이 안 생김 | StarCraft 폴더 쓰기 권한(Program Files 아래면 권한 문제 가능), `[log] directory` 값. 게임 화면에 `[log file could not be opened]` 표시 |
| Debug 빌드 DLL이 크래시 | Debug 빌드는 `BWAPId.dll`과 `ai_dbg` 설정이 필요. Release로 빌드할 것 |
| 건물이 계속 안 지어짐 | 로그에서 `[Build] … failed` / `did not start` 확인. 6회 위치 실패 시 20초 쿨다운 후 재시도 |
| 병력이 공격을 안 나감 | `Snapshot`의 `army=`와 `attackSize=` 비교, 시간 조건(`earliestAttackFrame`), `no reachable target is known` 로그 |
| 같은 명령 반복/멈춤 | 로그 `received the same … 8 times`, `no progress towards …` (해당 목표는 3분간 제외) |
| 예외 | `ERROR` 줄. 매니저별로 격리되어 다른 매니저는 계속 동작한다. 단, 접근 위반(널 포인터) 같은 SEH 예외는 C++ `catch`로 잡히지 않으므로 BWAPI의 `StarCraft\Errors\` 로그를 확인 |

## 9. 구조

```
src/core/   BWAPI 없이 테스트 가능한 로직
  DifficultyConfig  난이도별 모든 수치 (유일한 출처)
  BotSettings, IniFile  설정 파일
  Planning          보급 예측, 예산(우선순위/저축), 병력 조합, 공격/후퇴 판단, 타겟 선택, 자원 클러스터링
  CommandHistory    같은 명령 반복 방지 + 반복 감지
  Logger, Metrics   로그와 검증용 지표
src/bot/    BWAPI 레이어
  BeginnerBot       AIModule: 이벤트 처리, 매니저 호출, 예외 격리, 로그/결과 기록
  Commander         모든 유닛 명령의 단일 통로 (중복 제거, 실패 로그)
  MapInfo           시작 위치, 광물 클러스터로 기지 위치/앞마당/집결지 계산
  InformationManager 본 적 있는 적 건물/유닛만 기억, 적 본진 확인/추론
  WorkerManager     (경제) 광물/가스 배분, 일꾼 대여(건설/정찰)
  BuildingManager   건설 작업: 자원 예약, 위치/일꾼 선택, 재시도, 건설 재개
  SupplyManager     (보급) 디포 필요 판단, 막힘 감지/적응
  ProductionManager (생산) 우선순위 지출: 디포 > SCV > 빌드오더/확장 > 연구 > 컴셋 > 병력
  ScoutManager      일꾼 정찰
  CombatManager     (전투) 집결/공격/후퇴/방어, 집중 공격, 재집결, 막힘 감지, 스팀/스캔
  StrategyManager   공격 조건 적응, 공격 목표 선택, 기지 위협 감지
tools/configurator  BeginnerBotConfig.exe (난이도/종족/맵 설정)
tests/              단위 테스트, BWAPI 데이터 테스트, 모듈 로드 테스트
```
외부 라이브러리는 BWAPI 4.4.0 외에 없다 (지형 분석 라이브러리 BWEM/BWTA 미사용).

## 10. 구현된 기능

- SCV 연속 생산(난이도별 목표 수), 광물 패치 균등 배분, 정제소당 SCV 3기, 기지 간 일꾼 재분배
- 서플라이 디포: 고정 여유 또는 생산량 기반 예측, 보급 막힘 시 무조건 건설, 막힌 뒤 여유 확대(3단계 이상)
- 빌드 오더(인구수 트리거), 배럭/정제소/아카데미/엔지니어링 베이, 자원이 쌓이면 배럭 추가(4~5), 모든 단계 확장(시점·최대 기지 수는 난이도별: 1~2단계 2기지, 3~4단계 3기지, 5단계 4기지)
- 마린/메딕/파이어뱃 생산(근접 적을 본 경우 파이어뱃), 가스 부족 시 마린으로 대체
- 메카닉(2단계 이상): 팩토리·머신샵·아머리, 벌처/시즈 탱크/골리앗. 정찰한 적 조합(근접·중장갑·경보병·공중)에 따라 메카닉 비중과 팩토리 수를 조절(3단계 이상). 탱크는 위협이 사거리에 들어오면 시즈, 3초간 없으면 해제
- 상위 테크(4~5단계): 아머리(골리앗, 차량 업그레이드, 카론 부스터), 스타포트(레이스), 사이언스 퍼실리티(사이언스 베슬), 5단계는 배틀크루저
- 스팀팩, 시즈 모드, U-238, 보병 공격/방어 업그레이드(난이도별), 컴셋 스테이션과 클로킹 유닛 스캔(3단계 이상)
- 자원 예약(건설 이동 중 돈 중복 사용 방지), 우선순위 예산(탐욕/저축)
- 건설 실패 처리: 위치 재선정, 일꾼 사망 시 교체, 건설 중단 시 다른 SCV로 이어짓기, 반복 실패 시 쿨다운
- 정찰: 미확인 시작 위치 순회, 적 본진 관찰, 다치면 귀환, 사망 시 재정찰(4~5)
- 적 정보: 본 것만 기억, 시작 위치 소거법 추론(2단계 이상), 본 적 병력으로 전력 추정(4~5)
- 공격: 병력 수/시간/전력비 조건, 집결 후 출발, 증원은 4기 단위 합류, 재집결, 목표 소멸 시 다음 목표, 진행 없으면 목표 제외
- 전투: 집중 공격 레벨 0~3, 후퇴(국지 전력비/웨이브 잔존율), 기지 방어, 원정 중 방어 복귀
- 공격 실패 시 다음 공격 조건 상향(난이도별 폭)
- 명령 중복 방지와 반복/막힘 감지, 매니저별 예외 격리, 상세 로그와 경기 결과 CSV

## 11. 아직 구현되지 않은 것 / 한계

- Protoss, Zerg 플레이 (Terran만)
- 공중 유닛(레이스·베슬·드랍십), 벙커, 터렛, 드랍, 스파이더 마인, 메카닉 업그레이드, 리프트 오프 등 고급 테란 운영
- 지형 분석(초크/램프): 집결지는 본진~앞마당 사이 지점, 건물 배치는 BWAPI `getBuildLocation` 기본 알고리즘이라 입구/자원줄을 막을 수 있음
- 섬 맵(지상 경로 없음) 공격, 공중으로 뜬 건물 추적
- 개별 유닛 컨트롤(카이팅, 산개), 스펠 대응
- 적 확장/전략 예측, 테크 대응(예: 다크 템플러를 본 뒤에야 터렛을 지음, 미리 대비하지 않음; 스캔은 3단계 이상)
- 다인전(2명 이상 적) 전략 (동작은 하지만 1:1 기준으로 설계)
- **실제 게임 검증, 난이도별 승률 측정, 3단계와 기본 AI의 동등성 검증** — [docs/VALIDATION.md](docs/VALIDATION.md)의 절차로 진행 필요

## 참고
- BWAPI: https://github.com/bwapi/bwapi (README Quick Start, `ExampleAIModule`)
- BWAPI 문서: https://bwapi.github.io/
