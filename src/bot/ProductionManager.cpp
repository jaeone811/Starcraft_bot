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

    // The opening for the enemy race (BeginnerBot.ini opening = liquipedia, the
    // levels that counter what they know): Liquipedia's TvP/TvT Factory first, TvZ
    // bio with an early Engineering Bay. Dragoons cut down the Marines of the
    // infantry opening and broke the Bunker from out of range, game after game.
    // opening = classic keeps the difficulty's own order against every race.
    liquipedia_ = ctx_.settings.opening == "liquipedia" && ctx_.config.enemyInfoUsage >= 1;
    const EnemyRace race = liquipedia_ ? ctx_.info->enemyRaceKind() : EnemyRace::Unknown;
    if (!buildOrderSet_ || race != openingRace_)
    {
      buildOrder_ = raceOpening(ctx_.config.buildOrder, race, ctx_.config.maxFactories);
      if (race != EnemyRace::Unknown)
      {
        std::string steps;
        for (const auto& s : buildOrder_)
          steps += concat(toString(s.item), "@", s.supply, " ");
        ctx_.log.info("Production", concat("opening vs ", ctx_.info->enemyRace().c_str(), " (Liquipedia): ", steps));
      }
      else if (!buildOrderSet_ && !liquipedia_)
        ctx_.log.info("Production", "opening: classic (the difficulty's build order against every race)");
      factoryFirst_ = race == EnemyRace::Protoss || race == EnemyRace::Terran;
      openingRace_ = race;
      buildOrderSet_ = true;
    }

    const bool armyFirst = ctx_.strategy->seriousThreat() && ctx_.config.armyFirstWhenThreatened;
    // Every level: until the early army is there, it is bought before tech and
    // research (but after workers). The level sets how big that army is; the levels
    // that use what they scout also want at least the enemy army they have seen.
    const int enemyArmySeen = ctx_.config.enemyInfoUsage >= 1 ? ctx_.info->knownEnemyCombatUnits() : 0;
    rush_ = ctx_.config.enemyInfoUsage >= 1 && ctx_.info->rushSeen();
    const int earlyTarget = earlyDefenseTarget(ctx_.frame, ctx_.config.earlyDefenseArmy, enemyArmySeen, rush_);
    const bool earlyDefense = !armyFirst && fightingUnits() < earlyTarget;
    if (earlyDefense && !loggedEarlyDefense_)
      ctx_.log.info("Production", concat("early defense: ", fightingUnits(), " fighting units, want ", earlyTarget,
                                         " (enemy army seen ", enemyArmySeen, ") - army before tech until 8:00"));
    loggedEarlyDefense_ = loggedEarlyDefense_ || earlyDefense;
    // Ordering alone did not hold the money: while every Barracks was busy, research
    // took it (Stim, Siege Mode, U-238, +1 between 4:44 and 6:27) and only 8 units
    // met the zealots at 6:45 in two games. Now research, the Comsat and the tech
    // buildings wait until the early army is there.
    // The army keeps pace with a rising target, so this switches every few seconds
    // (22 log lines in one game): only the first wait is logged.
    // Under threat only while the army is smaller than the early army: zealots kept
    // coming one or two at a time for five minutes and Stim waited until 9:38.
    const bool hold = earlyDefense || (armyFirst && fightingUnits() < ctx_.config.earlyDefenseArmy);
    if (hold && !loggedHoldTech_)
      ctx_.log.info("Production", "tech and research wait for the army (logged once)");
    loggedHoldTech_ = loggedHoldTech_ || hold;
    holdTech_ = hold;
    // Only until the first Factory is started: after that the mech units need gas,
    // and with the cap on all game (zealots kept the base threatened) Factories,
    // Machine Shops and tanks waited for gas that Firebats and Medics took.
    const bool factoryStarted = Broodwar->self()->allUnitCount(UnitTypes::Terran_Factory) > 0;
    ctx_.workers->setGasCap(hold && !factoryStarted ? earlyGasNeed() : -1);
    planSupply(budget);
    ensureCommandCenter(budget);
    buildDetection(budget);
    // The Comsat before the next SCV: the Command Center was never idle (an SCV
    // always in training) and the Comsat never came, not even after a Dark Templar.
    buildComsat(budget);
    if (armyFirst)
      trainArmy(budget);
    trainWorkers(budget);
    buildBunker(budget);
    // Siege Mode before the next Marines: the first Siege Tank holds Dragoons that
    // the Marines cannot (Siege Mode waited until 8:13 behind the early army).
    if (liquipedia_ && rangeResearchFirst())
      doResearch(budget);
    if (earlyDefense)
      trainArmy(budget);
    followBuildOrder(budget);
    macroBuildings(budget);
    doResearch(budget);
    if (!armyFirst && !earlyDefense)
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
    const int baseTarget = workerTarget(ctx_.workers->mineralPatchesAtBases(), ctx_.workers->gasRefineries(),
                                     ctx_.config.workersPerMineralPatch, ctx_.config.maxWorkers);
    // Not while the early army is short (a rush is on): then every mineral goes there.
    const int advance = holdTech_ ? 0 : workersInAdvance(planned(UnitTypes::Terran_Command_Center), ctx_.config.maxBases, ctx_.frame,
                                                         ctx_.config.expandEarliestFrame);
    const int target = std::min(baseTarget + advance, ctx_.config.maxWorkers);
    int current = self->allUnitCount(scv);
    int scvsThisFrame = 0;
    for (Unit cc : self->getUnits())
    {
      if (current >= target)
        break;
      if (cc->getType() != UnitTypes::Terran_Command_Center || !idleProducer(cc))
        continue;
      if (comsatWanted_ && !cc->getAddon())
        continue;   // the Comsat goes on first
      // SCVs keep coming while buildings wait for their builder: their reservations
      // stopped SCV production at 18 from 3:30 to 6:30. Money for a building whose
      // SCV is on its way is still kept.
      if (!budget.tryReserve(costOf(scv)) &&
          Broodwar->self()->minerals() - ctx_.buildings->committedMinerals() < scv.mineralPrice() * (scvsThisFrame + 1))
        break;
      ++scvsThisFrame;
      if (ctx_.commander->train(cc, scv))
      {
        ++current;
        ++ctx_.metrics.workersTrained;
        lastPlan_ += "scv ";
      }
    }
  }

  int ProductionManager::fightingUnits() const
  {
    // Finished or in production, without Medics and Science Vessels.
    Player self = Broodwar->self();
    int n = 0;
    for (UnitType t : { UnitTypes::Terran_Marine, UnitTypes::Terran_Firebat, UnitTypes::Terran_Vulture,
                        UnitTypes::Terran_Siege_Tank_Tank_Mode, UnitTypes::Terran_Siege_Tank_Siege_Mode, UnitTypes::Terran_Goliath,
                        UnitTypes::Terran_Wraith, UnitTypes::Terran_Battlecruiser })
      n += self->allUnitCount(t);
    return n;
  }

  void ProductionManager::buildBunker(Budget& budget)
  {
    // One Bunker just inside the main's ramp once a Barracks is up; CombatManager
    // puts 4 marines in it.
    const UnitType bunker = UnitTypes::Terran_Bunker;
    Player self = Broodwar->self();
    if (!ctx_.config.earlyBunker || ctx_.frame >= 24 * 600 || planned(bunker) > 0 || ctx_.buildings->onCooldown(bunker) ||
        self->completedUnitCount(UnitTypes::Terran_Barracks) == 0)
      return;
    // A Bunker lost while heavy ranged enemies are known is rebuilt only with U-238:
    // without the range Dragoons broke three in a row from outside it.
    if (self->deadUnitCount(bunker) > 0 && ctx_.info->enemyMix().heavy > 0 &&
        self->getUpgradeLevel(UpgradeTypes::U_238_Shells) == 0)
    {
      lastPlan_ += "(bunker waits for U-238) ";
      return;
    }
    // Not into a fight: two Bunkers started while zealots stood at the spot were
    // killed unfinished (200 minerals and two SCVs' time during a rush).
    for (Unit e : ctx_.info->visibleEnemiesNear(ctx_.map->bunkerSpot(), 10 * 32))
      if (!e->getType().isWorker() && !e->isFlying() && canAttackGround(e->getType()))
      {
        lastPlan_ += "(enemy at bunker spot) ";
        return;
      }
    if (budget.tryReserve(costOf(bunker)))
    {
      ctx_.log.info("Production", "Bunker at the main ramp against early attacks");
      ctx_.buildings->request(bunker, TilePosition(ctx_.map->bunkerSpot()));
      lastPlan_ += "bunker ";
    }
    else
      lastPlan_ += "(saving:bunker) ";
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
    // Enemy air attackers get the same Turrets: Mutalisks killed every SCV while the
    // army was out (no Turret, an Engineering Bay standing since 5:52).
    // Against Protoss the levels that counter what they know put up Turrets from 6:00
    // without waiting to see a Dark Templar: one came at 7:11, there was no Comsat
    // and the Turret could not be started next to it; the main fell by 9:29.
    const bool hidden = ctx_.info->hiddenAttackerSeen();
    const bool hiddenTech = ctx_.info->hiddenTechSeen();
    const int air = ctx_.info->enemyMix().air;
    const bool protossInsurance = ctx_.config.enemyInfoUsage >= 1 && ctx_.info->enemyRace() == Races::Protoss;
    if (!detectionWanted(hidden, hiddenTech, air, protossInsurance, ctx_.frame))
      return;
    const char* why = hidden ? "hidden attackers" : hiddenTech ? "Dark Templar tech" : air >= 2 ? "enemy air" : "against Protoss (Dark Templar)";
    Player self = Broodwar->self();
    const UnitType ebay = UnitTypes::Terran_Engineering_Bay;
    const UnitType turret = UnitTypes::Terran_Missile_Turret;
    if (planned(ebay) == 0)
    {
      if (ctx_.buildings->onCooldown(ebay))
        return;
      if (budget.reserveFirst(costOf(ebay)))
      {
        ctx_.log.info("Production", concat(why, ": Engineering Bay for Missile Turrets"));
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
    // rally point (the army). A turret detects 11 tiles around it. Against six or
    // more flyers a single turret by the mineral line dies alone: two there. Never
    // more turrets than wanted, even if one could not be placed where it was wanted.
    std::vector<std::pair<TilePosition, int>> spots;
    const int perBase = air >= 6 ? 2 : 1;
    for (Unit depot : ctx_.workers->depots())
      spots.emplace_back(depot->getTilePosition(), perBase);
    spots.emplace_back(TilePosition(ctx_.map->rallyPoint()), 1);
    int wanted = 0;
    for (const auto& s : spots)
      wanted += s.second;
    if (planned(turret) >= wanted)
      return;
    for (const auto& [spot, want] : spots)
    {
      int near = 0;
      for (Unit u : self->getUnits())
        near += u->getType() == turret && u->getDistance(Position(spot)) < 7 * 32 ? 1 : 0;
      if (near >= want)
        continue;
      if (budget.reserveFirst(costOf(turret)))
      {
        ctx_.log.info("Production", concat(why, ": Missile Turret near ", spot.x, ",", spot.y));
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
    if (ctx_.workers->workerCount() == 0)
      return;   // nothing can be built (the stall release logged twice per frame after the last SCV died)
    const int supplyUi = Broodwar->self()->supplyUsed() / 2;
    const bool stalled = supplyStalled();
    bool released = false;
    std::map<BuildItem, int> wanted;
    for (const auto& step : buildOrder_)
    {
      const int want = ++wanted[step.item];
      const UnitType type = toUnitType(step.item);
      if (planned(type) >= want)
        continue;              // already have / building it
      if (holdTech_ && !makesEarlyArmy(step.item))
      {
        lastPlan_ += concat("(after army:", toString(step.item), ") ");
        continue;              // tech waits until the early army is there
      }
      // An early attack is coming: the second Barracks now, gas later.
      if (rush_ && holdTech_ && step.item == BuildItem::Refinery)
      {
        lastPlan_ += "(after army:Refinery) ";
        continue;
      }
      // Factory first against Protoss: the second Barracks comes early only once
      // Zealots are coming (Dragoons are the Factory's job).
      const bool rushBarracks = rush_ && step.item == BuildItem::Barracks && want <= 2 &&
                                (!factoryFirst_ || ctx_.info->recentMeleeUnits() >= 2);
      if (!rushBarracks && !buildStepDue(supplyUi, step.supply, stalled, released))
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

  double ProductionManager::ownArmyStrength() const
  {
    // Combined like the enemy's (Planning::combineStrength); a plain sum made our
    // army look many times weaker than the enemy's.
    std::vector<double> own;
    for (Unit u : Broodwar->self()->getUnits())
      if (isOurArmyType(u->getType()) && u->isCompleted())
        own.push_back(unitStrengthNow(u));
    return combineStrength(own);
  }

  const BaseLocation* ProductionManager::nextExpansion(bool relaxed) const
  {
    const double army = ownArmyStrength();
    // Free base closest to the main (the natural first): no Command Center of ours,
    // no enemy building seen there, not an unchecked start location, reachable by ground.
    const Position main = ctx_.map->mainPosition();
    const auto& bases = ctx_.map->bases();
    const BaseLocation* best = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    std::string reasons;
    for (int i = 0; i < static_cast<int>(bases.size()); ++i)
    {
      const BaseLocation& b = bases[i];
      bool taken = false;
      const char* why = "";
      auto reject = [&](bool cond, const char* reason) {
        if (cond && !taken)
          why = reason;
        taken = taken || cond;
      };
      bool haveDepot = false;
      for (Unit u : Broodwar->self()->getUnits())
        haveDepot = haveDepot || (u->getType().isResourceDepot() && u->getDistance(b.center) < 6 * 32);
      if (haveDepot)
        continue;
      for (const auto& [id, e] : ctx_.info->enemyBuildings())
        reject(e.position.getApproxDistance(b.center) < 12 * 32, "enemy building");
      // Not where enemy fighting units were seen in the last minute that our army
      // could not handle (they killed the Command Center being built there and every
      // SCV sent after it). Any enemy at all kept the natural waiting from 7:30 to
      // 11:45 on one base: Zerglings poking the Bunker 14 tiles away counted.
      // Not on the enemy's side: a base nearer to a known enemy building than to our
      // nearest Command Center (a center base next to the enemy's third took 25 SCVs).
      if (i != ctx_.map->naturalIndex())
      {
        int ours = std::numeric_limits<int>::max(), theirs = std::numeric_limits<int>::max();
        for (Unit u : Broodwar->self()->getUnits())
          if (u->getType().isResourceDepot())
            ours = std::min(ours, u->getPosition().getApproxDistance(b.center));
        for (const auto& [id, e] : ctx_.info->enemyBuildings())
          theirs = std::min(theirs, e.position.getApproxDistance(b.center));
        reject(theirs < ours, "enemy side");
      }
      const bool takenBefore = taken;
      std::vector<double> seen;
      for (const auto& [id, e] : ctx_.info->enemyUnits())
        if (!e.type.isWorker() && canAttackGround(e.type) && ctx_.frame - e.lastSeenFrame < 24 * 60 &&
            e.position.getApproxDistance(b.center) < 15 * 32)
          seen.push_back(typeStrength(e.type));
      const double seenStrength = combineStrength(seen);
      reject(baseContested(seenStrength, army), "enemy army there");
      const bool lostThere = ctx_.buildings->siteDangerous(b.center);
      reject(lostThere, "lost there lately");
      // A base other than the natural: no enemy fighting unit seen there in the last
      // two minutes at all (a center base where enemy Vultures and Tanks came and
      // went had SCVs sent to it for two and a half minutes).
      if (i != ctx_.map->naturalIndex() && !relaxed)
        for (const auto& [id, e] : ctx_.info->enemyUnits())
          reject(!e.type.isWorker() && canAttackGround(e.type) && ctx_.frame - e.lastSeenFrame < 24 * 120 &&
                     e.position.getApproxDistance(b.center) < 15 * 32,
                 "enemies passed by");
      const bool contestedOnly = taken && !takenBefore;
      if (contestedOnly && i == ctx_.map->naturalIndex() && ctx_.frame >= nextContestedLogFrame_)
      {
        ctx_.log.info("Production", concat("natural contested: enemies seen there (", static_cast<int>(seenStrength),
                                           ") are more than half our army (", static_cast<int>(army), "); waiting"));
        nextContestedLogFrame_ = ctx_.frame + 24 * 60;
      }
      reject(b.isStartLocation && b.depotTile != ctx_.map->mainTile() && ctx_.info->startStatus(b.depotTile) != StartStatus::Empty,
             "enemy start location");
      // Something we can see stands on the spot (enemy units, a building we did not
      // recognise): another base. The third base on Boxer once failed this way twice.
      reject(!taken && Broodwar->isExplored(b.depotTile) && !Broodwar->canBuildHere(b.depotTile, UnitTypes::Terran_Command_Center),
             "spot blocked");
      reject(!ctx_.map->groundReachable(main, b.center), "not reachable");
      if (taken)
      {
        reasons += concat(b.depotTile.x, ",", b.depotTile.y, " ", why, "; ");
        // The natural is free but enemies were just there: wait for it rather than
        // taking a base further out (a center base went up while the natural was
        // contested and had nothing to protect it). Not once a Command Center or its
        // builders were lost there: then the other bases are looked at. Sieged Tanks
        // killed the natural at 9:47 and two more Command Centers there while it
        // waited; seven other bases were never considered and the main ran dry.
        if (i == ctx_.map->naturalIndex() && contestedOnly && !lostThere)
          return nullptr;
        continue;
      }
      // Nearest to any base of ours (the bases beside the natural come before the
      // map center), the natural first.
      int d = std::numeric_limits<int>::max();
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType().isResourceDepot())
          d = std::min(d, u->getPosition().getApproxDistance(b.center));
      if (i == ctx_.map->naturalIndex())
        d = 0;
      if (d < bestDist)
      {
        bestDist = d;
        best = &b;
      }
    }
    if (!best && !reasons.empty() && ctx_.frame >= nextNoBaseLogFrame_)
    {
      ctx_.log.info("Production", concat("no base to take", relaxed ? " (relaxed)" : "", ": ", reasons));
      nextNoBaseLogFrame_ = ctx_.frame + 24 * 120;
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
    // Our bases are running dry: a base where enemies passed by lately is better than
    // none (the main and the natural were mined out at 23:00 and 19 SCVs stood idle).
    const BaseLocation* next = nextExpansion();
    if (!next)
      next = nextExpansion(true);
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
    ex.underThreat = ctx_.strategy->seriousThreat();
    ex.ownArmyStrength = ownArmyStrength();
    ex.knownEnemyStrength = ctx_.info->knownEnemyArmyStrength();
    const ExpansionParams ep{ ctx_.config.maxBases, ctx_.config.expandEarliestFrame, ctx_.config.expandAtWorkers };
    const BaseLocation* next = ctx_.buildings->onCooldown(cc) ? nullptr : nextExpansion();
    ex.nextIsNatural = next && ctx_.map->naturalIndex() >= 0 && next == &ctx_.map->bases()[ctx_.map->naturalIndex()];
    if (next && shouldExpand(ex, ep))
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

    // Every level: more geysers as the economy grows. A base takes its geyser once
    // the mineral lines are (nearly) full, and any free geyser is taken when gas runs
    // short while minerals are left. Waiting for "gas < 100 with 300 minerals" alone
    // was too late: levels 4-5 ran their upper tech on one Refinery for 4-6 minutes
    // because the army spent every mineral first.
    const UnitType refinery = UnitTypes::Terran_Refinery;
    if (self->completedUnitCount(refinery) >= 1 && ctx_.buildings->notStarted(refinery) == 0 &&
        !ctx_.buildings->onCooldown(refinery) && freeGeyserAvailable())
    {
      const bool saturated = planned(refinery) < self->completedUnitCount(cc) && ex.workers >= ex.workerTarget - 2;
      const bool shortOnGas = budget.gas() < 100 && budget.minerals() >= 300;
      if ((saturated || shortOnGas) && budget.tryReserve(costOf(refinery)))
      {
        ctx_.log.info("Production", concat(saturated ? "bases saturated (" : "short on gas (",
                                           saturated ? ex.workers : self->gas(), saturated ? " workers)" : " gas)",
                                           ": Refinery #", planned(refinery) + 1));
        ctx_.buildings->request(refinery);
        lastPlan_ += "refinery ";
      }
    }

    // Enemy air units: an Armory unlocks Goliaths.
    const UnitType armory = UnitTypes::Terran_Armory;
    if (ctx_.config.mechShare > 0.0 && (ctx_.info->enemyMix().air >= 2 || ctx_.strategy->counter() == Counter::Goliaths) &&
        planned(armory) == 0 &&
        Broodwar->self()->completedUnitCount(factory) >= 1 && !ctx_.buildings->onCooldown(armory))
    {
      // Saved up for first while the enemy air is ahead of our anti-air.
      const Cost cost{ armory.mineralPrice(), armory.gasPrice(), 0 };
      if (ctx_.strategy->antiAirShort() ? budget.reserveFirst(cost) : budget.tryReserve(cost))
      {
        ctx_.log.info("Production", concat("enemy air units seen (", ctx_.info->enemyMix().air, "): adding an Armory for Goliaths"));
        ctx_.buildings->request(armory);
        lastPlan_ += "armory ";
      }
      else
        lastPlan_ += "(saving:armory) ";
    }
  }

  int ProductionManager::earlyGasNeed() const
  {
    // Gas for what is still bought while the early army is short: the early
    // buildings of the build order not started yet, a Machine Shop, a few
    // Firebats/Medics.
    int need = UnitTypes::Terran_Machine_Shop.gasPrice() + 4 * UnitTypes::Terran_Firebat.gasPrice();
    std::map<BuildItem, int> wanted;
    for (const auto& step : buildOrder_)
    {
      const int want = ++wanted[step.item];
      const UnitType type = toUnitType(step.item);
      if (makesEarlyArmy(step.item) && Broodwar->self()->allUnitCount(type) + ctx_.buildings->inProgress(type) < want)
        need += type.gasPrice();
    }
    return need;
  }

  bool ProductionManager::rangeResearchFirst() const
  {
    return ctx_.info->enemyMix().heavy > 0 || factoryFirst_;
  }

  void ProductionManager::doResearch(Budget& budget)
  {
    // Heavy ranged enemies (Dragoons, Tanks, Lurkers) outrange Marines and the
    // Bunker: Siege Mode and U-238 come first and do not wait for the early army.
    // Dragoons broke three Bunkers from out of range while Siege Mode waited until
    // 9:12 with a Tank standing since 5:44, and U-238 never started.
    const bool rangeFirst = rangeResearchFirst();
    if (rangeFirst && !loggedRangeFirst_)
      ctx_.log.info("Production", "heavy ranged enemies (or Liquipedia vs Protoss/Terran): Siege Mode and U-238 first, even before the early army is complete");
    loggedRangeFirst_ = loggedRangeFirst_ || rangeFirst;
    if (holdTech_ && !rangeFirst)
      return;
    // While enemy air outmatches our anti-air, only research that helps against it.
    const bool airFirst = ctx_.strategy->antiAirShort();
    const std::vector<ResearchItem> base = raceResearch(ctx_.config.research, openingRace_);
    const int tanks = Broodwar->self()->allUnitCount(UnitTypes::Terran_Siege_Tank_Tank_Mode) +
                      Broodwar->self()->allUnitCount(UnitTypes::Terran_Siege_Tank_Siege_Mode);
    const bool enemyAirSeen = ctx_.info->enemyMix().air > 0;
    for (ResearchItem item : researchOrder(base, rangeFirst))
    {
      if (isResearchDone(item) || isResearchInProgress(item))
        continue;
      if (holdTech_ && !extendsRange(item))
        continue;   // the rest waits for the early army
      if (airFirst && !helpsAgainstAir(item))
      {
        lastPlan_ += concat("(after anti-air:", toString(item), ") ");
        continue;
      }
      if (const char* wait = researchWaits(item, openingRace_, tanks, enemyAirSeen))
      {
        lastPlan_ += concat("(", wait, ":", toString(item), ") ");
        continue;
      }
      if (!researchServesCounter(item, ctx_.strategy->counter()))
      {
        lastPlan_ += concat("(after counter:", toString(item), ") ");
        continue;   // the gas goes to the counter units
      }
      const UnitType where = researchBuilding(item);
      Unit building = nullptr;
      for (Unit u : Broodwar->self()->getUnits())
        if (u->getType() == where && idleProducer(u))
          building = u;
      if (!building)
        continue; // building missing or busy; the build order provides it
      // e.g. Charon Boosters (Machine Shop) also need an Armory.
      const UnitType required = isResearchItemTech(item) ? toTechType(item).requiredUnit() : toUpgradeType(item).whatsRequired(1);
      if (required != UnitTypes::None && Broodwar->self()->completedUnitCount(required) == 0)
        continue;
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
    comsatWanted_ = false;
    const UnitType comsat = UnitTypes::Terran_Comsat_Station;
    Player self = Broodwar->self();
    if (!ctx_.config.useComsatScan || self->allUnitCount(comsat) > 0 ||
        self->completedUnitCount(UnitTypes::Terran_Academy) < 1 || ctx_.frame < comsatRetryFrame_)
      return;
    // After the early army, unless something hidden is attacking or expected
    // (Protoss: Dark Templar; their tech seen).
    const bool hiddenExpected = ctx_.info->hiddenAttackerSeen() || ctx_.info->hiddenTechSeen() || ctx_.info->enemyMinesSeen() ||
                                (ctx_.config.enemyInfoUsage >= 1 && ctx_.info->enemyRace() == Races::Protoss);
    if (holdTech_ && !hiddenExpected)
      return;
    comsatWanted_ = true;
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
    // Starport units (levels 4-5) take their share first; the rest follows as before.
    trainStarports(budget);
    const double share = currentMechShare();
    const double target = ctx_.strategy->mechShare();
    // Enemy air ahead of our anti-air: Marines first (the Factories make Goliaths
    // once the Armory stands; a Vulture does not shoot up).
    if (ctx_.strategy->antiAirShort())
    {
      trainInfantry(budget);
      trainFactories(budget, target > 0.0);
      return;
    }
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
    const EnemyMix enemy = ctx_.config.enemyInfoUsage >= 1 ? ctx_.info->expectedEnemyMix() : ctx_.info->enemyMix();
    for (Unit factory : self->getUnits())
    {
      if (factory->getType() != UnitTypes::Terran_Factory || !factory->isCompleted())
        continue;
      ctx_.commander->setRally(factory, ctx_.map->rallyPoint(), 24 * 60);
      if (!idleProducer(factory) || ctx_.config.mechShare <= 0.0)
        continue;
      // Every Factory gets a Machine Shop (tanks and Siege Mode need it).
      // While a blocked add-on waits for its retry, the Factory builds Vultures / Goliaths.
      // Without the gas for it the Factory builds a Vulture meanwhile instead of
      // standing idle (three Factories made 4 Vultures in 5 minutes once).
      // Not while enemies stand at the Factory: six Machine Shops were started and
      // killed one after another while Dragoons were there.
      bool enemyAtFactory = false;
      for (Unit e : ctx_.info->visibleEnemiesNear(factory->getPosition(), 8 * 32))
        enemyAtFactory = enemyAtFactory || (!e->getType().isWorker() && canAttackGround(e->getType()));
      if (!factory->getAddon() && enemyAtFactory)
        lastPlan_ += "(enemy at Factory: add-on waits) ";
      if (!factory->getAddon() && !enemyAtFactory && ctx_.frame >= machineShopRetryFrame_)
      {
        const UnitType shop = UnitTypes::Terran_Machine_Shop;
        if (budget.tryReserve({ shop.mineralPrice(), shop.gasPrice(), 0 }))
        {
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
      }
      if (!trainUnits)
        continue;
      const bool shopReady = factory->getAddon() && factory->getAddon()->isCompleted();
      const FactoryUnit choice = chooseFactoryUnit(vultures, tanks, goliaths, enemy, shopReady, armoryReady);
      UnitType type = choice == FactoryUnit::Goliath ? UnitTypes::Terran_Goliath
                    : choice == FactoryUnit::Vulture ? UnitTypes::Terran_Vulture : UnitTypes::Terran_Siege_Tank_Tank_Mode;
      // Massing a counter: every Factory that can make it does.
      const Counter counter = ctx_.strategy->counter();
      if (counter == Counter::SiegeTanks && shopReady)
        type = UnitTypes::Terran_Siege_Tank_Tank_Mode;
      else if (counter == Counter::Goliaths && armoryReady)
        type = UnitTypes::Terran_Goliath;
      else if (counter == Counter::Vultures)
        type = UnitTypes::Terran_Vulture;
      // Short on gas: the Vulture (minerals only) keeps the Factory busy. Not a
      // Goliath wanted against air: it waits for its gas (Vultures were made all
      // game while Mutalisks killed the SCVs).
      const bool goliathForAir = type == UnitTypes::Terran_Goliath && ctx_.strategy->antiAirShort();
      if (type != UnitTypes::Terran_Vulture && budget.gas() < type.gasPrice() && !goliathForAir)
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
  void ProductionManager::trainStarports(Budget& budget)
  {
    const DifficultyConfig& c = ctx_.config;
    if (c.airShare <= 0.0 && c.maxScienceVessels <= 0)
      return;
    Player self = Broodwar->self();
    int wraiths = self->allUnitCount(UnitTypes::Terran_Wraith);
    int vessels = self->allUnitCount(UnitTypes::Terran_Science_Vessel);
    int battlecruisers = self->allUnitCount(UnitTypes::Terran_Battlecruiser);
    int ground = 0;
    for (Unit u : self->getUnits())
      if (isOurArmyType(u->getType()) && !isAirArmyType(u->getType()))
        ++ground;
    const bool needTower = c.maxScienceVessels > 0 || c.useBattlecruisers;
    const bool scienceReady = self->completedUnitCount(UnitTypes::Terran_Science_Facility) > 0;
    const bool physicsLab = self->completedUnitCount(UnitTypes::Terran_Physics_Lab) > 0;

    // Battlecruisers need a Physics Lab on the Science Facility.
    if (c.useBattlecruisers)
      for (Unit sf : self->getUnits())
        if (sf->getType() == UnitTypes::Terran_Science_Facility && idleProducer(sf) && !sf->getAddon())
        {
          const UnitType lab = UnitTypes::Terran_Physics_Lab;
          if (budget.tryReserve(costOf(lab)) && ctx_.commander->buildAddon(sf, lab))
          {
            ctx_.log.info("Production", "building Physics Lab");
            lastPlan_ += "physics-lab ";
          }
        }

    for (Unit sp : self->getUnits())
    {
      if (sp->getType() != UnitTypes::Terran_Starport || !sp->isCompleted())
        continue;
      ctx_.commander->setRally(sp, ctx_.map->rallyPoint(), 24 * 60);
      if (!idleProducer(sp))
        continue;
      // Science Vessels and Battlecruisers need a Control Tower; Wraiths do not.
      if (needTower && !sp->getAddon())
      {
        const UnitType tower = UnitTypes::Terran_Control_Tower;
        if (budget.tryReserve(costOf(tower)) && ctx_.commander->buildAddon(sp, tower))
        {
          ctx_.log.info("Production", "building Control Tower");
          lastPlan_ += "control-tower ";
          continue;
        }
      }
      const bool towerReady = sp->getAddon() && sp->getAddon()->isCompleted();
      const int air = wraiths + vessels + battlecruisers;
      const bool airBehind = air < c.airShare * (air + ground + 1);
      const bool vesselWanted = scienceReady && towerReady && vessels < c.maxScienceVessels;
      if (!airBehind && !vesselWanted)
        continue;
      const StarportUnit choice = chooseStarportUnit(wraiths, vessels, battlecruisers, c.maxScienceVessels, scienceReady && towerReady,
                                                     c.useBattlecruisers && towerReady && physicsLab);
      UnitType type = choice == StarportUnit::ScienceVessel ? UnitTypes::Terran_Science_Vessel
                    : choice == StarportUnit::Battlecruiser ? UnitTypes::Terran_Battlecruiser : UnitTypes::Terran_Wraith;
      if (!budget.tryReserve(costOf(type)))
      {
        lastPlan_ += "(saving:air) ";
        continue;
      }
      if (ctx_.commander->train(sp, type))
      {
        ++ctx_.metrics.armyTrained;
        if (type == UnitTypes::Terran_Science_Vessel) ++vessels;
        else if (type == UnitTypes::Terran_Battlecruiser) ++battlecruisers;
        else ++wraiths;
        lastPlan_ += concat(type == UnitTypes::Terran_Science_Vessel ? "vessel " : type == UnitTypes::Terran_Battlecruiser ? "bc " : "wraith ");
        if (!loggedFirstAir_)
        {
          loggedFirstAir_ = true;
          ctx_.log.info("Production", concat("first Starport unit started: ", type.c_str()));
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
