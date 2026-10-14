#pragma once
// BWAPI AI module: wires the managers together, handles BWAPI events, logging and
// error isolation (an exception in one manager never stops the others).

#include "BotContext.h"

#include <BWAPI.h>

#include <memory>

namespace bb
{
  class BeginnerBot : public BWAPI::AIModule
  {
  public:
    BeginnerBot();
    ~BeginnerBot() override;

    void onStart() override;
    void onEnd(bool isWinner) override;
    void onFrame() override;
    void onSendText(std::string text) override;
    void onPlayerLeft(BWAPI::Player player) override;
    void onUnitDiscover(BWAPI::Unit unit) override;
    void onUnitShow(BWAPI::Unit unit) override;
    void onUnitCreate(BWAPI::Unit unit) override;
    void onUnitDestroy(BWAPI::Unit unit) override;
    void onUnitMorph(BWAPI::Unit unit) override;
    void onUnitRenegade(BWAPI::Unit unit) override;
    void onUnitComplete(BWAPI::Unit unit) override;

  private:
    template <typename F>
    void guarded(const char* where, F&& f);
    void createModules();
    void drawDebug();
    void logSnapshot();
    void writeResultRow(bool isWinner);

    BotContext ctx_;
    bool active_ = false;        // false for replays / unsupported race
    bool workersOnly_ = false;   // non-Terran race: keep mining, nothing else
    int lastFrameRun_ = 0;
    int lastSnapshotFrame_ = 0;
    int lastEconomyFrame_ = -100;
    int lastProductionFrame_ = -100;
    int lastCombatFrame_ = -100;
    int lastInfoFrame_ = -100;

    std::unique_ptr<Commander> commander_;
    std::unique_ptr<MapInfo> map_;
    std::unique_ptr<InformationManager> info_;
    std::unique_ptr<WorkerManager> workers_;
    std::unique_ptr<BuildingManager> buildings_;
    std::unique_ptr<SupplyManager> supply_;
    std::unique_ptr<ProductionManager> production_;
    std::unique_ptr<ScoutManager> scout_;
    std::unique_ptr<CombatManager> combat_;
    std::unique_ptr<StrategyManager> strategy_;
  };
}
