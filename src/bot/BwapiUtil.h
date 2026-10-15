#pragma once
// Small BWAPI helpers shared by the managers.

#include "DifficultyConfig.h"
#include "Planning.h"

#include <BWAPI.h>

namespace bb
{
  BWAPI::UnitType toUnitType(BuildItem item);
  bool isResearchItemTech(ResearchItem item);
  BWAPI::TechType toTechType(ResearchItem item);       // valid when isResearchItemTech
  BWAPI::UpgradeType toUpgradeType(ResearchItem item); // valid when !isResearchItemTech
  BWAPI::UnitType researchBuilding(ResearchItem item);
  bool isResearchDone(ResearchItem item);
  bool isResearchInProgress(ResearchItem item);

  inline Point toPoint(BWAPI::Position p) { return { p.x, p.y }; }
  inline BWAPI::Position toPosition(Point p) { return BWAPI::Position(p.x, p.y); }
  inline BWAPI::Position tileCenter(BWAPI::TilePosition t) { return BWAPI::Position(t) + BWAPI::Position(16, 16); }
  // Center of a building of `type` placed with its top-left at `t`.
  BWAPI::Position buildingCenter(BWAPI::TilePosition t, BWAPI::UnitType type);

  bool isOurArmyType(BWAPI::UnitType type);  // infantry (Marine / Firebat / Medic), mech and Starport units
  bool isSiegeTank(BWAPI::UnitType type);    // either mode
  bool isMechType(BWAPI::UnitType type);     // Vulture / Siege Tank / Goliath
  bool isAirArmyType(BWAPI::UnitType type);  // Wraith / Battlecruiser / Science Vessel
  // Counts one enemy unit type into the melee / heavy / light / air groups.
  void addToEnemyMix(EnemyMix& mix, BWAPI::UnitType type);
  bool isMeleeType(BWAPI::UnitType type);    // enemy units firebats are good against

  // Damage per frame against ground units (special cases for Bunker, Carrier, Reaver).
  double groundDamagePerFrame(BWAPI::UnitType type);
  bool canAttackGround(BWAPI::UnitType type);
  // Strength of a unit type at full health; used for remembered (not visible) units.
  double typeStrength(BWAPI::UnitType type);
  // Strength of a visible unit at its current health.
  double unitStrengthNow(BWAPI::Unit unit);

  bool isValid(BWAPI::Unit unit); // non-null and exists()
  bool isOurs(BWAPI::Unit unit);
  bool isEnemyUnit(BWAPI::Unit unit);
}
