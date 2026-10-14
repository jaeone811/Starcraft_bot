#pragma once
// Strategic state that changes during the game: the current attack conditions
// (adapted after failed waves), attack target selection from known information,
// targets found unreachable, and base threat detection.

#include "BotContext.h"

#include <BWAPI.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bb
{
  struct AttackTarget
  {
    BWAPI::Position position;
    std::string reason;
  };

  class StrategyManager
  {
  public:
    explicit StrategyManager(BotContext& ctx);

    void update();

    int attackSize() const { return attackSize_; }
    double attackStrengthRatio() const { return strengthRatio_; }

    // Called by the combat manager when a wave ends.
    void onWaveEnd(int unitsLost, int enemyKilled, int valueLost, int valueKilled, const char* reason);

    // Picks where the army should go. Only uses buildings we have seen, start
    // locations, and expansions we have not looked at recently.
    std::optional<AttackTarget> chooseAttackTarget(BWAPI::Position from) const;
    void markUnreachable(BWAPI::Position target, int frames);
    bool isMarkedUnreachable(BWAPI::Position target) const;

    // Visible enemy units close to any of our buildings.
    const std::vector<BWAPI::Unit>& baseThreats() const { return threats_; }
    bool baseUnderThreat() const { return !threats_.empty(); }
    BWAPI::Position threatCenter() const { return threatCenter_; }

    // Whether firebats should be mixed in (enemy melee units were seen).
    double firebatShare() const;

  private:
    void detectThreats();

    BotContext& ctx_;
    int attackSize_;
    double strengthRatio_;
    std::map<std::pair<int, int>, int> unreachableUntil_; // tile -> frame
    std::vector<BWAPI::Unit> threats_;
    BWAPI::Position threatCenter_;
  };
}
