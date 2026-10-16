#include "MapInfo.h"

#include "BwapiUtil.h"
#include "Planning.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    bool walkableAt(Position p)
    {
      if (!p.isValid())
        return false;
      return Broodwar->isWalkable(p.x / 8, p.y / 8);
    }
  }

  Position MapInfo::mainPosition() const
  {
    return buildingCenter(mainTile_, UnitTypes::Terran_Command_Center);
  }

  const BaseLocation* MapInfo::natural() const
  {
    return natural_ >= 0 ? &bases_[natural_] : nullptr;
  }

  bool MapInfo::groundReachable(Position from, Position to) const
  {
    if (!from.isValid() || !to.isValid())
      return false;
    return Broodwar->hasPath(from, to);
  }

  void MapInfo::onStart()
  {
    mainTile_ = Broodwar->self()->getStartLocation();
    for (TilePosition t : Broodwar->getStartLocations())
      if (t != mainTile_)
        otherStarts_.push_back(t);

    // 1. Group the initial mineral fields into clusters (one cluster ~ one base).
    std::vector<Position> mineralPositions;
    std::vector<Point> points;
    for (Unit m : Broodwar->getStaticMinerals())
    {
      if (m->getInitialResources() <= 8) // tiny "blocker" minerals are not bases
        continue;
      mineralPositions.push_back(m->getInitialPosition());
      points.push_back(toPoint(m->getInitialPosition()));
    }
    std::vector<Position> geyserPositions;
    for (Unit g : Broodwar->getStaticGeysers())
      geyserPositions.push_back(g->getInitialPosition());

    const auto clusters = clusterPoints(points, 6 * 32);
    for (const auto& cluster : clusters)
    {
      if (cluster.size() < 4)
      {
        if (cluster.size() >= 2)
          ctx_.log.info("Map", concat("mineral group of ", cluster.size(), " near ", points[cluster[0]].x / 32, ",",
                                      points[cluster[0]].y / 32, " is not counted as a base"));
        continue;
      }
      std::vector<Point> clusterPoints;
      std::vector<Position> resources;
      for (int idx : cluster)
      {
        clusterPoints.push_back(points[idx]);
        resources.push_back(mineralPositions[idx]);
      }
      const Position mineralCenter = toPosition(centroid(clusterPoints));
      BaseLocation base;
      base.mineralPatches = static_cast<int>(cluster.size());
      for (const Position& g : geyserPositions)
        if (g.getApproxDistance(mineralCenter) < 12 * 32)
        {
          resources.push_back(g);
          ++base.geysers;
        }

      // Start locations are exact Command Center tiles; use them directly.
      for (TilePosition s : Broodwar->getStartLocations())
      {
        if (buildingCenter(s, UnitTypes::Terran_Command_Center).getApproxDistance(mineralCenter) < 14 * 32)
        {
          base.depotTile = s;
          base.isStartLocation = true;
          break;
        }
      }
      if (!base.isStartLocation)
        base.depotTile = findDepotTile(resources, mineralCenter);
      if (!base.depotTile.isValid() || base.depotTile == TilePositions::None)
      {
        ctx_.log.info("Map", concat("no Command Center spot found near minerals at ", mineralCenter.x / 32, ",", mineralCenter.y / 32));
        continue;
      }
      base.center = buildingCenter(base.depotTile, UnitTypes::Terran_Command_Center);
      bases_.push_back(base);
    }

    // Make sure every start location is known as a base even if its minerals were odd.
    for (TilePosition s : Broodwar->getStartLocations())
    {
      bool found = false;
      for (const auto& b : bases_)
        found = found || b.depotTile == s;
      if (!found)
      {
        BaseLocation base;
        base.depotTile = s;
        base.center = buildingCenter(s, UnitTypes::Terran_Command_Center);
        base.isStartLocation = true;
        bases_.push_back(base);
      }
    }

    // 2. Natural expansion: the closest reachable non-start base.
    const Position main = mainPosition();
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < static_cast<int>(bases_.size()); ++i)
    {
      if (bases_[i].isStartLocation)
        continue;
      const int d = main.getApproxDistance(bases_[i].center);
      if (d < bestDist && groundReachable(main, bases_[i].center))
      {
        bestDist = d;
        natural_ = i;
      }
    }

    computeRally();
    computeChoke();
    computeSearchCells();

    std::string list;
    for (const auto& b : bases_)
      list += concat(b.depotTile.x, ",", b.depotTile.y, "(", b.mineralPatches, "m", b.geysers, "g", b.isStartLocation ? " start" : "", ") ");
    ctx_.log.info("Map", concat("bases: ", list));

    ctx_.log.info("Map", concat("map=", Broodwar->mapFileName(), " size=", Broodwar->mapWidth(), "x", Broodwar->mapHeight(),
                                " startLocations=", Broodwar->getStartLocations().size(), " bases=", bases_.size(),
                                " main=", mainTile_.x, ",", mainTile_.y,
                                " natural=", (natural_ >= 0 ? concat(bases_[natural_].depotTile.x, ",", bases_[natural_].depotTile.y) : std::string("none")),
                                " rally=", rally_.x, ",", rally_.y));
  }

  TilePosition MapInfo::findDepotTile(const std::vector<Position>& resources, Position resourceCenter) const
  {
    const TilePosition c(resourceCenter);
    const UnitType cc = UnitTypes::Terran_Command_Center;
    TilePosition best = TilePositions::None;
    double bestScore = std::numeric_limits<double>::max();
    for (int y = c.y - 12; y <= c.y + 12; ++y)
      for (int x = c.x - 12; x <= c.x + 12; ++x)
      {
        const TilePosition t(x, y);
        if (!t.isValid())
          continue;
        // canBuildHere enforces Brood War's minimum distance between depots and resources.
        if (!Broodwar->canBuildHere(t, cc))
          continue;
        const Position center = buildingCenter(t, cc);
        double score = 0.0;
        for (const Position& r : resources)
          score += center.getDistance(r);
        if (score < bestScore)
        {
          bestScore = score;
          best = t;
        }
      }
    return best;
  }

  void MapInfo::computeRally()
  {
    const Position main = mainPosition();
    rally_ = main;
    if (const BaseLocation* nat = natural())
    {
      for (double f : { 0.4, 0.3, 0.5, 0.2, 0.6 })
      {
        const Position p(main.x + static_cast<int>((nat->center.x - main.x) * f),
                         main.y + static_cast<int>((nat->center.y - main.y) * f));
        if (walkableAt(p) && groundReachable(main, p))
        {
          rally_ = p;
          return;
        }
      }
    }
    // No natural: step from the main towards the map center.
    const Position mapCenter(Broodwar->mapWidth() * 16, Broodwar->mapHeight() * 16);
    const double dx = mapCenter.x - main.x, dy = mapCenter.y - main.y;
    const double len = std::max(1.0, std::sqrt(dx * dx + dy * dy));
    for (int tiles : { 8, 6, 4 })
    {
      const Position p(main.x + static_cast<int>(dx / len * tiles * 32), main.y + static_cast<int>(dy / len * tiles * 32));
      if (walkableAt(p) && groundReachable(main, p))
      {
        rally_ = p;
        return;
      }
    }
  }

  Position MapInfo::rallyPoint() const
  {
    if (naturalOwned_ && naturalRally_.isValid())
      return naturalRally_;
    return bunkerRally_.isValid() ? bunkerRally_ : rally_;
  }

  bool MapInfo::inMain(TilePosition t) const
  {
    if (mainArea_.empty() || !t.isValid())
      return false;
    return mainArea_[static_cast<size_t>(t.y) * Broodwar->mapWidth() + t.x] != 0;
  }

  void MapInfo::computeMainArea()
  {
    // Every level: Barracks and Factories go inside the main, behind the Bunker at
    // its entrance. "Behind the entrance" used to mean "no further from the Command
    // Center than the entrance", which on Boxer took tiles beside the main, in front
    // of the Bunker; a fill from the Command Center walled off at the entrance got
    // out to the natural. Inside the main is where the ground way to the natural
    // goes through the entrance: distance to the natural = distance to the entrance
    // + the entrance's distance to the natural (a few tiles of slack).
    if (natural_ < 0 || mainChoke_ == TilePositions::None)
      return;
    const int w = Broodwar->mapWidth(), h = Broodwar->mapHeight();
    const std::vector<char> blocked = blockedTiles();
    constexpr int kFar = std::numeric_limits<int>::max();
    auto index = [w](TilePosition t) { return static_cast<size_t>(t.y) * w + t.x; };
    auto distancesFrom = [&](TilePosition from) {
      std::vector<int> d(static_cast<size_t>(w) * h, kFar);
      std::vector<TilePosition> frontier{ from };
      d[index(from)] = 0;
      for (size_t q = 0; q < frontier.size(); ++q)
      {
        const TilePosition c = frontier[q];
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
          {
            const int nx = c.x + dx, ny = c.y + dy;
            if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h)
              continue;
            const size_t i = static_cast<size_t>(ny) * w + nx;
            // No cutting corners between two blocked tiles.
            if (blocked[i] || d[i] != kFar ||
                (dx != 0 && dy != 0 && (blocked[static_cast<size_t>(c.y) * w + nx] || blocked[static_cast<size_t>(ny) * w + c.x])))
              continue;
            d[i] = d[index(c)] + 1;
            frontier.push_back(TilePosition(nx, ny));
          }
      }
      return d;
    };
    const std::vector<int> toNatural = distancesFrom(TilePosition(bases_[natural_].center));
    const std::vector<int> toChoke = distancesFrom(mainChoke_);
    const int chokeToNatural = toNatural[index(mainChoke_)];
    if (chokeToNatural == kFar)
    {
      ctx_.log.info("Map", "main area: no ground way from the entrance to the natural; not used");
      return;
    }
    std::vector<char> area(static_cast<size_t>(w) * h, 0);
    int tiles = 0;
    for (size_t i = 0; i < area.size(); ++i)
      if (toNatural[i] != kFar && toChoke[i] != kFar && toChoke[i] >= 4 && toChoke[i] + chokeToNatural <= toNatural[i] + 3)
      {
        area[i] = 1;
        ++tiles;
      }
    const TilePosition cc(mainTile_.x + 2, mainTile_.y + 1);
    if (!area[index(cc)] || tiles > w * h / 4)
    {
      ctx_.log.info("Map", concat("main area: ", tiles, " tiles, the Command Center ", area[index(cc)] ? "inside" : "outside", "; not used"));
      return;
    }
    mainArea_ = std::move(area);
    ctx_.log.info("Map", concat("main area: ", tiles, " tiles behind the entrance"));
  }

  std::vector<char> MapInfo::blockedTiles() const
  {
    // Tiles a unit cannot stand on: static terrain, minerals and geysers.
    const int w = Broodwar->mapWidth(), h = Broodwar->mapHeight();
    std::vector<char> blocked(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
      {
        bool ok = true;
        for (int i = 1; i <= 2 && ok; ++i)
          for (int j = 1; j <= 2 && ok; ++j)
            ok = Broodwar->isWalkable(x * 4 + i, y * 4 + j);
        blocked[static_cast<size_t>(y) * w + x] = ok ? 0 : 1;
      }
    auto blockUnit = [&](Unit r) {
      const TilePosition t = r->getInitialTilePosition();
      const UnitType type = r->getInitialType();
      for (int y = t.y; y < t.y + type.tileHeight(); ++y)
        for (int x = t.x; x < t.x + type.tileWidth(); ++x)
          if (x >= 0 && y >= 0 && x < w && y < h)
            blocked[static_cast<size_t>(y) * w + x] = 1;
    };
    for (Unit m : Broodwar->getStaticMinerals())
      blockUnit(m);
    for (Unit g : Broodwar->getStaticGeysers())
      blockUnit(g);
    return blocked;
  }

  std::vector<TilePosition> MapInfo::groundPath(TilePosition from, TilePosition to) const
  {
    // Breadth-first search over walkable tiles. Small enough for 256x256 maps at game start.
    const int w = Broodwar->mapWidth(), h = Broodwar->mapHeight();
    std::vector<char> blocked = blockedTiles();
    auto idx = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
    std::vector<int> parent(static_cast<size_t>(w) * h, -2);
    std::vector<TilePosition> queue;
    if (!from.isValid() || !to.isValid())
      return {};
    blocked[idx(from.x, from.y)] = 0;
    blocked[idx(to.x, to.y)] = 0;
    parent[idx(from.x, from.y)] = -1;
    queue.push_back(from);
    for (size_t q = 0; q < queue.size(); ++q)
    {
      const TilePosition c = queue[q];
      if (c == to)
        break;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
          const int nx = c.x + dx, ny = c.y + dy;
          if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h || blocked[idx(nx, ny)] || parent[idx(nx, ny)] != -2)
            continue;
          if (dx != 0 && dy != 0 && (blocked[idx(c.x + dx, c.y)] || blocked[idx(c.x, c.y + dy)]))
            continue; // no corner cutting
          parent[idx(nx, ny)] = static_cast<int>(idx(c.x, c.y));
          queue.push_back(TilePosition(nx, ny));
        }
    }
    if (parent[idx(to.x, to.y)] == -2)
      return {};
    std::vector<TilePosition> path;
    for (int i = static_cast<int>(idx(to.x, to.y)); i >= 0; i = parent[static_cast<size_t>(i)])
      path.push_back(TilePosition(i % w, i / w));
    std::reverse(path.begin(), path.end());
    return path;
  }

  void MapInfo::computeChoke()
  {
    // The way out of the main: walk the ground path towards the enemy and take the
    // last tile that is still on the main's height level. The army waits a few
    // tiles inside it and the early Bunker covers it (the user: "defend the
    // entrance with the Bunker", not deep in the main behind the Barracks).
    TilePosition goal = TilePositions::None;
    int bestDist = std::numeric_limits<int>::max();
    for (TilePosition t : otherStarts_)
      if (mainTile_.getApproxDistance(t) < bestDist)
      {
        bestDist = mainTile_.getApproxDistance(t);
        goal = t;
      }
    if (goal == TilePositions::None && natural_ >= 0)
      goal = bases_[natural_].depotTile;
    const TilePosition start(mainPosition());
    const std::vector<TilePosition> path = groundPath(start, goal);

    bunkerSpot_ = rally_;
    if (path.empty())
    {
      ctx_.log.info("Map", "no ground path out of the main found: rally point kept");
      return;
    }
    // The entrance is where the way out is narrowest: clearance = distance to the
    // nearest unwalkable tile. A height drop (a ramp) counts too, but Boxer's mains
    // are on the same level as the rest, so 0.6.2's height test found nothing.
    const int w = Broodwar->mapWidth(), h = Broodwar->mapHeight();
    const std::vector<char> blocked = blockedTiles();
    std::vector<int> clearance(blocked.size(), std::numeric_limits<int>::max());
    std::vector<TilePosition> frontier;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (blocked[static_cast<size_t>(y) * w + x] || x == 0 || y == 0 || x == w - 1 || y == h - 1)
        {
          clearance[static_cast<size_t>(y) * w + x] = 0;
          frontier.push_back(TilePosition(x, y));
        }
    for (size_t q = 0; q < frontier.size(); ++q)
    {
      const TilePosition c = frontier[q];
      const int d = clearance[static_cast<size_t>(c.y) * w + c.x];
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
          const int nx = c.x + dx, ny = c.y + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h || clearance[static_cast<size_t>(ny) * w + nx] <= d + 1)
            continue;
          clearance[static_cast<size_t>(ny) * w + nx] = d + 1;
          frontier.push_back(TilePosition(nx, ny));
        }
    }
    const int mainLevel = Broodwar->getGroundHeight(mainTile_) / 2;
    const TilePosition natTile = natural_ >= 0 ? TilePosition(bases_[natural_].center) : TilePositions::None;
    int choke = -1, narrowest = std::numeric_limits<int>::max();
    for (size_t k = 1; k < path.size(); ++k)
    {
      const int fromMain = path[k].getApproxDistance(start);
      if (fromMain < 8)
        continue;
      if (fromMain > 40 || (natTile != TilePositions::None && path[k].getApproxDistance(natTile) < 8))
        break;
      if (Broodwar->getGroundHeight(path[k]) / 2 < mainLevel)
      {
        choke = static_cast<int>(k) - 1; // the top of a ramp down
        break;
      }
      const int c = clearance[static_cast<size_t>(path[k].y) * w + path[k].x];
      if (c < narrowest)
      {
        narrowest = c;
        choke = static_cast<int>(k);
      }
    }
    auto at = [&](int k) { return path[static_cast<size_t>(std::clamp(k, 0, static_cast<int>(path.size()) - 1))]; };
    auto center = [](TilePosition t) { return Position(t.x * 32 + 16, t.y * 32 + 16); };
    if (choke >= 4)
    {
      mainChoke_ = at(choke);
      mainEntranceDistance_ = mainChoke_.getApproxDistance(start);
      bunkerSpot_ = center(at(choke - 3));
      rally_ = center(at(choke - 4));
      mainMineField_ = center(at(choke + 3));
      ctx_.log.info("Map", concat("main entrance at ", mainChoke_.x, ",", mainChoke_.y, " (", narrowest == std::numeric_limits<int>::max() ? 0 : narrowest,
                                  " tiles clearance): rally ", rally_.x / 32, ",", rally_.y / 32, ", bunker near ", bunkerSpot_.x / 32, ",",
                                  bunkerSpot_.y / 32));
    }
    else
      ctx_.log.info("Map", "no entrance found on the way out of the main: rally point kept");
    if (choke >= 4)
      computeMainArea();

    // In front of the natural: the path tile closest to it, then 5 tiles further out.
    if (natural_ >= 0)
    {
      const TilePosition nat(bases_[natural_].center);
      int best = -1, bestD = std::numeric_limits<int>::max();
      for (size_t k = 0; k < path.size(); ++k)
        if (path[k].getApproxDistance(nat) < bestD)
        {
          bestD = path[k].getApproxDistance(nat);
          best = static_cast<int>(k);
        }
      naturalRally_ = bestD <= 12 ? center(at(best + 5)) : bases_[natural_].center;
      if (bestD <= 12)
        naturalMineField_ = center(at(best + 9));
    }
  }

  void MapInfo::computeSearchCells()
  {
    // 8x8-tile squares; in each the walkable point closest to its middle that the
    // main can reach by ground (islands and cliffs without a way up are skipped).
    constexpr int kCell = 8;
    const Position main = mainPosition();
    for (int cy = 0; cy < Broodwar->mapHeight(); cy += kCell)
      for (int cx = 0; cx < Broodwar->mapWidth(); cx += kCell)
      {
        bool found = false;
        for (int r = 0; r <= kCell / 2 && !found; ++r)
          for (int dy = -r; dy <= r && !found; ++dy)
            for (int dx = -r; dx <= r && !found; ++dx)
            {
              const Position p(((cx + kCell / 2 + dx) * 32) + 16, ((cy + kCell / 2 + dy) * 32) + 16);
              if (p.isValid() && walkableAt(p) && groundReachable(main, p))
              {
                searchCells_.push_back({ p, -1 });
                found = true;
              }
            }
      }
    ctx_.log.info("Map", concat(searchCells_.size(), " search cells for hidden buildings"));
  }

  void MapInfo::update()
  {
    if (natural_ >= 0)
    {
      bool owned = false;
      for (Unit u : Broodwar->self()->getUnits())
        owned = owned || (u->getType().isResourceDepot() && u->isCompleted() &&
                          u->getTilePosition().getApproxDistance(bases_[natural_].depotTile) <= 2);
      if (owned != naturalOwned_)
        ctx_.log.info("Map", owned ? concat("natural is ours: rally moves to ", naturalRally_.x, ",", naturalRally_.y)
                                   : std::string("natural lost: rally back at the main ramp"));
      naturalOwned_ = owned;
    }
    // The army waits right behind our Bunker at the entrance, so attackers at the
    // Bunker are in range of both. The Bunker once stood 7 tiles from the rally and
    // the zealots took it alone.
    Unit bunker = nullptr;
    if (bunkerSpot_.isValid())
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType() == UnitTypes::Terran_Bunker && u->isCompleted() && u->getDistance(bunkerSpot_) <= 12 * 32 &&
            (!bunker || u->getDistance(bunkerSpot_) < bunker->getDistance(bunkerSpot_)))
          bunker = u;
    const int bunkerId = bunker ? bunker->getID() : -1;
    if (bunkerId != bunkerRallyFor_)
    {
      bunkerRallyFor_ = bunkerId;
      bunkerRally_ = Positions::None;
      if (bunker)
      {
        const Position from = bunker->getPosition();
        const Position main = mainPosition();
        const double len = std::max(1.0, from.getDistance(main));
        for (int d = 64; d <= 128 && !bunkerRally_.isValid(); d += 16)
        {
          const Position p(from.x + static_cast<int>((main.x - from.x) * d / len), from.y + static_cast<int>((main.y - from.y) * d / len));
          if (p.isValid() && walkableAt(p) && groundReachable(main, p))
            bunkerRally_ = p;
        }
      }
      if (bunkerRally_.isValid())
        ctx_.log.info("Map", concat("rally right behind the Bunker at ", bunkerRally_.x, ",", bunkerRally_.y));
      else if (bunkerId < 0)
        ctx_.log.info("Map", "no Bunker at the entrance: rally back at the entrance");
    }
    for (auto& b : bases_)
      if (Broodwar->isVisible(b.depotTile))
        b.lastSeenFrame = ctx_.frame;
    for (auto& c : searchCells_)
      if (Broodwar->isVisible(TilePosition(c.point)))
        c.lastSeenFrame = ctx_.frame;
  }
}
