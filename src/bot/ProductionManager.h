#pragma once
// Production: decides what to spend money on each round, in priority order:
//   supply depot > (army, if threatened and the difficulty says so) > SCVs >
//   build order / expansion / extra barracks > research > Comsat > army.
// With strictPriority an unaffordable higher item holds back its cost so cheaper
// items do not keep delaying it.

#include "BotContext.h"
#include "Planning.h"

#include <BWAPI.h>

namespace bb
{
  struct BaseLocation;

  class ProductionManager
  {
  public:
    explicit ProductionManager(BotContext& ctx) : ctx_(ctx) {}

    void onStart();
    void update(int elapsedFrames);
    const std::string& lastPlan() const { return lastPlan_; }

  private:
    void planSupply(Budget& budget);
    void ensureCommandCenter(Budget& budget);
    void trainWorkers(Budget& budget);
    void followBuildOrder(Budget& budget);
    void macroBuildings(Budget& budget);
    void replaceMinedOutBase(Budget& budget);
    void doResearch(Budget& budget);
    void buildComsat(Budget& budget);
    void trainArmy(Budget& budget);
    void trainInfantry(Budget& budget);
    void trainFactories(Budget& budget, bool trainUnits);
    void measureIdle(int elapsedFrames, const Budget& budget);

    int planned(BWAPI::UnitType type) const;
    bool requirementsMet(BWAPI::UnitType type) const;
    bool freeGeyserAvailable() const;
    bool supplyStalled();
    const BaseLocation* nextExpansion() const;

    BotContext& ctx_;
    std::string lastPlan_;
    bool loggedFirstArmy_ = false;
    int comsatRetryFrame_ = 0;
    bool loggedNoExpansion_ = false;
    bool loggedFirstMech_ = false;
    int machineShopRetryFrame_ = 0;
    int stalledSinceFrame_ = -1;
    int loggedStallStep_ = -1;
  };
}
