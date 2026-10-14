#pragma once
// Static map knowledge computed once at game start from information every player
// has: start locations, and the initial mineral/geyser layout (BWAPI exposes these
// as "static" units regardless of fog of war). No enemy positions are read here.

#include "BotContext.h"

#include <BWAPI.h>

#include <vector>

namespace bb
{
  struct BaseLocation
  {
    BWAPI::TilePosition depotTile = BWAPI::TilePositions::None; // where a Command Center fits
    BWAPI::Position center;                                    // center of that Command Center
    int mineralPatches = 0;
    int geysers = 0;
    bool isStartLocation = false;
    int lastSeenFrame = -1;                                     // last frame the depot tile was visible
  };

  class MapInfo
  {
  public:
    explicit MapInfo(BotContext& ctx) : ctx_(ctx) {}

    void onStart();
    void update(); // refreshes lastSeenFrame

    const std::vector<BaseLocation>& bases() const { return bases_; }
    std::vector<BaseLocation>& bases() { return bases_; }
    BWAPI::TilePosition mainTile() const { return mainTile_; }
    BWAPI::Position mainPosition() const;
    int naturalIndex() const { return natural_; }             // -1 if none was found
    const BaseLocation* natural() const;
    BWAPI::Position rallyPoint() const { return rally_; }      // where the army waits
    const std::vector<BWAPI::TilePosition>& otherStartLocations() const { return otherStarts_; }

    // Ground connection between two points (uses BWAPI's static region data).
    bool groundReachable(BWAPI::Position from, BWAPI::Position to) const;

  private:
    BWAPI::TilePosition findDepotTile(const std::vector<BWAPI::Position>& resources, BWAPI::Position resourceCenter) const;
    void computeRally();

    BotContext& ctx_;
    std::vector<BaseLocation> bases_;
    std::vector<BWAPI::TilePosition> otherStarts_;
    BWAPI::TilePosition mainTile_ = BWAPI::TilePositions::None;
    int natural_ = -1;
    BWAPI::Position rally_;
  };
}
