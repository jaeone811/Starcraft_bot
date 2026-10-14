#include "ProductionManager.h"

#include "BuildingManager.h"
#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "StrategyManager.h"
#include "SupplyManager.h"
#include "WorkerManager.h"

#include <limits>
#include <map>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    Cost costOf(UnitType t) { return { t.mineralPrice(), t.gasPrice(), t.supplyRequired() }; }
    Cost costOf(TechType t) { return { t.mineralPrice(), t.gasPrice(), 0 }; }
    Cost costOf(UpgradeType t) { return { t.mineralPrice(1), t.gasPrice(1), 0 }; }

    // Below this many minerals left at our bases a new base is started: a Command
    // Center takes 75 s to build plus the walk, roughly what ~2 minutes of mining leaves.
    constexpr int kLowMineralsLeft = 3000;

    bool idleProducer(Unit u)
    {
      return isValid(u) && u->isCompleted() && !u->isLifted() && u->getTrainingQueue().empty() &&
             !u->isConstructing() && !u->isResearching() && !u->isUpgrading();
    }
  }

  void ProductionManager::onStart()
  {
    ctx_.log.info("Production", concat("worker target uses ", ctx_.config.workersPerMineralPatch,
                                       " per patch (max ", ctx_.config.maxWorkers, "), strictPriority=",
                                       ctx_.config.strictPriority));
  }

  int ProductionManager::planned(UnitType type) const
  {
    return Broodwar->self()->allUnitCount(type) + ctx_.buildings->notStarted(type);
  }

  bool ProductionManager::requirementsMet(UnitType type) const
  {
    for (const auto& [req, count] : type.requiredUnits())
      if (req.isBuilding() && Broodwar->self()->completedUnitCount(req) < count)
        return false;
    return true;
  }

  bool ProductionManager::freeGeyserAvailable() const
  {
    for (Unit depot : ctx_.workers->depots())
      for (Unit g : Broodwar->getGeysers())
        if (g->getType() == UnitTypes::Resource_Vespene_Geyser && g->getDistance(depot) <= 10 * 32)
          return true;
    return false;
  }

  void ProductionManager::update(int elapsedFrames)
  {
    Player self = Broodwar->self();
    Budget budget(self->minerals() - ctx_.buildings->reservedMinerals(), self->gas() - ctx_.buildings->reservedGas(),
                  self->supplyTotal() - self->supplyUsed(), ctx_.config.strictPriority);
    lastPlan_.clear();

    const bool armyFirst = ctx_.strategy->baseUnderThreat() && ctx_.config.armyFirstWhenThreatened;
    planSupply(budget);
    ensureCommandCenter(budget);
    if (armyFirst)
      trainArmy(budget);
    trainWorkers(budget);
    followBuildOrder(budget);
    macroBuildings(budget);
    doResearch(budget);
    buildComsat(budget);
    if (!armyFirst)
      trainArmy(budget);
    measureIdle(elapsedFrames);
  }

  void ProductionManager::planSupply(Budget& budget)
  {
    const UnitType depot = UnitTypes::Terran_Supply_Depot;
    if (!ctx_.supply->depotNeeded() || ctx_.buildings->onCooldown(depot) || ctx_.workers->workerCount() == 0)
      return;
    if (budget.tryReserve({ depot.mineralPrice(), 0, 0 }))
    {
      ctx_.buildings->request(depot);
      lastPlan_ += "depot ";
    }
    else
      lastPlan_ += "(saving:depot) ";
  }

  void ProductionManager::ensureCommandCenter(Budget& budget)
  {
    // Lost every Command Center: rebuilding one has top priority after supply.
    const UnitType cc = UnitTypes::Terran_Command_Center;
    if (planned(cc) > 0 || ctx_.workers->workerCount() == 0 || ctx_.buildings->onCooldown(cc))
      return;
    if (budget.tryReserve({ cc.mineralPrice(), 0, 0 }))
    {
      ctx_.log.warn("Production", "no Command Center left; rebuilding at the main");
      ctx_.buildings->request(cc, ctx_.map->mainTile());
    }
  }

  void ProductionManager::trainWorkers(Budget& budget)
  {
    Player self = Broodwar->self();
    const UnitType scv = UnitTypes::Terran_SCV;
    const int target = workerTarget(ctx_.workers->mineralPatchesAtBases(), ctx_.workers->gasRefineries(),
                                     ctx_.config.workersPerMineralPatch, ctx_.config.maxWorkers);
    int current = self->allUnitCount(scv);
    for (Unit cc : self->getUnits())
    {
      if (current >= target)
        break;
      if (cc->getType() != UnitTypes::Terran_Command_Center || !idleProducer(cc))
        continue;
      if (!budget.tryReserve(costOf(scv)))
        break;
      if (ctx_.commander->train(cc, scv))
      {
        ++current;
        ++ctx_.metrics.workersTrained;
        lastPlan_ += "scv ";
      }
    }
  }

  void ProductionManager::followBuildOrder(Budget& budget)
  {
    const int supplyUi = Broodwar->self()->supplyUsed() / 2;
    std::map<BuildItem, int> wanted;
    for (const auto& step : ctx_.config.buildOrder)
    {
      const int want = ++wanted[step.item];
      const UnitType type = toUnitType(step.item);
      if (planned(type) >= want)
        continue;              // already have / building it
      if (supplyUi < step.supply)
        break;                 // later steps wait for their supply count
      if (!requirementsMet(type) || ctx_.buildings->onCooldown(type))
        continue;              // e.g. Academy before the first Barracks finished
      if (step.item == BuildItem::Refinery && !freeGeyserAvailable())
        continue;
      if (budget.tryReserve({ type.mineralPrice(), type.gasPrice(), 0 }))
      {
        ctx_.buildings->request(type);
        lastPlan_ += concat(toString(step.item), " ");
      }
      else
      {
        lastPlan_ += concat("(saving:", toString(step.item), ") ");
        if (ctx_.config.strictPriority)
          return;              // keep the build order in sequence
      }
    }
  }

  const BaseLocation* ProductionManager::nextExpansion() const
  {
    // Free base closest to the main (the natural first): no Command Center of ours,
    // no enemy building seen there, not an unchecked start location, reachable by ground.
    const Position main = ctx_.map->mainPosition();
    const auto& bases = ctx_.map->bases();
    const BaseLocation* best = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < static_cast<int>(bases.size()); ++i)
    {
      const BaseLocation& b = bases[i];
      bool taken = false;
      for (Unit u : Broodwar->self()->getUnits())
        taken = taken || (u->getType().isResourceDepot() && u->getDistance(b.center) < 6 * 32);
      for (const auto& [id, e] : ctx_.info->enemyBuildings())
        taken = taken || e.position.getApproxDistance(b.center) < 12 * 32;
      if (b.isStartLocation && b.depotTile != ctx_.map->mainTile() && ctx_.info->startStatus(b.depotTile) != StartStatus::Empty)
        taken = true;
      if (taken || !ctx_.map->groundReachable(main, b.center))
        continue;
      const int d = i == ctx_.map->naturalIndex() ? 0 : main.getApproxDistance(b.center);
      if (d < bestDist)
      {
        bestDist = d;
        best = &b;
      }
    }
    return best;
  }

  void ProductionManager::replaceMinedOutBase(Budget& budget)
  {
    // Every difficulty does this, like rebuilding a lost Command Center: once the
    // minerals run out there is no income and the bot can only lose.
    const UnitType cc = UnitTypes::Terran_Command_Center;
    const int depots = static_cast<int>(ctx_.workers->depots().size());
    if (depots == 0 || ctx_.workers->mineralsLeftAtBases() >= kLowMineralsLeft || planned(cc) > depots ||
        ctx_.buildings->onCooldown(cc) || ctx_.strategy->baseUnderThreat())
      return;
    const BaseLocation* next = nextExpansion();
    if (!next)
    {
      if (!loggedNoExpansion_)
        ctx_.log.warn("Production", concat("only ", ctx_.workers->mineralsLeftAtBases(), " minerals left but no free base is known"));
      loggedNoExpansion_ = true;
      return;
    }
    loggedNoExpansion_ = false;
    if (budget.tryReserve({ cc.mineralPrice(), 0, 0 }))
    {
      ctx_.log.info("Production", concat("only ", ctx_.workers->mineralsLeftAtBases(), " minerals left at our bases: new base at ",
                                         next->depotTile.x, ",", next->depotTile.y));
      ctx_.buildings->request(cc, next->depotTile);
      lastPlan_ += "expand(minerals) ";
    }
    else
      lastPlan_ += "(saving:expand) ";
  }

  void ProductionManager::macroBuildings(Budget& budget)
  {
    replaceMinedOutBase(budget);

    // Expansion to the natural.
    const UnitType cc = UnitTypes::Terran_Command_Center;
    const BaseLocation* nat = ctx_.map->natural();
    if (ctx_.config.expand && nat && ctx_.workers->workerCount() >= ctx_.config.expandAtWorkers && planned(cc) < 2 &&
        !ctx_.buildings->onCooldown(cc) && !ctx_.strategy->baseUnderThreat())
    {
      if (budget.tryReserve({ cc.mineralPrice(), 0, 0 }))
      {
        ctx_.log.info("Production", concat("expanding to the natural at ", nat->depotTile.x, ",", nat->depotTile.y));
        ctx_.buildings->request(cc, nat->depotTile);
        lastPlan_ += "expand ";
      }
      else
        lastPlan_ += "(saving:expand) ";
    }

    // Extra barracks while money piles up (keeps production running at full capacity).
    const UnitType rax = UnitTypes::Terran_Barracks;
    if (ctx_.config.extraBarracksMineralBank > 0 && budget.minerals() >= ctx_.config.extraBarracksMineralBank &&
        planned(rax) < ctx_.config.maxBarracks && ctx_.buildings->notStarted(rax) == 0 && !ctx_.buildings->onCooldown(rax) &&
        Broodwar->self()->completedUnitCount(rax) >= 1)
    {
      if (budget.tryReserve({ rax.mineralPrice(), 0, 0 }))
      {
        ctx_.log.info("Production", concat("banking ", Broodwar->self()->minerals(), " minerals: adding Barracks #", planned(rax) + 1));
        ctx_.buildings->request(rax);
        lastPlan_ += "extra-barracks ";
      }
    }
  }

  void ProductionManager::doResearch(Budget& budget)
  {
    for (ResearchItem item : ctx_.config.research)
    {
      if (isResearchDone(item) || isResearchInProgress(item))
        continue;
      const UnitType where = researchBuilding(item);
      Unit building = nullptr;
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType() == where && idleProducer(u))
          building = u;
      if (!building)
        continue; // building missing or busy; the build order provides it
      const Cost cost = isResearchItemTech(item) ? costOf(toTechType(item)) : costOf(toUpgradeType(item));
      if (!budget.tryReserve(cost))
      {
        lastPlan_ += concat("(saving:", toString(item), ") ");
        if (ctx_.config.strictPriority)
          return;
        continue;
      }
      const bool ok = isResearchItemTech(item) ? ctx_.commander->research(building, toTechType(item))
                                               : ctx_.commander->upgrade(building, toUpgradeType(item));
      if (ok)
      {
        ctx_.log.info("Production", concat("research started: ", toString(item)));
        lastPlan_ += concat(toString(item), " ");
      }
    }
  }

  void ProductionManager::buildComsat(Budget& budget)
  {
    const UnitType comsat = UnitTypes::Terran_Comsat_Station;
    Player self = Broodwar->self();
    if (!ctx_.config.useComsatScan || self->allUnitCount(comsat) > 0 ||
        self->completedUnitCount(UnitTypes::Terran_Academy) < 1 || ctx_.frame < comsatRetryFrame_)
      return;
    for (Unit cc : self->getUnits())
    {
      if (cc->getType() != UnitTypes::Terran_Command_Center || !idleProducer(cc) || cc->getAddon())
        continue;
      if (!budget.tryReserve({ comsat.mineralPrice(), comsat.gasPrice(), 0 }))
        return;
      if (ctx_.commander->buildAddon(cc, comsat))
      {
        ctx_.log.info("Production", "building Comsat Station");
        lastPlan_ += "comsat ";
      }
      else if (!ctx_.commander->lastWasSuppressed())
      {
        // Usually the add-on spot next to the Command Center is blocked.
        comsatRetryFrame_ = ctx_.frame + 24 * 120;
        ctx_.log.warn("Production", concat("Comsat Station could not be started (", Broodwar->getLastError().c_str(),
                                           "); retrying in 2 minutes"));
      }
      return;
    }
  }

  void ProductionManager::trainArmy(Budget& budget)
  {
    Player self = Broodwar->self();
    const bool academy = self->completedUnitCount(UnitTypes::Terran_Academy) > 0;
    int marines = self->allUnitCount(UnitTypes::Terran_Marine);
    int medics = self->allUnitCount(UnitTypes::Terran_Medic);
    int firebats = self->allUnitCount(UnitTypes::Terran_Firebat);
    const double firebatShare = ctx_.strategy->firebatShare();

    // Factories: a Machine Shop first, then Siege Tanks at the difficulty's ratio.
    int tanks = self->allUnitCount(UnitTypes::Terran_Siege_Tank_Tank_Mode) +
                self->allUnitCount(UnitTypes::Terran_Siege_Tank_Siege_Mode);
    for (Unit factory : self->getUnits())
    {
      if (factory->getType() != UnitTypes::Terran_Factory || !factory->isCompleted())
        continue;
      ctx_.commander->setRally(factory, ctx_.map->rallyPoint(), 24 * 60);
      if (!idleProducer(factory) || ctx_.config.tankRatio <= 0.0)
        continue;
      if (!factory->getAddon())
      {
        const UnitType shop = UnitTypes::Terran_Machine_Shop;
        if (ctx_.frame < machineShopRetryFrame_ || !budget.tryReserve({ shop.mineralPrice(), shop.gasPrice(), 0 }))
          continue;
        if (ctx_.commander->buildAddon(factory, shop))
        {
          ctx_.log.info("Production", "building Machine Shop");
          lastPlan_ += "machine-shop ";
        }
        else if (!ctx_.commander->lastWasSuppressed())
        {
          machineShopRetryFrame_ = ctx_.frame + 24 * 120;
          ctx_.log.warn("Production", concat("Machine Shop could not be started (", Broodwar->getLastError().c_str(),
                                             "); retrying in 2 minutes"));
        }
        continue;
      }
      if (!factory->getAddon()->isCompleted() || !wantTank(tanks, marines + firebats + medics, ctx_.config.tankRatio))
        continue;
      const UnitType tank = UnitTypes::Terran_Siege_Tank_Tank_Mode;
      if (!budget.tryReserve(costOf(tank)))
      {
        lastPlan_ += "(saving:tank) ";
        continue;
      }
      if (ctx_.commander->train(factory, tank))
      {
        ++tanks;
        ++ctx_.metrics.armyTrained;
        lastPlan_ += "tank ";
        if (!loggedFirstTank_)
        {
          loggedFirstTank_ = true;
          ctx_.log.info("Production", "first Siege Tank started");
        }
      }
    }

    for (Unit rax : self->getUnits())
    {
      if (rax->getType() != UnitTypes::Terran_Barracks || !rax->isCompleted())
        continue;
      ctx_.commander->setRally(rax, ctx_.map->rallyPoint(), 24 * 60);
      if (!idleProducer(rax))
        continue;

      Infantry choice = chooseInfantry(marines, medics, firebats, ctx_.config.medicRatio, firebatShare, academy);
      UnitType type = choice == Infantry::Medic ? UnitTypes::Terran_Medic
                    : choice == Infantry::Firebat ? UnitTypes::Terran_Firebat : UnitTypes::Terran_Marine;
      // Not enough gas for the ideal unit: a Marine is the always-available fallback.
      if (type != UnitTypes::Terran_Marine && budget.gas() < type.gasPrice())
        type = UnitTypes::Terran_Marine;
      if (!budget.tryReserve(costOf(type)))
        break;
      if (ctx_.commander->train(rax, type))
      {
        ++ctx_.metrics.armyTrained;
        if (type == UnitTypes::Terran_Marine) ++marines;
        else if (type == UnitTypes::Terran_Medic) ++medics;
        else ++firebats;
        lastPlan_ += concat(type == UnitTypes::Terran_Marine ? "marine " : type == UnitTypes::Terran_Medic ? "medic " : "firebat ");
        if (!loggedFirstArmy_)
        {
          loggedFirstArmy_ = true;
          ctx_.log.info("Production", concat("first combat unit started: ", type.c_str()));
        }
      }
    }
  }

  void ProductionManager::measureIdle(int elapsedFrames)
  {
    Player self = Broodwar->self();
    const bool couldAffordMarine = self->minerals() >= 50 && self->supplyTotal() - self->supplyUsed() >= 2;
    if (!couldAffordMarine)
      return;
    for (Unit u : self->getUnits())
      if ((u->getType() == UnitTypes::Terran_Barracks) && idleProducer(u))
        ctx_.metrics.idleProductionFrames += elapsedFrames;
  }
}
