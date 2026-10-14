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

  // What an idle Factory should build next.
  enum class FactoryUnit { Vulture, SiegeTank, Goliath };
  FactoryUnit chooseFactoryUnit(int vultures, int tanks, int goliaths, const EnemyMix& enemy,
                                bool hasMachineShop, bool armoryReady);

  // Factories wanted for the current mech share (at least one once mech is used).
  int factoriesWanted(double mechShare, double maxMechShare, int maxFactories);

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
