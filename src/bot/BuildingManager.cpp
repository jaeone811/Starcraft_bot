#include "BuildingManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
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
    constexpr int kMaxBuildReissues = 4;           // build orders re-sent to an SCV that did not start
    constexpr int kBlockedTileFrames = 24 * 180;   // a spot where construction did not start is avoided this long
    constexpr int kExploreTimeout = 24 * 120;      // walking to an unexplored spot
    constexpr int kDangerousSiteFrames = 24 * 180; // a site where builders were lost is avoided this long
    constexpr int kFarSiteTiles = 15;              // a site this far from every Command Center of ours is "far"

    bool rectsOverlap(TilePosition a, UnitType ta, TilePosition b, UnitType tb, int margin)
    {
      return a.x - margin < b.x + tb.tileWidth() && b.x - margin < a.x + ta.tileWidth() &&
             a.y - margin < b.y + tb.tileHeight() && b.y - margin < a.y + ta.tileHeight();
    }

    bool footprintExplored(TilePosition t, UnitType type)
    {
      for (int x = t.x; x < t.x + type.tileWidth(); ++x)
        for (int y = t.y; y < t.y + type.tileHeight(); ++y)
          if (!Broodwar->isExplored(x, y))
            return false;
      return true;
    }

    // Buildings whose units walk out of their sides.
    bool producesGroundUnits(UnitType t)
    {
      return t == UnitTypes::Terran_Barracks || t == UnitTypes::Terran_Factory;
    }

    // Tile rectangle of a building, with the add-on columns on its right side.
    struct TileRect { int x, y, w, h; };
    TileRect footprint(TilePosition t, UnitType type)
    {
      return { t.x, t.y, type.tileWidth() + (type.canBuildAddon() ? 2 : 0), type.tileHeight() };
    }

    // A Factory's (or Starport's) add-on column is buildable: Broodwar->canBuildHere
    // does not look at it, and a Machine Shop once failed with Unbuildable_Location.
    bool addonSpaceFree(TilePosition t, UnitType type)
    {
      if (!type.canBuildAddon())
        return true;
      for (int x = t.x + type.tileWidth(); x < t.x + type.tileWidth() + 2; ++x)
        for (int y = t.y + type.tileHeight() - 2; y < t.y + type.tileHeight(); ++y)
          if (!TilePosition(x, y).isValid() || !Broodwar->isBuildable(x, y, true))
            return false;
      return true;
    }

    bool rectsOverlap(const TileRect& a, const TileRect& b, int margin)
    {
      return a.x - margin < b.x + b.w && b.x - margin < a.x + a.w && a.y - margin < b.y + b.h && b.y - margin < a.y + a.h;
    }

    // The add-on spot of one of our Command Centers/Factories/Starports without its
    // add-on yet: nothing else goes there (the Comsat failed twice with
    // Unbuildable_Location because a depot stood on its spot).
    bool takesAddonSpot(TilePosition t, UnitType type)
    {
      const TileRect r{ t.x, t.y, type.tileWidth(), type.tileHeight() };
      for (Unit u : Broodwar->self()->getUnits())
      {
        const UnitType ut = u->getType();
        if (!ut.canBuildAddon() || u->getAddon() || u->isLifted())
          continue;
        const TilePosition p = u->getTilePosition();
        if (rectsOverlap(r, TileRect{ p.x + ut.tileWidth(), p.y + ut.tileHeight() - 2, 2, 2 }, 0))
          return true;
      }
      return false;
    }

    // Every tile of the one-tile ring around `r` lies inside the map, is fully
    // walkable and holds no building or resource. A unit that comes out next to the
    // building can then walk around it to any side.
    bool ringOpen(const TileRect& r)
    {
      for (int x = r.x - 1; x <= r.x + r.w; ++x)
        for (int y = r.y - 1; y <= r.y + r.h; ++y)
        {
          if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            continue;
          const TilePosition t(x, y);
          if (!t.isValid())
            return false;
          for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
              if (!Broodwar->isWalkable(x * 4 + i, y * 4 + j))
                return false;
          for (Unit u : Broodwar->getUnitsOnTile(t))
            if ((u->getType().isBuilding() && !u->isFlying()) || u->getType().isResourceContainer())
              return false;
        }
      return true;
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

  int BuildingManager::committedMinerals() const
  {
    int sum = 0;
    for (const auto& t : tasks_)
      if (t.state == State::Assigned)
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
    // A Command Center takes three times as long as a depot: its supply only counts
    // once it will finish before a depot started now would.
    const int depotTime = UnitTypes::Terran_Supply_Depot.buildTime();
    int sum = 0;
    for (const auto& t : tasks_)
    {
      if (t.type.isResourceDepot() &&
          !(t.state == State::UnderConstruction && isValid(t.building) && t.building->getRemainingBuildTime() <= depotTime))
        continue;
      sum += t.type.supplyProvided();
    }
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

  bool BuildingManager::enemiesAtSite(Position p) const
  {
    for (Unit e : ctx_.info->visibleEnemiesNear(p, 10 * 32))
      if (!e->getType().isWorker() && !e->getType().isBuilding() && canAttackGround(e->getType()))
        return true;
    return false;
  }

  bool BuildingManager::waitForSafeSite(Task& task, Position site, bool& remove)
  {
    // No SCV walks into enemy fighting units at the building site: the third Command
    // Center lost three builders that way and every replacement walked the same way.
    // The task waits; after 90 s of enemies there it is given up, so the planner can
    // choose again (another base, or later).
    if (!enemiesAtSite(site))
    {
      task.dangerSince = -1;
      return false;
    }
    if (task.dangerSince < 0)
    {
      task.dangerSince = ctx_.frame;
      ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), ": enemies at the site, no SCV sent until they are gone"));
    }
    if (isValid(task.builder) && !task.builder->isConstructing())
      releaseBuilder(task);
    if (task.state != State::UnderConstruction && ctx_.frame - task.dangerSince > 24 * 90)
    {
      fail(task, "enemies stayed at the site");
      remove = true;
    }
    return true;
  }

  bool BuildingManager::siteDangerous(Position p) const
  {
    for (const auto& [site, until] : dangerousSites_)
      if (ctx_.frame < until && site.getApproxDistance(p) < 10 * 32)
        return true;
    return false;
  }

  bool BuildingManager::farFromOurBases(Position site) const
  {
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType().isResourceDepot() && u->isCompleted() && u->getDistance(site) <= kFarSiteTiles * 32)
        return false;
    return true;
  }

  bool BuildingManager::builderTroubleGivesUp(Task& task, Position site, bool& remove)
  {
    // A far site where two builders were killed or attacked on the way is given up
    // and avoided for 3 minutes: 25 SCVs walked one after another to a center base
    // next to the enemy's third and died on the way.
    if (task.builderTrouble < 2)
      return false;
    dangerousSites_.push_back({ site, ctx_.frame + kDangerousSiteFrames });
    ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), ": ", task.builderTrouble,
                                  " builders lost or attacked on the way; site ", site.x / 32, ",", site.y / 32,
                                  " avoided for 3 minutes"));
    fail(task, "builders lost on the way");
    remove = true;
    return true;
  }

  bool BuildingManager::nearArmySpot(const Task& task, TilePosition tile) const
  {
    // The Bunker, the spot right behind it where the army waits and the rally point
    // stay clear: Barracks and Factories built after the Bunker went right behind it
    // (the Bunker in front of their doors, the army squeezed between them), and a
    // depot did not start for 1.5 minutes where the army stood.
    const UnitType type = task.type;
    if (type == UnitTypes::Terran_Bunker || type == UnitTypes::Terran_Missile_Turret || type.isResourceDepot() || type.isRefinery())
      return false;
    const TileRect r = footprint(tile, type);
    bool bunkerStands = false;
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType() == UnitTypes::Terran_Bunker)
      {
        bunkerStands = true;
        if (rectsOverlap(r, footprint(u->getTilePosition(), u->getType()), 3))
          return true;
      }
    auto near = [&](Position p, int margin) {
      if (!p.isValid() || p == Positions::None)
        return false;
      const TilePosition t(p);
      return rectsOverlap(r, TileRect{ t.x, t.y, 1, 1 }, margin);
    };
    if (!bunkerStands && near(ctx_.map->bunkerSpot(), 3))
      return true;
    return near(ctx_.map->bunkerRally(), 2) || near(ctx_.map->rallyPoint(), 2);
  }

  bool BuildingManager::recentlyBlocked(TilePosition tile) const
  {
    for (const auto& [t, until] : blockedTiles_)
      if (t == tile && ctx_.frame < until)
        return true;
    return false;
  }

  bool BuildingManager::blocksRamp(const Task& task, TilePosition tile) const
  {
    // Nothing but the Bunker within 3 tiles of the top of the main's ramp: the way
    // out stays open and nothing stands in front of the Bunker.
    const TilePosition choke = ctx_.map->mainChoke();
    if (task.type == UnitTypes::Terran_Bunker || !choke.isValid() || choke == TilePositions::None)
      return false;
    const TileRect r = footprint(tile, task.type);
    return rectsOverlap(r, TileRect{ choke.x, choke.y, 1, 1 }, 3);
  }

  bool BuildingManager::keepsExitsOpen(const Task& task, TilePosition tile, bool behindEntrance) const
  {
    // A Barracks or Factory (with its add-on) keeps a free one-tile ring, and nothing
    // else is built in the ring of one. Without it a Factory built next to the map
    // edge on Boxer let its first tank out into a pocket it never left.
    const TileRect r = footprint(tile, task.type);
    if (producesGroundUnits(task.type) && !ringOpen(r))
      return false;
    // Behind the entrance (closer to the main than it is), so the Bunker at the
    // entrance covers them; one game had Barracks and Factories in front of it.
    const int entrance = ctx_.map->mainEntranceDistance();
    if (behindEntrance && entrance > 0 && !task.type.isResourceDepot() && task.type != UnitTypes::Terran_Missile_Turret &&
        TilePosition(r.x + r.w / 2, r.y + r.h / 2).getApproxDistance(ctx_.map->mainTile()) > entrance - 2)
      return false;
    for (Unit u : Broodwar->self()->getUnits())
      if (producesGroundUnits(u->getType()) && !u->isLifted() && rectsOverlap(r, footprint(u->getTilePosition(), u->getType()), 1))
        return false;
    for (const auto& other : tasks_)
      if (other.id != task.id && other.state != State::UnderConstruction && other.tile.isValid() &&
          other.tile != TilePositions::None && (producesGroundUnits(other.type) || producesGroundUnits(task.type)) &&
          rectsOverlap(r, footprint(other.tile, other.type), 1))
        return false;
    return true;
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
    // search anchor so a blocked area is not tried forever. The first pass also
    // wants free exits around production buildings (keepsExitsOpen); when no such
    // spot is found, the second pass takes any legal one rather than not building.
    static const TilePosition offsets[] = { {0, 0}, {6, 0}, {-6, 0}, {0, 6}, {0, -6}, {8, 8}, {-8, -8}, {8, -8}, {-8, 8},
                                            {12, 0}, {-12, 0}, {0, 12}, {0, -12}, {12, 12}, {-12, -12}, {12, -12}, {-12, 12} };
    const int n = static_cast<int>(sizeof(offsets) / sizeof(offsets[0]));
    TilePosition anchor = ctx_.map->mainTile();
    if (!ctx_.workers->depots().empty())
      anchor = ctx_.workers->depots().front()->getTilePosition();
    // A requested spot (e.g. a Missile Turret by a Command Center) is searched only
    // around that spot. Falling back to the main would put the building where it is
    // not needed: a turret meant for the natural once ended up in the main 13 times.
    if (task.desired.isValid() && task.desired != TilePositions::None)
    {
      for (bool spaced : { true, false })
      {
        TilePosition tile = Broodwar->getBuildLocation(type, task.desired, 12);
        if (tile.isValid() && tile != TilePositions::None && !overlapsOtherTask(task, tile) && !recentlyBlocked(tile) &&
            tile.getApproxDistance(task.desired) <= 12 && !blocksRamp(task, tile) && (!spaced || keepsExitsOpen(task, tile, true)))
          return tile;
      }
      return TilePositions::None;
    }
    // Every legal tile around the main, nearest to the Command Center first. 17
    // sampled spots found nothing with free exits once the main filled up, and the
    // fallback then put a Barracks where its first marine stayed boxed in all game.
    // Pass 1 wants everything; pass 2 lets a building stand outside the entrance but
    // keeps the ring around Barracks/Factories open; pass 3 takes any legal tile.
    const TilePosition centre = ctx_.map->mainTile();
    const int radius = std::max(ctx_.map->mainEntranceDistance(), 16) + 4;
    std::vector<TilePosition> legal;
    for (int dy = -radius; dy <= radius; ++dy)
      for (int dx = -radius; dx <= radius; ++dx)
      {
        const TilePosition t(centre.x + dx, centre.y + dy);
        if (dx * dx + dy * dy > radius * radius || !t.isValid() || !Broodwar->canBuildHere(t, type) || !addonSpaceFree(t, type) ||
            overlapsOtherTask(task, t) || recentlyBlocked(t) || blocksRamp(task, t) || takesAddonSpot(t, type) ||
            nearArmySpot(task, t))
          continue;
        legal.push_back(t);
      }
    auto distance2 = [&](TilePosition t) {
      const int x = t.x + type.tileWidth() / 2 - anchor.x, y = t.y + type.tileHeight() / 2 - anchor.y;
      return x * x + y * y;
    };
    std::sort(legal.begin(), legal.end(), [&](TilePosition a, TilePosition b) {
      const int da = distance2(a), db = distance2(b);
      return da != db ? da < db : (a.y != b.y ? a.y < b.y : a.x < b.x);
    });
    // Pass 2 stays on the main's plateau (its ground height): the next spot by
    // distance can be down the ramp, in front of the Bunker.
    const int mainHeight = Broodwar->getGroundHeight(ctx_.map->mainTile());
    for (int pass = 0; pass < 4; ++pass)
      for (TilePosition t : legal)
      {
        if (pass < 3 && !keepsExitsOpen(task, t, pass == 0))
          continue;
        if (pass == 1 && Broodwar->getGroundHeight(t) != mainHeight)
          continue;
        if (pass == 1)
          ctx_.log.info("Build", concat("no spot behind the entrance for ", type.c_str(), ": using ", t.x, ",", t.y, " on the main plateau"));
        if (pass == 2)
          ctx_.log.info("Build", concat("no spot on the main plateau for ", type.c_str(), ": using ", t.x, ",", t.y));
        if (pass == 3)
          ctx_.log.info("Build", concat("no spot with free exits for ", type.c_str(), ": using ", t.x, ",", t.y));
        return t;
      }
    // The main is full: anywhere BWAPI finds, further out.
    for (int i = 0; i < n; ++i)
    {
      TilePosition desired = anchor + offsets[(task.placementAttempts + i) % n];
      desired.makeValid();
      TilePosition tile = Broodwar->getBuildLocation(type, desired, 40);
      if (!tile.isValid() || tile == TilePositions::None || overlapsOtherTask(task, tile) || recentlyBlocked(tile) ||
          blocksRamp(task, tile) || nearArmySpot(task, tile))
        continue;
      ctx_.log.info("Build", concat("main is full for ", type.c_str(), ": using ", tile.x, ",", tile.y));
      return tile;
    }
    return TilePositions::None;
  }

  bool BuildingManager::swapBusyBuilder(Task& task)
  {
    // Unit_Busy is about the SCV, not the spot (one stuck SCV refused six depot orders
    // in a row and the depot failed): take another SCV, keep the spot.
    if (Broodwar->getLastError() != Errors::Unit_Busy || !isValid(task.builder))
      return false;
    ctx_.log.info("Build", concat("task #", task.id, " SCV#", task.builder->getID(), " refuses the build order (Unit_Busy); using another SCV"));
    ctx_.workers->avoidAsBuilder(task.builder, ctx_.frame + 24 * 60);
    releaseBuilder(task);
    ++ctx_.metrics.buildRetries;
    return true;
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
      if (waitForSafeSite(task, buildingCenter(task.tile, task.type), remove))
        return;
      if (ctx_.frame < task.pathDangerUntil)
        return;   // the last builder was attacked on the way
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
      // BWAPI accepts a build order only on explored tiles (the natural is often
      // unexplored at the easy levels). Walk the SCV there first.
      if (!footprintExplored(task.tile, task.type))
      {
        if (ctx_.frame - task.stateFrame > kExploreTimeout)
        {
          fail(task, "spot never got explored");
          remove = true;
          return;
        }
        if (!task.loggedUnexplored)
          ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), " at ", task.tile.x, ",", task.tile.y,
                                        " is unexplored: SCV#", task.builder->getID(), " walks there first"));
        task.loggedUnexplored = true;
        ctx_.commander->move(task.builder, buildingCenter(task.tile, task.type));
        return;
      }
      if (ctx_.commander->build(task.builder, task.type, task.tile))
      {
        task.state = State::Assigned;
        task.stateFrame = ctx_.frame;
        task.reissues = 0;
        ctx_.log.debug("Build", concat("task #", task.id, " SCV#", task.builder->getID(), " sent to ", task.tile.x, ",", task.tile.y));
      }
      else if (!ctx_.commander->lastWasSuppressed() && swapBusyBuilder(task))
        return;
      else if (!ctx_.commander->lastWasSuppressed())
      {
        // The spot is not valid any more; try somewhere else, but not forever
        // (a Command Center spot is fixed, so findLocation returns it again).
        task.tile = TilePositions::None;
        ++ctx_.metrics.buildRetries;
        if (++task.placementAttempts > kMaxPlacementAttempts)
        {
          fail(task, concat("build order rejected: ", Broodwar->getLastError().c_str()).c_str());
          remove = true;
        }
      }
      return;
    }
    case State::Assigned:
    {
      startConstructionCheck(task);
      if (task.state == State::UnderConstruction)
        return;
      if (enemiesAtSite(buildingCenter(task.tile, task.type)) &&
          task.builder->getDistance(buildingCenter(task.tile, task.type)) > 6 * 32)
      {
        // Turn back on the way (not at the site: an SCV placing the building there
        // was released, built it anyway, and was ordered to mine for minutes).
        // Waiting is handled in WaitingForBuilder.
        task.state = State::WaitingForBuilder;
        task.stateFrame = ctx_.frame;
        waitForSafeSite(task, buildingCenter(task.tile, task.type), remove);
        return;
      }
      const Position site = buildingCenter(task.tile, task.type);
      if (!isValid(task.builder))
      {
        ctx_.log.info("Build", concat("task #", task.id, " lost its builder; choosing another"));
        task.builder = nullptr;
        task.state = State::WaitingForBuilder;
        task.stateFrame = ctx_.frame;
        ++ctx_.metrics.buildRetries;
        if (farFromOurBases(site))
        {
          ++task.builderTrouble;
          task.pathDangerUntil = ctx_.frame + 24 * 30;
          builderTroubleGivesUp(task, site, remove);
        }
        return;
      }
      // Attacked on the way to a far site: back home, and the next builder waits
      // 30 s. Near home the army is there to clear the way.
      if (task.builder->isUnderAttack() && task.builder->getDistance(site) > 6 * 32 && farFromOurBases(site))
      {
        ++task.builderTrouble;
        ctx_.log.info("Build", concat("task #", task.id, " SCV#", task.builder->getID(), " attacked on the way to ", task.type.c_str(),
                                      ": turning back"));
        releaseBuilder(task);
        task.state = State::WaitingForBuilder;
        task.stateFrame = ctx_.frame;
        task.pathDangerUntil = ctx_.frame + 24 * 30;
        builderTroubleGivesUp(task, site, remove);
        return;
      }
      // Travel time plus a margin; SCVs move ~5 pixels per frame.
      const int timeout = 24 * 20 + task.builder->getDistance(buildingCenter(task.tile, task.type)) / 4;
      // The SCV keeps arriving without starting (units or a building in the way): give
      // the spot up early instead of waiting for the timeout.
      if (ctx_.frame - task.stateFrame > timeout + 24 * 30 || task.reissues > kMaxBuildReissues)
      {
        ++ctx_.metrics.buildRetries;
        ctx_.log.info("Build", concat("task #", task.id, " ", task.type.c_str(), " did not start at ", task.tile.x, ",",
                                      task.tile.y, " (builder order ", task.builder->getOrder().c_str(), ", ", task.reissues,
                                      " repeated orders); trying another spot"));
        if (!task.type.isResourceDepot() && !task.type.isRefinery())
          blockedTiles_.push_back({ task.tile, ctx_.frame + kBlockedTileFrames });
        task.reissues = 0;
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
        const bool sent = ctx_.commander->build(task.builder, task.type, task.tile);
        if (sent)
          ++task.reissues;
        else if (!ctx_.commander->lastWasSuppressed() && swapBusyBuilder(task))
        {
          task.state = State::WaitingForBuilder;
          task.stateFrame = ctx_.frame;
        }
        else if (!ctx_.commander->lastWasSuppressed())
        {
          task.tile = TilePositions::None;
          task.state = State::WaitingForBuilder;
          task.stateFrame = ctx_.frame;
          ++ctx_.metrics.buildRetries;
          if (++task.placementAttempts > kMaxPlacementAttempts)
          {
            fail(task, concat("build order rejected: ", Broodwar->getLastError().c_str()).c_str());
            remove = true;
          }
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
      // Nobody is building it (builder died or wandered off): send an SCV to resume,
      // but not into the enemies that stand there.
      if (waitForSafeSite(task, task.building->getPosition(), remove))
        return;
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
