#include "BotSettings.h"

#include "DifficultyConfig.h"
#include "IniFile.h"

#include <cctype>
#include <cstdlib>

namespace bb
{
  namespace
  {
    // The keys the environment may set.
    const std::pair<const char*, const char*> kOverridable[] = {
      { "bot", "difficulty" }, { "bot", "opening" }, { "log", "level" }, { "log", "directory" },
      { "game", "local_speed" }, { "game", "frame_skip" }, { "game", "debug_draw" }, { "game", "enable_user_input" } };

    std::string upper(std::string s)
    {
      for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return s;
    }

    // Returns true when the environment set anything.
    bool applyEnvironment(IniFile& ini, const EnvLookup& env)
    {
      if (!env)
        return false;
      bool any = false;
      for (const auto& [section, key] : kOverridable)
        if (const auto value = env("JAETANKBOT_" + upper(section) + "__" + upper(key)))
        {
          ini.set(section, key, *value);
          any = true;
        }
      return any;
    }

    BotSettings fromIni(const IniFile& ini)
    {
      BotSettings s;
      const auto rawDifficulty = ini.get("bot", "difficulty");
      s.difficulty = ini.getInt("bot", "difficulty", kDefaultDifficulty);
      if (rawDifficulty && ini.getInt("bot", "difficulty", -999) == -999)
      {
        s.warnings.push_back("difficulty '" + *rawDifficulty + "' is not a number; using " + std::to_string(kDefaultDifficulty));
        s.difficulty = kDefaultDifficulty;
      }
      if (s.difficulty < kMinDifficulty || s.difficulty > kMaxDifficulty)
      {
        s.warnings.push_back("difficulty " + std::to_string(s.difficulty) + " is outside 1..5; using " + std::to_string(kDefaultDifficulty));
        s.difficulty = kDefaultDifficulty;
      }

      s.race = ini.getString("bot", "race", s.race);
      if (toLower(s.race) != "terran")
      {
        s.warnings.push_back("race '" + s.race + "' is not implemented; only Terran is supported");
        s.race = "Terran";
      }

      s.opening = toLower(ini.getString("bot", "opening", s.opening));
      if (s.opening != "liquipedia" && s.opening != "classic")
      {
        s.warnings.push_back("opening '" + s.opening + "' unknown; using liquipedia");
        s.opening = "liquipedia";
      }

      s.logLevel = toLower(ini.getString("log", "level", s.logLevel));
      if (s.logLevel != "debug" && s.logLevel != "info" && s.logLevel != "warn" && s.logLevel != "error")
      {
        s.warnings.push_back("log level '" + s.logLevel + "' unknown; using info");
        s.logLevel = "info";
      }
      s.logDirectory = ini.getString("log", "directory", s.logDirectory);
      if (s.logDirectory.empty())
        s.logDirectory = "bwapi-data/write/JaeTankBot";
      s.snapshotIntervalFrames = ini.getInt("log", "snapshot_interval_frames", s.snapshotIntervalFrames);
      if (s.snapshotIntervalFrames < 24)
        s.snapshotIntervalFrames = 24;

      s.localSpeed = ini.getInt("game", "local_speed", s.localSpeed);
      if (s.localSpeed < -1)
        s.localSpeed = -1;
      s.frameSkip = ini.getInt("game", "frame_skip", s.frameSkip);
      if (s.frameSkip < 0)
        s.frameSkip = 0;
      s.debugDraw = ini.getBool("game", "debug_draw", s.debugDraw);
      s.enableUserInput = ini.getBool("game", "enable_user_input", s.enableUserInput);
      return s;
    }
  }

  std::optional<std::string> processEnvironment(const std::string& name)
  {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name.c_str()) != 0 || !value)
      return std::nullopt;
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name.c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
  }

  BotSettings parseBotSettings(const std::string& iniText, const EnvLookup& env)
  {
    IniFile ini;
    ini.parse(iniText);
    applyEnvironment(ini, env);
    return fromIni(ini);
  }

  BotSettings loadBotSettings(const std::vector<std::string>& candidatePaths, const EnvLookup& env)
  {
    for (const auto& path : candidatePaths)
    {
      IniFile ini;
      if (!ini.load(path))
        continue;
      const bool fromEnv = applyEnvironment(ini, env);
      BotSettings s = fromIni(ini);
      s.loadedFrom = fromEnv ? path + " + environment" : path;
      return s;
    }
    IniFile ini;
    const bool fromEnv = applyEnvironment(ini, env);
    BotSettings s = fromIni(ini);
    if (fromEnv)
      s.loadedFrom = "environment";
    else
      s.warnings.push_back("no settings file found; using defaults (difficulty 3)");
    return s;
  }

  std::vector<std::string> defaultSettingsPaths()
  {
    return { "bwapi-data/AI/JaeTankBot.ini", "bwapi-data/read/JaeTankBot.ini" };
  }
}
