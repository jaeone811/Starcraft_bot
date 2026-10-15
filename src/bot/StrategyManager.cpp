#include "StrategyManager.h"

#include "BwapiUtil.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "Planning.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kThreatRadius = 12 * 32;
    constexpr double kMechShareDropPerUpdate = 0.05; // per 5 s re-evaluation
    std::pair<int, int> tileKey(Position p) { return { p.x / 32, p.y / 32 }; }
  }

  StrategyManager::StrategyManager(BotContext& ctx)
    : ctx_(ctx), attackSize_(ctx.config.attackArmySize), strengthRatio_(ctx.config.attackStrengthRatio),
      mechShare_(ctx.config.mechShare), loggedMechShare_(ctx.config.mechShare)
  {
  }

  void StrategyManager::updateArmyMix()
  {
    // Re-evaluated every few seconds from the enemy units we have seen; logged when it moves.
    if (ctx_.frame - lastMixFrame_ < 24 * 5)
      return;
    lastMixFrame_ = ctx_.frame;
    const EnemyMix mix = ctx_.config.enemyInfoUsage >= 1 ? ctx_.info->expectedEnemyMix() : ctx_.info->enemyMix();
    const double target = targetMechShare(mix, ctx_.config.mechShare, ctx_.config.maxMechShare, ctx_.config.enemyInfoUsage >= 1);
    // Up at once, down slowly: the remembered enemy count drops whenever units die or
    // are forgotten, and the production plan should not flip with every skirmish.
    mechShare_ = target >= mechShare_ ? target : std::max(target, mechShare_ - kMechShareDropPerUpdate);
    if (std::abs(mechShare_ - loggedMechShare_) >= 0.1)
    {
      ctx_.log.info("Strategy", concat("army mix: mech share ", static_cast<int>(loggedMechShare_ * 100), "% -> ",
                                       static_cast<int>(mechShare_ * 100), "% (enemy seen: melee ", mix.melee, ", heavy ", mix.heavy,
                                       ", light ", mix.light, ", air ", mix.air, ")"));
      loggedMechShare_ = mechShare_;
    }
  }

  void StrategyManager::update()
  {
    detectThreats();
    updateArmyMix();
    for (auto it = unreachableUntil_.begin(); it != unreachableUntil_.end();)
    {
      if (ctx_.frame >= it->second)
        it = unreachableUntil_.erase(it);
      else
        ++it;
    }
  }

  void StrategyManager::onWaveEnd(int unitsLost, int enemyKilled, int valueLost, int valueKilled, const char* reason)
  {
    const bool failed = valueLost > valueKilled;
    ctx_.log.info("Strategy", concat("wave ended (", reason, "): lost ", unitsLost, " units (value ", valueLost,
                                     "), killed ", enemyKilled, " (value ", valueKilled, ") -> ",
                                     failed ? "FAILED" : "ok"));
    if (!failed)
      return;
    ++ctx_.metrics.failedWaves;
    const int oldSize = attackSize_;
    const double oldRatio = strengthRatio_;
    attackSize_ = std::min(ctx_.config.maxAttackSize, attackSize_ + ctx_.config.attackSizeIncreaseOnFail);
    if (ctx_.config.enemyInfoUsage >= 2)
      strengthRatio_ += ctx_.config.strengthRatioIncreaseOnFail;
    if (attackSize_ != oldSize || strengthRatio_ != oldRatio)
      ctx_.log.info("Strategy", concat("adapting after failed wave: attack size ", oldSize, " -> ", attackSize_,
                                       ", required strength ratio ", oldRatio, " -> ", strengthRatio_));
    else
      ctx_.log.info("Strategy", "no adaptation at this difficulty level");
  }

  void StrategyManager::markUnreachable(Position target, int frames)
  {
    unreachableUntil_[tileKey(target)] = ctx_.frame + frames;
    ctx_.log.info("Strategy", concat("target ", target.x, ",", target.y, " marked unreachable for ", frames / 24, "s"));
  }

  bool StrategyManager::isMarkedUnreachable(Position target) const
  {
    auto it = unreachableUntil_.find(tileKey(target));
    return it != unreachableUntil_.end() && ctx_.frame < it->second;
  }

  std::optional<AttackTarget> StrategyManager::chooseAttackTarget(Position from) const
  {
    auto usable = [&](Position p) {
      return p.isValid() && !isMarkedUnreachable(p) && ctx_.map->groundReachable(from, p);
    };

    // 1. Closest enemy building we have seen.
    std::optional<AttackTarget> best;
    int bestDist = std::numeric_limits<int>::max();
    for (const auto& [id, r] : ctx_.info->enemyBuildings())
    {
      if (!usable(r.position))
        continue;
      const int d = from.getApproxDistance(r.position);
      if (d < bestDist)
      {
        bestDist = d;
        best = AttackTarget{ r.position, concat("enemy ", r.type.c_str()) };
      }
    }
    if (best)
      return best;

    // 2. The enemy main (confirmed, or inferred when allowed by the difficulty).
    if (auto main = ctx_.info->enemyMain())
      if (usable(*main))
        return AttackTarget{ *main, ctx_.info->enemyMainConfirmed() ? "enemy main" : "inferred enemy main" };

    // 3. Start locations nobody has looked at yet.
    bestDist = std::numeric_limits<int>::max();
    for (TilePosition t : ctx_.info->unexploredStartLocations())
    {
      const Position p = buildingCenter(t, UnitTypes::Terran_Command_Center);
      if (!usable(p))
        continue;
      const int d = from.getApproxDistance(p);
      if (d < bestDist)
      {
        bestDist = d;
        best = AttackTarget{ p, "unexplored start location" };
      }
    }
    if (best)
      return best;

    // 4. Expansions we have not seen for the longest time.
    int oldest = std::numeric_limits<int>::max();
    for (const auto& b : ctx_.map->bases())
    {
      if (b.lastSeenFrame >= 0 && ctx_.frame - b.lastSeenFrame < 24 * 60)
        continue;
      if (b.depotTile == ctx_.map->mainTile() || !usable(b.center))
        continue;
      if (b.lastSeenFrame < oldest)
      {
        oldest = b.lastSeenFrame;
        best = AttackTarget{ b.center, "search expansion" };
      }
    }
    if (best)
      return best;

    // 5. Every base was looked at in the last minute and nothing is known: the enemy
    //    hides buildings away from the bases. Sweep the map, the part not seen for
    //    the longest first (nearest among equals). One level 4 game spent 8 minutes
    //    cycling through the same four empty bases with a maxed army.
    int oldestCell = std::numeric_limits<int>::max();
    int bestCellDist = std::numeric_limits<int>::max();
    for (const auto& c : ctx_.map->searchCells())
    {
      if (c.lastSeenFrame >= 0 && ctx_.frame - c.lastSeenFrame < 24 * 90)
        continue;
      if (!usable(c.point))
        continue;
      const int d = from.getApproxDistance(c.point);
      if (c.lastSeenFrame < oldestCell || (c.lastSeenFrame == oldestCell && d < bestCellDist))
      {
        oldestCell = c.lastSeenFrame;
        bestCellDist = d;
        best = AttackTarget{ c.point, "search map" };
      }
    }
    return best;
  }

  double StrategyManager::firebatShare() const
  {
    if (ctx_.config.enemyInfoUsage < 1 || ctx_.config.firebatRatioVsMelee <= 0.0)
      return 0.0;
    // Early on, the race tells what is coming (zealots / lings): firebats from the start.
    const int melee = std::max(ctx_.info->recentMeleeUnits(), ctx_.info->expectedEnemyMix().melee);
    if (melee < 2)
      return 0.0;
    if (ctx_.config.enemyInfoUsage == 1)
      return ctx_.config.firebatRatioVsMelee;
    // With a strength estimate available, scale with how much of the enemy army is melee.
    const int combat = std::max(1, ctx_.info->knownEnemyCombatUnits());
    const double meleeFraction = static_cast<double>(melee) / combat;
    return ctx_.config.firebatRatioVsMelee * std::clamp(meleeFraction * 1.5, 0.3, 1.0);
  }

  void StrategyManager::detectThreats()
  {
    threats_.clear();
    std::vector<Position> ours;
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType().isBuilding())
        ours.push_back(u->getPosition());
    std::vector<Point> pts;
    for (Player enemy : Broodwar->enemies())
      for (Unit e : enemy->getUnits())
      {
        if (!e->exists() || !e->isVisible())
          continue;
        const UnitType t = e->getType();
        const bool dangerous = (canAttackGround(t) && !t.isWorker() && !t.isBuilding()) || (t.isWorker() && e->isAttacking());
        if (!dangerous)
          continue;
        for (const Position& p : ours)
          if (p.getApproxDistance(e->getPosition()) < kThreatRadius)
          {
            threats_.push_back(e);
            pts.push_back(toPoint(e->getPosition()));
            break;
          }
      }
    if (!pts.empty())
      threatCenter_ = toPosition(centroid(pts));
  }
}
