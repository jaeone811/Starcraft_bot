#pragma once
// What we know about the enemy -- only from units we have actually seen.
// BWAPI hides units in the fog of war by default; this module remembers the last
// seen position of enemy buildings and units and forgets them when we look again
// and they are gone (or, for mobile units, after a while).

#include "BotContext.h"

#include <BWAPI.h>

#include <map>
#include <optional>
#include <vector>

namespace bb
{
  struct EnemyRecord
  {
    BWAPI::Unit unit = nullptr;
    BWAPI::UnitType type;
    BWAPI::Position position;
    BWAPI::TilePosition tile;
    int lastSeenFrame = 0;
  };

  enum class StartStatus { Unknown, Empty, Enemy };

  class InformationManager
  {
  public:
    explicit InformationManager(BotContext& ctx) : ctx_(ctx) {}

    void onStart();
    void update();
    void onUnitSeen(BWAPI::Unit unit);     // discover / show / morph / renegade
    void onUnitDestroy(BWAPI::Unit unit);

    const std::map<int, EnemyRecord>& enemyBuildings() const { return buildings_; }
    const std::map<int, EnemyRecord>& enemyUnits() const { return units_; }

    // Enemy main base: a start location where we saw an enemy building, or (if the
    // difficulty allows inference) the only start location we have not checked yet.
    std::optional<BWAPI::Position> enemyMain() const;
    bool enemyMainConfirmed() const;
    std::vector<BWAPI::TilePosition> unexploredStartLocations() const;
    StartStatus startStatus(BWAPI::TilePosition start) const;

    BWAPI::Race enemyRace() const { return enemyRace_; }
    int recentMeleeUnits() const;          // remembered melee units (zealots, lings, ...)
    double knownEnemyArmyStrength() const; // remembered non-worker combat units + static defense
    int knownEnemyCombatUnits() const;

    // Visible enemy units within `radius` pixels of `p` (only units we can currently see).
    std::vector<BWAPI::Unit> visibleEnemiesNear(BWAPI::Position p, int radius) const;

  private:
    void updateStartLocations();

    BotContext& ctx_;
    std::map<int, EnemyRecord> buildings_;
    std::map<int, EnemyRecord> units_;
    std::map<std::pair<int, int>, StartStatus> starts_;
    BWAPI::Race enemyRace_ = BWAPI::Races::Unknown;
    bool loggedInference_ = false;
  };
}
