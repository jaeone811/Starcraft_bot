#pragma once
// Economy: keeps every SCV busy. Mineral workers are spread evenly over the mineral
// patches of our bases, each completed refinery gets 3 gas workers, and other
// modules borrow workers (builders, scout) through acquireWorker()/release().

#include "BotContext.h"

#include <BWAPI.h>

#include <map>
#include <vector>

namespace bb
{
  enum class WorkerRole { Minerals, Gas, Build, Scout, Repair };

  class WorkerManager
  {
  public:
    explicit WorkerManager(BotContext& ctx) : ctx_(ctx) {}

    void update();
    void onUnitComplete(BWAPI::Unit unit);
    void onUnitDestroy(BWAPI::Unit unit);

    // Takes the most suitable mineral worker near `near`; nullptr if none is free.
    BWAPI::Unit acquireWorker(BWAPI::Position near, WorkerRole role);
    void release(BWAPI::Unit worker);
    // A worker that refused a build order (Unit_Busy) is not chosen as a builder again
    // until `untilFrame`; it was picked as the closest one every time before.
    void avoidAsBuilder(BWAPI::Unit worker, int untilFrame);

    int workerCount() const { return static_cast<int>(workers_.size()); }
    int countRole(WorkerRole role) const;
    int mineralPatchesAtBases() const { return mineralPatches_; }
    int mineralsLeftAtBases() const { return mineralsLeft_; }   // remaining minerals at our completed bases
    int gasRefineries() const { return refineries_; }
    const std::vector<BWAPI::Unit>& depots() const { return depots_; }

  private:
    struct WorkerData
    {
      BWAPI::Unit unit = nullptr;
      WorkerRole role = WorkerRole::Minerals;
      BWAPI::Unit target = nullptr;
    };
    struct Base
    {
      BWAPI::Unit depot = nullptr;
      std::vector<BWAPI::Unit> minerals;
      int assigned = 0;
    };

    void syncWorkers();
    void refreshBases();
    void assignGas();
    void handleMineralWorker(WorkerData& w);
    void handleGasWorker(WorkerData& w);
    BWAPI::Unit pickMineral(BWAPI::Position from, int* baseIndexOut);
    void setTarget(WorkerData& w, BWAPI::Unit target);
    int baseOfMineral(BWAPI::Unit mineral) const;

    BotContext& ctx_;
    std::map<int, WorkerData> workers_;
    std::map<int, int> mineralLoad_;
    std::map<int, int> avoidAsBuilderUntil_;   // worker id -> frame
    std::vector<Base> bases_;
    std::vector<BWAPI::Unit> depots_;
    int mineralPatches_ = 0;
    int mineralsLeft_ = 0;
    int refineries_ = 0;
    bool gasPaused_ = false;
    int lastRebalanceFrame_ = 0;
  };
}
