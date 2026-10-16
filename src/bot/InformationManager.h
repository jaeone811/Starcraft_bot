#pragma once
// What we know about the enemy -- only from units we have actually seen.
// BWAPI hides units in the fog of war by default; this module remembers the last
// seen position of enemy buildings and units and forgets them when we look again
// and they are gone (or, for mobile units, after a while).

#include "BotContext.h"
#include "Planning.h"

#include <algorithm>
#include <BWAPI.h>

#include <map>
#include <optional>
#include <set>
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
    double knownEnemyAirStrength() const;  // remembered flyers that attack ground (Mutalisks, Wraiths, ...)
    // The largest enemy army strength we have known, fading by half over 5 minutes:
    // an army out of sight is not gone (waves of 15-22 walked into sieged Tanks
    // while the known strength read 0-560).
    double estimatedEnemyArmyStrength() const { return std::max(knownEnemyArmyStrength(), peakEnemyStrength_); }
    double knownEnemyAirFraction() const;  // their share of the remembered army's strength
    int knownEnemyCombatUnits() const;
    EnemyMix enemyMix() const;             // remembered enemy army by counter group
    EnemyMix expectedEnemyMix() const;     // enemyMix plus what the enemy race usually opens with, early on
    EnemyRace enemyRaceKind() const;       // enemyRace() as the core enum (Unknown for Random until seen)
    // An enemy that attacks while hidden (Dark Templar, Lurker, cloaked Ghost/Wraith)
    // has been seen this game: we need detection.
    bool hiddenAttackerSeen() const { return hiddenAttackerFrame_ >= 0; }
    // A Citadel of Adun or Templar Archives (Dark Templar tech) has been seen.
    bool hiddenTechSeen() const { return hiddenTechFrame_ >= 0; }
    // Signs of an early attack seen before 3:00 (Planning::rushSigns).
    bool rushSeen() const { return rushFrame_ >= 0; }

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
    int hiddenAttackerFrame_ = -1;
    int hiddenTechFrame_ = -1;
    double peakEnemyStrength_ = 0.0;
    bool loggedMine_ = false;
    std::set<int> typesSeen_;           // enemy unit types logged once when first seen
    int rushFrame_ = -1;
  };
}
