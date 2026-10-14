# 검증 기록과 절차

## 1. 지금까지 실제로 확인된 것

| 항목 | 방법 | 결과 |
|---|---|---|
| BWAPI 버전 | 공식 저장소 `bwapi/bwapi` 태그 `v4.4.0`(2019-03-07, 최신 릴리스)의 `starcraftver.h` | `BWAPI_VER "4.4.0"`, `STARCRAFT_VER "1.16.1"` |
| 지원 게임 버전 | 공식 README Quick Start | StarCraft: Brood War **1.16.1** 필요. Remastered 지원 문구 없음 |
| BWAPILIB 소스 빌드 | Linux g++ 13.3 / CMake 3.28 | 빌드 성공 (`svnrev.h`는 CMake가 생성) |
| 봇 전체 소스 컴파일·링크 | Linux g++ (`-Wall -Wextra`) | 경고/오류 없음 |
| 단위 테스트 (`core_tests`) | 난이도 프리셋, INI, 설정 검증, 명령 중복 방지, 보급/예산/공격/후퇴/타겟 규칙 | 통과 |
| BWAPI 데이터 가정 (`bwapi_tests`) | 실제 BWAPILIB 유닛/무기/테크 테이블과 비교 | 통과 (테스트로 오류 2건 발견 후 수정: 질럿/파이어뱃 다단히트는 `maxGroundHits()`, 인터셉터 쿨다운 1프레임 문제) |
| 모듈 로드 (`module_load_test`) | 빌드된 모듈을 `dlopen`/`LoadLibrary`로 열고 `gameInit`/`newAIModule` 호출 | Linux 통과. Windows 결과는 CI 로그 참고 |
| 설정 도구 | 공식 `bwapi.ini` 사본에 `--apply-bwapi-ini` 실행 | 지정한 키 4~6개만 변경, 백업 파일 생성 확인 |
| Windows 32비트 MSVC 빌드 | GitHub Actions `windows-latest`, `-A Win32`, Release | `.github/workflows/build.yml` — 결과는 Actions 탭에서 확인 |

## 2. 검증되지 않은 것 (실제 게임 필요)

이 저장소를 만든 환경은 Linux 클라우드 컨테이너였고 StarCraft가 없었다. 따라서 아래는 **전부 미검증**이다.

- 실제 StarCraft 1.16.1 + BWAPI 4.4.0에서 DLL 로드, `onStart`/`onFrame` 동작
- 일꾼 생산·채취, 건물 배치(특히 `getBuildLocation` 결과가 입구/자원줄을 막는지), 건설 재시도
- 마린 생산, 집결, 공격, 후퇴, 막힌 경로 처리
- 컴셋 스캔 (BWAPI가 클로킹 유닛을 `isDetected()==false`로 노출하는지 확인 필요)
- 난이도별 실력 차이, 3단계와 기본 탑재 AI의 동등성

## 3. 1차 실전 테스트 절차 (한 맵, 한 전략)

처음에는 조건을 하나로 고정한다.

- 맵: 2인용 맵 하나 (예: `maps/(2)Boxer.scm` 또는 가지고 있는 2인 맵)
- 봇: Terran, 난이도 3
- 상대: 기본 탑재 컴퓨터 AI, Protoss
- `bwapi.ini`: `auto_menu = SINGLE_PLAYER`, `enemy_count = 1`, `game_type = MELEE`
- `BeginnerBot.ini`: `local_speed = 0` (최고 속도), `level = info`

로그(`bwapi-data/write/BeginnerBot/BeginnerBot_*.log`)에서 확인할 줄:

| 확인 항목 | 로그에서 찾을 문자열 |
|---|---|
| 난이도 적용 | `APPLIED difficulty=3 (Normal / 기본)` |
| 일꾼 생산/채취 | `Snapshot` 줄의 `workers=` 증가, `gathered=` 증가, `min=` 대부분 |
| 가스 채취 | `Snapshot`의 `workers=… (min=… gas=3 …)`, `gathered=광물/가스` 의 가스 값 증가 |
| 보급 관리 | `[Build] task #… queued: Terran_Supply_Depot`, `supply blocked` 횟수 |
| 병력 생산 | `first combat unit started`, `Snapshot`의 `army=` |
| 공격 | `attack wave 1: … -> x,y [enemy main …]` |
| 후퇴/적응 | `retreating:`, `wave ended (…) -> FAILED`, `adapting after failed wave` |
| 막힘/반복 | `no progress towards`, `received the same … 8 times in a row` |
| 예외 | `ERROR` 줄, 마지막 `metrics:` 의 `exceptions=` |

## 4. 난이도 비교 (반복 대전)

각 경기 종료 시 `bwapi-data/write/BeginnerBot/results.csv`에 한 줄이 추가된다.
요약: `python tools/summarize_results.py <results.csv 경로>`

권장 매트릭스 (각 칸 최소 10경기):

| 난이도 | (2)맵 A vs Protoss | (2)맵 A vs Zerg | (2)맵 A vs Terran | (4)맵 B vs Protoss |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |

판단 기준:
- 1~2단계: 승률이 낮더라도 `max_workers`, `gathered_minerals`, `army_trained`가 정상이고 `exceptions=0`, `unit_stuck_events`가 거의 0이어야 함 (쉬운 것이지 고장난 것이 아님).
- 3단계: 기본 AI 상대 승률과 평균 경기 시간을 기록. 승률이 극단적(예: 90%↑ 또는 10%↓)이면 3단계 전략을 조정.
- 4~5단계: 같은 조건에서 3단계보다 승률·`gathered_minerals`·`idle_production_frames`(낮을수록 좋음)가 나아야 함.
- 차이가 없으면 수치만 키우지 말고 판단 로직을 개선 (docs/DIFFICULTY.md 끝 참고).

## 5. 결과 기록 양식

| 날짜 | 맵 | 상대 | 난이도 | 경기 수 | 승률 | 평균 시간 | 비고 |
|---|---|---|---|---|---|---|---|
| 2026-10-14 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 1 | 0% (패) | 33:23 | 첫 실전. 로드·채취·건설·생산·정찰·스캔·공격·후퇴 로그 확인. 문제: 같은 명령 반복 359회(집결지 혼잡), 재집결이 끝나지 않아 목표를 "도달 불가"로 오판(웨이브 8회 중 3회는 손실·처치 0, 1회는 처치 0으로 종료), 17:30 본진 광물 고갈 후 확장 없음(이후 수입 0), 가스 4694 미사용 → 수정 후 재검증 필요 |
| 2026-10-14 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 2 (무효) | - | 1:04, 0:21 | 교전 없이 종료(게임을 나간 것으로 추정). results.csv에는 loss로 남아 있으므로 집계에서 제외할 것 |
| 2026-10-15 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 1 | 100% (승) | 39:35 | 1차 수정 적용. 확장 3회(11:31, 20:12, 30:24), 가스 조절, 일꾼 26, 광물 32618 확인. 문제: 귀환 중 유닛에 같은 명령 재전송(경고 654회), 재집결 시 선두가 되돌아가 웨이브 3~6(123~151기)이 교전 없이 종료, 같은 디포 자리 반복 시도로 158/158 보급 막힘 4분 → 2차 수정 + 시즈 탱크 추가, 재검증 필요 |
| 2026-10-15 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 1 (+ 0:56 무효 1) | 100% (승) | 23:21 | 2차 수정 + 탱크(버전 문자열은 아직 0.1.0). 반복 경고 654→17, 시즈 모드 연구·탱크 12대·확장 2회 확인. 문제: 재집결 과다, 선두가 목표에 있는데 "도달 불가"(front 0 tiles), 명령 실패 388회(로그 레벨 문제로 원인 미확인) → 0.2.0에서 재집결/시즈 수정 + 상황별 메카닉, 재검증 필요 |
| 2026-10-15 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 1 | 100% (승) | 15:39 | 0.2.0. 웨이브당 재집결 0~3회, 명령 실패 388→2, 반복 경고 1, 메카닉 적응(근접 9기 → 비중 25→50%)·벌처/탱크 생산 확인. 시즈 횟수는 로그 없음(미확인). 문제: 메카닉 비중 25↔50% 6회 흔들림, 짓는 중인 사령부 보급을 미리 계산해 82/82 막힘, 유휴 생산 지표가 저축 중인 배럭도 집계 → 0.2.1에서 수정, 재검증 필요 |
| 2026-10-15 | (2)Boxer.scm | Protoss (기본 AI) | 3 | 1 | 100% (승) | 30:03 | 0.2.1 (시작 위치 반대편). 문제: 첫 웨이브(15기, 6:00)가 9분간 끝나지 않고 증원 4기씩 계속 합류 → 95기 손실/39기 처치(질럿에게 소규모로 잘림), 증원이 잔존율 계산을 채워 후퇴하지 않음. 웨이브 2에서 재집결 14회·시즈 87회. 엔지니어링 베이 없음 → 0.3.0에서 증원 합류 규칙·잔존율 기준 수정, 3단계에 엔베+공1 추가, 재검증 필요 |
