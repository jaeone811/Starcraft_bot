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

  // A square of the map for searching hidden buildings: a ground-reachable point
  // in it and the last frame that point was visible.
  struct SearchCell
  {
    BWAPI::Position point;
    int lastSeenFrame = -1;
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
    // Where the army waits: the top of the main's ramp, and once the natural is ours,
    // in front of the natural.
    BWAPI::Position rallyPoint() const;
    BWAPI::TilePosition mainChoke() const { return mainChoke_; } // the main's entrance (narrowest point on the way out); None if not found
    BWAPI::Position bunkerSpot() const { return bunkerSpot_; }   // just inside the ramp, covering it
    const std::vector<BWAPI::TilePosition>& otherStartLocations() const { return otherStarts_; }
    const std::vector<SearchCell>& searchCells() const { return searchCells_; }

    // Ground connection between two points (uses BWAPI's static region data).
    bool groundReachable(BWAPI::Position from, BWAPI::Position to) const;

  private:
    BWAPI::TilePosition findDepotTile(const std::vector<BWAPI::Position>& resources, BWAPI::Position resourceCenter) const;
    void computeRally();
    void computeSearchCells();
    void computeChoke();
    std::vector<BWAPI::TilePosition> groundPath(BWAPI::TilePosition from, BWAPI::TilePosition to) const;
    std::vector<char> blockedTiles() const;

    BotContext& ctx_;
    std::vector<BaseLocation> bases_;
    std::vector<BWAPI::TilePosition> otherStarts_;
    BWAPI::TilePosition mainTile_ = BWAPI::TilePositions::None;
    int natural_ = -1;
    BWAPI::Position rally_;
    std::vector<SearchCell> searchCells_;
    BWAPI::TilePosition mainChoke_ = BWAPI::TilePositions::None;
    BWAPI::Position bunkerSpot_ = BWAPI::Positions::None;
    BWAPI::Position naturalRally_ = BWAPI::Positions::None;
    bool naturalOwned_ = false;
    int mainEntranceDistance_ = -1;
  public:
    // Air distance (tiles) from the main's Command Center to its entrance; -1 if unknown.
    int mainEntranceDistance() const { return mainEntranceDistance_; }
  };
}
