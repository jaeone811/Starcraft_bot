#pragma once
// The only place that sends unit commands to BWAPI. Every command goes through
// CommandHistory so repeated identical orders are dropped, and failures are
// logged with BWAPI's error text instead of being silently retried every frame.

#include "BotContext.h"

#include <BWAPI.h>

namespace bb
{
  class Commander
  {
  public:
    explicit Commander(BotContext& ctx) : ctx_(ctx) {}

    // Cooldowns are in frames: an identical command to the same unit is not
    // re-sent before the cooldown passes.
    bool move(BWAPI::Unit u, BWAPI::Position p, int cooldown = 48);
    bool attackMove(BWAPI::Unit u, BWAPI::Position p, int cooldown = 48);
    bool attackUnit(BWAPI::Unit u, BWAPI::Unit target, int cooldown = 24);
    bool gather(BWAPI::Unit worker, BWAPI::Unit resource, int cooldown = 96);
    bool returnCargo(BWAPI::Unit worker, int cooldown = 48);
    bool build(BWAPI::Unit worker, BWAPI::UnitType type, BWAPI::TilePosition tile, int cooldown = 72);
    bool resumeConstruction(BWAPI::Unit worker, BWAPI::Unit building, int cooldown = 72);
    bool train(BWAPI::Unit producer, BWAPI::UnitType type, int cooldown = 12);
    bool buildAddon(BWAPI::Unit producer, BWAPI::UnitType type, int cooldown = 48);
    bool research(BWAPI::Unit building, BWAPI::TechType tech, int cooldown = 48);
    bool upgrade(BWAPI::Unit building, BWAPI::UpgradeType upgrade, int cooldown = 48);
    bool useTech(BWAPI::Unit u, BWAPI::TechType tech, int cooldown = 72);
    bool useTechAt(BWAPI::Unit u, BWAPI::TechType tech, BWAPI::Position p, int cooldown = 72);
    bool setRally(BWAPI::Unit building, BWAPI::Position p, int cooldown = 240);
    bool stop(BWAPI::Unit u, int cooldown = 24);
    bool siege(BWAPI::Unit tank, int cooldown = 48);
    bool unsiege(BWAPI::Unit tank, int cooldown = 48);
    bool enterBunker(BWAPI::Unit u, BWAPI::Unit bunker, int cooldown = 48);
    bool unloadAll(BWAPI::Unit bunker, int cooldown = 48);

    void forget(BWAPI::Unit u);
    // True if the last call was dropped as a duplicate (not sent, not a failure).
    bool lastWasSuppressed() const { return lastSuppressed_; }

  private:
    template <typename Send>
    bool issue(BWAPI::Unit u, const CommandKey& key, int cooldown, const char* what, Send send);
    bool alreadyHeading(BWAPI::Unit u, BWAPI::Order order, BWAPI::Position p);

    BotContext& ctx_;
    bool lastSuppressed_ = false;
  };
}
