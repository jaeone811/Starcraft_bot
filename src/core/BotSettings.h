#pragma once
// Runtime settings read from JaeTankBot.ini (see config/JaeTankBot.ini).

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bb
{
  struct BotSettings
  {
    int difficulty = 3;           // 1..5
    std::string race = "Terran";  // the race this bot implements; checked against the game
    // liquipedia: openings per enemy race from Liquipedia's guides (0.9.1);
    // classic: the difficulty's build and research order for every race (as in 0.8.8).
    std::string opening = "liquipedia";
    std::string logLevel = "info";// debug | info | warn | error
    std::string logDirectory = "bwapi-data/write/JaeTankBot";
    int localSpeed = -1;          // -1 = leave the game speed alone, 0 = fastest
    int frameSkip = 0;
    bool debugDraw = true;        // draw state text on screen
    bool enableUserInput = false; // BWAPI blocks human input by default
    int snapshotIntervalFrames = 720; // periodic metrics line in the log

    std::string loadedFrom;       // path the settings came from ("" = defaults)
    std::vector<std::string> warnings;
  };

  // Looks up an environment variable (nullopt: not set).
  using EnvLookup = std::function<std::optional<std::string>(const std::string& name)>;
  // The process environment.
  std::optional<std::string> processEnvironment(const std::string& name);

  // Tries each path in order and loads the first that exists. Invalid values are
  // replaced by defaults and reported in `warnings`. Settings in the environment win
  // over the file: JAETANKBOT_<SECTION>__<KEY>, e.g. JAETANKBOT_BOT__DIFFICULTY=5 or
  // JAETANKBOT_GAME__LOCAL_SPEED=-1 (the launcher sets them per StarCraft instance
  // instead of rewriting the file).
  BotSettings loadBotSettings(const std::vector<std::string>& candidatePaths, const EnvLookup& env = processEnvironment);

  // Same validation for already-loaded INI text (used by tests and the configurator).
  BotSettings parseBotSettings(const std::string& iniText, const EnvLookup& env = nullptr);

  // Default search paths, relative to the StarCraft folder (BWAPI's working directory).
  std::vector<std::string> defaultSettingsPaths();
}
