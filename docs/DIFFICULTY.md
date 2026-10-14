# 난이도 설계 (1~5단계)

모든 난이도 수치는 `src/core/DifficultyConfig.cpp` 한 곳에만 있다. 게임 로직은 레벨 번호로 분기하지 않고
`DifficultyConfig`의 필드만 읽는다. 게임 시작 시 로그에 `APPLIED difficulty=N ...` 줄로 실제 적용된 값 전체가 기록된다.

난이도는 **전략/판단의 질**로만 조절한다. 무작위로 명령을 망치거나, 일부러 자원을 버리거나, 명령을 지연시키는 방식은 쓰지 않는다.
모든 난이도에서 일꾼 채취, 건설 재시도, 보급 막힘 시 서플라이 디포 강제 건설, 명령 중복 방지, 막힌 목표 포기, 후퇴(전멸 방지)가 동일하게 동작한다.

## 값 요약

| 항목 (필드) | 1 매우 쉬움 | 2 쉬움 | 3 기본 | 4 어려움 | 5 매우 어려움 | 사용 위치 |
|---|---|---|---|---|---|---|
| 빌드 오더 (`buildOrder`) | 배럭 12, 가스 20, 배럭 24, 아카데미 30 | 배럭 11, 가스 14, 배럭 18, 아카데미 22, 배럭 30 | 배럭 11, 가스 12, 배럭 16, 아카데미 19, 배럭 24 | 배럭 11, 가스 12, 배럭 15, 아카데미 17, 배럭 20, 엔베 26, 배럭 30 | 배럭 11, 가스 12, 배럭 15, 아카데미 16, 배럭 19, 엔베 24, 배럭 28 | `ProductionManager::followBuildOrder` |
| 패치당 일꾼 (`workersPerMineralPatch`) / 최대 | 1.5 / 18 | 1.75 / 22 | 2.0 / 26 | 2.2 / 34 | 2.5 / 50 | `ProductionManager::trainWorkers` |
| 최대 배럭 / 자원 쌓이면 추가 배럭 | 2 / - | 3 / - | 3 / - | 6 / 400 | 8 / 350 | `ProductionManager::macroBuildings` |
| 앞마당 확장 (`expand`) | - | - | - | - | 일꾼 20 | `ProductionManager::macroBuildings` |
| 연구 (`research`) | - | - | 스팀팩 | 스팀팩, U-238, 공1 | + 방1 | `ProductionManager::doResearch` |
| 보급 예측 (`supplyLookaheadFrames`) / 고정 여유 | 고정 2 | 고정 4 | 700f | 850f | 950f | `SupplyManager`, `Planning::needSupplyDepot` |
| 동시 디포 / 막힌 뒤 여유 증가 | 1 / 0 | 1 / 0 | 1 / +1 | 2 / +2 | 3 / +2 | `SupplyManager::update` |
| 우선순위 저축 (`strictPriority`) | X (탐욕) | X | O | O | O | `Planning::Budget` |
| 기지 공격받으면 병력 우선 | X | X | O | O | O | `ProductionManager::update` |
| 정찰 시작 (인구 / 시간) | - / 5:00 | 15 / 4:00 | 12 / 3:00 | 10 / 2:00 | 9 / 2:00 | `ScoutManager::shouldStart` |
| 소거법으로 적 본진 추론 | X | O | O | O | O | `InformationManager::enemyMain` |
| 적 본진 관찰 시간 | 0 | 0 | 5초 | 15초 | 25초 | `ScoutManager` (Observing) |
| 정찰 일꾼 사망 시 재정찰 | X | X | X | O | O | `ScoutManager::update` |
| 정보 활용 (`enemyInfoUsage`) | 0 위치만 | 1 +조합 | 1 +조합 | 2 +전력 추정 | 2 +전력 추정 | `StrategyManager::firebatShare`, `CombatManager::gather` |
| 메딕 비율 / 근접 상대 시 파이어뱃 | 0.15 / 0 | 0.2 / 0.1 | 0.25 / 0.25 | 0.25 / 0.35 | 0.3 / 0.4 | `Planning::chooseInfantry` |
| 공격 개시 병력 수 / 최소 시각 | 20 / 9:00 | 16 / 8:00 | 12 / 6:00 | 12 / 5:00 | 10 / 4:30 | `Planning::shouldAttack` |
| 공격 시 필요 전력비 (`attackStrengthRatio`) | - | - | - | 1.3 | 1.2 | `Planning::shouldAttack` |
| 집결 반경 (타일) | 10 | 9 | 8 | 7 | 6 | `CombatManager::attack` (재집결) |
| 원정 중 기지 방어 복귀 | X | X | O | O | O | `CombatManager::update` |
| 집중 공격 (`focusFireLevel`) | 0 어택땅 | 1 유닛 우선 | 2 +사거리/약한 적 | 3 +위협도(DPS/HP) | 3 | `Planning::chooseTarget` |
| 후퇴: 국지 전력비 / 웨이브 잔존율 | - / 25% | 2.5 / 30% | 1.8 / 35% | 1.4 / 40% | 1.25 / 40% | `Planning::shouldRetreat` |
| 스팀팩 사용 / 컴셋 스캔 | X / X | X / X | O / O | O / O | O / O | `CombatManager::fight`, `useScans` |
| 공격 실패 시 병력 수 증가 / 전력비 증가 | 0 / 0 | +2 / 0 | +2 / 0 | +4 / +0.15 | +4 / +0.2 | `StrategyManager::onWaveEnd` |

(`f` = 프레임. Fastest 속도에서 약 24프레임 = 게임 1초.)

## 단계별 의도

- **1 매우 쉬움**: 늦은 빌드와 정찰, 큰 병력이 모일 때까지 공격 안 함, 어택땅만 사용, 전력 비교 후퇴 없음(단, 웨이브가 25% 이하로 줄면 후퇴해 전멸은 피함). 정찰 결과는 공격 위치에만 쓰고 조합에는 반영하지 않음.
- **2 쉬움**: 정상적인 운영, 근접 유닛이 보이면 파이어뱃을 약간 섞음, 전투에서는 "반격 가능한 유닛 먼저" 정도의 단순한 우선순위.
- **3 기본**: 기본 탑재 AI와 비슷한 수준을 **목표**로 한 표준 바이오닉 운영. 보급 예측, 우선순위 저축, 공격받으면 병력 우선 생산, 사거리 안 약한 적 집중 공격, 스팀팩. *기본 AI와의 동등성은 검증되지 않았다* (docs/VALIDATION.md 참고).
- **4 어려움**: 생산 시설 추가로 가동률 증가, 업그레이드, 정찰로 본 적 병력의 전력 추정을 공격 시점에 반영, 위협도 기반 집중 공격, 공격 실패 시 요구 병력·전력비 상향.
- **5 매우 어려움**: 앞마당 확장과 일꾼 재분배, 더 빠른 정찰과 오래 관찰, 방어 업그레이드까지, 낮은 후퇴 임계값과 높은 적응 폭.

## 설정값만 바꿔서 난이도를 맞추지 말 것

docs/VALIDATION.md의 반복 대전에서 단계 간 차이가 나오지 않으면 숫자를 억지로 키우기보다 해당 판단 로직
(예: `chooseTarget`, `shouldRetreat`, 빌드 오더)을 개선한다.
