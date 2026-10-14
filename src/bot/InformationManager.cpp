#include "InformationManager.h"

#include "BwapiUtil.h"
#include "Planning.h"

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kUnitMemoryFrames = 24 * 180; // forget unseen mobile units after 3 minutes

    bool ignoredType(UnitType t)
    {
      return t == UnitTypes::Zerg_Larva || t == UnitTypes::Zerg_Egg || t == UnitTypes::Zerg_Lurker_Egg ||
             t == UnitTypes::Protoss_Interceptor || t == UnitTypes::Protoss_Scarab ||
             t == UnitTypes::Terran_Vulture_Spider_Mine;
    }

    std::pair<int, int> key(TilePosition t) { return { t.x, t.y }; }
  }

  void InformationManager::onStart()
  {
    for (TilePosition t : Broodwar->getStartLocations())
      if (t != Broodwar->self()->getStartLocation())
        starts_[key(t)] = StartStatus::Unknown;

    // The race picked in the lobby is public; "Random" stays unknown until we see a unit.
    if (Broodwar->enemies().size() == 1)
    {
      Race r = Broodwar->enemy()->getRace();
      if (r == Races::Terran || r == Races::Protoss || r == Races::Zerg)
        enemyRace_ = r;
    }
    ctx_.log.info("Info", concat("enemies=", Broodwar->enemies().size(), " enemyRace=", enemyRace_.c_str(),
                                 " unknownStartLocations=", starts_.size()));
  }

  void InformationManager::onUnitSeen(Unit unit)
  {
    if (!isEnemyUnit(unit) || !unit->isVisible())
      return;
    const UnitType type = unit->getType();
    if (ignoredType(type))
      return;
    if (enemyRace_ == Races::Unknown || enemyRace_ == Races::Random)
    {
      Race r = type.getRace();
      if (r == Races::Terran || r == Races::Protoss || r == Races::Zerg)
      {
        enemyRace_ = r;
        ctx_.log.info("Info", concat("enemy race identified from a ", type.c_str(), ": ", r.c_str()));
      }
    }
    EnemyRecord rec{ unit, type, unit->getPosition(), unit->getTilePosition(), ctx_.frame };
    const int id = unit->getID();
    if (type.isBuilding())
    {
      units_.erase(id);
      if (buildings_.find(id) == buildings_.end())
        ctx_.log.info("Info", concat("enemy building seen: ", type.c_str(), " at ", rec.tile.x, ",", rec.tile.y));
      buildings_[id] = rec;
    }
    else
    {
      buildings_.erase(id);
      units_[id] = rec;
    }
  }

  void InformationManager::onUnitDestroy(Unit unit)
  {
    if (!unit)
      return;
    const int id = unit->getID();
    if (buildings_.erase(id) || units_.erase(id))
      ++ctx_.metrics.enemyUnitsKilled;
  }

  void InformationManager::update()
  {
    for (Player enemy : Broodwar->enemies())
      for (Unit u : enemy->getUnits())
        onUnitSeen(u);

    // Buildings: if we can see the spot and the building is not there, it is gone.
    for (auto it = buildings_.begin(); it != buildings_.end();)
    {
      const EnemyRecord& r = it->second;
      const bool spotVisible = Broodwar->isVisible(r.tile);
      const bool stillThere = r.unit && r.unit->exists() && r.unit->isVisible();
      if (spotVisible && !stillThere)
      {
        ctx_.log.debug("Info", concat("enemy ", r.type.c_str(), " no longer at ", r.tile.x, ",", r.tile.y));
        it = buildings_.erase(it);
      }
      else
        ++it;
    }
    // Mobile units: keep them for the strength estimate until they are old.
    for (auto it = units_.begin(); it != units_.end();)
    {
      if (ctx_.frame - it->second.lastSeenFrame > kUnitMemoryFrames)
        it = units_.erase(it);
      else
        ++it;
    }
    updateStartLocations();
  }

  void InformationManager::updateStartLocations()
  {
    for (auto& [k, status] : starts_)
    {
      const TilePosition tile(k.first, k.second);
      const Position center = buildingCenter(tile, UnitTypes::Terran_Command_Center);
      bool enemyBuildingNear = false;
      for (const auto& [id, r] : buildings_)
        if (r.position.getApproxDistance(center) < 12 * 32)
          enemyBuildingNear = true;

      if (enemyBuildingNear && status != StartStatus::Enemy)
      {
        status = StartStatus::Enemy;
        if (ctx_.metrics.enemyMainFoundFrame < 0)
          ctx_.metrics.enemyMainFoundFrame = ctx_.frame;
        ctx_.log.info("Info", concat("enemy base found at start location ", tile.x, ",", tile.y));
      }
      else if (!enemyBuildingNear && Broodwar->isVisible(tile) && Broodwar->isVisible(tile + TilePosition(3, 2)))
      {
        if (status != StartStatus::Empty)
          ctx_.log.info("Info", concat("start location ", tile.x, ",", tile.y,
                                       status == StartStatus::Enemy ? " cleared (no enemy buildings left)" : " is empty"));
        status = StartStatus::Empty;
      }
    }
    if (!loggedInference_ && ctx_.config.inferEnemyStartByElimination && !enemyMainConfirmed())
    {
      auto unexplored = unexploredStartLocations();
      if (unexplored.size() == 1 && starts_.size() > 1)
      {
        loggedInference_ = true;
        ctx_.log.info("Info", concat("enemy main inferred by elimination at ", unexplored[0].x, ",", unexplored[0].y));
      }
    }
  }

  std::optional<Position> InformationManager::enemyMain() const
  {
    for (const auto& [k, status] : starts_)
      if (status == StartStatus::Enemy)
        return buildingCenter(TilePosition(k.first, k.second), UnitTypes::Terran_Command_Center);
    if (ctx_.config.inferEnemyStartByElimination)
    {
      auto unexplored = unexploredStartLocations();
      if (unexplored.size() == 1)
        return buildingCenter(unexplored[0], UnitTypes::Terran_Command_Center);
    }
    return std::nullopt;
  }

  bool InformationManager::enemyMainConfirmed() const
  {
    for (const auto& [k, status] : starts_)
      if (status == StartStatus::Enemy)
        return true;
    return false;
  }

  std::vector<TilePosition> InformationManager::unexploredStartLocations() const
  {
    std::vector<TilePosition> result;
    for (const auto& [k, status] : starts_)
      if (status == StartStatus::Unknown)
        result.emplace_back(k.first, k.second);
    return result;
  }

  StartStatus InformationManager::startStatus(TilePosition start) const
  {
    auto it = starts_.find(key(start));
    return it == starts_.end() ? StartStatus::Empty : it->second;
  }

  int InformationManager::recentMeleeUnits() const
  {
    int n = 0;
    for (const auto& [id, r] : units_)
      if (isMeleeType(r.type))
        ++n;
    return n;
  }

  double InformationManager::knownEnemyArmyStrength() const
  {
    std::vector<double> s;
    for (const auto& [id, r] : units_)
      if (!r.type.isWorker())
        s.push_back(typeStrength(r.type));
    for (const auto& [id, r] : buildings_)
      if (canAttackGround(r.type))
        s.push_back(typeStrength(r.type));
    return combineStrength(s);
  }

  EnemyMix InformationManager::enemyMix() const
  {
    EnemyMix mix;
    for (const auto& [id, r] : units_)
      addToEnemyMix(mix, r.type);
    return mix;
  }

  int InformationManager::knownEnemyCombatUnits() const
  {
    int n = 0;
    for (const auto& [id, r] : units_)
      if (!r.type.isWorker() && canAttackGround(r.type))
        ++n;
    return n;
  }

  std::vector<Unit> InformationManager::visibleEnemiesNear(Position p, int radius) const
  {
    std::vector<Unit> result;
    for (Player enemy : Broodwar->enemies())
      for (Unit u : enemy->getUnits())
        if (u->exists() && u->isVisible() && !ignoredType(u->getType()) && u->getPosition().getApproxDistance(p) <= radius)
          result.push_back(u);
    return result;
  }
}
