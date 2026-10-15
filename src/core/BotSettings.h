#pragma once
// Runtime settings read from BeginnerBot.ini (see config/BeginnerBot.ini).

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
    std::string logDirectory = "bwapi-data/write/BeginnerBot";
    int localSpeed = -1;          // -1 = leave the game speed alone, 0 = fastest
    int frameSkip = 0;
    bool debugDraw = true;        // draw state text on screen
    bool enableUserInput = false; // BWAPI blocks human input by default
    int snapshotIntervalFrames = 720; // periodic metrics line in the log

    std::string loadedFrom;       // path the settings came from ("" = defaults)
    std::vector<std::string> warnings;
  };

  // Tries each path in order and loads the first that exists. Invalid values are
  // replaced by defaults and reported in `warnings`.
  BotSettings loadBotSettings(const std::vector<std::string>& candidatePaths);

  // Same validation for already-loaded INI text (used by tests and the configurator).
  BotSettings parseBotSettings(const std::string& iniText);

  // Default search paths, relative to the StarCraft folder (BWAPI's working directory).
  std::vector<std::string> defaultSettingsPaths();
}
