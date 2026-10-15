#include "WorkerManager.h"

#include "BwapiUtil.h"
#include "Commander.h"

#include <algorithm>
#include <limits>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kBaseRadius = 10 * 32;   // minerals/refineries this close to a depot belong to it
    constexpr int kGasWorkersPerRefinery = 3;
    // Gas bank at which gas workers move to minerals, and where they go back to gas.
    // Every infantry unit needs minerals; an unused gas bank only stands for minerals not mined.
    constexpr int kGasBankHigh = 800;
    constexpr int kGasBankLow = 300;
  }

  void WorkerManager::onUnitComplete(Unit unit)
  {
    if (isOurs(unit) && unit->getType().isWorker() && workers_.find(unit->getID()) == workers_.end())
      workers_[unit->getID()] = WorkerData{ unit, WorkerRole::Minerals, nullptr };
  }

  void WorkerManager::onUnitDestroy(Unit unit)
  {
    if (!unit)
      return;
    auto it = workers_.find(unit->getID());
    if (it != workers_.end())
    {
      setTarget(it->second, nullptr);
      workers_.erase(it);
    }
    mineralLoad_.erase(unit->getID()); // a mined-out mineral patch
  }

  void WorkerManager::setTarget(WorkerData& w, Unit target)
  {
    if (w.target && w.role == WorkerRole::Minerals)
    {
      auto it = mineralLoad_.find(w.target->getID());
      if (it != mineralLoad_.end() && it->second > 0)
        --it->second;
    }
    w.target = target;
    if (target && w.role == WorkerRole::Minerals)
      ++mineralLoad_[target->getID()];
  }

  int WorkerManager::countRole(WorkerRole role) const
  {
    int n = 0;
    for (const auto& [id, w] : workers_)
      if (w.role == role)
        ++n;
    return n;
  }

  void WorkerManager::syncWorkers()
  {
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType().isWorker() && u->isCompleted() && workers_.find(u->getID()) == workers_.end())
        workers_[u->getID()] = WorkerData{ u, WorkerRole::Minerals, nullptr };
    for (auto it = workers_.begin(); it != workers_.end();)
    {
      if (!isValid(it->second.unit))
      {
        setTarget(it->second, nullptr);
        it = workers_.erase(it);
      }
      else
        ++it;
    }
    ctx_.metrics.maxWorkers = std::max(ctx_.metrics.maxWorkers, static_cast<int>(workers_.size()));
  }

  void WorkerManager::refreshBases()
  {
    depots_.clear();
    bases_.clear();
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType().isResourceDepot() && u->isCompleted() && !u->isLifted())
        depots_.push_back(u);
    for (Unit d : depots_)
    {
      Base b;
      b.depot = d;
      for (Unit m : Broodwar->getMinerals())
        if (m->exists() && m->getDistance(d) < kBaseRadius)
          b.minerals.push_back(m);
      bases_.push_back(b);
    }
    mineralPatches_ = 0;
    mineralsLeft_ = 0;
    for (const auto& b : bases_)
    {
      mineralPatches_ += static_cast<int>(b.minerals.size());
      for (Unit m : b.minerals)
        mineralsLeft_ += m->getResources();
    }

    // Drop stale targets (mined-out patches) and recount assignments.
    mineralLoad_.clear();
    for (auto& [id, w] : workers_)
    {
      if (w.target && !w.target->exists())
        w.target = nullptr;
      if (w.role == WorkerRole::Minerals && w.target)
        ++mineralLoad_[w.target->getID()];
    }
    for (auto& b : bases_)
      for (Unit m : b.minerals)
        b.assigned += mineralLoad_[m->getID()];
  }

  int WorkerManager::baseOfMineral(Unit mineral) const
  {
    for (int i = 0; i < static_cast<int>(bases_.size()); ++i)
      for (Unit m : bases_[i].minerals)
        if (m == mineral)
          return i;
    return -1;
  }

  Unit WorkerManager::pickMineral(Position from, int* baseIndexOut)
  {
    // Least saturated base first, then the least used patch closest to the depot.
    int bestBase = -1;
    double bestRatio = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(bases_.size()); ++i)
    {
      if (bases_[i].minerals.empty())
        continue;
      double ratio = static_cast<double>(bases_[i].assigned) / bases_[i].minerals.size();
      // Slight preference for the closer base when saturation is similar.
      ratio += bases_[i].depot->getDistance(from) / 32.0 * 0.01;
      if (ratio < bestRatio)
      {
        bestRatio = ratio;
        bestBase = i;
      }
    }
    if (baseIndexOut)
      *baseIndexOut = bestBase;
    if (bestBase < 0)
      return nullptr;
    Unit best = nullptr;
    int bestLoad = std::numeric_limits<int>::max();
    int bestDist = std::numeric_limits<int>::max();
    for (Unit m : bases_[bestBase].minerals)
    {
      const int load = mineralLoad_[m->getID()];
      const int dist = m->getDistance(bases_[bestBase].depot);
      if (load < bestLoad || (load == bestLoad && dist < bestDist))
      {
        best = m;
        bestLoad = load;
        bestDist = dist;
      }
    }
    return best;
  }

  void WorkerManager::assignGas()
  {
    const int gas = Broodwar->self()->gas();
    // While the early army is short only a little gas is needed (Factory, Machine
    // Shop, a few Firebats/Medics); 3 SCVs on gas with 500 in the bank were 3 SCVs
    // short on minerals during a zealot rush.
    const int high = gasCap_ >= 0 ? std::min(kGasBankHigh, gasCap_) : kGasBankHigh;
    const int low = gasCap_ >= 0 ? std::min(kGasBankLow, gasCap_ / 2) : kGasBankLow;
    // At most one switch per 20 seconds: SCVs walking between gas and minerals mine
    // nothing (the cap went on and off every ten seconds once).
    const bool canSwitch = ctx_.frame >= nextGasSwitchFrame_;
    if (canSwitch && !gasPaused_ && gas >= high && mineralPatches_ > 0)
    {
      nextGasSwitchFrame_ = ctx_.frame + 24 * 20;
      gasPaused_ = true;
      ctx_.log.info("Workers", concat("gas bank ", gas, " is more than needed now (", high, "): gas workers move to minerals"));
    }
    else if (canSwitch && gasPaused_ && (gas < low || mineralPatches_ == 0))
    {
      nextGasSwitchFrame_ = ctx_.frame + 24 * 20;
      gasPaused_ = false;
      ctx_.log.info("Workers", concat("gas bank ", gas, ": mining gas again"));
    }
    const int perRefinery = gasPaused_ ? 0 : kGasWorkersPerRefinery;

    refineries_ = 0;
    for (Unit r : Broodwar->self()->getUnits())
    {
      if (!r->getType().isRefinery() || !r->isCompleted())
        continue;
      bool nearDepot = false;
      for (Unit d : depots_)
        nearDepot = nearDepot || d->getDistance(r) < kBaseRadius;
      if (!nearDepot)
        continue;
      ++refineries_;
      int assigned = 0;
      for (auto& [id, w] : workers_)
      {
        if (w.role != WorkerRole::Gas || w.target != r)
          continue;
        if (assigned < perRefinery)
          ++assigned;
        else
        {
          w.role = WorkerRole::Minerals; // delivers its gas first (handleMineralWorker)
          w.target = nullptr;
        }
      }
      while (assigned < perRefinery)
      {
        Unit worker = acquireWorker(r->getPosition(), WorkerRole::Gas);
        if (!worker)
          break;
        workers_[worker->getID()].target = r;
        ++assigned;
        ctx_.log.debug("Workers", concat("SCV#", worker->getID(), " -> gas at Refinery#", r->getID()));
      }
    }
    // Gas workers whose refinery is gone go back to minerals.
    for (auto& [id, w] : workers_)
      if (w.role == WorkerRole::Gas && !isOurs(w.target))
      {
        w.role = WorkerRole::Minerals;
        w.target = nullptr;
      }
  }

  void WorkerManager::handleGasWorker(WorkerData& w)
  {
    Unit u = w.unit;
    if (u->isGatheringGas() && !u->isIdle())
      return;
    if (u->isCarryingMinerals() || u->isCarryingGas())
      ctx_.commander->returnCargo(u);
    else
      ctx_.commander->gather(u, w.target);
  }

  void WorkerManager::handleMineralWorker(WorkerData& w)
  {
    Unit u = w.unit;
    if (u->isConstructing())
      return;   // still finishing a building; it mines afterwards
    if (bases_.empty())
      return; // no Command Center: nothing to mine until one is rebuilt

    // Long-distance rebalancing after an expansion finished (one worker per second).
    if (w.target && u->isGatheringMinerals() && ctx_.frame - lastRebalanceFrame_ >= 24 && bases_.size() > 1)
    {
      const int from = baseOfMineral(w.target);
      if (from >= 0 && !bases_[from].minerals.empty())
      {
        const double fromRatio = static_cast<double>(bases_[from].assigned) / bases_[from].minerals.size();
        for (int i = 0; i < static_cast<int>(bases_.size()); ++i)
        {
          if (i == from || bases_[i].minerals.empty())
            continue;
          const double toRatio = static_cast<double>(bases_[i].assigned) / bases_[i].minerals.size();
          if (fromRatio > 2.2 && toRatio < 1.5 && !u->isCarryingMinerals())
          {
            int baseIdx = -1;
            Unit m = pickMineral(bases_[i].depot->getPosition(), &baseIdx);
            if (m && baseIdx == i)
            {
              --bases_[from].assigned;
              ++bases_[i].assigned;
              setTarget(w, m);
              ctx_.commander->gather(u, m);
              lastRebalanceFrame_ = ctx_.frame;
              ctx_.log.debug("Workers", concat("SCV#", u->getID(), " transferred to base ", i));
              return;
            }
          }
        }
      }
    }

    if (u->isGatheringMinerals() && !u->isIdle())
      return;
    if (u->isCarryingMinerals() || u->isCarryingGas())
    {
      ctx_.commander->returnCargo(u);
      return;
    }
    if (!w.target || !w.target->exists())
    {
      int baseIdx = -1;
      Unit m = pickMineral(u->getPosition(), &baseIdx);
      if (!m)
        return;
      setTarget(w, m);
      if (baseIdx >= 0)
        ++bases_[baseIdx].assigned;
    }
    ctx_.commander->gather(u, w.target);
  }

  void WorkerManager::update()
  {
    syncWorkers();
    refreshBases();
    assignGas();
    for (auto& [id, w] : workers_)
    {
      if (!isValid(w.unit) || !w.unit->isCompleted())
        continue;
      if (w.role == WorkerRole::Minerals)
        handleMineralWorker(w);
      else if (w.role == WorkerRole::Gas)
        handleGasWorker(w);
      // Build, Scout, Repair and Defend workers are driven by their owners.
    }
  }

  Unit WorkerManager::acquireWorker(Position near, WorkerRole role)
  {
    WorkerData* best = nullptr;
    int bestScore = std::numeric_limits<int>::max();
    for (auto& [id, w] : workers_)
    {
      if (w.role != WorkerRole::Minerals || !isValid(w.unit) || !w.unit->isCompleted() || w.unit->isConstructing())
        continue;
      if (role == WorkerRole::Build)
      {
        auto avoid = avoidAsBuilderUntil_.find(id);
        if (avoid != avoidAsBuilderUntil_.end() && ctx_.frame < avoid->second)
          continue;
      }
      int score = near.isValid() ? w.unit->getDistance(near) : 0;
      if (w.unit->isCarryingMinerals())
        score += 64; // prefer workers that are not about to deliver
      if (score < bestScore)
      {
        bestScore = score;
        best = &w;
      }
    }
    if (!best)
      return nullptr;
    setTarget(*best, nullptr);
    best->role = role;
    return best->unit;
  }

  void WorkerManager::avoidAsBuilder(Unit worker, int untilFrame)
  {
    if (worker)
      avoidAsBuilderUntil_[worker->getID()] = untilFrame;
  }

  void WorkerManager::release(Unit worker)
  {
    if (!worker)
      return;
    auto it = workers_.find(worker->getID());
    if (it == workers_.end())
    {
      if (isOurs(worker) && worker->isCompleted())
        workers_[worker->getID()] = WorkerData{ worker, WorkerRole::Minerals, nullptr };
      return;
    }
    it->second.role = WorkerRole::Minerals;
    it->second.target = nullptr;
  }
}
