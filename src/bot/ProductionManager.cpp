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

    // Supply that has not been able to grow for this long counts as stalled.
    constexpr int kSupplyStallFrames = 24 * 5;

    bool idleProducer(Unit u)
    {
      return isValid(u) && u->isCompleted() && !u->isLifted() && u->getTrainingQueue().empty() &&
             !u->isConstructing() && !u->isResearching() && !u->isUpgrading();
    }

    // Mech units (finished or in production) as a share of mech + infantry.
    double currentMechShare()
    {
      Player self = Broodwar->self();
      int mech = 0, infantry = 0;
      for (UnitType t : { UnitTypes::Terran_Vulture, UnitTypes::Terran_Siege_Tank_Tank_Mode,
                          UnitTypes::Terran_Siege_Tank_Siege_Mode, UnitTypes::Terran_Goliath })
        mech += self->allUnitCount(t);
      for (UnitType t : { UnitTypes::Terran_Marine, UnitTypes::Terran_Medic, UnitTypes::Terran_Firebat })
        infantry += self->allUnitCount(t);
      return mech + infantry > 0 ? static_cast<double>(mech) / (mech + infantry) : 0.0;
    }

    // How much longer a Factory unit takes than a Marine. Factories make mostly
    // tanks with some vultures (chooseFactoryUnit), so weigh them 0.7 / 0.3.
    double mechBuildTimeRatio()
    {
      const double mech = 0.7 * UnitTypes::Terran_Siege_Tank_Tank_Mode.buildTime() + 0.3 * UnitTypes::Terran_Vulture.buildTime();
      return mech / UnitTypes::Terran_Marine.buildTime();
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
    buildDetection(budget);
    if (armyFirst)
      trainArmy(budget);
    trainWorkers(budget);
    followBuildOrder(budget);
    macroBuildings(budget);
    doResearch(budget);
    buildComsat(budget);
    if (!armyFirst)
      trainArmy(budget);
    measureIdle(elapsedFrames, budget);
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

  bool ProductionManager::supplyStalled()
  {
    // Supply can still grow while workers are below their target, anything is in
    // production or a building is on its way (a released Barracks will train soon).
    // Otherwise it has stopped for good once that lasts a few seconds.
    Player self = Broodwar->self();
    const int target = workerTarget(ctx_.workers->mineralPatchesAtBases(), ctx_.workers->gasRefineries(),
                                    ctx_.config.workersPerMineralPatch, ctx_.config.maxWorkers);
    bool canGrow = self->allUnitCount(UnitTypes::Terran_SCV) < target || ctx_.buildings->taskCount() > 0;
    for (Unit u : self->getUnits())
      canGrow = canGrow || (u->isCompleted() && !u->getTrainingQueue().empty());
    if (canGrow)
    {
      stalledSinceFrame_ = -1;
      return false;
    }
    if (stalledSinceFrame_ < 0)
      stalledSinceFrame_ = ctx_.frame;
    return ctx_.frame - stalledSinceFrame_ >= kSupplyStallFrames;
  }

  void ProductionManager::buildDetection(Budget& budget)
  {
    // Every level: hidden attackers can only be fought with detection, and without it
    // the whole army dies for nothing (difficulty 2 lost 19 units to Dark Templar and
    // killed none). Comsat scans stay a difficulty choice; Missile Turrets are the
    // answer that does not depend on it, so every level builds them once needed.
    if (!ctx_.info->hiddenAttackerSeen())
      return;
    Player self = Broodwar->self();
    const UnitType ebay = UnitTypes::Terran_Engineering_Bay;
    const UnitType turret = UnitTypes::Terran_Missile_Turret;
    if (planned(ebay) == 0)
    {
      if (ctx_.buildings->onCooldown(ebay))
        return;
      if (budget.reserveFirst(costOf(ebay)))
      {
        ctx_.log.info("Production", "hidden attackers: Engineering Bay for Missile Turrets");
        ctx_.buildings->request(ebay);
        lastPlan_ += "ebay(detection) ";
      }
      else
        lastPlan_ += "(saving:ebay) ";
      return;
    }
    if (self->completedUnitCount(ebay) == 0 || ctx_.buildings->notStarted(turret) > 0 || ctx_.buildings->onCooldown(turret))
      return;

    // One turret by every Command Center (workers and production) and one at the
    // rally point (the army). A turret detects 11 tiles around it.
    std::vector<TilePosition> spots;
    for (Unit depot : ctx_.workers->depots())
      spots.push_back(depot->getTilePosition());
    spots.push_back(TilePosition(ctx_.map->rallyPoint()));
    for (TilePosition spot : spots)
    {
      bool covered = false;
      for (Unit u : self->getUnits())
        covered = covered || (u->getType() == turret && u->getDistance(Position(spot)) < 7 * 32);
      if (covered)
        continue;
      if (budget.reserveFirst(costOf(turret)))
      {
        ctx_.log.info("Production", concat("hidden attackers: Missile Turret near ", spot.x, ",", spot.y));
        ctx_.buildings->request(turret, spot);
        lastPlan_ += "turret ";
      }
      else
        lastPlan_ += "(saving:turret) ";
      return;
    }
  }

  void ProductionManager::followBuildOrder(Budget& budget)
  {
    const int supplyUi = Broodwar->self()->supplyUsed() / 2;
    const bool stalled = supplyStalled();
    bool released = false;
    std::map<BuildItem, int> wanted;
    for (const auto& step : ctx_.config.buildOrder)
    {
      const int want = ++wanted[step.item];
      const UnitType type = toUnitType(step.item);
      if (planned(type) >= want)
        continue;              // already have / building it
      if (!buildStepDue(supplyUi, step.supply, stalled, released))
        break;                 // later steps wait for their supply count
      if (supplyUi < step.supply)
      {
        released = true;
        if (loggedStallStep_ != step.supply)
          ctx_.log.info("Production", concat("supply stays at ", supplyUi, " (worker target reached, nothing in production): starting ",
                                             toString(step.item), "@", step.supply, " now"));
        loggedStallStep_ = step.supply;
      }
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
    if (budget.reserveFirst({ cc.mineralPrice(), 0, 0 }))
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

    // Expansions: the same rule at every level, the difficulty sets how early and
    // how many (Planning::shouldExpand). The natural first, then the nearest free base.
    const UnitType cc = UnitTypes::Terran_Command_Center;
    Player self = Broodwar->self();
    ExpansionInputs ex;
    ex.frame = ctx_.frame;
    ex.bases = planned(cc);
    ex.basesBuilding = ex.bases - self->completedUnitCount(cc);
    ex.workers = ctx_.workers->workerCount();
    ex.workerTarget = workerTarget(ctx_.workers->mineralPatchesAtBases(), ctx_.workers->gasRefineries(),
                                   ctx_.config.workersPerMineralPatch, ctx_.config.maxWorkers);
    ex.lastExpansionFrame = lastExpansionFrame_;
    ex.underThreat = ctx_.strategy->baseUnderThreat();
    const ExpansionParams ep{ ctx_.config.maxBases, ctx_.config.expandEarliestFrame, ctx_.config.expandAtWorkers };
    if (!ctx_.buildings->onCooldown(cc) && shouldExpand(ex, ep))
      if (const BaseLocation* next = nextExpansion())
      {
        if (budget.reserveFirst({ cc.mineralPrice(), 0, 0 }))
        {
          ctx_.log.info("Production", concat("expanding: base #", ex.bases + 1, " of ", ctx_.config.maxBases, " at ",
                                             next->depotTile.x, ",", next->depotTile.y, " (", ex.workers, " workers)"));
          ctx_.buildings->request(cc, next->depotTile);
          lastExpansionFrame_ = ctx_.frame;
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

    // More Factories so mech can keep up with the Barracks at the target share (the
    // build order gives the first one). A tank takes about twice as long as a marine,
    // so a single Factory next to three Barracks never reaches 25 % mech.
    // While mech is behind and every Factory is busy, it saves up for the next one
    // like any other priority item; otherwise it only uses money that is there now.
    const UnitType factory = UnitTypes::Terran_Factory;
    const int barracks = Broodwar->self()->completedUnitCount(rax);
    const int wantedFactories = factoriesWanted(ctx_.strategy->mechShare(), barracks, mechBuildTimeRatio(), ctx_.config.maxFactories);
    if (Broodwar->self()->completedUnitCount(factory) >= 1 && planned(factory) < wantedFactories &&
        ctx_.buildings->notStarted(factory) == 0 && !ctx_.buildings->onCooldown(factory))
    {
      bool factoryIdle = false;
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType() == factory && idleProducer(u))
          factoryIdle = true;
      const bool mechBehind = currentMechShare() < ctx_.strategy->mechShare() && !factoryIdle;
      const Cost cost{ factory.mineralPrice(), factory.gasPrice(), 0 };
      if (mechBehind ? budget.tryReserve(cost) : (budget.canAfford(cost) && budget.tryReserve(cost)))
      {
        ctx_.log.info("Production", concat("mech share ", static_cast<int>(currentMechShare() * 100), "% (target ",
                                           static_cast<int>(ctx_.strategy->mechShare() * 100), "%) with ", barracks,
                                           " Barracks: adding Factory #", planned(factory) + 1, " of ", wantedFactories));
        ctx_.buildings->request(factory);
        lastPlan_ += "factory ";
      }
      else if (mechBehind)
        lastPlan_ += "(saving:factory) ";
    }

    // Enemy air units: an Armory unlocks Goliaths.
    const UnitType armory = UnitTypes::Terran_Armory;
    if (ctx_.config.mechShare > 0.0 && ctx_.info->enemyMix().air >= 2 && planned(armory) == 0 &&
        Broodwar->self()->completedUnitCount(factory) >= 1 && !ctx_.buildings->onCooldown(armory))
    {
      if (budget.tryReserve({ armory.mineralPrice(), armory.gasPrice(), 0 }))
      {
        ctx_.log.info("Production", concat("enemy air units seen (", ctx_.info->enemyMix().air, "): adding an Armory for Goliaths"));
        ctx_.buildings->request(armory);
        lastPlan_ += "armory ";
      }
      else
        lastPlan_ += "(saving:armory) ";
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
    // Whichever side is behind the target mech share spends first (and, with strict
    // priority, saves up first); Factories stop once mech is clearly above the target.
    const double share = currentMechShare();
    const double target = ctx_.strategy->mechShare();
    if (target > 0.0 && share < target)
    {
      trainFactories(budget, true);
      trainInfantry(budget);
    }
    else
    {
      trainInfantry(budget);
      trainFactories(budget, target > 0.0 && share < target + 0.1);
    }
  }

  void ProductionManager::trainFactories(Budget& budget, bool trainUnits)
  {
    Player self = Broodwar->self();
    int vultures = self->allUnitCount(UnitTypes::Terran_Vulture);
    int tanks = self->allUnitCount(UnitTypes::Terran_Siege_Tank_Tank_Mode) +
                self->allUnitCount(UnitTypes::Terran_Siege_Tank_Siege_Mode);
    int goliaths = self->allUnitCount(UnitTypes::Terran_Goliath);
    const bool armoryReady = self->completedUnitCount(UnitTypes::Terran_Armory) > 0;
    const EnemyMix enemy = ctx_.info->enemyMix();
    for (Unit factory : self->getUnits())
    {
      if (factory->getType() != UnitTypes::Terran_Factory || !factory->isCompleted())
        continue;
      ctx_.commander->setRally(factory, ctx_.map->rallyPoint(), 24 * 60);
      if (!idleProducer(factory) || ctx_.config.mechShare <= 0.0)
        continue;
      // Every Factory gets a Machine Shop (tanks and Siege Mode need it).
      // While a blocked add-on waits for its retry, the Factory builds Vultures / Goliaths.
      if (!factory->getAddon() && ctx_.frame >= machineShopRetryFrame_)
      {
        const UnitType shop = UnitTypes::Terran_Machine_Shop;
        if (!budget.tryReserve({ shop.mineralPrice(), shop.gasPrice(), 0 }))
          continue;
        if (ctx_.commander->buildAddon(factory, shop))
        {
          ctx_.log.info("Production", "building Machine Shop");
          lastPlan_ += "machine-shop ";
          continue;
        }
        if (ctx_.commander->lastWasSuppressed())
          continue;
        // Usually a unit standing on the add-on spot; it moves soon.
        machineShopRetryFrame_ = ctx_.frame + 24 * 30;
        ctx_.log.warn("Production", concat("Machine Shop could not be started (", Broodwar->getLastError().c_str(),
                                           "); retrying in 30 seconds"));
      }
      if (!trainUnits)
        continue;
      const bool shopReady = factory->getAddon() && factory->getAddon()->isCompleted();
      const FactoryUnit choice = chooseFactoryUnit(vultures, tanks, goliaths, enemy, shopReady, armoryReady);
      UnitType type = choice == FactoryUnit::Goliath ? UnitTypes::Terran_Goliath
                    : choice == FactoryUnit::Vulture ? UnitTypes::Terran_Vulture : UnitTypes::Terran_Siege_Tank_Tank_Mode;
      // Short on gas: the Vulture (minerals only) keeps the Factory busy.
      if (type != UnitTypes::Terran_Vulture && budget.gas() < type.gasPrice())
        type = UnitTypes::Terran_Vulture;
      if (!budget.tryReserve(costOf(type)))
      {
        lastPlan_ += "(saving:mech) ";
        continue;
      }
      if (ctx_.commander->train(factory, type))
      {
        ++ctx_.metrics.armyTrained;
        if (type == UnitTypes::Terran_Vulture) ++vultures;
        else if (type == UnitTypes::Terran_Goliath) ++goliaths;
        else ++tanks;
        lastPlan_ += type == UnitTypes::Terran_Vulture ? "vulture " : type == UnitTypes::Terran_Goliath ? "goliath " : "tank ";
        if (!loggedFirstMech_)
        {
          loggedFirstMech_ = true;
          ctx_.log.info("Production", concat("first mech unit started: ", type.c_str()));
        }
      }
    }
  }
  void ProductionManager::trainInfantry(Budget& budget)
  {
    Player self = Broodwar->self();
    const bool academy = self->completedUnitCount(UnitTypes::Terran_Academy) > 0;
    int marines = self->allUnitCount(UnitTypes::Terran_Marine);
    int medics = self->allUnitCount(UnitTypes::Terran_Medic);
    int firebats = self->allUnitCount(UnitTypes::Terran_Firebat);
    const double firebatShare = ctx_.strategy->firebatShare();

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

  void ProductionManager::measureIdle(int elapsedFrames, const Budget& budget)
  {
    // Idle only counts when money was left after this round's spending and saving:
    // a Barracks waiting because everything is spent or saved for is not wasted.
    Player self = Broodwar->self();
    if (!budget.canAfford(costOf(UnitTypes::Terran_Marine)))
      return;
    for (Unit u : self->getUnits())
      if ((u->getType() == UnitTypes::Terran_Barracks) && idleProducer(u))
        ctx_.metrics.idleProductionFrames += elapsedFrames;
  }
}
