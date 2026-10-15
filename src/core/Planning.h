#pragma once
// Pure decision functions used by the managers. They take plain numbers instead
// of BWAPI objects so the strategic rules can be unit tested without the game.
//
// Supply values here use BWAPI's doubled units (a Marine costs 2, a depot gives 16)
// unless a parameter name says "Ui".

#include <vector>

namespace bb
{
  // ---------------------------------------------------------------- supply
  struct Producer
  {
    int buildTimeFrames = 1;
    int supplyPerUnit = 0;
  };

  struct SupplyState
  {
    int used = 0;
    int total = 0;
    int pending = 0;            // supply from depots/CCs that are planned or under construction
    int depotsInProgress = 0;   // planned + under construction
  };

  constexpr int kMaxSupply = 400;

  // How much free supply we want to keep, in doubled units.
  int desiredSupplyMargin(const std::vector<Producer>& producers, int lookaheadFrames, int fixedMarginUi, int extraMarginUi);

  // True when another supply depot should be started now.
  bool needSupplyDepot(const SupplyState& s, const std::vector<Producer>& producers, int lookaheadFrames,
                       int fixedMarginUi, int extraMarginUi, int maxConcurrentDepots, bool supplyBlockedNow);

  // ---------------------------------------------------------------- economy
  int workerTarget(int mineralPatches, int refineries, double workersPerPatch, int maxWorkers);

  // A build-order step waits until supply reaches its count. When supply has stopped
  // growing for good (worker target reached, nothing in production) the next step is
  // due anyway: otherwise Barracks@12 with an 11-worker target would never come.
  // Only the first waiting step is released, so the order stays in sequence.
  bool buildStepDue(int supplyUsed, int stepSupply, bool supplyStalled, bool earlierStepReleased);

  // ---------------------------------------------------------------- early defense
  // Fighting units we want to have by `frame` so an early attack (the built-in
  // Protoss sends zealots around 6:30) meets an army: none before 3:00, rising to
  // `armyBySixMinutes` at 6:00, held until 8:00, nothing after.
  int earlyDefenseTarget(int frame, int armyBySixMinutes);

  // ---------------------------------------------------------------- expansion
  struct ExpansionInputs
  {
    int frame = 0;
    int bases = 0;             // Command Centers we have or are building
    int basesBuilding = 0;     // ... of which not finished yet
    int workers = 0;
    int workerTarget = 0;      // what the current bases can use
    int lastExpansionFrame = -1;
    bool underThreat = false;
  };
  struct ExpansionParams
  {
    int maxBases = 1;
    int earliestFrame = 0;
    int atWorkers = 0;
  };
  // Same rule at every level, the parameters decide how early and how far:
  // one base at a time, not while our base is attacked, not before earliestFrame,
  // once workers reach atWorkers or the current bases are saturated, and the next
  // one at least kExpansionIntervalFrames after the previous.
  constexpr int kExpansionIntervalFrames = 24 * 150;
  bool shouldExpand(const ExpansionInputs& in, const ExpansionParams& p);

  // ---------------------------------------------------------------- budget
  struct Cost
  {
    int minerals = 0;
    int gas = 0;
    int supply = 0; // doubled units
  };

  // Spending plan for one decision round. Items are offered in priority order.
  // In strict mode an unaffordable item still holds back its cost so cheaper,
  // lower-priority items cannot delay it further ("save up").
  class Budget
  {
  public:
    Budget(int minerals, int gas, int supplyFree, bool strict)
      : minerals_(minerals), gas_(gas), supply_(supplyFree), strict_(strict) {}

    bool canAfford(const Cost& c) const;
    // Reserves and returns true if affordable. Otherwise returns false and, in strict
    // mode, blocks the missing resources (unless supply is the problem: saving money
    // does not help a supply block).
    bool tryReserve(const Cost& c);
    // Like tryReserve, but holds the missing money back at every level, strict or
    // not. For the few items without which the bot loses outright (a new base when
    // the minerals run out, detection against hidden attackers, expansions): at the
    // greedy levels cheap units would otherwise take every mineral as it comes in.
    bool reserveFirst(const Cost& c);

    int minerals() const { return minerals_; }
    int gas() const { return gas_; }
    int supply() const { return supply_; }

  private:
    int minerals_;
    int gas_;
    int supply_;
    bool strict_;
  };

  // ---------------------------------------------------------------- composition
  enum class Infantry { Marine, Medic, Firebat };

  Infantry chooseInfantry(int marines, int medics, int firebats, double medicRatio, double firebatShare, bool academyReady);

  // Enemy combat units we have seen (remembered), grouped by what counters them.
  struct EnemyMix
  {
    int melee = 0;   // zealots, zerglings, ultralisks, dark templar: vultures and tanks
    int heavy = 0;   // dragoons, archons, reavers, tanks, lurkers, goliaths: siege tanks
    int light = 0;   // marines, hydralisks, firebats, ...: infantry trades well
    int air = 0;     // flyers that fight: goliaths (and marines)
    int total() const { return melee + heavy + light + air; }
  };

  // Share of our army (by unit count) that Factories should build. Without
  // adaptation, or before enough enemies were seen, it is `baseShare`; otherwise it
  // moves towards `maxShare` the more the enemy army is melee, heavy or air.
  double targetMechShare(const EnemyMix& enemy, double baseShare, double maxShare, bool adapt);

  // Before much of the enemy army has been seen, plan for what that race usually
  // opens with (Protoss: zealots then dragoons; Zerg: lings then hydras; Terran:
  // marines then tanks), so the first units fit the opponent.
  enum class EnemyRace { Unknown, Protoss, Zerg, Terran };
  EnemyMix withRacePrior(const EnemyMix& seen, EnemyRace race);

  // What an idle Factory should build next.
  enum class FactoryUnit { Vulture, SiegeTank, Goliath };
  FactoryUnit chooseFactoryUnit(int vultures, int tanks, int goliaths, const EnemyMix& enemy,
                                bool hasMachineShop, bool armoryReady);

  // What an idle Starport should build next: Science Vessels first (detection and
  // support for the army, up to maxVessels), Battlecruisers when they are unlocked,
  // otherwise Wraiths.
  enum class StarportUnit { Wraith, ScienceVessel, Battlecruiser };
  StarportUnit chooseStarportUnit(int wraiths, int vessels, int battlecruisers, int maxVessels, bool vesselReady,
                                  bool battlecruiserReady);

  // Factories needed so mech units come out at the target share of the army while
  // `barracks` keep making infantry. A mech unit takes `mechBuildTimeRatio` times as
  // long as an infantry unit, so one Factory cannot keep up with one Barracks.
  // At least one once mech is used, never more than maxFactories.
  int factoriesWanted(double mechShare, int barracks, double mechBuildTimeRatio, int maxFactories);

  // ---------------------------------------------------------------- attack / retreat
  struct AttackInputs
  {
    int armySize = 0;
    int frame = 0;
    double ownStrength = 0.0;
    double knownEnemyStrength = 0.0;
    bool useStrengthEstimate = false;
  };

  struct AttackParams
  {
    int requiredSize = 12;
    int earliestFrame = 0;
    double requiredRatio = 0.0;
    int maxSize = 40;
  };

  // Known enemy strength this many times our own: no attack at any difficulty.
  constexpr double kHopelessStrengthRatio = 3.0;

  bool shouldAttack(const AttackInputs& in, const AttackParams& p);

  bool shouldRetreat(double localOwnStrength, double localEnemyStrength, double retreatRatio,
                     int armyNow, int armyAtWaveStart, double retreatArmyFraction);

  // Combat value of one unit: hit points times damage per frame.
  double unitStrength(int hitPoints, int shields, double damagePerFrame);
  // Lanchester square-law style sum: (sum of sqrt(strength))^2.
  double combineStrength(const std::vector<double>& unitStrengths);

  // ---------------------------------------------------------------- focus fire
  struct TargetInfo
  {
    bool canAttackUs = false;  // has a weapon that can hit our (ground) units
    bool isBuilding = false;
    bool isWorker = false;
    int hitPoints = 0;         // hp + shields
    double hpFraction = 1.0;
    double damagePerFrame = 0.0;
    int distance = 0;          // pixels from the deciding unit / army
    bool inRange = false;
  };

  // Index of the preferred target, or -1 to keep attack-moving (focus level 0, or no candidates).
  int chooseTarget(const std::vector<TargetInfo>& candidates, int focusFireLevel);

  // ---------------------------------------------------------------- geometry
  struct Point
  {
    int x = 0;
    int y = 0;
  };

  // Single-linkage clustering: points closer than maxLinkDistance end up in the same cluster.
  std::vector<std::vector<int>> clusterPoints(const std::vector<Point>& points, int maxLinkDistance);

  Point centroid(const std::vector<Point>& points);
  int countWithin(const std::vector<Point>& points, Point center, int radius);

  // Radius in tiles within which a group of `units` counts as standing together.
  // A group's area grows with its size, so a big army cannot fit the small radius
  // that suits a dozen units; `minimumTiles` (e.g. the difficulty's regroup radius)
  // is the lower bound.
  int groupRadiusTiles(int minimumTiles, int units);
}
