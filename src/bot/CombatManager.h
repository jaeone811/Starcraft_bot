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

#include <map>
#include <optional>
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
    enum class TankMode { Mobile, Sieged, Switching };

    // Siege / unsiege decision for one tank; Mobile means it may take move orders.
    TankMode updateSiege(BWAPI::Unit tank);

    void collectArmy();
    void manageBunker();
    void chaseWorkerHarass();
    void releaseChasers();
    void layMines();
    void repairBunker(BWAPI::Unit bunker);
    void releaseRepairers();
    void useScans();
    void dodgeScarabs();
    bool dodging(BWAPI::Unit u) const;
    void gather();
    void attack();
    void retreat();
    void defend();
    void startWave(const AttackTarget& target);
    void endWave(const char* reason, State next);
    void holdPosition(BWAPI::Position p);
    bool pickNewTarget(BWAPI::Position from);
    // An enemy building at our doorstep (within 30 tiles of the main or 12 of the
    // natural), nearest first; with checkStrength only if the army can clear it.
    std::optional<AttackTarget> doorstepTarget(bool checkStrength) const;
    // A sieged Tank stays where it is (updateSiege packs it up when nothing is near).
    bool staysSieged(BWAPI::Unit u);
    // A Comsat with energy for a Scanner Sweep.
    bool scanReady() const;
    // The nearest remembered sieged enemy Tank (seen within the last minute) within
    // `radius` of `p`, or None.
    BWAPI::Position siegedTankNear(BWAPI::Position p, int radius) const;
    // `spot`, moved out of the reach of a sieged enemy Tank (the main if that fails).
    BWAPI::Position outOfTankRange(BWAPI::Position spot) const;
    // Remembered enemy fighting units within 12 tiles of `p` (last minute).
    double rememberedEnemyStrengthNear(BWAPI::Position p) const;

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
    BWAPI::Position holdPoint_ = BWAPI::Positions::None;  // where a wave that cleared its targets waits
    int holdSinceFrame_ = 0;
    BWAPI::Position retreatFrom_ = BWAPI::Positions::None;
    int safeSinceFrame_ = -1;
    std::set<int> bunkerCrew_;          // marines sent into the Bunker (kept out of the army)
    bool bunkerReleased_ = false;
    std::map<int, int> crewSince_;      // marine id -> frame it was sent to the Bunker
    std::set<int> notCrew_;             // marines that did not reach the Bunker (e.g. boxed in)
    std::set<int> repairers_;           // SCVs repairing the Bunker
    bool holdingAtBunker_ = false;
    bool holdingOutmatched_ = false;    // "outmatched outside: waiting at the rally" logged per defense
    std::set<int> leashed_;             // units called back from chasing away from our buildings
    int leashCalls_ = 0;                // logged when the defense ends
    int nextRepairLogFrame_ = 0;
    BWAPI::Position fightCenter_ = BWAPI::Positions::None;   // group center while a wave/the defense fights
    int kiteSteps_ = 0;                 // back-off steps from melee units (reported per wave)
    BWAPI::Position guardedBase_ = BWAPI::Positions::None;
    bool loggedAirInBase_ = false;      // "flyers in the base" logged once
    std::map<int, int> dodgeUntil_;     // unit id -> frame it may take orders again (Scarab dodge)
    int dodges_ = 0;                    // units that stepped aside from Scarabs (reported per wave)
    bool doorstepWave_ = false;         // the wave clears our doorstep only (no targets beyond it)
    int hiddenSteps_ = 0;               // steps away from undetected attackers (logged when the defense ends)
    int nextDodgeLogFrame_ = 0;
    int nextNoScanLogFrame_ = 0;
    int nextTankRepairLogFrame_ = 0;
    bool holdingOutOfTankRange_ = false;   // "waits out of the Tanks' reach" logged once per contain
    bool goingForTank_ = false;            // "goes for the sieged Tank" logged once per contain
    int harasserId_ = -1;                // enemy worker attacking our SCVs in a base
    int harassSinceFrame_ = 0;
    std::set<int> chasers_;             // SCVs sent after it
    struct MineLayer { int since = 0; int until = 0; };
    std::map<int, MineLayer> mineLayers_;   // Vultures planting a mine (kept out of the army meanwhile)
    std::map<int, int> nextMineFrame_;      // Vulture id -> frame it may lay a combat mine again
    int nextFieldMineFrame_ = 0;
    bool loggedMineField_ = false;
    int ignoreThreatUntilFrame_ = 0;   // a threat too far from every base is not chased for a while
    int regroupStartFrame_ = 0;
    int nextRegroupAllowedFrame_ = 0;
    bool loggedNoTarget_ = false;

    // Current wave
    BWAPI::Position target_;
    std::string targetReason_;
    int waveStartSize_ = 0;
    int waveCommitted_ = 0;   // start size + every reinforcement group that joined
    int waveLost_ = 0;
    int waveValueLost_ = 0;
    int waveKilled_ = 0;
    int waveValueKilled_ = 0;
    int lastProgressCheckFrame_ = 0;
    int bestDistanceToTarget_ = 0;
    int bestLeadDistance_ = 0;
    int noProgressChecks_ = 0;
    int regroups_ = 0;
    int sieges_ = 0;
    std::map<int, int> tankThreatFrame_; // tank id -> last frame a threat was in siege range
  };
}
