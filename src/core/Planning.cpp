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

  bool buildStepDue(int supplyUsed, int stepSupply, bool supplyStalled, bool earlierStepReleased)
  {
    if (supplyUsed >= stepSupply)
      return true;
    return supplyStalled && !earlierStepReleased;
  }

  // ---------------------------------------------------------------- budget
  bool Budget::canAfford(const Cost& c) const
  {
    return minerals_ >= c.minerals && gas_ >= c.gas && supply_ >= c.supply;
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
    {
      minerals_ -= c.minerals;
      gas_ -= c.gas;
    }
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

  FactoryUnit chooseFactoryUnit(int vultures, int tanks, int goliaths, const EnemyMix& enemy,
                                bool hasMachineShop, bool armoryReady)
  {
    const int total = std::max(1, enemy.total());
    const int built = vultures + tanks + goliaths;
    // Air: goliaths up to 80% of the factory units, in proportion to the air seen.
    if (armoryReady && enemy.air > 0)
    {
      const double goliathShare = std::clamp(1.5 * enemy.air / total, 0.2, 0.8);
      if (goliaths < goliathShare * (built + 1))
        return FactoryUnit::Goliath;
    }
    if (!hasMachineShop)
      return armoryReady && enemy.air > 0 ? FactoryUnit::Goliath : FactoryUnit::Vulture;
    // Vultures beat melee units; otherwise they are a small cheap share next to tanks.
    const double vultureShare = 0.2 + 0.4 * static_cast<double>(enemy.melee) / total;
    if (vultures < vultureShare * (built + 1) && tanks > 0)
      return FactoryUnit::Vulture;
    return FactoryUnit::SiegeTank;
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
      double valueKey = 0.0;
      if (focusFireLevel == 2)
        valueKey = t.hitPoints;                                   // finish weak units
      else if (focusFireLevel >= 3)
        valueKey = -(t.damagePerFrame * 1000.0) / std::max(1, t.hitPoints); // most damage per hp first
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
