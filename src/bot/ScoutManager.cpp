#include "ScoutManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "WorkerManager.h"

#include <limits>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kRescoutDelay = 24 * 90;
  }

  const char* ScoutManager::stateName() const
  {
    switch (state_)
    {
    case State::NotStarted: return "waiting";
    case State::Exploring: return "exploring";
    case State::Observing: return "observing";
    case State::Returning: return "returning";
    case State::Done: return "done";
    }
    return "?";
  }

  bool ScoutManager::shouldStart() const
  {
    const auto& cfg = ctx_.config;
    if (ctx_.info->enemyMainConfirmed())
      return false;
    const int supplyUi = Broodwar->self()->supplyUsed() / 2;
    const bool bySupply = cfg.scoutAtSupply > 0 && supplyUi >= cfg.scoutAtSupply;
    const bool byTime = cfg.scoutAtFrame > 0 && ctx_.frame >= cfg.scoutAtFrame;
    return bySupply || byTime;
  }

  void ScoutManager::finish(const char* reason)
  {
    ctx_.log.info("Scout", concat("scouting finished: ", reason));
    if (isValid(scout_))
      ctx_.workers->release(scout_);
    scout_ = nullptr;
    state_ = State::Done;
  }

  void ScoutManager::onUnitDestroy(Unit unit)
  {
    if (unit && unit == scout_)
    {
      ctx_.log.info("Scout", "scout was killed");
      scout_ = nullptr;
      deathFrame_ = ctx_.frame;
      state_ = State::Done;
    }
  }

  void ScoutManager::update()
  {
    if (state_ == State::Done)
    {
      // Higher difficulties send another scout if the first died before finding the enemy.
      if (ctx_.config.rescoutAfterDeath && deathFrame_ >= 0 && ctx_.frame - deathFrame_ > kRescoutDelay &&
          !ctx_.info->enemyMainConfirmed())
      {
        deathFrame_ = -1;
        state_ = State::NotStarted;
      }
      else
        return;
    }

    if (state_ == State::NotStarted)
    {
      if (!shouldStart())
        return;
      scout_ = ctx_.workers->acquireWorker(ctx_.map->mainPosition(), WorkerRole::Scout);
      if (!scout_)
        return;
      ++ctx_.metrics.scoutsSent;
      state_ = State::Exploring;
      ctx_.log.info("Scout", concat("SCV#", scout_->getID(), " sent to scout"));
    }

    if (!isValid(scout_))
    {
      scout_ = nullptr;
      state_ = State::Done;
      return;
    }

    // Do not let the scout die for nothing.
    if (state_ != State::Returning && scout_->getHitPoints() < scout_->getType().maxHitPoints() / 2)
    {
      ctx_.log.info("Scout", "scout is hurt; returning home");
      state_ = State::Returning;
    }

    switch (state_)
    {
    case State::Exploring:
    {
      if (ctx_.info->enemyMainConfirmed())
      {
        if (ctx_.config.scoutObserveFrames > 0)
        {
          state_ = State::Observing;
          observeStartFrame_ = ctx_.frame;
          observeWaypoint_ = 0;
        }
        else
          state_ = State::Returning;
        return;
      }
      // Next unexplored start location: inferred main first (if allowed), else the closest one.
      std::vector<TilePosition> candidates = ctx_.info->unexploredStartLocations();
      if (candidates.empty())
      {
        finish("no unexplored start locations left");
        return;
      }
      Position best = Positions::None;
      int bestDist = std::numeric_limits<int>::max();
      for (TilePosition t : candidates)
      {
        const Position p = buildingCenter(t, UnitTypes::Terran_Command_Center);
        if (!ctx_.map->groundReachable(scout_->getPosition(), p))
          continue;
        const int d = scout_->getDistance(p);
        if (d < bestDist)
        {
          bestDist = d;
          best = p;
        }
      }
      if (best == Positions::None)
      {
        finish("remaining start locations are not reachable by ground");
        return;
      }
      if (best != target_)
      {
        target_ = best;
        ctx_.log.info("Scout", concat("heading to start location near ", best.x, ",", best.y));
      }
      ctx_.commander->move(scout_, target_, 24 * 4);
      return;
    }
    case State::Observing:
    {
      auto main = ctx_.info->enemyMain();
      if (!main || ctx_.frame - observeStartFrame_ > ctx_.config.scoutObserveFrames)
      {
        state_ = State::Returning;
        return;
      }
      // Walk a square around the enemy main to see its buildings.
      static const Position offsets[] = { {256, 0}, {0, 256}, {-256, 0}, {0, -256} };
      Position wp = *main + offsets[observeWaypoint_ % 4];
      wp.makeValid();
      if (scout_->getDistance(wp) < 64)
        ++observeWaypoint_;
      ctx_.commander->move(scout_, wp, 24 * 2);
      return;
    }
    case State::Returning:
      finish(ctx_.info->enemyMainConfirmed() ? "enemy main found" : "scout returned early");
      return;
    default:
      return;
    }
  }
}
