# CLAUDE.md — JaeTankBot

StarCraft: Brood War AI bot (Terran, difficulty 1-5) built as a BWAPI 4.4.0 AI module DLL.
Read README.md first, then docs/VALIDATION.md (what is verified / how to test) and docs/DIFFICULTY.md.

## Target platform (do not change)
- StarCraft: Brood War **1.16.1** + **BWAPI 4.4.0**. Remastered is not supported by BWAPI.
- The bot DLL must be **32-bit (Win32), Release, /MD**. BWAPI.dll is a 32-bit Release build.
- Only BWAPI 4.4.0 is a dependency. Do not add other libraries (no BWEM/BWTA, no test frameworks).

## Commands (Windows, from the repository root)
```bat
scripts\build_windows.bat [BWAPI folder]          :: configure (-A Win32) + build Release + ctest
cmake --build build --config Release                :: rebuild after edits
ctest --test-dir build -C Release --output-on-failure
scripts\install_to_starcraft.bat "C:\path\to\StarCraft"
build\Release\JaeTankBotConfig.exe --list
build\Release\JaeTankBotPlay.exe                   :: GUI launcher (no Chaoslauncher; per-instance settings via environment)
python tools\make_package.py                       :: dist\JaeTankBot-<ver>-win32.zip (never includes StarCraft files)
python tools\make_icon.py                          :: redraws resources\jaetankbot.ico
build\Release\JaeTankBotConfig.exe --starcraft "C:\path\to\StarCraft" --difficulty 3 --map "maps/(2)XXX.scm" --enemy-race Protoss --apply-bwapi-ini --yes
python tools\summarize_results.py "C:\path\to\StarCraft\bwapi-data\write\JaeTankBot\results.csv"
```
BWAPI folder default: `%USERPROFILE%\BWAPI` (must contain `include\BWAPI.h` and `BWAPILIB\`).
CI (.github/workflows/build.yml) builds the same on windows-latest (Win32) and ubuntu (compile check); keep both green.

## Code layout
- `src/core/` — no BWAPI includes, unit tested in `tests/core_tests.cpp`.
  - `DifficultyConfig.cpp` is the **only** place for per-difficulty numbers.
  - `Planning.cpp` holds the decision rules (supply, budget, composition, attack/retreat, focus fire).
- `src/bot/` — BWAPI layer. Every unit command goes through `Commander` (dedup + failure logging).
- `tests/bwapi_tests.cpp` checks assumptions against real BWAPI type data; add a check there when you rely on a new unit/weapon property.

## Rules
- Never modify StarCraft's own files. Only add files under `bwapi-data\AI\` and `bwapi-data\write\`.
  `bwapi.ini` may only be changed through JaeTankBotConfig (it makes a timestamped backup) or after asking the user.
- No cheat flags (`CompleteMapInformation`), no reading information about units that are not visible.
- Difficulty must come from strategy/decision quality. No random sabotage, no intentional resource waste,
  no invalid/endless repeated commands, no suicide attacks at any level.
- Do not add a DifficultyConfig field that the code does not use.
- Every manager update runs inside `guarded()`; keep the bot running after errors.
- Do not claim something works in game unless a game log shows it. Write "not verified" otherwise,
  and record real results in docs/VALIDATION.md section 5.
- Work on a branch; do not push to the default branch or open PRs without asking.

## In-game test loop
1. Build + ctest must pass. Install with `install_to_starcraft.bat`.
2. Configure `bwapi.ini` via JaeTankBotConfig: `auto_menu = SINGLE_PLAYER`, one 2-player map, enemy Protoss.
   For repeated games also set `auto_restart = ON` in `[auto_menu]` (ask the user first; JaeTankBotConfig does not set it).
   For fast games set `local_speed = 0` in `bwapi-data\AI\JaeTankBot.ini`.
3. **The user must start the game**: run Chaoslauncher as administrator, tick "BWAPI Injector [RELEASE]", press Start.
   Claude cannot click GUI buttons; ask the user and wait.
4. After the game(s), read the newest `bwapi-data\write\JaeTankBot\JaeTankBot_*.log` and `results.csv`.
   Checklist of log lines: docs/VALIDATION.md section 3.
5. Fix the cause in code (not by inflating numbers), rebuild, ctest, reinstall, ask the user for the next run.
