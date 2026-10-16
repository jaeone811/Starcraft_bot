#pragma once
// Shared services and module pointers. JaeTankBot owns everything; modules keep
// a reference to the context to reach each other.

#include "BotSettings.h"
#include "CommandHistory.h"
#include "DifficultyConfig.h"
#include "Logger.h"
#include "Metrics.h"

#include <sstream>
#include <string>

namespace bb
{
  class Commander;
  class MapInfo;
  class InformationManager;
  class WorkerManager;
  class BuildingManager;
  class SupplyManager;
  class ProductionManager;
  class ScoutManager;
  class CombatManager;
  class StrategyManager;

  struct BotContext
  {
    BotSettings settings;
    DifficultyConfig config;
    Logger log;
    CommandHistory history;
    Metrics metrics;
    int frame = 0;

    Commander* commander = nullptr;
    MapInfo* map = nullptr;
    InformationManager* info = nullptr;
    WorkerManager* workers = nullptr;
    BuildingManager* buildings = nullptr;
    SupplyManager* supply = nullptr;
    ProductionManager* production = nullptr;
    ScoutManager* scout = nullptr;
    CombatManager* combat = nullptr;
    StrategyManager* strategy = nullptr;
  };

  // Joins its arguments into one string: concat("wave ", n, " started").
  template <typename... Args>
  std::string concat(const Args&... args)
  {
    std::ostringstream ss;
    (ss << ... << args);
    return ss.str();
  }
}
