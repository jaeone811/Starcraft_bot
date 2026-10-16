// JaeTankBotPlay: a window to start games with JaeTankBot, without Chaoslauncher.
//
// - Human vs JaeTankBot: the human's StarCraft starts as the plain game (full
//   screen, its own sound, the usual menus). The bot's StarCraft starts next with
//   BWAPI loaded into it, hosts a "Local PC" game (SNP_DirectIP.snp in the StarCraft
//   folder provides it) and waits for the human to join.
// - JaeTankBot vs another bot: two StarCraft instances with BWAPI, the first hosts.
// - JaeTankBot vs the computer: one StarCraft instance, single player.
//
// BWAPI is loaded the way Chaoslauncher does it: StarCraft is started suspended,
// LoadLibrary("bwapi-data\BWAPI.dll") runs in it, then it is resumed. BWAPI lets a
// second instance start. Each instance gets its settings from the environment
// (BWAPI_CONFIG_<SECTION>__<KEY> for BWAPI, JAETANKBOT_<SECTION>__<KEY> for the
// bot), so bwapi.ini and JaeTankBot.ini are not changed. The bot's StarCraft runs
// without sound unless asked.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "DifficultyConfig.h"
#include "IniFile.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
using namespace bb;

#ifndef JAETANKBOT_VERSION
#define JAETANKBOT_VERSION "?"
#endif

namespace
{
  enum Id
  {
    ID_SCDIR = 100, ID_BROWSE, ID_MODE_HUMAN, ID_MODE_BOTS, ID_MODE_CPU, ID_DIFF, ID_MAP, ID_OPP, ID_RACE,
    ID_MUTE, ID_FAST, ID_HUMAN_BWAPI, ID_START, ID_CHAOS, ID_STATUS, ID_OPP_LABEL, ID_RACE_LABEL
  };
  constexpr UINT WM_STATUS = WM_APP + 1;   // lParam: std::wstring* (deleted by the receiver)
  constexpr UINT WM_DONE = WM_APP + 2;

  enum class Mode { Human, Bots, Computer };

  const wchar_t* kRaces[] = { L"Terran", L"Protoss", L"Zerg", L"Random" };

  HWND g_wnd = nullptr;
  HFONT g_font = nullptr;
  bool g_busy = false;
  int g_dpi = 96;
  std::vector<std::wstring> g_maps;       // relative to the StarCraft folder, '/' separated
  std::vector<std::wstring> g_opponents;  // bwapi-data/AI/<name>.dll

  int px(int v) { return MulDiv(v, g_dpi, 96); }

  std::wstring widen(const std::string& s)
  {
    if (s.empty())
      return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
  }

  std::string narrow(const std::wstring& s)
  {
    if (s.empty())
      return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
  }

  std::wstring upper(std::wstring s)
  {
    for (auto& c : s)
      c = static_cast<wchar_t>(std::towupper(c));
    return s;
  }

  HWND item(int id) { return GetDlgItem(g_wnd, id); }

  std::wstring text(int id)
  {
    const int n = GetWindowTextLengthW(item(id));
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(item(id), s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
  }

  // Thread-safe status line.
  void status(const std::wstring& line) { PostMessageW(g_wnd, WM_STATUS, 0, reinterpret_cast<LPARAM>(new std::wstring(line))); }

  void appendStatus(const std::wstring& line)
  {
    HWND s = item(ID_STATUS);
    const int len = GetWindowTextLengthW(s);
    SendMessageW(s, EM_SETSEL, len, len);
    SendMessageW(s, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>((line + L"\r\n").c_str()));
  }

  fs::path exeDir()
  {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
  }

  bool isStarCraftDir(const fs::path& p)
  {
    std::error_code ec;
    return fs::is_regular_file(p / "StarCraft.exe", ec) && fs::is_regular_file(p / "bwapi-data" / "BWAPI.dll", ec);
  }

  fs::path settingsPath() { return exeDir() / "JaeTankBotPlay.ini"; }

  // ------------------------------------------------------------------ starting StarCraft
  std::wstring environmentBlock(const std::vector<std::pair<std::wstring, std::wstring>>& overrides)
  {
    std::map<std::wstring, std::wstring> vars;   // upper-case name -> "Name=value"
    if (wchar_t* env = GetEnvironmentStringsW())
    {
      for (const wchar_t* p = env; *p; p += wcslen(p) + 1)
      {
        const std::wstring entry(p);
        const size_t eq = entry.find(L'=', 1);
        if (eq != std::wstring::npos)
          vars[upper(entry.substr(0, eq))] = entry;
      }
      FreeEnvironmentStringsW(env);
    }
    for (const auto& [name, value] : overrides)
      vars[upper(name)] = name + L"=" + value;
    std::wstring block;
    for (const auto& [key, entry] : vars)
      block += entry + L'\0';
    block += L'\0';
    return block;
  }

  struct Started
  {
    HANDLE process = nullptr;
    DWORD pid = 0;
  };

  // Starts StarCraft.exe in `dir`; with `bwapi` it loads bwapi-data\BWAPI.dll into it
  // before its first instruction runs.
  bool startStarCraft(const fs::path& dir, bool bwapi, const std::vector<std::pair<std::wstring, std::wstring>>& env, bool background,
                      Started& out, std::wstring& error)
  {
    const fs::path exe = dir / "StarCraft.exe";
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    std::wstring block = environmentBlock(env);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (background)
    {
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_SHOWMINNOACTIVE;
    }
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | (bwapi ? CREATE_SUSPENDED : 0);
    if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, block.data(), dir.wstring().c_str(), &si, &pi))
    {
      const DWORD e = GetLastError();
      error = e == ERROR_ELEVATION_REQUIRED ? L"관리자 권한이 필요합니다 (오류 740)" : L"StarCraft.exe 를 시작하지 못했습니다 (오류 " + std::to_wstring(e) + L")";
      return false;
    }
    if (bwapi)
    {
      const std::wstring dll = (dir / "bwapi-data" / "BWAPI.dll").wstring();
      const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
      void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      DWORD loaded = 0;
      if (remote && WriteProcessMemory(pi.hProcess, remote, dll.c_str(), bytes, nullptr))
      {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (HANDLE t = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLibrary, remote, 0, nullptr))
        {
          WaitForSingleObject(t, 30000);
          GetExitCodeThread(t, &loaded);
          CloseHandle(t);
        }
      }
      if (remote)
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
      if (!loaded)
      {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        error = L"BWAPI.dll 을 StarCraft 에 불러오지 못했습니다 (백신 프로그램이 막았을 수 있음)";
        return false;
      }
      ResumeThread(pi.hThread);
    }
    CloseHandle(pi.hThread);
    out.process = pi.hProcess;
    out.pid = pi.dwProcessId;
    return true;
  }

  // The StarCraft window of a process (class SWarClass), waiting up to `ms`.
  HWND waitForWindow(DWORD pid, int ms)
  {
    struct Search { DWORD pid; HWND found; } search{ pid, nullptr };
    for (int waited = 0; waited <= ms; waited += 250)
    {
      EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* s = reinterpret_cast<Search*>(lp);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        wchar_t cls[32] = {};
        GetClassNameW(h, cls, 32);
        if (owner == s->pid && wcscmp(cls, L"SWarClass") == 0)
        {
          s->found = h;
          return FALSE;
        }
        return TRUE;
      }, reinterpret_cast<LPARAM>(&search));
      if (search.found)
        return search.found;
      Sleep(250);
    }
    return nullptr;
  }

  bool stillRunning(HANDLE process) { return WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }

  struct Plan
  {
    fs::path dir;
    Mode mode = Mode::Human;
    int difficulty = 3;
    std::wstring map;
    std::wstring opponent;     // bwapi-data/AI/<name>.dll
    std::wstring race;         // opponent bot's or computer's race
    bool mute = true;
    bool fast = true;
    bool humanBwapi = false;
  };

  using Env = std::vector<std::pair<std::wstring, std::wstring>>;

  Env botEnvironment(const Plan& p, const std::wstring& ai, const std::wstring& character, const std::wstring& race, int left)
  {
    const bool lan = p.mode != Mode::Computer;
    Env env{
      { L"BWAPI_CONFIG_AI__AI", ai },
      { L"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", lan ? L"LAN" : L"SINGLE_PLAYER" },
      { L"BWAPI_CONFIG_AUTO_MENU__CHARACTER_NAME", character },
      { L"BWAPI_CONFIG_AUTO_MENU__RACE", race },
      { L"BWAPI_CONFIG_AUTO_MENU__MAP", p.map },
      { L"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", L"MELEE" },
      { L"BWAPI_CONFIG_AUTO_MENU__AUTO_RESTART", L"OFF" },
      { L"BWAPI_CONFIG_WINDOW__WINDOWED", L"ON" },
      { L"BWAPI_CONFIG_WINDOW__LEFT", std::to_wstring(left) },
      { L"BWAPI_CONFIG_WINDOW__TOP", L"0" },
      { L"BWAPI_CONFIG_WINDOW__WIDTH", L"640" },
      { L"BWAPI_CONFIG_WINDOW__HEIGHT", L"480" },
      { L"BWAPI_CONFIG_STARCRAFT__SOUND", p.mute ? L"OFF" : L"ON" },
    };
    if (lan)
    {
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__LAN_MODE", L"Local PC" });
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__GAME", L"JOIN_FIRST" });
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__WAIT_FOR_MIN_PLAYERS", L"2" });
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__WAIT_FOR_MAX_PLAYERS", L"2" });
    }
    else
    {
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__ENEMY_COUNT", L"1" });
      env.push_back({ L"BWAPI_CONFIG_AUTO_MENU__ENEMY_RACE", p.race });
    }
    // JaeTankBot's own settings for this instance.
    env.push_back({ L"JAETANKBOT_BOT__DIFFICULTY", std::to_wstring(p.difficulty) });
    env.push_back({ L"JAETANKBOT_GAME__LOCAL_SPEED", p.mode != Mode::Human && p.fast ? L"0" : L"-1" });
    return env;
  }

  void runPlan(Plan p)
  {
    std::wstring error;
    Started human, bot, other;
    auto finish = [&] {
      for (HANDLE h : { human.process, bot.process, other.process })
        if (h)
          CloseHandle(h);
      PostMessageW(g_wnd, WM_DONE, 0, 0);
    };
    const std::wstring ourAi = L"bwapi-data/AI/JaeTankBot.dll";
    switch (p.mode)
    {
    case Mode::Human:
    {
      status(L"1/2 사람용 스타크래프트 시작" + std::wstring(p.humanBwapi ? L" (BWAPI 창 모드)" : L" (일반 게임)"));
      Env humanEnv;
      if (p.humanBwapi)
        humanEnv = { { L"BWAPI_CONFIG_AI__AI", L"NONE" }, { L"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", L"OFF" },
                     { L"BWAPI_CONFIG_WINDOW__WINDOWED", L"ON" }, { L"BWAPI_CONFIG_STARCRAFT__SOUND", L"ON" } };
      if (!startStarCraft(p.dir, p.humanBwapi, humanEnv, false, human, error))
      {
        status(L"실패: " + error);
        return finish();
      }
      const HWND humanWindow = waitForWindow(human.pid, 30000);
      if (!humanWindow)
        status(L"사람용 창을 찾지 못했지만 계속합니다");
      Sleep(1500);
      status(L"2/2 봇 스타크래프트 시작 (난이도 " + std::to_wstring(p.difficulty) + L", " + (p.mute ? L"소리 끔" : L"소리 켬") + L")");
      if (!startStarCraft(p.dir, true, botEnvironment(p, ourAi, L"JaeTankBot", L"Terran", 0), true, bot, error))
      {
        status(L"실패: " + error);
        return finish();
      }
      Sleep(6000);
      if (!stillRunning(bot.process))
      {
        status(L"봇 스타크래프트가 바로 꺼졌습니다. '사람 창도 BWAPI 로' 를 체크하고 다시 해 보세요.");
        return finish();
      }
      if (humanWindow && IsWindow(humanWindow))
      {
        ShowWindow(humanWindow, SW_RESTORE);
        SetForegroundWindow(humanWindow);
      }
      status(L"준비 완료. 사람 창에서: Multiplayer → Expansion(Brood War) → Local PC → 아이디 선택 →");
      status(L"  게임 목록의 JaeTankBot 게임에 참가하면 봇이 시작합니다. (봇 창은 작업 표시줄에 최소화)");
      return finish();
    }
    case Mode::Bots:
    {
      status(L"1/2 JaeTankBot (게임을 만듦)");
      if (!startStarCraft(p.dir, true, botEnvironment(p, ourAi, L"JaeTankBot", L"Terran", 0), false, bot, error))
      {
        status(L"실패: " + error);
        return finish();
      }
      waitForWindow(bot.pid, 30000);
      Sleep(5000);
      const std::wstring name = fs::path(p.opponent).stem().wstring();
      status(L"2/2 " + name + L" (" + p.race + L", 게임에 참가)");
      if (!startStarCraft(p.dir, true, botEnvironment(p, p.opponent, name.substr(0, 12), p.race, 660), false, other, error))
      {
        status(L"실패: " + error);
        return finish();
      }
      Sleep(6000);
      if (!stillRunning(other.process))
        status(L"두 번째 스타크래프트가 바로 꺼졌습니다. Chaoslauncher - MultiInstance 로 시도해 보세요.");
      else
        status(L"두 봇이 같은 게임에 들어가면 자동으로 시작합니다.");
      return finish();
    }
    case Mode::Computer:
      status(L"JaeTankBot 대 컴퓨터 (" + p.race + L")");
      if (!startStarCraft(p.dir, true, botEnvironment(p, ourAi, L"JaeTankBot", L"Terran", 0), false, bot, error))
        status(L"실패: " + error);
      else
        status(L"싱글 플레이 게임이 자동으로 시작됩니다.");
      return finish();
    }
    finish();
  }

  // ------------------------------------------------------------------ lists and settings
  void fillCombo(int id, const std::vector<std::wstring>& items, const std::wstring& select)
  {
    HWND c = item(id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i].c_str()));
      if (_wcsicmp(items[i].c_str(), select.c_str()) == 0)
        sel = static_cast<int>(i);
    }
    SendMessageW(c, CB_SETCURSEL, sel, 0);
  }

  std::wstring comboText(int id)
  {
    HWND c = item(id);
    const int i = static_cast<int>(SendMessageW(c, CB_GETCURSEL, 0, 0));
    if (i < 0)
      return {};
    const int n = static_cast<int>(SendMessageW(c, CB_GETLBTEXTLEN, i, 0));
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    SendMessageW(c, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(s.data()));
    s.resize(static_cast<size_t>(n));
    return s;
  }

  void scanFolder(const std::wstring& keepMap, const std::wstring& keepOpponent)
  {
    const fs::path dir = text(ID_SCDIR);
    g_maps.clear();
    g_opponents.clear();
    std::error_code ec;
    if (isStarCraftDir(dir))
    {
      for (auto it = fs::recursive_directory_iterator(dir / "maps", fs::directory_options::skip_permission_denied, ec);
           it != fs::recursive_directory_iterator(); it.increment(ec))
      {
        if (ec)
          break;
        const std::wstring ext = upper(it->path().extension().wstring());
        if (it->is_regular_file(ec) && (ext == L".SCM" || ext == L".SCX"))
        {
          std::wstring rel = fs::relative(it->path(), dir, ec).generic_wstring();
          if (upper(rel).find(L"REPLAYS") == std::wstring::npos)
            g_maps.push_back(rel);
        }
      }
      for (const auto& e : fs::directory_iterator(dir / "bwapi-data" / "AI", ec))
      {
        const std::wstring name = e.path().filename().wstring();
        if (upper(e.path().extension().wstring()) == L".DLL" && _wcsicmp(name.c_str(), L"JaeTankBot.dll") != 0)
          g_opponents.push_back(L"bwapi-data/AI/" + name);
      }
    }
    std::sort(g_maps.begin(), g_maps.end());
    std::sort(g_opponents.begin(), g_opponents.end());
    fillCombo(ID_MAP, g_maps, keepMap.empty() ? L"maps/Download/(2)Boxer.scm" : keepMap);
    fillCombo(ID_OPP, g_opponents, keepOpponent);
  }

  Mode currentMode()
  {
    if (SendMessageW(item(ID_MODE_BOTS), BM_GETCHECK, 0, 0) == BST_CHECKED)
      return Mode::Bots;
    if (SendMessageW(item(ID_MODE_CPU), BM_GETCHECK, 0, 0) == BST_CHECKED)
      return Mode::Computer;
    return Mode::Human;
  }

  void updateEnabled()
  {
    const Mode m = currentMode();
    EnableWindow(item(ID_OPP), m == Mode::Bots && !g_busy);
    EnableWindow(item(ID_RACE), m != Mode::Human && !g_busy);
    EnableWindow(item(ID_FAST), m != Mode::Human && !g_busy);
    EnableWindow(item(ID_HUMAN_BWAPI), m == Mode::Human && !g_busy);
    SetWindowTextW(item(ID_RACE_LABEL), m == Mode::Computer ? L"컴퓨터 종족" : L"상대 봇 종족");
    EnableWindow(item(ID_START), !g_busy);
  }

  void loadSettings()
  {
    IniFile ini;
    ini.load(settingsPath().string());
    std::wstring dir = widen(ini.getString("launcher", "starcraft", ""));
    const fs::path guess = exeDir().parent_path().parent_path();   // installed in <StarCraft>\bwapi-data\AI
    if (!isStarCraftDir(dir) && isStarCraftDir(guess))
      dir = guess.wstring();
    SetWindowTextW(item(ID_SCDIR), dir.c_str());
    const std::string mode = ini.getString("launcher", "mode", "human");
    const int modeId = mode == "bots" ? ID_MODE_BOTS : mode == "computer" ? ID_MODE_CPU : ID_MODE_HUMAN;
    CheckRadioButton(g_wnd, ID_MODE_HUMAN, ID_MODE_CPU, modeId);
    SendMessageW(item(ID_DIFF), CB_SETCURSEL, std::clamp(ini.getInt("launcher", "difficulty", 3), 1, 5) - 1, 0);
    fillCombo(ID_RACE, std::vector<std::wstring>(std::begin(kRaces), std::end(kRaces)), widen(ini.getString("launcher", "race", "Terran")));
    SendMessageW(item(ID_MUTE), BM_SETCHECK, ini.getBool("launcher", "mute_bot", true) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(item(ID_FAST), BM_SETCHECK, ini.getBool("launcher", "fast_bot_games", true) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(item(ID_HUMAN_BWAPI), BM_SETCHECK, ini.getBool("launcher", "human_bwapi", false) ? BST_CHECKED : BST_UNCHECKED, 0);
    scanFolder(widen(ini.getString("launcher", "map", "")), widen(ini.getString("launcher", "opponent", "bwapi-data/AI/Steamhammer.dll")));
    updateEnabled();
  }

  void saveSettings(const Plan& p)
  {
    IniFile ini;
    ini.load(settingsPath().string());
    ini.set("launcher", "starcraft", narrow(p.dir.wstring()));
    ini.set("launcher", "mode", p.mode == Mode::Bots ? "bots" : p.mode == Mode::Computer ? "computer" : "human");
    ini.set("launcher", "difficulty", std::to_string(p.difficulty));
    ini.set("launcher", "map", narrow(p.map));
    ini.set("launcher", "opponent", narrow(p.opponent));
    ini.set("launcher", "race", narrow(p.race));
    ini.set("launcher", "mute_bot", p.mute ? "on" : "off");
    ini.set("launcher", "fast_bot_games", p.fast ? "on" : "off");
    ini.set("launcher", "human_bwapi", p.humanBwapi ? "on" : "off");
    ini.save(settingsPath().string());
  }

  bool anyStarCraftRunning()
  {
    return FindWindowW(L"SWarClass", nullptr) != nullptr;
  }

  void onStart()
  {
    Plan p;
    p.dir = text(ID_SCDIR);
    if (!isStarCraftDir(p.dir))
    {
      MessageBoxW(g_wnd, L"StarCraft 폴더를 골라 주세요.\nStarCraft.exe 와 bwapi-data\\BWAPI.dll 이 있어야 합니다.", L"JaeTankBot", MB_ICONWARNING);
      return;
    }
    if (!fs::exists(p.dir / "bwapi-data" / "AI" / "JaeTankBot.dll"))
    {
      MessageBoxW(g_wnd, L"bwapi-data\\AI\\JaeTankBot.dll 이 없습니다. install.bat 으로 먼저 설치하세요.", L"JaeTankBot", MB_ICONWARNING);
      return;
    }
    p.mode = currentMode();
    p.difficulty = static_cast<int>(SendMessageW(item(ID_DIFF), CB_GETCURSEL, 0, 0)) + 1;
    p.map = comboText(ID_MAP);
    p.opponent = comboText(ID_OPP);
    p.race = comboText(ID_RACE);
    p.mute = SendMessageW(item(ID_MUTE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.fast = SendMessageW(item(ID_FAST), BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.humanBwapi = SendMessageW(item(ID_HUMAN_BWAPI), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (p.map.empty())
    {
      MessageBoxW(g_wnd, L"maps 폴더에서 맵을 찾지 못했습니다.", L"JaeTankBot", MB_ICONWARNING);
      return;
    }
    if (p.mode == Mode::Bots && p.opponent.empty())
    {
      MessageBoxW(g_wnd, L"bwapi-data\\AI 에 상대 봇 DLL 이 없습니다.", L"JaeTankBot", MB_ICONWARNING);
      return;
    }
    if (p.mode == Mode::Human && !fs::exists(p.dir / "SNP_DirectIP.snp"))
    {
      MessageBoxW(g_wnd, L"StarCraft 폴더에 SNP_DirectIP.snp (BWAPI 의 Local PC 연결) 가 없습니다.\nBWAPI 4.4.0 을 설치하세요.", L"JaeTankBot", MB_ICONWARNING);
      return;
    }
    if (anyStarCraftRunning() &&
        MessageBoxW(g_wnd, L"이미 실행 중인 스타크래프트가 있습니다. 닫고 시작하는 것이 안전합니다.\n그래도 계속할까요?", L"JaeTankBot",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    saveSettings(p);
    g_busy = true;
    updateEnabled();
    std::thread(runPlan, p).detach();
  }

  void onBrowse()
  {
    BROWSEINFOW bi{};
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = L"StarCraft 1.16.1 폴더 (StarCraft.exe 와 bwapi-data 가 있는 곳)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi))
    {
      wchar_t path[MAX_PATH] = {};
      if (SHGetPathFromIDListW(pidl, path))
      {
        SetWindowTextW(item(ID_SCDIR), path);
        scanFolder(comboText(ID_MAP), comboText(ID_OPP));
      }
      CoTaskMemFree(pidl);
    }
  }

  void onChaoslauncher()
  {
    wchar_t home[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    const fs::path launcher = fs::path(home) / "BWAPI" / "Chaoslauncher" / "Chaoslauncher - MultiInstance.exe";
    if (!fs::exists(launcher) ||
        reinterpret_cast<INT_PTR>(ShellExecuteW(g_wnd, L"open", launcher.wstring().c_str(), nullptr, launcher.parent_path().wstring().c_str(), SW_SHOWNORMAL)) <= 32)
      MessageBoxW(g_wnd, (L"Chaoslauncher 를 찾지 못했습니다:\n" + launcher.wstring()).c_str(), L"JaeTankBot", MB_ICONINFORMATION);
  }

  // ------------------------------------------------------------------ window
  HWND add(const wchar_t* cls, const wchar_t* label, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0)
  {
    HWND c = CreateWindowExW(ex, cls, label, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), g_wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
  }

  void createControls()
  {
    const int L = 16, W = 520;
    int y = 14;
    add(L"STATIC", L"StarCraft 1.16.1 폴더", 0, L, y, 200, 18, -1);
    y += 20;
    add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, L, y, W - 90, 24, ID_SCDIR, WS_EX_CLIENTEDGE);
    add(L"BUTTON", L"찾기...", BS_PUSHBUTTON | WS_TABSTOP, L + W - 82, y, 82, 24, ID_BROWSE);
    y += 36;
    add(L"BUTTON", L"대전 방식", BS_GROUPBOX, L, y, W, 88, -1);
    add(L"BUTTON", L"사람 대 JaeTankBot (내 화면은 일반 스타크래프트)", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, L + 12, y + 20, W - 24, 20, ID_MODE_HUMAN);
    add(L"BUTTON", L"JaeTankBot 대 다른 봇", BS_AUTORADIOBUTTON, L + 12, y + 42, W - 24, 20, ID_MODE_BOTS);
    add(L"BUTTON", L"JaeTankBot 대 컴퓨터", BS_AUTORADIOBUTTON, L + 12, y + 64, W - 24, 20, ID_MODE_CPU);
    y += 100;
    add(L"STATIC", L"봇 난이도", 0, L, y + 4, 120, 18, -1);
    HWND diff = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L + 130, y, W - 130, 200, ID_DIFF);
    for (int l = kMinDifficulty; l <= kMaxDifficulty; ++l)
    {
      const DifficultyConfig c = makeDifficultyConfig(l);
      const std::wstring label = std::to_wstring(l) + L" - " + widen(c.koreanName) + L" (" + widen(c.name) + L")";
      SendMessageW(diff, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    y += 32;
    add(L"STATIC", L"맵", 0, L, y + 4, 120, 18, -1);
    add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L + 130, y, W - 130, 300, ID_MAP);
    y += 32;
    add(L"STATIC", L"상대 봇 (DLL)", 0, L, y + 4, 120, 18, ID_OPP_LABEL);
    add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L + 130, y, W - 130, 300, ID_OPP);
    y += 32;
    add(L"STATIC", L"상대 봇 종족", 0, L, y + 4, 120, 18, ID_RACE_LABEL);
    add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, L + 130, y, 160, 200, ID_RACE);
    y += 36;
    add(L"BUTTON", L"봇 스타크래프트 소리 끄기", BS_AUTOCHECKBOX | WS_TABSTOP, L, y, 250, 20, ID_MUTE);
    add(L"BUTTON", L"봇끼리는 최고 속도", BS_AUTOCHECKBOX | WS_TABSTOP, L + 270, y, 250, 20, ID_FAST);
    y += 24;
    add(L"BUTTON", L"사람 창도 BWAPI 로 (창 모드, 일반 실행이 안 될 때)", BS_AUTOCHECKBOX | WS_TABSTOP, L, y, W, 20, ID_HUMAN_BWAPI);
    y += 32;
    add(L"BUTTON", L"게임 시작", BS_DEFPUSHBUTTON | WS_TABSTOP, L, y, 200, 34, ID_START);
    add(L"BUTTON", L"Chaoslauncher 열기", BS_PUSHBUTTON | WS_TABSTOP, L + W - 170, y, 170, 34, ID_CHAOS);
    y += 46;
    add(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, L, y, W, 120, ID_STATUS, WS_EX_CLIENTEDGE);
  }

  LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
  {
    switch (msg)
    {
    case WM_CREATE:
      g_wnd = h;
      createControls();
      loadSettings();
      appendStatus(L"JaeTankBot " + widen(JAETANKBOT_VERSION) + L" - Chaoslauncher 없이 바로 시작합니다.");
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp))
      {
      case ID_START: onStart(); break;
      case ID_BROWSE: onBrowse(); break;
      case ID_CHAOS: onChaoslauncher(); break;
      case ID_MODE_HUMAN: case ID_MODE_BOTS: case ID_MODE_CPU: updateEnabled(); break;
      case ID_SCDIR:
        if (HIWORD(wp) == EN_KILLFOCUS)
          scanFolder(comboText(ID_MAP), comboText(ID_OPP));
        break;
      }
      return 0;
    case WM_STATUS:
    {
      auto* line = reinterpret_cast<std::wstring*>(lp);
      appendStatus(*line);
      delete line;
      return 0;
    }
    case WM_DONE:
      g_busy = false;
      updateEnabled();
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
  }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
  SetProcessDPIAware();
  INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
  InitCommonControlsEx(&icc);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HDC screen = GetDC(nullptr);
  g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
  ReleaseDC(nullptr, screen);
  g_font = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");

  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.lpszClassName = L"JaeTankBotPlay";
  RegisterClassExW(&wc);

  RECT r{ 0, 0, px(552), px(568) };
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  const std::wstring title = L"JaeTankBot " + widen(JAETANKBOT_VERSION);
  HWND wnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                             r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  ShowWindow(wnd, show);
  SetFocus(GetDlgItem(wnd, ID_START));
  UpdateWindow(wnd);

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0))
  {
    if (!IsDialogMessageW(wnd, &m))
    {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  CoUninitialize();
  return 0;
}
