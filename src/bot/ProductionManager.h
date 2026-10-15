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
    void buildDetection(Budget& budget);
    void trainWorkers(Budget& budget);
    void followBuildOrder(Budget& budget);
    void macroBuildings(Budget& budget);
    void replaceMinedOutBase(Budget& budget);
    void doResearch(Budget& budget);
    void buildComsat(Budget& budget);
    void trainArmy(Budget& budget);
    void trainInfantry(Budget& budget);
    void trainFactories(Budget& budget, bool trainUnits);
    void trainStarports(Budget& budget);
    void measureIdle(int elapsedFrames, const Budget& budget);

    int planned(BWAPI::UnitType type) const;
    bool requirementsMet(BWAPI::UnitType type) const;
    bool freeGeyserAvailable() const;
    bool supplyStalled();
    int fightingUnits() const;
    void buildBunker(Budget& budget);
    const BaseLocation* nextExpansion() const;
    double ownArmyStrength() const;     // finished army, combined (Planning::combineStrength)

    BotContext& ctx_;
    std::string lastPlan_;
    bool loggedFirstArmy_ = false;
    int comsatRetryFrame_ = 0;
    bool loggedNoExpansion_ = false;
    bool loggedFirstMech_ = false;
    bool loggedFirstAir_ = false;
    int machineShopRetryFrame_ = 0;
    int stalledSinceFrame_ = -1;
    int lastExpansionFrame_ = -1;
    mutable int nextContestedLogFrame_ = 0;
    bool loggedEarlyDefense_ = false;
    bool holdTech_ = false;
    bool loggedHoldTech_ = false;
    bool loggedRangeFirst_ = false;
    bool rush_ = false;       // an early attack is expected (seen by the scout, levels that use what they see)
    int earlyGasNeed() const;   // research and tech buildings wait: the early army (or the defense) comes first
    int loggedStallStep_ = -1;
  };
}
