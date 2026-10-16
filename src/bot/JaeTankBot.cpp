#include "JaeTankBot.h"

#include "BuildingManager.h"
#include "BwapiUtil.h"
#include "CombatManager.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "PlatformPaths.h"
#include "ProductionManager.h"
#include "ScoutManager.h"
#include "StrategyManager.h"
#include "SupplyManager.h"
#include "WorkerManager.h"

#include <starcraftver.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace BWAPI;

namespace bb
{
  namespace
  {
#ifndef JAETANKBOT_VERSION
#define JAETANKBOT_VERSION "dev"
#endif
    const char* kBotVersion = JAETANKBOT_VERSION; // from project(VERSION) in CMakeLists.txt

    // Update intervals in frames. Commands take effect after the network latency
    // (2+ frames), so running every frame would only produce duplicate orders.
    constexpr int kInfoInterval = 6;
    constexpr int kEconomyInterval = 6;
    constexpr int kProductionInterval = 6;
    constexpr int kCombatInterval = 6;
  }

  JaeTankBot::JaeTankBot() = default;
  JaeTankBot::~JaeTankBot() = default;

  template <typename F>
  void JaeTankBot::guarded(const char* where, F&& f)
  {
    try
    {
      f();
    }
    catch (const std::exception& e)
    {
      ++ctx_.metrics.exceptions;
      ctx_.log.error(where, concat("exception: ", e.what()));
    }
    catch (...)
    {
      ++ctx_.metrics.exceptions;
      ctx_.log.error(where, "unknown exception");
    }
  }

  void JaeTankBot::createModules()
  {
    commander_ = std::make_unique<Commander>(ctx_);
    map_ = std::make_unique<MapInfo>(ctx_);
    info_ = std::make_unique<InformationManager>(ctx_);
    workers_ = std::make_unique<WorkerManager>(ctx_);
    buildings_ = std::make_unique<BuildingManager>(ctx_);
    supply_ = std::make_unique<SupplyManager>(ctx_);
    production_ = std::make_unique<ProductionManager>(ctx_);
    scout_ = std::make_unique<ScoutManager>(ctx_);
    combat_ = std::make_unique<CombatManager>(ctx_);
    strategy_ = std::make_unique<StrategyManager>(ctx_);
    ctx_.commander = commander_.get();
    ctx_.map = map_.get();
    ctx_.info = info_.get();
    ctx_.workers = workers_.get();
    ctx_.buildings = buildings_.get();
    ctx_.supply = supply_.get();
    ctx_.production = production_.get();
    ctx_.scout = scout_.get();
    ctx_.combat = combat_.get();
    ctx_.strategy = strategy_.get();
  }

  void JaeTankBot::onStart()
  {
    // Paths in the settings are relative to the StarCraft folder; do not rely on the
    // process working directory alone.
    const std::filesystem::path root = starcraftRoot();
    std::vector<std::string> settingsPaths;
    for (const auto& p : defaultSettingsPaths())
      settingsPaths.push_back((root / p).string());
    const std::filesystem::path moduleDir = moduleDirectory();
    if (!moduleDir.empty())
      settingsPaths.push_back((moduleDir / "JaeTankBot.ini").string());
    ctx_.settings = loadBotSettings(settingsPaths);
    if (std::filesystem::path(ctx_.settings.logDirectory).is_relative())
      ctx_.settings.logDirectory = (root / ctx_.settings.logDirectory).string();
    ctx_.config = makeDifficultyConfig(ctx_.settings.difficulty);
    ctx_.log.setLevel(parseLogLevel(ctx_.settings.logLevel));
    const bool logOpen = ctx_.log.open(ctx_.settings.logDirectory, "JaeTankBot_" + timestampForFileName() + ".log");

    try
    {
      ctx_.log.info("Bot", concat("JaeTankBot ", kBotVersion, " compiled against BWAPI ", BWAPI_VER,
                                  " (StarCraft ", STARCRAFT_VER, ")"));
      ctx_.log.info("Bot", concat("StarCraft folder: ", root.string(), "  module folder: ", moduleDir.string()));
      ctx_.log.info("Bot", concat("settings: ", ctx_.settings.loadedFrom.empty() ? std::string("<defaults>") : ctx_.settings.loadedFrom));
      for (const auto& w : ctx_.settings.warnings)
        ctx_.log.warn("Settings", w);
      ctx_.log.info("Bot", concat("APPLIED ", describe(ctx_.config)));

      if (Broodwar->isReplay())
      {
        ctx_.log.info("Bot", "replay detected; the bot stays passive");
        Broodwar->printf("JaeTankBot: replay, AI inactive");
        return;
      }
      if (!Broodwar->self())
      {
        ctx_.log.error("Bot", "no self player; the bot stays passive");
        return;
      }

      ctx_.log.info("Bot", concat("map=", Broodwar->mapFileName(), " (", Broodwar->mapName(), ") self=",
                                  Broodwar->self()->getName(), " race=", Broodwar->self()->getRace().c_str(),
                                  " gameType=", Broodwar->getGameType().c_str(), " latencyFrames=", Broodwar->getLatencyFrames()));
      if (Broodwar->self()->getRace() != Races::Terran)
      {
        workersOnly_ = true;
        ctx_.log.error("Bot", concat("this bot only implements Terran but the game gave us ",
                                     Broodwar->self()->getRace().c_str(),
                                     ". Set race = Terran in bwapi.ini [auto_menu] or pick Terran in the lobby. "
                                     "Falling back to mining only."));
        Broodwar->printf("JaeTankBot: only Terran is supported; mining only");
      }

      Broodwar->setCommandOptimizationLevel(2);
      if (ctx_.settings.enableUserInput)
        Broodwar->enableFlag(Flag::UserInput);
      if (ctx_.settings.localSpeed >= 0)
        Broodwar->setLocalSpeed(ctx_.settings.localSpeed);
      if (ctx_.settings.frameSkip > 0)
        Broodwar->setFrameSkip(ctx_.settings.frameSkip);

      createModules();
      map_->onStart();
      info_->onStart();
      production_->onStart();
      active_ = true;

      // Game text is plain ASCII: StarCraft 1.16.1 does not render UTF-8.
      Broodwar->printf("JaeTankBot %s - difficulty %d (%s)%s", kBotVersion, ctx_.config.level, ctx_.config.name.c_str(),
                       logOpen ? "" : " [log file could not be opened]");
    }
    catch (const std::exception& e)
    {
      ++ctx_.metrics.exceptions;
      ctx_.log.error("Bot", concat("onStart failed: ", e.what()));
    }
    ctx_.log.flush();
  }

  void JaeTankBot::onFrame()
  {
    if (!active_ || Broodwar->isPaused())
      return;
    ctx_.frame = Broodwar->getFrameCount();
    ctx_.log.setFrame(ctx_.frame);

    if (workersOnly_)
    {
      if (ctx_.frame - lastEconomyFrame_ >= kEconomyInterval)
      {
        lastEconomyFrame_ = ctx_.frame;
        guarded("Workers", [&] { workers_->update(); });
        guarded("Workers", [&] {
          for (Unit d : workers_->depots())
            if (d->getTrainingQueue().empty() && Broodwar->self()->minerals() >= 50)
              commander_->train(d, Broodwar->self()->getRace().getWorker());
        });
      }
      return;
    }

    if (ctx_.frame - lastInfoFrame_ >= kInfoInterval)
    {
      lastInfoFrame_ = ctx_.frame;
      guarded("Info", [&] { info_->update(); });
      guarded("Map", [&] { map_->update(); });
      guarded("Strategy", [&] { strategy_->update(); });
    }
    if (ctx_.frame - lastEconomyFrame_ >= kEconomyInterval)
    {
      const int elapsed = std::max(1, ctx_.frame - lastEconomyFrame_);
      lastEconomyFrame_ = ctx_.frame;
      guarded("Supply", [&] { supply_->update(std::min(elapsed, kEconomyInterval)); });
      guarded("Workers", [&] { workers_->update(); });
      guarded("Build", [&] { buildings_->update(); });
      guarded("Scout", [&] { scout_->update(); });
    }
    if (ctx_.frame - lastProductionFrame_ >= kProductionInterval)
    {
      const int elapsed = std::max(1, ctx_.frame - lastProductionFrame_);
      lastProductionFrame_ = ctx_.frame;
      guarded("Production", [&] { production_->update(std::min(elapsed, kProductionInterval)); });
    }
    if (ctx_.frame - lastCombatFrame_ >= kCombatInterval)
    {
      lastCombatFrame_ = ctx_.frame;
      guarded("Combat", [&] { combat_->update(); });
    }

    if (ctx_.frame - lastSnapshotFrame_ >= ctx_.settings.snapshotIntervalFrames)
    {
      lastSnapshotFrame_ = ctx_.frame;
      guarded("Bot", [&] { logSnapshot(); });
      ctx_.log.flush();
    }
    if (ctx_.settings.debugDraw)
      guarded("Draw", [&] { drawDebug(); });
  }

  void JaeTankBot::logSnapshot()
  {
    Player self = Broodwar->self();
    ctx_.log.info("Snapshot", concat(
      "supply=", self->supplyUsed() / 2, "/", self->supplyTotal() / 2,
      " minerals=", self->minerals(), " gas=", self->gas(),
      " gathered=", self->gatheredMinerals(), "/", self->gatheredGas(),
      " workers=", workers_->workerCount(), " (min=", workers_->countRole(WorkerRole::Minerals),
      " gas=", workers_->countRole(WorkerRole::Gas), " build=", workers_->countRole(WorkerRole::Build),
      " scout=", workers_->countRole(WorkerRole::Scout), ")",
      " barracks=", self->completedUnitCount(UnitTypes::Terran_Barracks),
      " factories=", self->completedUnitCount(UnitTypes::Terran_Factory),
      " mech=", self->completedUnitCount(UnitTypes::Terran_Vulture), "v/",
      self->completedUnitCount(UnitTypes::Terran_Siege_Tank_Tank_Mode) +
        self->completedUnitCount(UnitTypes::Terran_Siege_Tank_Siege_Mode), "t/",
      self->completedUnitCount(UnitTypes::Terran_Goliath), "g (target ", static_cast<int>(strategy_->mechShare() * 100), "%)",
      " air=", self->completedUnitCount(UnitTypes::Terran_Wraith), "w/", self->completedUnitCount(UnitTypes::Terran_Science_Vessel), "v/",
      self->completedUnitCount(UnitTypes::Terran_Battlecruiser), "b",
      " army=", combat_->armySize(), " combat=", combat_->stateName(), " wave=", combat_->waveSize(),
      " attackSize=", strategy_->attackSize(), " buildTasks=", buildings_->taskCount(),
      " knownEnemyBuildings=", info_->enemyBuildings().size(), " knownEnemyUnits=", info_->enemyUnits().size(),
      " supplyBlockedFrames=", ctx_.metrics.supplyBlockedFrames, " plan=[", production_->lastPlan(), "]"));
  }

  void JaeTankBot::drawDebug()
  {
    Player self = Broodwar->self();
    Broodwar->drawTextScreen(5, 5, "JaeTankBot %s  difficulty %d (%s)", kBotVersion, ctx_.config.level, ctx_.config.name.c_str());
    Broodwar->drawTextScreen(5, 17, "workers %d  army %d  combat %s  attackSize %d  scout %s",
                             workers_->workerCount(), combat_->armySize(), combat_->stateName(),
                             strategy_->attackSize(), scout_->stateName());
    Broodwar->drawTextScreen(5, 29, "plan: %s", production_->lastPlan().c_str());
    Broodwar->drawTextScreen(5, 41, "supply %d/%d  blocked frames %d  build tasks %d", self->supplyUsed() / 2,
                             self->supplyTotal() / 2, ctx_.metrics.supplyBlockedFrames, buildings_->taskCount());
    const Position rally = map_->rallyPoint();
    Broodwar->drawCircleMap(rally, 32, Colors::Green);
  }

  void JaeTankBot::onEnd(bool isWinner)
  {
    try
    {
      if (Broodwar->self())
      {
        ctx_.metrics.gatheredMinerals = Broodwar->self()->gatheredMinerals();
        ctx_.metrics.gatheredGas = Broodwar->self()->gatheredGas();
      }
      ctx_.log.info("Bot", concat("game over: ", isWinner ? "WIN" : "LOSS", " after ", Broodwar->getFrameCount(),
                                  " frames (", formatGameTime(Broodwar->getFrameCount()), ")"));
      ctx_.log.info("Bot", concat("metrics: ", metricsSummary(ctx_.metrics)));
      ctx_.log.info("Bot", concat("log warnings=", ctx_.log.warningCount(), " errors=", ctx_.log.errorCount()));
      if (active_ && !Broodwar->isReplay())
        writeResultRow(isWinner);
    }
    catch (const std::exception& e)
    {
      ctx_.log.error("Bot", concat("onEnd failed: ", e.what()));
    }
    ctx_.log.close();
  }

  void JaeTankBot::writeResultRow(bool isWinner)
  {
    const std::string path = (std::filesystem::path(ctx_.settings.logDirectory) / "results.csv").string();
    ensureDirectory(ctx_.settings.logDirectory);
    const bool exists = std::filesystem::exists(std::filesystem::path(path));
    std::ofstream out(path, std::ios::app);
    if (!out)
    {
      ctx_.log.warn("Bot", concat("could not write ", path));
      return;
    }
    if (!exists)
      out << metricsCsvHeader() << '\n';
    std::string mapName = Broodwar->mapFileName();
    for (char& c : mapName)
      if (c == ',')
        c = ';';
    const std::string prefix = concat(timestampForFileName(), ',', mapName, ',', ctx_.config.level, ',',
                                      info_ ? info_->enemyRace().c_str() : "Unknown", ',', isWinner ? "win" : "loss", ',',
                                      Broodwar->getFrameCount(), ',', formatGameTime(Broodwar->getFrameCount()));
    out << metricsCsvRow(prefix, ctx_.metrics) << '\n';
    ctx_.log.info("Bot", concat("result appended to ", path));
  }

  void JaeTankBot::onSendText(std::string text)
  {
    // Only reachable when enable_user_input is on: forward what the user typed.
    Broodwar->sendText("%s", text.c_str());
  }

  void JaeTankBot::onPlayerLeft(Player player)
  {
    if (player)
      ctx_.log.info("Bot", concat("player left: ", player->getName()));
  }

  void JaeTankBot::onUnitDiscover(Unit unit)
  {
    if (active_)
      guarded("Info", [&] { info_->onUnitSeen(unit); });
  }

  void JaeTankBot::onUnitShow(Unit unit)
  {
    if (active_)
      guarded("Info", [&] { info_->onUnitSeen(unit); });
  }

  void JaeTankBot::onUnitMorph(Unit unit)
  {
    if (active_)
      guarded("Info", [&] { info_->onUnitSeen(unit); });
  }

  void JaeTankBot::onUnitRenegade(Unit unit)
  {
    if (active_)
      guarded("Info", [&] { info_->onUnitSeen(unit); });
  }

  void JaeTankBot::onUnitCreate(Unit unit)
  {
    if (active_ && unit && unit->getPlayer() == Broodwar->self())
      ctx_.log.debug("Units", concat("created ", unit->getType().c_str(), "#", unit->getID()));
  }

  void JaeTankBot::onUnitComplete(Unit unit)
  {
    if (!active_ || !unit)
      return;
    guarded("Workers", [&] { workers_->onUnitComplete(unit); });
    if (unit->getPlayer() == Broodwar->self())
      ctx_.log.debug("Units", concat("completed ", unit->getType().c_str(), "#", unit->getID()));
  }

  void JaeTankBot::onUnitDestroy(Unit unit)
  {
    if (!active_ || !unit)
      return;
    guarded("Events", [&] {
      if (unit->getPlayer() == Broodwar->self())
      {
        ++ctx_.metrics.ownUnitsLost;
        if (unit->getType().isBuilding())
          ctx_.log.warn("Units", concat("lost building ", unit->getType().c_str(), "#", unit->getID()));
        // A Command Center lost away from the main (finished or not): the planner does
        // not send the next one into the same Tanks right away.
        if (unit->getType().isResourceDepot() && unit->getDistance(ctx_.map->mainPosition()) > 12 * 32)
          buildings_->avoidSite(unit->getPosition());
        else
          ctx_.log.debug("Units", concat("lost ", unit->getType().c_str(), "#", unit->getID()));
      }
      commander_->forget(unit);
      workers_->onUnitDestroy(unit);
      scout_->onUnitDestroy(unit);
      combat_->onUnitDestroy(unit);
      info_->onUnitDestroy(unit);
    });
  }
}
