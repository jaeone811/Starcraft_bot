#include "SupplyManager.h"

#include "BuildingManager.h"
#include "BwapiUtil.h"
#include "Planning.h"

#include <BWAPI.h>

#include <algorithm>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kMaxExtraMarginUi = 6;
    constexpr int kSameBlockFrames = 24 * 20;
  }

  void SupplyManager::update(int elapsedFrames)
  {
    Player self = Broodwar->self();
    const int total = self->supplyTotal();
    const int used = self->supplyUsed();
    // Blocked = we cannot start even a Marine/SCV (2 supply) and are not maxed.
    // Zero total supply means every depot and Command Center is gone, not a block.
    const bool blockedNow = total > 0 && total < kMaxSupply && used + 2 > total;
    if (blockedNow)
    {
      ctx_.metrics.supplyBlockedFrames += elapsedFrames;
      // A block that comes back right after it ended (a unit started, supply freed,
      // the next one blocks again) is the same block: no new event, no new adaptation.
      if (!blocked_ && ctx_.frame - blockEndFrame_ > kSameBlockFrames)
      {
        ++ctx_.metrics.supplyBlockEvents;
        ctx_.log.info("Supply", concat("supply blocked at ", used / 2, "/", total / 2,
                                       " (pending +", ctx_.buildings->pendingSupply() / 2, ")"));
        if (ctx_.config.supplyMarginIncreaseOnBlock > 0 && extraMarginUi_ < kMaxExtraMarginUi)
        {
          extraMarginUi_ = std::min(kMaxExtraMarginUi, extraMarginUi_ + ctx_.config.supplyMarginIncreaseOnBlock);
          ctx_.log.info("Supply", concat("adapting: supply margin +", extraMarginUi_, " after a block"));
        }
      }
    }
    if (blocked_ && !blockedNow)
      blockEndFrame_ = ctx_.frame;
    blocked_ = blockedNow;
  }

  bool SupplyManager::depotNeeded() const
  {
    Player self = Broodwar->self();
    SupplyState s;
    s.used = self->supplyUsed();
    s.total = self->supplyTotal();
    s.pending = ctx_.buildings->pendingSupply();
    s.depotsInProgress = ctx_.buildings->inProgress(UnitTypes::Terran_Supply_Depot);

    // Current production capacity drives the forecast.
    std::vector<Producer> producers;
    for (Unit u : self->getUnits())
    {
      if (!u->isCompleted())
        continue;
      if (u->getType() == UnitTypes::Terran_Command_Center)
        producers.push_back({ UnitTypes::Terran_SCV.buildTime(), UnitTypes::Terran_SCV.supplyRequired() });
      else if (u->getType() == UnitTypes::Terran_Barracks)
        producers.push_back({ UnitTypes::Terran_Marine.buildTime(), UnitTypes::Terran_Marine.supplyRequired() });
      else if (u->getType() == UnitTypes::Terran_Factory)
        producers.push_back({ UnitTypes::Terran_Siege_Tank_Tank_Mode.buildTime(), UnitTypes::Terran_Siege_Tank_Tank_Mode.supplyRequired() });
    }
    return needSupplyDepot(s, producers, ctx_.config.supplyLookaheadFrames, ctx_.config.supplyFixedMargin,
                           extraMarginUi_, ctx_.config.maxConcurrentDepots, blocked_);
  }
}
