// JaeTankBotConfig: pick the AI difficulty (1-5), race and map before a game.
//
// Writes <StarCraft>/bwapi-data/AI/JaeTankBot.ini (the bot's own settings) and,
// only when asked (--apply-bwapi-ini or answering "y"), updates bwapi.ini's [ai]
// and [auto_menu] keys after making a timestamped backup.
//
//   JaeTankBotConfig                                  interactive, current folder
//   JaeTankBotConfig --starcraft "C:\StarCraft"       interactive, writes into StarCraft
//   JaeTankBotConfig --starcraft DIR --difficulty 4 --map "maps/(2)Lost Temple.scm"
//                     --enemy-race Protoss --apply-bwapi-ini --yes
//   JaeTankBotConfig --list                           show the difficulty levels
//   JaeTankBotConfig --starcraft DIR --vs-human --launch
//                     a human plays the bot on this PC: the bot's StarCraft hosts a
//                     "Local PC" game, the human's StarCraft joins it (two instances
//                     through Chaoslauncher - MultiInstance)
//
// Built a second time as JaeTankBotPlay.exe (JTB_PLAY): --vs-human --launch by
// default, the StarCraft folder found from where the exe is, questions asked.

#include "DifficultyConfig.h"
#include "IniFile.h"
#include "Logger.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;
using namespace bb;

namespace
{
  struct Options
  {
    std::string starcraftDir;
    std::string output;
    std::optional<int> difficulty;
    std::optional<std::string> race;
    std::optional<std::string> map;
    std::optional<std::string> enemyRace;
    bool applyBwapiIni = false;
    bool yes = false;
    bool list = false;
    bool help = false;
    bool vsHuman = false;                // the second StarCraft instance is a human player
    std::optional<std::string> humanRace;
    bool launch = false;                 // start Chaoslauncher - MultiInstance afterwards
    std::string chaoslauncher;
  };

  const std::vector<std::string> kEnemyRaces = { "Protoss", "Terran", "Zerg", "Random" };

  void printUsage()
  {
    std::cout <<
      "JaeTankBotConfig - JaeTankBot settings\n"
      "  --starcraft DIR       StarCraft 1.16.1 folder (contains bwapi-data)\n"
      "  --output FILE         where to write JaeTankBot.ini (default: DIR/bwapi-data/AI/JaeTankBot.ini)\n"
      "  --difficulty 1-5      AI difficulty (default 3)\n"
      "  --race Terran         bot race (only Terran is implemented)\n"
      "  --map PATH            map for bwapi.ini auto_menu, relative to DIR (e.g. maps/(2)Boxer.scm)\n"
      "  --enemy-race RACE     computer opponent race: Protoss | Terran | Zerg | Random\n"
      "  --apply-bwapi-ini     also update bwapi.ini (a backup is created first)\n"
      "  --yes                 do not ask questions\n"
      "  --list                print the difficulty levels and exit\n"
      "  --vs-human            a human plays the bot on this PC (two StarCraft instances, Local PC game)\n"
      "  --human-race RACE     the human's race: Terran | Protoss | Zerg | Random (default Terran)\n"
      "  --launch              start Chaoslauncher - MultiInstance afterwards\n"
      "  --chaoslauncher EXE   path of \"Chaoslauncher - MultiInstance.exe\" (default %USERPROFILE%\\BWAPI\\Chaoslauncher)\n";
  }

  bool parseArgs(int argc, char** argv, Options& o)
  {
    for (int i = 1; i < argc; ++i)
    {
      const std::string a = argv[i];
      auto next = [&](std::string& out) {
        if (i + 1 >= argc)
        {
          std::cerr << "missing value for " << a << "\n";
          return false;
        }
        out = argv[++i];
        return true;
      };
      std::string v;
      if (a == "--starcraft") { if (!next(o.starcraftDir)) return false; }
      else if (a == "--output") { if (!next(o.output)) return false; }
      else if (a == "--difficulty")
      {
        if (!next(v)) return false;
        try { o.difficulty = std::stoi(v); } catch (...) { std::cerr << "invalid difficulty: " << v << "\n"; return false; }
      }
      else if (a == "--race") { if (!next(v)) return false; o.race = v; }
      else if (a == "--map") { if (!next(v)) return false; o.map = v; }
      else if (a == "--enemy-race") { if (!next(v)) return false; o.enemyRace = v; }
      else if (a == "--apply-bwapi-ini") o.applyBwapiIni = true;
      else if (a == "--yes" || a == "-y") o.yes = true;
      else if (a == "--list") o.list = true;
      else if (a == "--vs-human") o.vsHuman = true;
      else if (a == "--human-race") { if (!next(v)) return false; o.humanRace = v; }
      else if (a == "--launch") o.launch = true;
      else if (a == "--chaoslauncher") { if (!next(o.chaoslauncher)) return false; }
      else if (a == "--help" || a == "-h") o.help = true;
      else
      {
        std::cerr << "unknown option: " << a << "\n";
        return false;
      }
    }
    return true;
  }

  void printDifficulties()
  {
    for (int l = kMinDifficulty; l <= kMaxDifficulty; ++l)
    {
      const DifficultyConfig c = makeDifficultyConfig(l);
      std::cout << "  " << l << ") " << c.koreanName << " (" << c.name << ")" << (l == kDefaultDifficulty ? "  [기본값]" : "")
                << "\n     " << c.description << "\n";
    }
  }

  std::string ask(const std::string& question, const std::string& def)
  {
    std::cout << question << (def.empty() ? "" : " [" + def + "]") << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line))
      return def;
    line = trim(line);
    return line.empty() ? def : line;
  }

  bool validRace(const std::string& r) { return toLower(r) == "terran"; }

  // Folder of this exe (JaeTankBotPlay.exe sits in <StarCraft>/bwapi-data/AI).
  fs::path exeDir()
  {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0)
      return fs::path(buf).parent_path();
#endif
    return fs::current_path();
  }

  // The StarCraft folder: given, or the one this exe was installed into, or the
  // current folder.
  std::string findStarCraft()
  {
    for (const fs::path& p : { exeDir().parent_path().parent_path(), fs::current_path() })
      if (fs::is_regular_file(p / "bwapi-data" / "BWAPI.dll"))
        return p.string();
    return {};
  }

  fs::path defaultChaoslauncher()
  {
    const char* home = std::getenv("USERPROFILE");
    return home ? fs::path(home) / "BWAPI" / "Chaoslauncher" / "Chaoslauncher - MultiInstance.exe" : fs::path();
  }

  bool launchProgram(const fs::path& exe)
  {
#ifdef _WIN32
    // ShellExecute shows the administrator prompt Chaoslauncher asks for.
    const auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", exe.wstring().c_str(), nullptr, exe.parent_path().wstring().c_str(), SW_SHOWNORMAL));
    return r > 32;
#else
    (void)exe;
    return false;
#endif
  }

  void waitForEnter(bool interactive)
  {
    if (!interactive)
      return;
    std::cout << "\nEnter 를 누르면 창이 닫힙니다." << std::flush;
    std::string line;
    std::getline(std::cin, line);
  }

  std::optional<std::string> normalizeEnemyRace(const std::string& r)
  {
    for (const auto& e : kEnemyRaces)
      if (toLower(e) == toLower(r))
        return e;
    return std::nullopt;
  }
}

namespace
{
  int playVsHuman(const Options& o, const fs::path& scDir, const fs::path& botIniPath, const fs::path& bwapiIniPath, IniFile& botIni,
                  bool hadBotIni, IniFile& bwapiIni, bool haveBwapiIni, int difficulty, const std::string& race, std::string map)
  {
    const bool interactive = !o.yes;
    if (!haveBwapiIni)
    {
      std::cerr << "error: --vs-human needs --starcraft DIR with an existing bwapi-data/bwapi.ini\n";
      waitForEnter(interactive);
      return 1;
    }
    std::string humanRace = o.humanRace.value_or("Terran");
    if (!o.humanRace && interactive)
      humanRace = ask("사람(플레이어) 종족 (Terran/Protoss/Zerg/Random)", humanRace);
    const auto human = normalizeEnemyRace(humanRace);
    if (!human)
    {
      std::cerr << "error: the human's race must be Terran, Protoss, Zerg or Random\n";
      waitForEnter(interactive);
      return 1;
    }
    if (!o.map && interactive)
      map = ask("맵 경로 (StarCraft 폴더 기준, 예: maps/(2)Boxer.scm)", map.empty() ? "maps/(2)Boxer.scm" : map);
    if (!map.empty() && map.find_first_of("*?") == std::string::npos && !fs::exists(scDir / map))
      std::cout << "warning: map file '" << (scDir / map).string() << "' does not exist\n";

    // The bot's settings: normal game speed (a human plays), no orders from the human
    // in the bot's window.
    const DifficultyConfig chosen = makeDifficultyConfig(difficulty);
    if (!hadBotIni)
      botIni.parse("; JaeTankBot settings - written by JaeTankBotConfig\n");
    botIni.set("bot", "difficulty", std::to_string(difficulty));
    botIni.set("bot", "race", race);
    botIni.set("game", "local_speed", "-1");
    botIni.set("game", "enable_user_input", "off");
    if (!botIni.save(botIniPath.string()))
    {
      std::cerr << "error: could not write " << botIniPath.string() << "\n";
      waitForEnter(interactive);
      return 1;
    }

    std::error_code ec;
    const fs::path backup = bwapiIniPath.string() + ".jaetankbot-" + timestampForFileName() + ".bak";
    fs::copy_file(bwapiIniPath, backup, fs::copy_options::none, ec);
    if (ec)
    {
      std::cerr << "error: could not back up bwapi.ini (" << ec.message() << "); bwapi.ini was not changed\n";
      waitForEnter(interactive);
      return 1;
    }
    // First StarCraft instance: the bot hosts. Second: no AI module ("HUMAN" is not a
    // DLL, so BWAPI loads nothing and the human plays), joins the first game.
    bwapiIni.set("ai", "ai", "bwapi-data/AI/JaeTankBot.dll, HUMAN");
    bwapiIni.set("auto_menu", "auto_menu", "LAN");
    bwapiIni.set("auto_menu", "lan_mode", "Local PC");
    bwapiIni.set("auto_menu", "game", "JOIN_FIRST");
    bwapiIni.set("auto_menu", "race", race + ", " + *human);
    bwapiIni.set("auto_menu", "game_type", "MELEE");
    bwapiIni.set("auto_menu", "wait_for_min_players", "2");
    bwapiIni.set("auto_menu", "wait_for_max_players", "2");
    bwapiIni.set("auto_menu", "auto_restart", "OFF");
    if (!map.empty())
      bwapiIni.set("auto_menu", "map", map);
    // Two StarCraft windows on one screen.
    bwapiIni.set("window", "windowed", "ON");
    if (!bwapiIni.save(bwapiIniPath.string()))
    {
      std::cerr << "error: could not write " << bwapiIniPath.string() << " (backup: " << backup.string() << ")\n";
      waitForEnter(interactive);
      return 1;
    }
    std::cout << "\n사람 대 JaeTankBot 준비 완료\n"
              << "  봇: 난이도 " << difficulty << " (" << chosen.koreanName << "), " << race << "\n"
              << "  사람: " << *human << ", 맵: " << (map.empty() ? "(bwapi.ini 그대로)" : map) << "\n"
              << "  bwapi.ini 백업: " << backup.string() << "\n\n"
              << "1. Chaoslauncher - MultiInstance 에서 \"BWAPI Injector [RELEASE]\" 를 체크하고 Start → 봇 창 (게임을 만들고 기다림)\n"
              << "2. Start 를 한 번 더 → 사람 창 (자동으로 그 게임에 들어감, AI 모듈 오류 문구는 정상)\n"
              << "3. 두 번째 창에서 직접 플레이. 봇 대 봇으로 돌아가려면 위 백업을 bwapi.ini 로 되돌린다.\n";
    if (o.launch)
    {
      const fs::path launcher = o.chaoslauncher.empty() ? defaultChaoslauncher() : fs::path(o.chaoslauncher);
      if (!launcher.empty() && fs::exists(launcher) && launchProgram(launcher))
        std::cout << "\nChaoslauncher 실행: " << launcher.string() << "\n";
      else
        std::cout << "\nChaoslauncher 를 찾지 못했습니다 (" << launcher.string()
                  << "). 직접 실행하거나 --chaoslauncher 로 경로를 주세요.\n";
    }
    waitForEnter(interactive);
    return 0;
  }
}

int main(int argc, char** argv)
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  Options o;
#ifdef JTB_PLAY
  o.vsHuman = true;
  o.launch = true;
#endif
  if (!parseArgs(argc, argv, o))
  {
    printUsage();
    return 2;
  }
  if (o.help)
  {
    printUsage();
    return 0;
  }
  if (o.list)
  {
    printDifficulties();
    return 0;
  }
  if (o.vsHuman && o.starcraftDir.empty())
    o.starcraftDir = findStarCraft();
  if (o.vsHuman && o.starcraftDir.empty() && !o.yes)
    o.starcraftDir = ask("StarCraft 폴더 (bwapi-data 가 있는 곳)", "");

  const fs::path scDir = o.starcraftDir.empty() ? fs::path() : fs::path(o.starcraftDir);
  if (!o.starcraftDir.empty() && !fs::is_directory(scDir / "bwapi-data"))
  {
    std::cerr << "error: '" << o.starcraftDir << "' has no bwapi-data folder. Install BWAPI 4.4.0 into StarCraft first.\n";
    return 1;
  }
  const fs::path botIniPath = !o.output.empty() ? fs::path(o.output)
                            : scDir.empty() ? fs::path("JaeTankBot.ini")
                                            : scDir / "bwapi-data" / "AI" / "JaeTankBot.ini";
  const fs::path bwapiIniPath = scDir / "bwapi-data" / "bwapi.ini";

  IniFile botIni;
  const bool hadBotIni = botIni.load(botIniPath.string());
  IniFile bwapiIni;
  const bool haveBwapiIni = !scDir.empty() && bwapiIni.load(bwapiIniPath.string());

  // ---- difficulty
  int difficulty = o.difficulty.value_or(botIni.getInt("bot", "difficulty", kDefaultDifficulty));
  if (!o.difficulty && !o.yes)
  {
    std::cout << "AI 난이도를 선택하세요:\n";
    printDifficulties();
    const std::string answer = ask("난이도 (1-5)", std::to_string(difficulty));
    try { difficulty = std::stoi(answer); } catch (...) { difficulty = -1; }
  }
  if (difficulty < kMinDifficulty || difficulty > kMaxDifficulty)
  {
    std::cerr << "error: difficulty must be 1..5\n";
    return 1;
  }

  // ---- race
  std::string race = o.race.value_or("Terran");
  if (!o.race && !o.yes)
  {
    std::cout << "\n종족: Terran (구현됨) / Protoss, Zerg (미구현 - 선택 불가)\n";
    race = ask("종족", "Terran");
  }
  if (!validRace(race))
  {
    std::cerr << "error: race '" << race << "' is not implemented. Only Terran is supported.\n";
    return 1;
  }
  race = "Terran";

  // ---- map / opponent (only meaningful with bwapi.ini auto_menu)
  std::string map = o.map.value_or(haveBwapiIni ? bwapiIni.getString("auto_menu", "map", "") : "");
  if (o.vsHuman)
    return playVsHuman(o, scDir, botIniPath, bwapiIniPath, botIni, hadBotIni, bwapiIni, haveBwapiIni, difficulty, race, map);
  std::string enemyRace = o.enemyRace.value_or(haveBwapiIni ? bwapiIni.getString("auto_menu", "enemy_race", "Protoss") : "Protoss");
  bool applyBwapi = o.applyBwapiIni;
  if (haveBwapiIni && !o.yes && !o.applyBwapiIni)
  {
    std::cout << "\nbwapi.ini 의 자동 시작(auto_menu) 설정도 바꿀까요? (봇 DLL 경로, 종족, 맵, 상대 종족)\n";
    applyBwapi = toLower(ask("bwapi.ini 수정 (y/N)", "N")) == "y";
    if (applyBwapi)
    {
      map = ask("맵 경로 (StarCraft 폴더 기준, 예: maps/(2)Boxer.scm)", map);
      enemyRace = ask("컴퓨터 상대 종족 (Protoss/Terran/Zerg/Random)", enemyRace);
    }
  }
  if (applyBwapi)
  {
    if (!haveBwapiIni)
    {
      std::cerr << "error: --apply-bwapi-ini needs --starcraft DIR with an existing bwapi-data/bwapi.ini\n";
      return 1;
    }
    auto normalized = normalizeEnemyRace(enemyRace);
    if (!normalized)
    {
      std::cerr << "error: enemy race must be Protoss, Terran, Zerg or Random\n";
      return 1;
    }
    enemyRace = *normalized;
    if (!map.empty() && map.find_first_of("*?") == std::string::npos && !fs::exists(scDir / map))
      std::cout << "warning: map file '" << (scDir / map).string() << "' does not exist\n";
  }

  // ---- write JaeTankBot.ini (keeps any other keys and comments)
  const DifficultyConfig chosen = makeDifficultyConfig(difficulty);
  if (!hadBotIni)
    botIni.parse("; JaeTankBot settings - written by JaeTankBotConfig\n"
                 "; difficulty: 1 VeryEasy, 2 Easy, 3 Normal (default), 4 Hard, 5 VeryHard\n");
  botIni.set("bot", "difficulty", std::to_string(difficulty));
  botIni.set("bot", "race", race);
  std::error_code ec;
  if (botIniPath.has_parent_path())
    fs::create_directories(botIniPath.parent_path(), ec);
  if (!botIni.save(botIniPath.string()))
  {
    std::cerr << "error: could not write " << botIniPath.string() << "\n";
    return 1;
  }
  std::cout << "\n저장됨: " << botIniPath.string() << "\n  difficulty = " << difficulty << " (" << chosen.koreanName << " / "
            << chosen.name << ")\n  race = " << race << "\n";

  // ---- optionally update bwapi.ini
  if (applyBwapi)
  {
    const fs::path backup = bwapiIniPath.string() + ".jaetankbot-" + timestampForFileName() + ".bak";
    fs::copy_file(bwapiIniPath, backup, fs::copy_options::none, ec);
    if (ec)
    {
      std::cerr << "error: could not back up bwapi.ini (" << ec.message() << "); bwapi.ini was not changed\n";
      return 1;
    }
    bwapiIni.set("ai", "ai", "bwapi-data/AI/JaeTankBot.dll");
    bwapiIni.set("auto_menu", "auto_menu", "SINGLE_PLAYER");
    bwapiIni.set("auto_menu", "race", race);
    bwapiIni.set("auto_menu", "enemy_count", "1");
    bwapiIni.set("auto_menu", "enemy_race", enemyRace);
    bwapiIni.set("auto_menu", "game_type", "MELEE");
    if (!map.empty())
      bwapiIni.set("auto_menu", "map", map);
    if (!bwapiIni.save(bwapiIniPath.string()))
    {
      std::cerr << "error: could not write " << bwapiIniPath.string() << " (backup: " << backup.string() << ")\n";
      return 1;
    }
    std::cout << "bwapi.ini 수정됨 (백업: " << backup.string() << ")\n  ai = bwapi-data/AI/JaeTankBot.dll\n"
              << "  auto_menu = SINGLE_PLAYER, race = " << race << ", enemy_race = " << enemyRace
              << ", map = " << (map.empty() ? "(unchanged)" : map) << "\n";
  }
  else
  {
    std::cout << "bwapi.ini 는 변경하지 않았습니다.\n";
  }
  return 0;
}
