#pragma once
// Difficulty presets for BeginnerBot.
//
// Every per-difficulty number lives here and nowhere else. The game logic only
// reads these fields; it never branches on the level number itself.
// This header is deliberately free of BWAPI types so it can be unit tested.

#include "Planning.h"

#include <string>
#include <vector>

namespace bb
{
  // Buildings the planner can ask for (mapped to BWAPI::UnitType in the bot layer).
  enum class BuildItem
  {
    SupplyDepot,
    Barracks,
    Refinery,
    Academy,
    EngineeringBay,
    CommandCenter,
    Factory,
    Armory,
    Starport,
    ScienceFacility
  };

  // Research/upgrades the planner can ask for.
  enum class ResearchItem
  {
    StimPacks,
    U238Shells,
    InfantryWeapons1,
    InfantryArmor1,
    SiegeMode,
    VehicleWeapons1,
    VehiclePlating1,
    CharonBoosters,
    SpiderMines
  };

  const char* toString(BuildItem item);
  // What the early army needs: supply, production, gas and the Academy (firebats,
  // medics). The rest waits while the early army is short.
  bool makesEarlyArmy(BuildItem item);
  const char* toString(ResearchItem item);
  // Research that makes our anti-air units better (Marines, Goliaths). The rest
  // waits while enemy air outmatches our anti-air: Spider Mines, +1 vehicle weapons
  // and Charon... took the gas that Goliaths needed against Mutalisks.
  bool helpsAgainstAir(ResearchItem item);
  // Research that makes the counter units better. The rest waits while we mass a
  // counter: Stim, infantry upgrades and Charon took the gas Siege Tanks needed
  // against a Dragoon army.
  bool researchServesCounter(ResearchItem item, Counter counter);
  // Research that gives our units the range to answer heavy ranged enemies
  // (Dragoons, Tanks, Lurkers): Siege Mode and U-238 Shells.
  bool extendsRange(ResearchItem item);
  // The research list with the range research first while heavy ranged enemies are
  // known (otherwise unchanged).
  std::vector<ResearchItem> researchOrder(const std::vector<ResearchItem>& list, bool rangeFirst);

  // One step of a build order: build `item` once our supply (UI units, not the
  // doubled BWAPI units) has reached `supply`. Each step adds one more building of
  // that type on top of what earlier steps asked for.
  struct BuildStep
  {
    BuildItem item;
    int supply;
  };

  // The build order against Protoss (Liquipedia's TvP openings: one Barracks, an
  // early Factory, Siege Tanks hold the Dragoons): the first Factory takes the place
  // of the second Barracks, and with room for two Factories the next Barracks
  // becomes the second one. Build orders without a Factory stay as they are.
  std::vector<BuildStep> factoryFirst(const std::vector<BuildStep>& base, int maxFactories);
  // The build order against Zerg (Liquipedia's TvZ: Barracks, Academy and an early
  // Engineering Bay for +1 and Turrets against Mutalisks; Tanks later for Lurkers):
  // the Engineering Bay takes the first Factory's place and the Factory its own.
  std::vector<BuildStep> bioFirst(const std::vector<BuildStep>& base);
  // The opening for the enemy race: factoryFirst against Protoss and Terran,
  // bioFirst against Zerg, the base build order when the race is unknown.
  std::vector<BuildStep> raceOpening(const std::vector<BuildStep>& base, EnemyRace race, int maxFactories);
  // Research for the enemy race: Siege Mode / U-238 first against Protoss; Siege
  // Mode, Spider Mines and the vehicle upgrades first against Terran (Liquipedia's
  // TvT is a Tank and Vulture game); Stim, U-238 and the infantry upgrades first
  // against Zerg; unchanged otherwise.
  std::vector<ResearchItem> raceResearch(const std::vector<ResearchItem>& list, EnemyRace race);
  // Research that waits for now, with the reason for the plan log (nullptr: go ahead).
  //  - Charon Boosters only lengthen the Goliath's anti-air range: not before enemy
  //    air was seen (it was researched against Vultures and Tanks).
  //  - Against Terran the gas goes to Siege Tanks first: everything but Siege Mode
  //    waits for 2 Tanks, Stim / U-238 / infantry upgrades for 6. Five researches
  //    took all the gas from 4:31 to 6:42 and the first Tank came after the enemy's
  //    Tank push at 7:00.
  const char* researchWaits(ResearchItem item, EnemyRace race, int tanks, bool enemyAirSeen);

  struct DifficultyConfig
  {
    int level = 3;
    std::string name;          // English, used in game chat and logs
    std::string koreanName;    // Shown by the configurator and in logs
    std::string description;   // Korean one-line description

    // ---- Build order / economy -------------------------------------------
    std::vector<BuildStep> buildOrder;
    double workersPerMineralPatch = 2.0; // worker target = patches * this + refineries * 3
    int maxWorkers = 26;
    int maxBarracks = 3;
    int extraBarracksMineralBank = 0;    // >0: build another barracks while banking this much (up to maxBarracks)
    int maxBases = 1;                    // expansions up to this many Command Centers (Planning::shouldExpand)
    int expandEarliestFrame = 0;         // ... not before this frame
    int expandAtWorkers = 0;             // ... once we have this many workers (or the bases are saturated)
    std::vector<ResearchItem> research;  // researched in this order once the building exists

    // ---- Supply -------------------------------------------------------------
    int supplyLookaheadFrames = 0;       // >0: forecast supply use over this many frames
    int supplyFixedMargin = 2;           // used when lookahead is 0 (UI supply units)
    int maxConcurrentDepots = 1;

    // ---- Resource allocation ----------------------------------------------
    bool strictPriority = false;         // save up for a blocked higher-priority item
    bool armyFirstWhenThreatened = false;// when our base is attacked, army beats workers/buildings
    int earlyDefenseArmy = 0;            // fighting units wanted by 6:00; below Planning::earlyDefenseTarget the army is bought before tech
    bool earlyBunker = false;            // a Bunker at the rally point with 4 marines until the early game is over

    // ---- Scouting / information -------------------------------------------
    int scoutAtSupply = 0;               // 0 = only the frame fallback triggers the scout
    int scoutAtFrame = 0;                // fallback trigger frame
    bool inferEnemyStartByElimination = false; // last unexplored start location must be the enemy
    int scoutObserveFrames = 0;          // time the scout spends looking around the enemy main
    bool rescoutAfterDeath = false;
    int enemyInfoUsage = 0;              // 0 = location only, 1 = + composition counters, 2 = + strength estimate

    // ---- Composition --------------------------------------------------------
    double medicRatio = 0.0;             // medics per marine
    double firebatRatioVsMelee = 0.0;    // share of firebats when enemy melee units were seen (needs enemyInfoUsage >= 1)
    double mechShare = 0.0;              // share of the army built at Factories (vultures, tanks, goliaths); needs a Factory in the build order
    double maxMechShare = 0.0;           // with enemyInfoUsage >= 1 the share grows up to this against armies mech handles well
    int maxFactories = 0;                // Factories are added as the mech share grows
    double airShare = 0.0;               // share of the army from the Starport (Wraiths, Battlecruisers); needs a Starport in the build order
    int maxScienceVessels = 0;           // detectors / support that follow the army; needs a Science Facility in the build order
    bool useBattlecruisers = false;      // Physics Lab on the Science Facility, Battlecruisers from the Starport

    // ---- Attack ---------------------------------------------------------------
    int attackArmySize = 12;
    int earliestAttackFrame = 0;
    double attackStrengthRatio = 0.0;    // needs enemyInfoUsage >= 2; own / known enemy strength
    int regroupRadiusTiles = 8;          // cohesion radius while moving out
    bool recallArmyToDefend = false;     // pull an attacking army back when the base is hit

    // ---- Combat ---------------------------------------------------------------
    int focusFireLevel = 0;              // 0 attack-move, 1 units before buildings, 2 + weak/close, 3 + threat value
    double retreatStrengthRatio = 0.0;   // retreat if local enemy > own * ratio (0 = never by strength)
    double retreatArmyFraction = 0.25;   // retreat when the wave shrank below this fraction
    bool useStim = false;                // stim when engaging (only if researched)
    bool useComsatScan = false;          // build a Comsat Station and scan cloaked attackers

    // ---- Adaptation -------------------------------------------------------------
    int attackSizeIncreaseOnFail = 0;
    double strengthRatioIncreaseOnFail = 0.0;
    int maxAttackSize = 40;
    int supplyMarginIncreaseOnBlock = 0; // extra margin after we got supply blocked (UI units)
  };

  constexpr int kMinDifficulty = 1;
  constexpr int kMaxDifficulty = 5;
  constexpr int kDefaultDifficulty = 3;

  // Returns the preset for `level`; out-of-range levels are clamped.
  DifficultyConfig makeDifficultyConfig(int level);

  // Multi-line human readable dump used in the log at game start.
  std::string describe(const DifficultyConfig& config);
}
