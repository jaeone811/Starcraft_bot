#include "BuildingManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "MapInfo.h"
#include "WorkerManager.h"

#include <algorithm>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kMaxPlacementAttempts = 6;
    constexpr int kFailureCooldownFrames = 24 * 20;
    constexpr int kNoBuilderTimeout = 24 * 30;
    constexpr int kAbandonedFrames = 48;

    bool rectsOverlap(TilePosition a, UnitType ta, TilePosition b, UnitType tb, int margin)
    {
      return a.x - margin < b.x + tb.tileWidth() && b.x - margin < a.x + ta.tileWidth() &&
             a.y - margin < b.y + tb.tileHeight() && b.y - margin < a.y + ta.tileHeight();
    }
  }

  bool BuildingManager::onCooldown(UnitType type) const
  {
    auto it = cooldownUntil_.find(type.getID());
    return it != cooldownUntil_.end() && ctx_.frame < it->second;
  }

  bool BuildingManager::request(UnitType type, TilePosition desired)
  {
    if (onCooldown(type))
      return false;
    Task t;
    t.id = nextId_++;
    t.type = type;
    t.desired = desired;
    t.stateFrame = ctx_.frame;
    tasks_.push_back(t);
    ctx_.log.info("Build", concat("task #", t.id, " queued: ", type.c_str(),
                                  desired.isValid() ? concat(" at ", desired.x, ",", desired.y) : std::string()));
    return true;
  }

  int BuildingManager::notStarted(UnitType type) const
  {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [type](const Task& t) {
      return t.type == type && t.state != State::UnderConstruction;
    }));
  }

  int BuildingManager::inProgress(UnitType type) const
  {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [type](const Task& t) { return t.type == type; }));
  }

  int BuildingManager::reservedMinerals() const
  {
    int sum = 0;
    for (const auto& t : tasks_)
      if (t.state != State::UnderConstruction)
        sum += t.type.mineralPrice();
    return sum;
  }

  int BuildingManager::reservedGas() const
  {
    int sum = 0;
    for (const auto& t : tasks_)
      if (t.state != State::UnderConstruction)
        sum += t.type.gasPrice();
    return sum;
  }

  int BuildingManager::pendingSupply() const
  {
    int sum = 0;
    for (const auto& t : tasks_)
      sum += t.type.supplyProvided();
    return sum;
  }

  bool BuildingManager::overlapsOtherTask(const Task& task, TilePosition tile) const
  {
    for (const auto& other : tasks_)
    {
      if (other.id == task.id || other.state == State::UnderConstruction || !other.tile.isValid() ||
          other.tile == TilePositions::None)
        continue;
      if (rectsOverlap(tile, task.type, other.tile, other.type, 1))
        return true;
    }
    // Keep the natural expansion spot free for the Command Center.
    if (task.type != UnitTypes::Terran_Command_Center)
      if (const BaseLocation* nat = ctx_.map->natural())
        if (rectsOverlap(tile, task.type, nat->depotTile, UnitTypes::Terran_Command_Center, 2))
          return true;
    return false;
  }

  TilePosition BuildingManager::findLocation(const Task& task) const
  {
    const UnitType type = task.type;

    if (type.isRefinery())
    {
      for (Unit depot : ctx_.workers->depots())
        for (Unit g : Broodwar->getGeysers())
        {
          if (g->getType() != UnitTypes::Resource_Vespene_Geyser || g->getDistance(depot) > 10 * 32)
            continue;
          if (!overlapsOtherTask(task, g->getTilePosition()))
            return g->getTilePosition();
        }
      return TilePositions::None;
    }

    if (type.isResourceDepot())
    {
      TilePosition spot = task.desired.isValid() && task.desired != TilePositions::None ? task.desired : ctx_.map->mainTile();
      return Broodwar->canBuildHere(spot, type) ? spot : TilePositions::None;
    }

    // Everything else goes around the main base. Each failed attempt shifts the
    // search anchor so a blocked area is not tried forever.
    static const TilePosition offsets[] = { {0, 0}, {6, 0}, {-6, 0}, {0, 6}, {0, -6}, {8, 8}, {-8, -8}, {8, -8}, {-8, 8} };
    const int n = static_cast<int>(sizeof(offsets) / sizeof(offsets[0]));
    TilePosition anchor = ctx_.map->mainTile();
    if (!ctx_.workers->depots().empty())
      anchor = ctx_.workers->depots().front()->getTilePosition();
    for (int i = 0; i < n; ++i)
    {
      TilePosition desired = anchor + offsets[(task.placementAttempts + i) % n];
      desired.makeValid();
      TilePosition tile = Broodwar->getBuildLocation(type, desired, 40);
      if (tile.isValid() && tile != TilePositions::None && !overlapsOtherTask(task, tile))
        return tile;
    }
    return TilePositions::None;
  }

  void BuildingManager::releaseBuilder(Task& task)
  {
    if (task.builder)
      ctx_.workers->release(task.builder);
    task.builder = nullptr;
  }

  void BuildingManager::fail(Task& task, const char* reason)
  {
    ++ctx_.metrics.buildFailures;
    cooldownUntil_[task.type.getID()] = ctx_.frame + kFailureCooldownFrames;
    ctx_.log.warn("Build", concat("task #", task.id, " ", task.type.c_str(), " failed: ", reason,
                                  " (will be retried after a cooldown)"));
    releaseBuilder(task);
  }

  void BuildingManager::startConstructionCheck(Task& task)
  {
    Unit found = nullptr;
    if (isValid(task.builder) && isValid(task.builder->getBuildUnit()) && task.builder->getBuildUnit()->getType() == task.type)
      found = task.builder->getBuildUnit();
    if (!found)
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType() == task.type && !u->isCompleted() && u->getTilePosition() == task.tile)
          found = u;
    if (found)
    {
      task.building = found;
      task.state = State::UnderConstruction;
      task.stateFrame = task.lastProgressFrame = ctx_.frame;
      ++ctx_.metrics.buildingsStarted;
      ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), " started at ", task.tile.x, ",", task.tile.y));
    }
  }

  void BuildingManager::updateTask(Task& task, bool& remove)
  {
    switch (task.state)
    {
    case State::WaitingForBuilder:
    {
      if (!task.tile.isValid() || task.tile == TilePositions::None)
      {
        task.tile = findLocation(task);
        if (task.tile == TilePositions::None || !task.tile.isValid())
        {
          if (++task.placementAttempts > kMaxPlacementAttempts)
          {
            fail(task, "no valid location");
            remove = true;
          }
          return;
        }
      }
      if (!isValid(task.builder))
      {
        task.builder = ctx_.workers->acquireWorker(buildingCenter(task.tile, task.type), WorkerRole::Build);
        if (!task.builder)
        {
          if (ctx_.frame - task.stateFrame > kNoBuilderTimeout)
          {
            fail(task, "no worker available");
            remove = true;
          }
          return;
        }
      }
      if (ctx_.commander->build(task.builder, task.type, task.tile))
      {
        task.state = State::Assigned;
        task.stateFrame = ctx_.frame;
        ctx_.log.debug("Build", concat("task #", task.id, " SCV#", task.builder->getID(), " sent to ", task.tile.x, ",", task.tile.y));
      }
      else if (!ctx_.commander->lastWasSuppressed())
      {
        // The spot is not valid any more; try somewhere else.
        task.tile = TilePositions::None;
        ++task.placementAttempts;
        ++ctx_.metrics.buildRetries;
      }
      return;
    }
    case State::Assigned:
    {
      startConstructionCheck(task);
      if (task.state == State::UnderConstruction)
        return;
      if (!isValid(task.builder))
      {
        ctx_.log.info("Build", concat("task #", task.id, " lost its builder; choosing another"));
        task.builder = nullptr;
        task.state = State::WaitingForBuilder;
        task.stateFrame = ctx_.frame;
        ++ctx_.metrics.buildRetries;
        return;
      }
      // Travel time plus a margin; SCVs move ~5 pixels per frame.
      const int timeout = 24 * 20 + task.builder->getDistance(buildingCenter(task.tile, task.type)) / 4;
      if (ctx_.frame - task.stateFrame > timeout + 24 * 30)
      {
        ++ctx_.metrics.buildRetries;
        ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), " did not start at ", task.tile.x, ",",
                                      task.tile.y, "; trying another spot"));
        task.tile = TilePositions::None;
        task.state = State::WaitingForBuilder;
        task.stateFrame = ctx_.frame;
        if (++task.placementAttempts > kMaxPlacementAttempts)
        {
          fail(task, "construction never started");
          remove = true;
        }
        return;
      }
      const Order order = task.builder->getOrder();
      if (order != Orders::PlaceBuilding && order != Orders::ConstructingBuilding && order != Orders::Move)
      {
        // The SCV got distracted (e.g. returned cargo). Repeat the order, but only
        // after the Commander's cooldown, and pick a new spot if it is rejected.
        if (!ctx_.commander->build(task.builder, task.type, task.tile) && !ctx_.commander->lastWasSuppressed())
        {
          task.tile = TilePositions::None;
          task.state = State::WaitingForBuilder;
          task.stateFrame = ctx_.frame;
          ++task.placementAttempts;
          ++ctx_.metrics.buildRetries;
        }
      }
      return;
    }
    case State::UnderConstruction:
    {
      if (!isValid(task.building))
      {
        ctx_.log.warn("Build", concat("task #", task.id, " ", task.type.c_str(), " was destroyed while under construction"));
        releaseBuilder(task);
        remove = true;
        return;
      }
      if (task.building->isCompleted())
      {
        ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), " completed"));
        releaseBuilder(task);
        remove = true;
        return;
      }
      if (task.building->getBuildUnit())
      {
        task.lastProgressFrame = ctx_.frame;
        return;
      }
      if (ctx_.frame - task.lastProgressFrame < kAbandonedFrames)
        return;
      // Nobody is building it (builder died or wandered off): send an SCV to resume.
      if (!isValid(task.builder) || task.builder->isConstructing())
      {
        releaseBuilder(task);
        task.builder = ctx_.workers->acquireWorker(task.building->getPosition(), WorkerRole::Build);
        if (task.builder)
          ctx_.log.info("Build", concat("task #", task.id, " resuming construction with SCV#", task.builder->getID()));
      }
      if (task.builder)
        ctx_.commander->resumeConstruction(task.builder, task.building);
      return;
    }
    }
  }

  void BuildingManager::update()
  {
    for (size_t i = 0; i < tasks_.size();)
    {
      bool remove = false;
      updateTask(tasks_[i], remove);
      if (remove)
        tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
      else
        ++i;
    }
  }
}
