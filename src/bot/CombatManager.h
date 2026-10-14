#pragma once
// Army control. State machine:
//   Gather  -> army waits at the rally point until the attack conditions hold
//   Attack  -> the wave moves to the chosen target, regrouping when spread out
//   Retreat -> back to the rally point after a lost fight
//   Defend  -> enemy units near our buildings
// Difficulty settings decide when to attack, how to pick targets in a fight
// (focus fire level), when to retreat and whether to stim / scan.

#include "BotContext.h"
#include "StrategyManager.h"

#include <BWAPI.h>

#include <set>
#include <vector>

namespace bb
{
  class CombatManager
  {
  public:
    explicit CombatManager(BotContext& ctx) : ctx_(ctx) {}

    void update();
    void onUnitDestroy(BWAPI::Unit unit);

    const char* stateName() const;
    int armySize() const { return static_cast<int>(army_.size()); }
    int waveSize() const { return static_cast<int>(wave_.size()); }
    const std::string& targetReason() const { return targetReason_; }

  private:
    enum class State { Gather, Attack, Retreat, Defend };

    void collectArmy();
    void useScans();
    void gather();
    void attack();
    void retreat();
    void defend();
    void startWave(const AttackTarget& target);
    void endWave(const char* reason, State next);
    bool pickNewTarget(BWAPI::Position from);

    // Unit-level fighting used by Attack and Defend.
    void fight(BWAPI::Unit u, BWAPI::Position advanceTo, double localOwn, double localEnemy);
    void moveMedic(BWAPI::Unit medic, BWAPI::Position groupCenter);
    BWAPI::Position groupCenter(const std::vector<BWAPI::Unit>& units) const;

    BotContext& ctx_;
    State state_ = State::Gather;
    std::vector<BWAPI::Unit> army_;
    std::set<int> wave_;
    int stateFrame_ = 0;
    int nextAttackAllowedFrame_ = 0;
    int lastScanFrame_ = -10000;
    int lastThreatFrame_ = -10000;
    int readySinceFrame_ = -1;  // first frame the attack conditions held in Gather
    bool regrouping_ = false;
    int regroupStartFrame_ = 0;
    int nextRegroupAllowedFrame_ = 0;
    bool loggedNoTarget_ = false;

    // Current wave
    BWAPI::Position target_;
    std::string targetReason_;
    int waveStartSize_ = 0;
    int waveLost_ = 0;
    int waveValueLost_ = 0;
    int waveKilled_ = 0;
    int waveValueKilled_ = 0;
    int lastProgressCheckFrame_ = 0;
    int bestDistanceToTarget_ = 0;
    int noProgressChecks_ = 0;
  };
}
