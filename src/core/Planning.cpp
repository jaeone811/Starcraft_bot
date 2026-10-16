#include "Planning.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

namespace bb
{
  // ---------------------------------------------------------------- supply
  int desiredSupplyMargin(const std::vector<Producer>& producers, int lookaheadFrames, int fixedMarginUi, int extraMarginUi)
  {
    int margin = 0;
    if (lookaheadFrames > 0)
    {
      for (const auto& p : producers)
        if (p.buildTimeFrames > 0)
          margin += lookaheadFrames * p.supplyPerUnit / p.buildTimeFrames;
      // Never plan tighter than the fixed margin.
      margin = std::max(margin, fixedMarginUi * 2);
    }
    else
    {
      margin = fixedMarginUi * 2;
    }
    return margin + std::max(0, extraMarginUi) * 2;
  }

  bool needSupplyDepot(const SupplyState& s, const std::vector<Producer>& producers, int lookaheadFrames,
                       int fixedMarginUi, int extraMarginUi, int maxConcurrentDepots, bool supplyBlockedNow)
  {
    if (s.total + s.pending >= kMaxSupply)
      return false;
    // A supply block with nothing on the way always gets a depot, regardless of settings.
    if (supplyBlockedNow && s.pending == 0)
      return true;
    if (s.depotsInProgress >= std::max(1, maxConcurrentDepots))
      return false;
    const int free = s.total + s.pending - s.used;
    return free <= desiredSupplyMargin(producers, lookaheadFrames, fixedMarginUi, extraMarginUi);
  }

  // ---------------------------------------------------------------- economy
  int workerTarget(int mineralPatches, int refineries, double workersPerPatch, int maxWorkers)
  {
    const int fromMinerals = static_cast<int>(std::lround(std::max(0, mineralPatches) * std::max(0.0, workersPerPatch)));
    const int target = fromMinerals + std::max(0, refineries) * 3;
    return std::clamp(target, 0, std::max(0, maxWorkers));
  }

  int workersInAdvance(int bases, int maxBases, int frame, int expandEarliestFrame)
  {
    constexpr int kAdvance = 6;
    return bases < maxBases && frame >= expandEarliestFrame - 24 * 90 ? kAdvance : 0;
  }

  int earlyDefenseTarget(int frame, int armyBySixMinutes, int enemyArmySeen, bool rushSeen)
  {
    constexpr int kStart = 24 * 180, kFull = 24 * 360, kEnd = 24 * 480;
    if (armyBySixMinutes <= 0 || frame >= kEnd)
      return 0;
    int ramp = 0;
    if (frame >= kFull || rushSeen)
      ramp = armyBySixMinutes;
    else if (frame >= kStart)
      ramp = static_cast<int>(std::ceil(static_cast<double>(armyBySixMinutes) * (frame - kStart) / (kFull - kStart)));
    return std::max(ramp, enemyArmySeen);
  }

  bool rushSigns(const EarlyScouting& s)
  {
    // A proxy before 5:00 is an attack on its way (a Gateway at our natural at 2:52,
    // then a Forge and three Cannons there).
    if (s.proxyBuildings > 0 && s.frame < 24 * 300)
      return true;
    if (s.frame >= 24 * 180)
      return false;
    return s.productionBuildings >= 2 || (s.spawningPool && s.frame < 24 * 150) || s.combatUnits >= 2;
  }

  int naturalAtSupply(EnemyRace race)
  {
    switch (race)
    {
    case EnemyRace::Zerg: return 15;
    case EnemyRace::Terran: return 20;
    case EnemyRace::Protoss: return 24;
    case EnemyRace::Unknown: break;
    }
    return 0;
  }

  bool shouldExpand(const ExpansionInputs& in, const ExpansionParams& p)
  {
    if (in.bases >= p.maxBases || in.basesBuilding > 0 || in.underThreat)
      return false;
    if (in.openingNatural && in.nextIsNatural)
      return true;
    if (in.frame < p.earliestFrame)
      return false;
    // A base other than the natural lies further out than the army can cover: only
    // when our army is at least as strong as the enemy army we know. A third Command
    // Center went up twice next to UAlbertaBot's army and took seven SCVs with it,
    // and a center base was taken while the natural was contested.
    if (!in.nextIsNatural && in.ownArmyStrength < in.knownEnemyStrength)
      return false;
    if (in.lastExpansionFrame >= 0 && in.frame - in.lastExpansionFrame < kExpansionIntervalFrames)
      return false;
    return in.workers >= p.atWorkers || (in.workerTarget > 0 && in.workers >= in.workerTarget);
  }

  bool buildStepDue(int supplyUsed, int stepSupply, bool supplyStalled, bool earlierStepReleased)
  {
    if (supplyUsed >= stepSupply)
      return true;
    return supplyStalled && !earlierStepReleased;
  }

  // ---------------------------------------------------------------- budget
  bool Budget::canAfford(const Cost& c) const
  {
    // A resource the item does not need is not checked: gas held for a Tank must not
    // stop a Marine.
    return (c.minerals <= 0 || minerals_ >= c.minerals) && (c.gas <= 0 || gas_ >= c.gas) &&
           (c.supply <= 0 || supply_ >= c.supply);
  }

  bool Budget::tryReserve(const Cost& c)
  {
    if (canAfford(c))
    {
      minerals_ -= c.minerals;
      gas_ -= c.gas;
      supply_ -= c.supply;
      return true;
    }
    if (strict_ && supply_ >= c.supply)
      holdShort(c);
    return false;
  }

  void Budget::holdShort(const Cost& c)
  {
    if (minerals_ < c.minerals)
      minerals_ -= c.minerals;
    if (gas_ < c.gas)
      gas_ -= c.gas;
  }

  bool Budget::reserveFirst(const Cost& c)
  {
    if (canAfford(c))
      return tryReserve(c);
    if (supply_ >= c.supply)
      holdShort(c);
    return false;
  }

  // ---------------------------------------------------------------- composition
  Infantry chooseInfantry(int marines, int medics, int firebats, double medicRatio, double firebatShare, bool academyReady)
  {
    if (!academyReady)
      return Infantry::Marine;
    const int shooters = marines + firebats;
    if (medicRatio > 0.0 && shooters >= 4 && medics < static_cast<int>(shooters * medicRatio))
      return Infantry::Medic;
    if (firebatShare > 0.0 && firebats < static_cast<int>((shooters + 1) * firebatShare))
      return Infantry::Firebat;
    return Infantry::Marine;
  }

  double targetMechShare(const EnemyMix& enemy, double baseShare, double maxShare, bool adapt)
  {
    const int total = enemy.total();
    if (!adapt || maxShare <= baseShare || total < 4)
      return baseShare;
    // Mech answers melee, heavy ground and (with goliaths) air; infantry is better
    // against light ranged units, so those pull the share back to the base.
    const double score = (enemy.melee + enemy.heavy + 0.6 * enemy.air - 0.5 * enemy.light) / total;
    return baseShare + (maxShare - baseShare) * std::clamp(score, 0.0, 1.0);
  }

  EnemyMix withRacePrior(const EnemyMix& seen, EnemyRace race)
  {
    if (seen.total() >= 6 || race == EnemyRace::Unknown)
      return seen;
    EnemyMix m = seen;
    switch (race)
    {
    case EnemyRace::Protoss: m.melee += 3; m.heavy += 2; break;
    case EnemyRace::Zerg: m.melee += 2; m.light += 3; break;
    case EnemyRace::Terran: m.light += 3; m.heavy += 2; break;
    case EnemyRace::Unknown: break;
    }
    return m;
  }

  FactoryUnit chooseFactoryUnit(int vultures, int tanks, int goliaths, const EnemyMix& enemy,
                                bool hasMachineShop, bool armoryReady)
  {
    const int total = std::max(1, enemy.total());
    const int built = vultures + tanks + goliaths;
    // With an Armory: a quarter goliaths (they hold well against ground armies too),
    // up to 80% of the factory units in proportion to the air seen.
    if (armoryReady)
    {
      const double goliathShare = std::clamp(0.25 + 1.5 * enemy.air / total, 0.25, 0.8);
      if (goliaths < goliathShare * (built + 1))
        return FactoryUnit::Goliath;
    }
    if (!hasMachineShop)
      return armoryReady && enemy.air > 0 ? FactoryUnit::Goliath : FactoryUnit::Vulture;
    // Vultures beat melee units; otherwise they are a small cheap share next to tanks.
    const double vultureShare = 0.2 + 0.4 * static_cast<double>(enemy.melee) / total;
    // Against mostly melee (zealots, lings) a Vulture may come first.
    if (vultures < vultureShare * (built + 1) && (tanks > 0 || 2 * enemy.melee >= total))
      return FactoryUnit::Vulture;
    return FactoryUnit::SiegeTank;
  }

  StarportUnit chooseStarportUnit(int wraiths, int vessels, int battlecruisers, int maxVessels, bool vesselReady,
                                  bool battlecruiserReady)
  {
    if (vesselReady && vessels < maxVessels)
      return StarportUnit::ScienceVessel;
    // Battlecruisers carry the late game; a few Wraiths alongside are cheap cover.
    if (battlecruiserReady && battlecruisers <= wraiths)
      return StarportUnit::Battlecruiser;
    return StarportUnit::Wraith;
  }

  int factoriesWanted(double mechShare, int barracks, double mechBuildTimeRatio, int maxFactories)
  {
    if (mechShare <= 0.0 || maxFactories <= 0)
      return 0;
    // mech : infantry = share : (1 - share), produced at 1/ratio the rate per building.
    const double share = std::min(mechShare, 0.95);
    const double needed = share / (1.0 - share) * std::max(barracks, 1) * std::max(mechBuildTimeRatio, 0.0);
    return std::clamp(static_cast<int>(std::lround(needed)), 1, maxFactories);
  }

  // ---------------------------------------------------------------- attack / retreat
  bool shouldAttack(const AttackInputs& in, const AttackParams& p)
  {
    // A maxed-out army always moves out; otherwise it would sit at home forever.
    if (in.armySize >= p.maxSize)
      return true;
    if (in.frame < p.earliestFrame)
      return false;
    if (in.armySize < p.requiredSize)
      return false;
    // Every level: do not walk into an enemy we know is overwhelmingly stronger.
    // That would be a suicide attack, not an easy opponent; the army grows instead
    // (and still goes once it is maxed out, above).
    if (in.knownEnemyStrength > in.ownStrength * kHopelessStrengthRatio)
      return false;
    if (in.useStrengthEstimate && p.requiredRatio > 0.0 && in.knownEnemyStrength > 0.0)
      return in.ownStrength >= in.knownEnemyStrength * p.requiredRatio;
    return true;
  }

  bool shouldRetreat(double localOwnStrength, double localEnemyStrength, double retreatRatio,
                     int armyNow, int armyAtWaveStart, double retreatArmyFraction)
  {
    if (armyAtWaveStart > 0 && armyNow < armyAtWaveStart * retreatArmyFraction)
      return true;
    if (retreatRatio > 0.0 && localEnemyStrength > 0.0 && localEnemyStrength > localOwnStrength * retreatRatio)
      return true;
    return false;
  }

  double unitStrength(int hitPoints, int shields, double damagePerFrame)
  {
    return std::max(0, hitPoints + shields) * std::max(0.0, damagePerFrame);
  }

  double combineStrength(const std::vector<double>& unitStrengths)
  {
    double sum = 0.0;
    for (double s : unitStrengths)
      sum += std::sqrt(std::max(0.0, s));
    return sum * sum;
  }

  double mixedDamagePerFrame(double groundDpf, double airDpf, double enemyAirFraction)
  {
    const double a = std::clamp(enemyAirFraction, 0.0, 1.0);
    return (1.0 - a) * std::max(0.0, groundDpf) + a * std::max(0.0, airDpf);
  }

  bool antiAirShort(double enemyAirStrength, double ownAntiAirStrength)
  {
    return enemyAirStrength > 0.0 && ownAntiAirStrength < enemyAirStrength;
  }

  bool detectionWanted(bool hiddenSeen, bool hiddenTechSeen, int enemyAirAttackers, bool protossInsurance, int frame)
  {
    return hiddenSeen || hiddenTechSeen || enemyAirAttackers >= 2 || (protossInsurance && frame >= 24 * 360);
  }

  bool canBreakSiege(double ownStrength, double enemyStrength)
  {
    return ownStrength > 0.0 && ownStrength >= enemyStrength * 1.2;
  }

  bool canClearDoorstep(double ownStrength, double defendersStrength, int ownShooters)
  {
    return ownShooters >= 4 && ownStrength >= defendersStrength * 1.5;
  }

  const char* toString(Counter c)
  {
    switch (c)
    {
    case Counter::None: return "none";
    case Counter::SiegeTanks: return "Siege Tanks";
    case Counter::Vultures: return "Vultures";
    case Counter::Goliaths: return "Goliaths";
    case Counter::Firebats: return "Firebats";
    case Counter::Marines: return "Marines";
    }
    return "?";
  }

  bool isFactoryCounter(Counter c)
  {
    return c == Counter::SiegeTanks || c == Counter::Vultures || c == Counter::Goliaths;
  }

  bool dominantForce(int typeCount, int armyCount, bool alreadyMassing)
  {
    if (alreadyMassing)
      return typeCount >= 4 && typeCount * 10 >= armyCount * 3;
    return typeCount >= 6 && typeCount * 10 >= armyCount * 4;
  }

  bool defenseOutmatched(double ownStrength, double enemyStrength)
  {
    return enemyStrength > 0.0 && enemyStrength > ownStrength * 1.2;
  }

  bool onLeash(int tilesToNearestBuilding, bool alreadyLeashed, int leashTiles, int returnTiles)
  {
    return alreadyLeashed ? tilesToNearestBuilding > returnTiles : tilesToNearestBuilding > leashTiles;
  }

  bool baseContested(double enemyStrengthSeenThere, double ownArmyStrength)
  {
    return enemyStrengthSeenThere > 0.0 && enemyStrengthSeenThere * 2.0 > ownArmyStrength;
  }

  // ---------------------------------------------------------------- focus fire
  int chooseTarget(const std::vector<TargetInfo>& candidates, int focusFireLevel)
  {
    if (focusFireLevel <= 0 || candidates.empty())
      return -1;

    // Smaller key wins.
    auto key = [focusFireLevel](const TargetInfo& t) {
      int category;
      if (focusFireLevel == 1)
        category = t.isBuilding ? (t.canAttackUs ? 2 : 3) : (t.canAttackUs ? 0 : 1);
      else
        category = t.canAttackUs ? 0 : (t.isBuilding ? 2 : 1);

      const int rangeKey = (focusFireLevel >= 2 && !t.inRange) ? 1 : 0;
      // Hit points as our weapon sees them: a Firebat's flames do a quarter of their
      // damage to a Dragoon, so for it a Dragoon has four times its hit points.
      const double factor = std::clamp(t.ourDamageFactor, 0.1, 1.0);
      const double effectiveHp = std::max(1, t.hitPoints) / factor;
      double valueKey = 0.0;
      if (focusFireLevel == 2)
        valueKey = effectiveHp;                                   // finish weak units
      else if (focusFireLevel >= 3)
        valueKey = -(t.damagePerFrame * 1000.0) / effectiveHp;    // most damage per hp first
      return std::make_tuple(category, rangeKey, valueKey, t.distance);
    };

    int best = 0;
    auto bestKey = key(candidates[0]);
    for (int i = 1; i < static_cast<int>(candidates.size()); ++i)
    {
      auto k = key(candidates[i]);
      if (k < bestKey)
      {
        bestKey = k;
        best = i;
      }
    }
    return best;
  }

  // ---------------------------------------------------------------- geometry
  std::vector<std::vector<int>> clusterPoints(const std::vector<Point>& points, int maxLinkDistance)
  {
    const int n = static_cast<int>(points.size());
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    auto find = [&parent](int i) {
      while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
      return i;
    };
    const long long limit = static_cast<long long>(maxLinkDistance) * maxLinkDistance;
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j)
      {
        const long long dx = points[i].x - points[j].x, dy = points[i].y - points[j].y;
        if (dx * dx + dy * dy <= limit)
          parent[find(i)] = find(j);
      }
    std::vector<std::vector<int>> clusters;
    std::vector<int> clusterOfRoot(n, -1);
    for (int i = 0; i < n; ++i)
    {
      const int r = find(i);
      if (clusterOfRoot[r] < 0)
      {
        clusterOfRoot[r] = static_cast<int>(clusters.size());
        clusters.emplace_back();
      }
      clusters[clusterOfRoot[r]].push_back(i);
    }
    return clusters;
  }

  Point awayFrom(Point unit, Point danger, int distance)
  {
    const double dx = unit.x - danger.x, dy = unit.y - danger.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0)
      return { unit.x + distance, unit.y };
    return { unit.x + static_cast<int>(dx * distance / len), unit.y + static_cast<int>(dy * distance / len) };
  }

  Point centroid(const std::vector<Point>& points)
  {
    if (points.empty())
      return {};
    long long sx = 0, sy = 0;
    for (const auto& p : points) { sx += p.x; sy += p.y; }
    const long long n = static_cast<long long>(points.size());
    return { static_cast<int>(sx / n), static_cast<int>(sy / n) };
  }

  int countWithin(const std::vector<Point>& points, Point center, int radius)
  {
    const long long r2 = static_cast<long long>(radius) * radius;
    int count = 0;
    for (const auto& p : points)
    {
      const long long dx = p.x - center.x, dy = p.y - center.y;
      if (dx * dx + dy * dy <= r2)
        ++count;
    }
    return count;
  }

  int groupRadiusTiles(int minimumTiles, int units)
  {
    const int bySize = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max(0, units))))) + 2;
    return std::max(minimumTiles, bySize);
  }
}
