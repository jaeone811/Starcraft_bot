// Checks the bot's assumptions about Brood War data against the real BWAPI 4.4.0
// type tables (BWAPILIB). No game is needed: these are static type properties.

#include "TestFramework.h"

#include "BwapiUtil.h"
#include "PlatformPaths.h"

#include <filesystem>

#include <BWAPI.h>

using namespace BWAPI;
using namespace bb;

TEST_CASE("build items map to the expected Terran buildings")
{
  CHECK(toUnitType(BuildItem::SupplyDepot) == UnitTypes::Terran_Supply_Depot);
  CHECK(toUnitType(BuildItem::Barracks) == UnitTypes::Terran_Barracks);
  CHECK(toUnitType(BuildItem::Refinery) == UnitTypes::Terran_Refinery);
  CHECK(toUnitType(BuildItem::Academy) == UnitTypes::Terran_Academy);
  CHECK(toUnitType(BuildItem::EngineeringBay) == UnitTypes::Terran_Engineering_Bay);
  CHECK(toUnitType(BuildItem::CommandCenter) == UnitTypes::Terran_Command_Center);
  CHECK(toUnitType(BuildItem::Factory) == UnitTypes::Terran_Factory);
  CHECK(toUnitType(BuildItem::Refinery).isRefinery());
  CHECK(toUnitType(BuildItem::CommandCenter).isResourceDepot());
}

TEST_CASE("supply numbers use BWAPI's doubled units")
{
  CHECK_EQ(UnitTypes::Terran_Marine.supplyRequired(), 2);
  CHECK_EQ(UnitTypes::Terran_SCV.supplyRequired(), 2);
  CHECK_EQ(UnitTypes::Terran_Supply_Depot.supplyProvided(), 16);
  CHECK_EQ(UnitTypes::Terran_Command_Center.supplyProvided(), 20);
}

TEST_CASE("costs used by the budget")
{
  CHECK_EQ(UnitTypes::Terran_Marine.mineralPrice(), 50);
  CHECK_EQ(UnitTypes::Terran_Marine.gasPrice(), 0);
  CHECK_EQ(UnitTypes::Terran_Medic.gasPrice(), 25);
  CHECK_EQ(UnitTypes::Terran_Firebat.gasPrice(), 25);
  CHECK_EQ(UnitTypes::Terran_Supply_Depot.mineralPrice(), 100);
  CHECK_EQ(UnitTypes::Terran_Barracks.mineralPrice(), 150);
  CHECK_EQ(UnitTypes::Terran_Command_Center.mineralPrice(), 400);
  CHECK(UnitTypes::Terran_SCV.buildTime() > 0);
  CHECK(UnitTypes::Terran_Marine.buildTime() > 0);
}

TEST_CASE("tech tree assumptions")
{
  // Medics and Firebats need an Academy; the production code checks requiredUnits().
  CHECK(UnitTypes::Terran_Medic.requiredUnits().count(UnitTypes::Terran_Academy) == 1);
  CHECK(UnitTypes::Terran_Firebat.requiredUnits().count(UnitTypes::Terran_Academy) == 1);
  CHECK(UnitTypes::Terran_Academy.requiredUnits().count(UnitTypes::Terran_Barracks) == 1);
  CHECK(UnitTypes::Terran_Comsat_Station.isAddon());
  CHECK(UnitTypes::Terran_Comsat_Station.whatBuilds().first == UnitTypes::Terran_Command_Center);
  CHECK(researchBuilding(ResearchItem::StimPacks) == UnitTypes::Terran_Academy);
  CHECK(researchBuilding(ResearchItem::U238Shells) == UnitTypes::Terran_Academy);
  CHECK(researchBuilding(ResearchItem::InfantryWeapons1) == UnitTypes::Terran_Engineering_Bay);
  CHECK(researchBuilding(ResearchItem::InfantryArmor1) == UnitTypes::Terran_Engineering_Bay);
  CHECK(toTechType(ResearchItem::StimPacks) == TechTypes::Stim_Packs);
  CHECK(toUpgradeType(ResearchItem::U238Shells) == UpgradeTypes::U_238_Shells);
  // Tanks: Factory (needs Barracks) + Machine Shop; Siege Mode is researched at the Machine Shop.
  CHECK(UnitTypes::Terran_Factory.requiredUnits().count(UnitTypes::Terran_Barracks) == 1);
  CHECK(UnitTypes::Terran_Machine_Shop.isAddon());
  // BuildingManager's add-on column: 2x2 at the right edge, bottom-aligned.
  CHECK_EQ(UnitTypes::Terran_Machine_Shop.tileWidth(), 2);
  CHECK_EQ(UnitTypes::Terran_Machine_Shop.tileHeight(), 2);
  CHECK_EQ(UnitTypes::Terran_Factory.tileWidth(), 4);
  CHECK_EQ(UnitTypes::Terran_Factory.tileHeight(), 3);
  CHECK(UnitTypes::Terran_Factory.canBuildAddon());
  CHECK(!UnitTypes::Terran_Barracks.canBuildAddon());
  CHECK(UnitTypes::Terran_Machine_Shop.whatBuilds().first == UnitTypes::Terran_Factory);
  CHECK(UnitTypes::Terran_Siege_Tank_Tank_Mode.whatBuilds().first == UnitTypes::Terran_Factory);
  CHECK(UnitTypes::Terran_Siege_Tank_Tank_Mode.requiredUnits().count(UnitTypes::Terran_Machine_Shop) == 1);
  CHECK(researchBuilding(ResearchItem::SiegeMode) == UnitTypes::Terran_Machine_Shop);
  CHECK(toTechType(ResearchItem::SiegeMode) == TechTypes::Tank_Siege_Mode);
  // CombatManager uses a 12-tile siege range and keeps tanks off air targets.
  CHECK_EQ(UnitTypes::Terran_Siege_Tank_Siege_Mode.groundWeapon().maxRange(), 12 * 32);
  CHECK(UnitTypes::Terran_Siege_Tank_Tank_Mode.airWeapon() == WeaponTypes::None);
  CHECK(UnitTypes::Terran_Firebat.airWeapon() == WeaponTypes::None);
  CHECK(UnitTypes::Terran_Marine.airWeapon() != WeaponTypes::None);
  CHECK(isOurArmyType(UnitTypes::Terran_Siege_Tank_Siege_Mode));
  CHECK(canAttackGround(UnitTypes::Terran_Siege_Tank_Tank_Mode));
  CHECK_EQ(UnitTypes::Terran_Siege_Tank_Tank_Mode.gasPrice(), 100);
  // ProductionManager sizes Factories by build time: a tank/vulture mix takes ~1.8x a marine.
  CHECK_EQ(UnitTypes::Terran_Siege_Tank_Tank_Mode.buildTime(), 750);
  CHECK_EQ(UnitTypes::Terran_Vulture.buildTime(), 450);
  CHECK_EQ(UnitTypes::Terran_Marine.buildTime(), 360);
  // BuildingManager keeps a ring free around Barracks / Factory plus a 2-tile add-on column.
  CHECK(UnitTypes::Terran_Factory.canBuildAddon());
  CHECK(!UnitTypes::Terran_Barracks.canBuildAddon());
  CHECK_EQ(UnitTypes::Terran_Machine_Shop.tileWidth(), 2);
  CHECK_EQ(UnitTypes::Terran_Factory.tileWidth(), 4);
  // Detection: Dark Templar are always cloaked; turrets need an Engineering Bay and see 11 tiles.
  CHECK(UnitTypes::Protoss_Dark_Templar.hasPermanentCloak());
  CHECK(canAttackGround(UnitTypes::Protoss_Dark_Templar));
  CHECK(canAttackGround(UnitTypes::Zerg_Lurker));
  CHECK(UnitTypes::Terran_Missile_Turret.isDetector());
  CHECK(UnitTypes::Terran_Missile_Turret.requiredUnits().count(UnitTypes::Terran_Engineering_Bay) == 1);
  CHECK_EQ(UnitTypes::Terran_Missile_Turret.sightRange(), 11 * 32);
  // Upper tech tree used by levels 4-5.
  CHECK(toUnitType(BuildItem::Armory) == UnitTypes::Terran_Armory);
  CHECK(toUnitType(BuildItem::Starport) == UnitTypes::Terran_Starport);
  CHECK(toUnitType(BuildItem::ScienceFacility) == UnitTypes::Terran_Science_Facility);
  CHECK(UnitTypes::Terran_Starport.requiredUnits().count(UnitTypes::Terran_Factory) == 1);
  CHECK(UnitTypes::Terran_Science_Facility.requiredUnits().count(UnitTypes::Terran_Starport) == 1);
  CHECK(UnitTypes::Terran_Control_Tower.whatBuilds().first == UnitTypes::Terran_Starport);
  CHECK(UnitTypes::Terran_Physics_Lab.whatBuilds().first == UnitTypes::Terran_Science_Facility);
  CHECK(UnitTypes::Terran_Science_Vessel.requiredUnits().count(UnitTypes::Terran_Control_Tower) == 1);
  CHECK(UnitTypes::Terran_Science_Vessel.requiredUnits().count(UnitTypes::Terran_Science_Facility) == 1);
  CHECK(UnitTypes::Terran_Science_Vessel.isDetector());
  CHECK(UnitTypes::Terran_Battlecruiser.requiredUnits().count(UnitTypes::Terran_Control_Tower) == 1);
  CHECK(UnitTypes::Terran_Battlecruiser.requiredUnits().count(UnitTypes::Terran_Physics_Lab) == 1);
  CHECK(UnitTypes::Terran_Wraith.requiredUnits().count(UnitTypes::Terran_Control_Tower) == 0);
  CHECK(canAttackGround(UnitTypes::Terran_Wraith));
  CHECK(isOurArmyType(UnitTypes::Terran_Battlecruiser) && isAirArmyType(UnitTypes::Terran_Science_Vessel));
  CHECK(researchBuilding(ResearchItem::VehicleWeapons1) == UnitTypes::Terran_Armory);
  CHECK(researchBuilding(ResearchItem::VehiclePlating1) == UnitTypes::Terran_Armory);
  CHECK(researchBuilding(ResearchItem::CharonBoosters) == UnitTypes::Terran_Machine_Shop);
  // Spider Mines: Machine Shop tech, laid at a position by Vultures (CombatManager::layMines).
  CHECK(toTechType(ResearchItem::SpiderMines) == TechTypes::Spider_Mines);
  CHECK(researchBuilding(ResearchItem::SpiderMines) == UnitTypes::Terran_Machine_Shop);
  CHECK(TechTypes::Spider_Mines.targetsPosition());
  CHECK(TechTypes::Spider_Mines.whatUses().count(UnitTypes::Terran_Vulture) == 1);
  CHECK(UnitTypes::Terran_Vulture_Spider_Mine.groundWeapon() != WeaponTypes::None);
  CHECK(UpgradeTypes::Charon_Boosters.whatsRequired(1) == UnitTypes::Terran_Armory);
  // The early Bunker needs only a Barracks and holds 4 marines.
  CHECK(UnitTypes::Terran_Bunker.requiredUnits().count(UnitTypes::Terran_Barracks) == 1);
  CHECK_EQ(UnitTypes::Terran_Bunker.spaceProvided(), 4);
  // Other mech: Vultures need only a Factory (and no gas), Goliaths an Armory and hit air.
  CHECK(UnitTypes::Terran_Vulture.whatBuilds().first == UnitTypes::Terran_Factory);
  CHECK(UnitTypes::Terran_Vulture.requiredUnits().count(UnitTypes::Terran_Machine_Shop) == 0);
  CHECK_EQ(UnitTypes::Terran_Vulture.gasPrice(), 0);
  CHECK(UnitTypes::Terran_Vulture.airWeapon() == WeaponTypes::None);
  CHECK(UnitTypes::Terran_Goliath.requiredUnits().count(UnitTypes::Terran_Armory) == 1);
  CHECK(UnitTypes::Terran_Goliath.airWeapon() != WeaponTypes::None);
  CHECK(UnitTypes::Terran_Armory.requiredUnits().count(UnitTypes::Terran_Factory) == 1);
  CHECK(isMechType(UnitTypes::Terran_Goliath) && isOurArmyType(UnitTypes::Terran_Vulture));
  // Enemy groups used by the mech share.
  EnemyMix mix;
  addToEnemyMix(mix, UnitTypes::Protoss_Zealot);
  addToEnemyMix(mix, UnitTypes::Protoss_Dragoon);
  addToEnemyMix(mix, UnitTypes::Zerg_Hydralisk);
  addToEnemyMix(mix, UnitTypes::Protoss_Carrier);
  addToEnemyMix(mix, UnitTypes::Zerg_Overlord);   // not a fighter
  addToEnemyMix(mix, UnitTypes::Protoss_Probe);   // workers do not count
  CHECK_EQ(mix.melee, 1);
  CHECK_EQ(mix.heavy, 1);
  CHECK_EQ(mix.light, 1);
  CHECK_EQ(mix.air, 1);
  // Scanner Sweep needs no research (BWAPI reports whatResearches() == None); it only costs energy.
  CHECK(TechTypes::Scanner_Sweep.whatResearches() == UnitTypes::None);
  CHECK_EQ(TechTypes::Scanner_Sweep.energyCost(), 50);
}

TEST_CASE("combat value helpers")
{
  CHECK(groundDamagePerFrame(UnitTypes::Terran_Marine) > 0.0);
  CHECK(groundDamagePerFrame(UnitTypes::Protoss_Zealot) > groundDamagePerFrame(UnitTypes::Terran_Marine));
  CHECK(!canAttackGround(UnitTypes::Terran_Medic));
  CHECK(typeStrength(UnitTypes::Terran_Medic) > 0.0);   // healing still counts
  CHECK(canAttackGround(UnitTypes::Protoss_Photon_Cannon));
  CHECK(canAttackGround(UnitTypes::Zerg_Sunken_Colony));
  CHECK(canAttackGround(UnitTypes::Terran_Bunker));      // special-cased: assumed loaded
  CHECK(canAttackGround(UnitTypes::Protoss_Carrier));    // special-cased: interceptors
  CHECK(groundDamagePerFrame(UnitTypes::Protoss_Carrier) < 5.0); // not 8 x "1 frame cooldown"
  CHECK(canAttackGround(UnitTypes::Protoss_Reaver));     // BWAPI lists no ground weapon; special-cased
  CHECK(!canAttackGround(UnitTypes::Zerg_Overlord));
  CHECK(isMeleeType(UnitTypes::Zerg_Zergling));
  CHECK(!isMeleeType(UnitTypes::Zerg_Hydralisk));
  CHECK(isOurArmyType(UnitTypes::Terran_Medic));
  CHECK(!isOurArmyType(UnitTypes::Terran_SCV));
  CHECK(typeStrength(UnitTypes::Protoss_Dragoon) > typeStrength(UnitTypes::Terran_Marine));
}

TEST_CASE("building geometry helper")
{
  const Position c = buildingCenter(TilePosition(10, 20), UnitTypes::Terran_Command_Center); // 4x3 tiles
  CHECK_EQ(c.x, 10 * 32 + 64);
  CHECK_EQ(c.y, 20 * 32 + 48);
}

TEST_CASE("StarCraft folder detection")
{
  namespace fs = std::filesystem;
  CHECK(!moduleDirectory().empty()); // the test executable itself
  const fs::path old = fs::current_path();
  const fs::path tmp = fs::temp_directory_path() / "beginnerbot_root_test";
  fs::create_directories(tmp / "bwapi-data");
  fs::current_path(tmp);
  CHECK(fs::equivalent(starcraftRoot(), tmp));
  fs::current_path(old);
  fs::remove_all(tmp);
}

int main()
{
  return bbtest::runAll();
}
