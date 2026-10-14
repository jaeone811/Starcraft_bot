#pragma once
// Supply planning: decides when a Supply Depot is needed using the difficulty's
// margin/forecast settings, detects supply blocks and (on higher difficulties)
// widens the margin after getting blocked.

#include "BotContext.h"

namespace bb
{
  class SupplyManager
  {
  public:
    explicit SupplyManager(BotContext& ctx) : ctx_(ctx) {}

    void update(int elapsedFrames);
    // True when the production manager should queue a depot this round.
    bool depotNeeded() const;
    bool supplyBlocked() const { return blocked_; }
    int extraMarginUi() const { return extraMarginUi_; }

  private:
    BotContext& ctx_;
    bool blocked_ = false;
    int extraMarginUi_ = 0;
  };
}
