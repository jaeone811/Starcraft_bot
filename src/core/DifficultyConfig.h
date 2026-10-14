#pragma once
// Difficulty presets for BeginnerBot.
//
// Every per-difficulty number lives here and nowhere else. The game logic only
// reads these fields; it never branches on the level number itself.
// This header is deliberately free of BWAPI types so it can be unit tested.

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
    Factory
  };

  // Research/upgrades the planner can ask for.
  enum class ResearchItem
  {
    StimPacks,
    U238Shells,
    InfantryWeapons1,
    InfantryArmor1,
    SiegeMode
  };

  const char* toString(BuildItem item);
  const char* toString(ResearchItem item);

  // One step of a build order: build `item` once our supply (UI units, not the
  // doubled BWAPI units) has reached `supply`. Each step adds one more building of
  // that type on top of what earlier steps asked for.
  struct BuildStep
  {
    BuildItem item;
    int supply;
  };

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
    bool expand = false;                 // take the natural expansion
    int expandAtWorkers = 0;             // ... once we have at least this many workers
    std::vector<ResearchItem> research;  // researched in this order once the building exists

    // ---- Supply -------------------------------------------------------------
    int supplyLookaheadFrames = 0;       // >0: forecast supply use over this many frames
    int supplyFixedMargin = 2;           // used when lookahead is 0 (UI supply units)
    int maxConcurrentDepots = 1;

    // ---- Resource allocation ----------------------------------------------
    bool strictPriority = false;         // save up for a blocked higher-priority item
    bool armyFirstWhenThreatened = false;// when our base is attacked, army beats workers/buildings

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
    double tankRatio = 0.0;              // siege tanks per infantry unit (needs a Factory in the build order)

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
