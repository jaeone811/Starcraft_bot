#pragma once
// Construction tasks. A task reserves its cost as soon as it is created (Brood War
// only deducts money when the SCV actually starts building), picks a location and
// a builder, and survives the usual failures: blocked spot, builder killed,
// building destroyed while under construction, construction abandoned.

#include "BotContext.h"

#include <BWAPI.h>

#include <map>
#include <vector>

namespace bb
{
  class BuildingManager
  {
  public:
    explicit BuildingManager(BotContext& ctx) : ctx_(ctx) {}

    void update();

    // Creates a task. `desired` is an optional preferred tile (e.g. an expansion).
    // Returns false if the type is on a failure cooldown.
    bool request(BWAPI::UnitType type, BWAPI::TilePosition desired = BWAPI::TilePositions::None);
    bool onCooldown(BWAPI::UnitType type) const;

    // Tasks that have not started construction yet (their cost is reserved).
    int notStarted(BWAPI::UnitType type) const;
    // Tasks still in progress (not started or under construction).
    int inProgress(BWAPI::UnitType type) const;
    int reservedMinerals() const;
    int reservedGas() const;
    // Supply that will be added by depots/CCs that are planned or being built (doubled units).
    int pendingSupply() const;
    int taskCount() const { return static_cast<int>(tasks_.size()); }

  private:
    enum class State { WaitingForBuilder, Assigned, UnderConstruction };
    struct Task
    {
      int id = 0;
      BWAPI::UnitType type;
      BWAPI::TilePosition desired = BWAPI::TilePositions::None;
      BWAPI::TilePosition tile = BWAPI::TilePositions::None;
      State state = State::WaitingForBuilder;
      BWAPI::Unit builder = nullptr;
      BWAPI::Unit building = nullptr;
      int stateFrame = 0;      // frame the current state began
      int placementAttempts = 0;
      int lastProgressFrame = 0;
      int reissues = 0;        // build orders repeated while Assigned
    };

    BWAPI::TilePosition findLocation(const Task& task) const;
    bool overlapsOtherTask(const Task& task, BWAPI::TilePosition tile) const;
    bool keepsExitsOpen(const Task& task, BWAPI::TilePosition tile) const;
    bool recentlyBlocked(BWAPI::TilePosition tile) const;
    void updateTask(Task& task, bool& remove);
    void startConstructionCheck(Task& task);
    void fail(Task& task, const char* reason);
    void releaseBuilder(Task& task);

    BotContext& ctx_;
    std::vector<Task> tasks_;
    std::map<int, int> cooldownUntil_; // unit type id -> frame
    std::vector<std::pair<BWAPI::TilePosition, int>> blockedTiles_; // spot -> avoid until frame
    int nextId_ = 1;
  };
}
