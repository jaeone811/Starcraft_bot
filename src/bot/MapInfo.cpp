#include "MapInfo.h"

#include "BwapiUtil.h"
#include "Planning.h"

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
        continue;
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
        ctx_.log.debug("Map", concat("no Command Center spot found near minerals at ", mineralCenter.x, ",", mineralCenter.y));
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
    computeSearchCells();

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
    for (auto& b : bases_)
      if (Broodwar->isVisible(b.depotTile))
        b.lastSeenFrame = ctx_.frame;
    for (auto& c : searchCells_)
      if (Broodwar->isVisible(TilePosition(c.point)))
        c.lastSeenFrame = ctx_.frame;
  }
}
