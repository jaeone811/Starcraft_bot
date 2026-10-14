#include "BwapiUtil.h"

#include <algorithm>

using namespace BWAPI;

namespace bb
{
  UnitType toUnitType(BuildItem item)
  {
    switch (item)
    {
    case BuildItem::SupplyDepot: return UnitTypes::Terran_Supply_Depot;
    case BuildItem::Barracks: return UnitTypes::Terran_Barracks;
    case BuildItem::Refinery: return UnitTypes::Terran_Refinery;
    case BuildItem::Academy: return UnitTypes::Terran_Academy;
    case BuildItem::EngineeringBay: return UnitTypes::Terran_Engineering_Bay;
    case BuildItem::CommandCenter: return UnitTypes::Terran_Command_Center;
    case BuildItem::Factory: return UnitTypes::Terran_Factory;
    }
    return UnitTypes::None;
  }

  bool isResearchItemTech(ResearchItem item)
  {
    return item == ResearchItem::StimPacks || item == ResearchItem::SiegeMode;
  }

  TechType toTechType(ResearchItem item)
  {
    switch (item)
    {
    case ResearchItem::StimPacks: return TechTypes::Stim_Packs;
    case ResearchItem::SiegeMode: return TechTypes::Tank_Siege_Mode;
    default: return TechTypes::None;
    }
  }

  UpgradeType toUpgradeType(ResearchItem item)
  {
    switch (item)
    {
    case ResearchItem::U238Shells: return UpgradeTypes::U_238_Shells;
    case ResearchItem::InfantryWeapons1: return UpgradeTypes::Terran_Infantry_Weapons;
    case ResearchItem::InfantryArmor1: return UpgradeTypes::Terran_Infantry_Armor;
    default: return UpgradeTypes::None;
    }
  }

  UnitType researchBuilding(ResearchItem item)
  {
    if (isResearchItemTech(item))
      return toTechType(item).whatResearches();
    return toUpgradeType(item).whatUpgrades();
  }

  bool isResearchDone(ResearchItem item)
  {
    Player self = Broodwar->self();
    if (isResearchItemTech(item))
      return self->hasResearched(toTechType(item));
    return self->getUpgradeLevel(toUpgradeType(item)) >= 1;
  }

  bool isResearchInProgress(ResearchItem item)
  {
    Player self = Broodwar->self();
    if (isResearchItemTech(item))
      return self->isResearching(toTechType(item));
    return self->isUpgrading(toUpgradeType(item));
  }

  Position buildingCenter(TilePosition t, UnitType type)
  {
    return Position(t) + Position(type.tileWidth() * 16, type.tileHeight() * 16);
  }

  bool isOurArmyType(UnitType type)
  {
    return type == UnitTypes::Terran_Marine || type == UnitTypes::Terran_Firebat || type == UnitTypes::Terran_Medic ||
           isMechType(type);
  }

  bool isMechType(UnitType type)
  {
    return isSiegeTank(type) || type == UnitTypes::Terran_Vulture || type == UnitTypes::Terran_Goliath;
  }

  void addToEnemyMix(EnemyMix& mix, UnitType type)
  {
    if (type.isWorker() || type.isBuilding())
      return;
    const bool fightsAir = type.airWeapon() != WeaponTypes::None;
    if (type.isFlyer())
    {
      if (canAttackGround(type) || fightsAir)
        ++mix.air; // overlords, observers and shuttles do not count
      return;
    }
    if (!canAttackGround(type))
      return;
    if (isMeleeType(type))
      ++mix.melee;
    else if (type == UnitTypes::Protoss_Dragoon || type == UnitTypes::Protoss_Archon || type == UnitTypes::Protoss_Reaver ||
             type == UnitTypes::Zerg_Lurker || isSiegeTank(type) || type == UnitTypes::Terran_Goliath)
      ++mix.heavy;
    else
      ++mix.light;
  }

  bool isSiegeTank(UnitType type)
  {
    return type == UnitTypes::Terran_Siege_Tank_Tank_Mode || type == UnitTypes::Terran_Siege_Tank_Siege_Mode;
  }

  bool isMeleeType(UnitType type)
  {
    return type == UnitTypes::Protoss_Zealot || type == UnitTypes::Zerg_Zergling ||
           type == UnitTypes::Zerg_Ultralisk || type == UnitTypes::Protoss_Dark_Templar;
  }

  double groundDamagePerFrame(UnitType type)
  {
    if (type == UnitTypes::Terran_Bunker)
      return 4 * groundDamagePerFrame(UnitTypes::Terran_Marine); // assume it is loaded
    if (type == UnitTypes::Protoss_Carrier)
      return 8 * 6.0 / 37.0; // 8 interceptors; BWAPI lists the interceptor cooldown as 1 frame, which is not its real rate
    if (type == UnitTypes::Protoss_Reaver)
      return 100.0 / 60.0; // scarab, roughly
    WeaponType w = type.groundWeapon();
    if (w == WeaponTypes::None || w == WeaponTypes::Unknown || w.damageCooldown() <= 0)
      return 0.0;
    // Zealots and Firebats hit more than once per attack: BWAPI models that with maxGroundHits().
    return static_cast<double>(w.damageAmount()) * std::max(1, w.damageFactor()) * std::max(1, type.maxGroundHits()) /
           w.damageCooldown();
  }

  bool canAttackGround(UnitType type)
  {
    return groundDamagePerFrame(type) > 0.0;
  }

  double typeStrength(UnitType type)
  {
    double dpf = groundDamagePerFrame(type);
    if (type == UnitTypes::Terran_Medic)
      dpf = 0.5 * groundDamagePerFrame(UnitTypes::Terran_Marine); // healing keeps marines alive
    return unitStrength(type.maxHitPoints(), type.maxShields(), dpf);
  }

  double unitStrengthNow(Unit unit)
  {
    if (!unit)
      return 0.0;
    UnitType type = unit->getType();
    double dpf = groundDamagePerFrame(type);
    if (type == UnitTypes::Terran_Medic)
      dpf = 0.5 * groundDamagePerFrame(UnitTypes::Terran_Marine);
    if (type.isWorker())
      dpf *= 0.25; // workers rarely fight
    if (!unit->isCompleted())
      return 0.0;
    return unitStrength(unit->getHitPoints(), unit->getShields(), dpf);
  }

  bool isValid(Unit unit)
  {
    return unit != nullptr && unit->exists();
  }

  bool isOurs(Unit unit)
  {
    return isValid(unit) && unit->getPlayer() == Broodwar->self();
  }

  bool isEnemyUnit(Unit unit)
  {
    return isValid(unit) && Broodwar->self() && Broodwar->self()->isEnemy(unit->getPlayer());
  }
}
