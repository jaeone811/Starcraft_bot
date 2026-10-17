#pragma once
// Worker scout. Starts at the difficulty's supply/frame trigger, visits start
// locations we have not seen, looks around the enemy main for a while (higher
// difficulties) and returns to mining. It never "knows" anything it did not see.

#include "BotContext.h"

#include <BWAPI.h>

#include <vector>

namespace bb
{
  class ScoutManager
  {
  public:
    explicit ScoutManager(BotContext& ctx) : ctx_(ctx) {}

    void update();
    void onUnitDestroy(BWAPI::Unit unit);
    const char* stateName() const;

  private:
    enum class State { NotStarted, Exploring, Observing, Returning, Done };

    bool shouldStart() const;
    void finish(const char* reason);

    BotContext& ctx_;
    State state_ = State::NotStarted;
    BWAPI::Unit scout_ = nullptr;
    BWAPI::Position target_;
    int observeStartFrame_ = 0;
    int observeWaypoint_ = 0;
    int deathFrame_ = -1;
  };
}
