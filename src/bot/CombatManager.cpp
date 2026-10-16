#include "CombatManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "Planning.h"
#include "WorkerManager.h"

#include <algorithm>
#include <limits>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kTile = 32;
    constexpr int kLocalRadius = 12 * kTile;     // fight evaluation radius around the wave
    constexpr int kTargetSearchRadius = 8 * kTile;
    constexpr int kJoinFightRadius = 22 * kTile;  // wave units this close to a local enemy count for the fight
    constexpr int kReinforcementBatch = 4;
    constexpr int kJoinNearHomeTiles = 25;            // small reinforcement groups join only while the wave is this close to the rally point
    constexpr int kProgressCheckFrames = 24 * 10;
    constexpr int kMaxNoProgressChecks = 3;
    constexpr int kRallyRadiusTiles = 5;              // smallest "at the rally point" radius
    constexpr int kMaxRegroupFrames = 24 * 6;         // a regroup that takes longer is given up
    constexpr int kRegroupCooldownFrames = 24 * 15;   // advance at least this long before regrouping again
    constexpr int kSiegeRange = 12 * kTile;           // Siege Mode range (checked in bwapi_tests)
    constexpr int kUnsiegeDelayFrames = 24 * 3;       // stay sieged this long after the last threat in range
    constexpr int kRegroupNearTargetTiles = 20;       // regroup only when the lead is this close to the target

    // Support units follow the army instead of fighting: Medics heal, Science Vessels
    // detect hidden units around the army.
    bool isSupport(Unit u) { return u->getType() == UnitTypes::Terran_Medic || u->getType() == UnitTypes::Terran_Science_Vessel; }

    // Units that fight. Medics and Science Vessels cannot hold ground or kill anything,
    // so army and wave sizes count only these (a "16-unit" wave once had 9 medics).
    int shooterCount(const std::vector<Unit>& units)
    {
      int n = 0;
      for (Unit u : units)
        if (!isSupport(u))
          ++n;
      return n;
    }

    // Close enough to the rally point. A unit that has stopped a bit further out is
    // as close as the crowd (or the buildings around the spot) lets it get; ordering
    // it there again would only repeat the same command.
    bool settledNear(Unit u, Position rally, int radius)
    {
      const int d = u->getDistance(rally);
      return d <= radius || (d <= radius + 4 * kTile && !u->isMoving());
    }

    // A sieged (or switching) tank cannot move; true while that is the case.
    bool packUp(Commander& commander, Unit u)
    {
      const Order order = u->getOrder();
      if (order == Orders::Sieging || order == Orders::Unsieging)
        return true;
      if (u->getType() != UnitTypes::Terran_Siege_Tank_Siege_Mode)
        return false;
      commander.unsiege(u);
      return true;
    }

    int valueOf(UnitType t) { return t.mineralPrice() + t.gasPrice(); }

    // Our strength against an enemy of which `enemyAir` (by strength) flies: only
    // what can shoot flyers counts against them.
    double strengthOf(const std::vector<Unit>& units, double enemyAir = 0.0)
    {
      std::vector<double> s;
      for (Unit u : units)
        if (u->exists())
          s.push_back(unitStrengthNow(u, enemyAir));
      return combineStrength(s);
    }

    // Our finished Missile Turrets near `p` (they defend the base with the army).
    void addTurretsNear(std::vector<Unit>& units, Position p, int radius)
    {
      for (Unit t : Broodwar->self()->getUnits())
        if (t->getType() == UnitTypes::Terran_Missile_Turret && t->isCompleted() && t->getDistance(p) < radius)
          units.push_back(t);
    }

    // Units that can shoot while backing off from melee attackers and from ranged
    // units they outrange.
    bool kitesMelee(UnitType t)
    {
      return t == UnitTypes::Terran_Marine || t == UnitTypes::Terran_Vulture || t == UnitTypes::Terran_Goliath ||
             t == UnitTypes::Terran_Siege_Tank_Tank_Mode;
    }

    // Zealots, Zerglings, Ultralisks, Dark Templar, Firebats: ground weapon of melee range.
    bool isMeleeAttacker(UnitType t)
    {
      const WeaponType w = t.groundWeapon();
      return w != WeaponTypes::None && w.maxRange() <= 32 && !t.isWorker() && !t.isBuilding();
    }

    double enemyStrengthOf(const std::vector<Unit>& units)
    {
      std::vector<double> s;
      for (Unit u : units)
        if (canAttackGround(u->getType()))
          s.push_back(unitStrengthNow(u));
      return combineStrength(s);
    }
  }

  const char* CombatManager::stateName() const
  {
    switch (state_)
    {
    case State::Gather: return "gather";
    case State::Attack: return regrouping_ ? "attack(regroup)" : "attack";
    case State::Retreat: return "retreat";
    case State::Defend: return "defend";
    }
    return "?";
  }

  void CombatManager::collectArmy()
  {
    army_.clear();
    for (Unit u : Broodwar->self()->getUnits())
      if (isOurArmyType(u->getType()) && u->isCompleted() && u->exists() && !u->isLoaded() && !bunkerCrew_.count(u->getID()))
        army_.push_back(u);
  }

  void CombatManager::manageBunker()
  {
    // Early game: 4 marines sit in the Bunker at the rally point and shoot from
    // there. Once the early game is over (10:00) or the army is big, they come out
    // and join it again.
    Unit bunker = nullptr;
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType() == UnitTypes::Terran_Bunker && u->isCompleted())
        bunker = u;
    for (auto it = bunkerCrew_.begin(); it != bunkerCrew_.end();)
    {
      Unit m = Broodwar->getUnit(*it);
      it = (!isValid(m) || (!m->isLoaded() && !bunker)) ? bunkerCrew_.erase(it) : std::next(it);
    }
    if (!bunker)
    {
      releaseRepairers();
      return;
    }
    if (!bunkerReleased_ && (ctx_.frame >= 24 * 600 || shooterCount(army_) >= 30))
    {
      bunkerReleased_ = true;
      ctx_.log.info("Combat", concat("early game over: ", bunker->getLoadedUnits().size(), " marines leave the Bunker"));
    }
    if (bunkerReleased_)
    {
      if (!bunker->getLoadedUnits().empty())
        ctx_.commander->unloadAll(bunker);
      bunkerCrew_.clear();
      releaseRepairers();
      return;
    }
    repairBunker(bunker);
    // Fill it from the marines at home (never from a wave that is out).
    while (bunkerCrew_.size() < 4)
    {
      Unit best = nullptr;
      for (Unit u : army_)
        if (u->getType() == UnitTypes::Terran_Marine && !wave_.count(u->getID()) && !bunkerCrew_.count(u->getID()) &&
            !notCrew_.count(u->getID()) && u->getDistance(bunker) <= 20 * kTile &&
            (!best || u->getDistance(bunker) < best->getDistance(bunker)))
          best = u;
      if (!best)
        break;
      bunkerCrew_.insert(best->getID());
      crewSince_[best->getID()] = ctx_.frame;
      army_.erase(std::remove(army_.begin(), army_.end(), best), army_.end());
    }
    for (auto it = bunkerCrew_.begin(); it != bunkerCrew_.end();)
    {
      Unit m = Broodwar->getUnit(*it);
      if (isValid(m) && !m->isLoaded() && ctx_.frame - crewSince_[*it] > 24 * 20)
      {
        // A marine boxed in behind a Barracks was the closest one and stayed in the
        // crew all game, so the Bunker held 3.
        ctx_.log.info("Combat", concat("Marine#", *it, " did not reach the Bunker in 20 s: another marine goes in"));
        notCrew_.insert(*it);
        it = bunkerCrew_.erase(it);
        continue;
      }
      if (isValid(m) && !m->isLoaded())
        ctx_.commander->enterBunker(m, bunker);
      ++it;
    }
  }

  void CombatManager::chaseWorkerHarass()
  {
    // An enemy worker that attacks our SCVs in a base is chased by three SCVs from
    // nearby (UAlbertaBot's scouting Probe killed four SCVs, three of them while
    // they built the first Barracks, and nothing answered it). They stop once it is
    // dead, has left the base (15 tiles from every Command Center) or after 20 s.
    constexpr int kChasers = 3;
    auto nearestDepot = [](Position p) {
      int best = std::numeric_limits<int>::max();
      for (Unit d : Broodwar->self()->getUnits())
        if (d->getType().isResourceDepot())
          best = std::min(best, d->getDistance(p));
      return best;
    };
    for (auto it = chasers_.begin(); it != chasers_.end();)
      it = isValid(Broodwar->getUnit(*it)) ? std::next(it) : chasers_.erase(it);

    Unit harasser = harasserId_ >= 0 ? Broodwar->getUnit(harasserId_) : nullptr;
    if (harasser && (!harasser->exists() || !harasser->isVisible() || nearestDepot(harasser->getPosition()) > 15 * kTile ||
                     ctx_.frame - harassSinceFrame_ > 24 * 20))
    {
      ctx_.log.info("Combat", concat("enemy worker harass over (", harasser->exists() ? "left or chase too long" : "killed", ")"));
      harasser = nullptr;
      harasserId_ = -1;
      releaseChasers();
    }
    if (!harasser)
      for (Player enemy : Broodwar->enemies())
        for (Unit e : enemy->getUnits())
        {
          if (harasser || !e->exists() || !e->isVisible() || !e->getType().isWorker() || nearestDepot(e->getPosition()) > 12 * kTile)
            continue;
          const Unit target = e->getOrderTarget();
          const bool attacksOurs = e->isAttacking() || e->isStartingAttack() ||
                                   (e->getOrder() == Orders::AttackUnit && target && target->getPlayer() == Broodwar->self());
          if (attacksOurs)
          {
            harasser = e;
            harasserId_ = e->getID();
            harassSinceFrame_ = ctx_.frame;
            ctx_.log.info("Combat", concat("enemy ", e->getType().c_str(), " attacks our SCVs at ", e->getTilePosition().x, ",",
                                           e->getTilePosition().y, ": SCVs chase it"));
          }
        }
    if (!harasser)
      return;
    while (static_cast<int>(chasers_.size()) < kChasers)
    {
      Unit scv = ctx_.workers->acquireWorker(harasser->getPosition(), WorkerRole::Defend);
      if (!scv)
        break;
      chasers_.insert(scv->getID());
    }
    for (int id : chasers_)
    {
      Unit scv = Broodwar->getUnit(id);
      if (scv->getOrderTarget() != harasser)
        ctx_.commander->attackUnit(scv, harasser);
    }
  }

  void CombatManager::releaseChasers()
  {
    for (int id : chasers_)
    {
      Unit w = Broodwar->getUnit(id);
      if (isValid(w))
        ctx_.workers->release(w);
    }
    chasers_.clear();
  }

  void CombatManager::layMines()
  {
    // Vultures with Spider Mines:
    //  - in a fight, one under themselves when ground enemies that set mines off
    //    are close (zealots chasing a Vulture run into it);
    //  - at home, up to 6 mines in a field just outside the entrance (or in front
    //    of the natural once it is ours).
    // A Vulture planting a mine is kept out of the army for a moment so the next
    // move or attack order does not cancel the mine.
    for (auto it = mineLayers_.begin(); it != mineLayers_.end();)
    {
      Unit v = Broodwar->getUnit(it->first);
      const bool done = !isValid(v) || ctx_.frame > it->second.until ||
                        (ctx_.frame > it->second.since + 8 && v->getOrder() != Orders::PlaceMine);
      it = done ? mineLayers_.erase(it) : std::next(it);
    }
    if (Broodwar->self()->hasResearched(TechTypes::Spider_Mines))
    {
      auto minesNear = [](Position p, int radius) {
        int n = 0;
        for (Unit m : Broodwar->self()->getUnits())
          if (m->getType() == UnitTypes::Terran_Vulture_Spider_Mine && m->getDistance(p) <= radius)
            ++n;
        return n;
      };
      auto setsMinesOff = [](UnitType t) {
        // Mines ignore air units and hovering ones.
        return !t.isFlyer() && !t.isBuilding() && t != UnitTypes::Terran_Vulture && t != UnitTypes::Protoss_Archon &&
               t != UnitTypes::Protoss_Dark_Archon && t != UnitTypes::Terran_Vulture_Spider_Mine;
      };
      // In a fight.
      for (Unit v : army_)
      {
        if (v->getType() != UnitTypes::Terran_Vulture || v->getSpiderMineCount() == 0 || mineLayers_.count(v->getID()) ||
            ctx_.frame < nextMineFrame_[v->getID()])
          continue;
        bool enemyClose = false;
        for (Unit e : ctx_.info->visibleEnemiesNear(v->getPosition(), 5 * kTile))
          enemyClose = enemyClose || setsMinesOff(e->getType());
        if (!enemyClose || minesNear(v->getPosition(), 2 * kTile) > 0)
          continue;
        if (ctx_.commander->useTechAt(v, TechTypes::Spider_Mines, v->getPosition()))
        {
          mineLayers_[v->getID()] = { ctx_.frame, ctx_.frame + 24 * 2 };
          nextMineFrame_[v->getID()] = ctx_.frame + 24 * 3;
        }
      }
      // A field at home.
      constexpr int kFieldMines = 6;
      const Position spot = ctx_.map->mineFieldSpot();
      if (state_ == State::Gather && !ctx_.strategy->baseUnderThreat() && ctx_.frame >= nextFieldMineFrame_ && spot.isValid() &&
          minesNear(spot, 4 * kTile) < kFieldMines)
      {
        static const Position offsets[] = { {0, 0}, {64, 0}, {-64, 0}, {0, 64}, {0, -64}, {64, 64}, {-64, -64}, {64, -64}, {-64, 64} };
        Position target = Positions::None;
        for (const Position& o : offsets)
        {
          const Position p = spot + o;
          if (p.isValid() && Broodwar->isWalkable(WalkPosition(p)) && minesNear(p, 40) == 0)
          {
            target = p;
            break;
          }
        }
        Unit best = nullptr;
        for (Unit v : army_)
          if (v->getType() == UnitTypes::Terran_Vulture && v->getSpiderMineCount() > 0 && !mineLayers_.count(v->getID()) &&
              !wave_.count(v->getID()) && v->getDistance(spot) <= 40 * kTile && (!best || v->getDistance(spot) < best->getDistance(spot)))
            best = v;
        if (best && target.isValid() && ctx_.commander->useTechAt(best, TechTypes::Spider_Mines, target))
        {
          if (!loggedMineField_)
            ctx_.log.info("Combat", concat("Vultures lay a Spider Mine field at ", spot.x / 32, ",", spot.y / 32));
          loggedMineField_ = true;
          mineLayers_[best->getID()] = { ctx_.frame, ctx_.frame + 24 * 15 };
          nextFieldMineFrame_ = ctx_.frame + 24 * 2;
        }
      }
    }
    army_.erase(std::remove_if(army_.begin(), army_.end(), [this](Unit u) { return mineLayers_.count(u->getID()) > 0; }), army_.end());
  }

  void CombatManager::repairBunker(Unit bunker)
  {
    // SCVs repair the Bunker while ground enemies are at it. In two games zealots
    // took a full Bunker in seconds at 6:45 while every SCV kept mining. The SCVs go
    // back to mining once it is whole again or nothing is near it.
    constexpr int kRepairers = 3;
    constexpr int kRepairRadius = 8 * kTile;
    for (auto it = repairers_.begin(); it != repairers_.end();)
      it = isValid(Broodwar->getUnit(*it)) ? std::next(it) : repairers_.erase(it);
    bool enemyNear = false;
    for (Unit e : ctx_.info->visibleEnemiesNear(bunker->getPosition(), kRepairRadius))
      enemyNear = enemyNear || (!e->isFlying() && !e->getType().isWorker() && canAttackGround(e->getType()));
    // Low minerals do not send them away: repair just waits for money. Releasing
    // them then made them walk back and forth and the log repeat every second.
    if (!enemyNear || bunker->getHitPoints() >= bunker->getType().maxHitPoints())
    {
      releaseRepairers();
      return;
    }
    while (static_cast<int>(repairers_.size()) < kRepairers)
    {
      Unit scv = ctx_.workers->acquireWorker(bunker->getPosition(), WorkerRole::Repair);
      if (!scv)
        break;
      if (repairers_.empty() && ctx_.frame >= nextRepairLogFrame_)
        ctx_.log.info("Combat", concat("Bunker under attack (", bunker->getHitPoints(), "/", bunker->getType().maxHitPoints(),
                                       " hp): SCVs repair it"));
      nextRepairLogFrame_ = ctx_.frame + 24 * 30;
      repairers_.insert(scv->getID());
    }
    for (int id : repairers_)
      ctx_.commander->repair(Broodwar->getUnit(id), bunker);
  }

  void CombatManager::releaseRepairers()
  {
    for (int id : repairers_)
    {
      Unit w = Broodwar->getUnit(id);
      if (isValid(w))
        ctx_.workers->release(w);
    }
    repairers_.clear();
  }

  Position CombatManager::groupCenter(const std::vector<Unit>& units) const
  {
    std::vector<Point> shooters, all;
    for (Unit u : units)
    {
      all.push_back(toPoint(u->getPosition()));
      if (!isSupport(u))
        shooters.push_back(toPoint(u->getPosition()));
    }
    if (all.empty())
      return ctx_.map->rallyPoint();
    return toPosition(centroid(shooters.empty() ? all : shooters));
  }

  void CombatManager::onUnitDestroy(Unit unit)
  {
    if (!unit)
      return;
    tankThreatFrame_.erase(unit->getID());
    if (wave_.erase(unit->getID()))
    {
      ++waveLost_;
      waveValueLost_ += valueOf(unit->getType());
    }
    else if (state_ == State::Attack && Broodwar->self()->isEnemy(unit->getPlayer()))
    {
      ++waveKilled_;
      waveValueKilled_ += valueOf(unit->getType());
    }
  }

  void CombatManager::useScans()
  {
    if (!ctx_.config.useComsatScan || ctx_.frame - lastScanFrame_ < 24 * 10)
      return;
    Unit comsat = nullptr;
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType() == UnitTypes::Terran_Comsat_Station && u->isCompleted() && u->getEnergy() >= 50)
        comsat = u;
    if (!comsat)
      return;
    for (Player enemy : Broodwar->enemies())
      for (Unit e : enemy->getUnits())
      {
        if (!e->exists() || e->isDetected() || !(e->isCloaked() || e->isBurrowed()))
          continue;
        // Only scan hidden units that are next to our own units.
        bool nearUs = false;
        for (Unit u : Broodwar->self()->getUnits())
          if (u->getDistance(e) < 8 * kTile)
          {
            nearUs = true;
            break;
          }
        if (!nearUs)
          continue;
        if (ctx_.commander->useTechAt(comsat, TechTypes::Scanner_Sweep, e->getPosition(), 24 * 10))
        {
          lastScanFrame_ = ctx_.frame;
          ++ctx_.metrics.scansUsed;
          ctx_.log.info("Combat", concat("scanning cloaked ", e->getType().c_str(), " at ", e->getPosition().x, ",", e->getPosition().y));
        }
        return;
      }
  }

  bool CombatManager::dodging(Unit u) const
  {
    auto it = dodgeUntil_.find(u->getID());
    return it != dodgeUntil_.end() && ctx_.frame < it->second;
  }

  void CombatManager::dodgeScarabs()
  {
    // Every level: a Scarab does 100 damage to everything within about two tiles of
    // the unit it flies at; Reavers cut down our clumped army a group at a time.
    // Our units next to that unit step 3 tiles straight away from it for half a
    // second; the target itself keeps fighting (the Scarab follows it anyway).
    constexpr int kSplash = 2 * kTile + 8;
    for (auto it = dodgeUntil_.begin(); it != dodgeUntil_.end();)
      it = ctx_.frame >= it->second ? dodgeUntil_.erase(it) : std::next(it);
    for (Player enemy : Broodwar->enemies())
      for (Unit s : enemy->getUnits())
      {
        if (!s->exists() || s->getType() != UnitTypes::Protoss_Scarab)
          continue;
        Unit target = s->getOrderTarget() ? s->getOrderTarget() : s->getTarget();
        if (!isValid(target) || target->getPlayer() != Broodwar->self())
          continue;
        const Position hit = target->getPosition();
        int moved = 0;
        for (Unit u : army_)
        {
          if (u == target || u->isFlying() || u->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode || dodging(u) ||
              u->getDistance(hit) > kSplash)
            continue;
          const Position dest = toPosition(awayFrom(toPoint(u->getPosition()), toPoint(hit), 3 * kTile));
          if (!dest.isValid())
            continue;
          ctx_.commander->move(u, dest, 4);
          dodgeUntil_[u->getID()] = ctx_.frame + 12;
          ++moved;
        }
        dodges_ += moved;
        if (moved > 0 && ctx_.frame >= nextDodgeLogFrame_)
        {
          ctx_.log.info("Combat", concat("Scarab at ", target->getType().c_str(), ": ", moved, " units next to it step aside"));
          nextDodgeLogFrame_ = ctx_.frame + 24 * 30;
        }
      }
  }

  void CombatManager::update()
  {
    collectArmy();
    manageBunker();
    chaseWorkerHarass();
    layMines();
    useScans();
    dodgeScarabs();

    if (ctx_.strategy->baseUnderThreat())
    {
      lastThreatFrame_ = ctx_.frame;
      // An army holding out on the map only comes home when the units near home and
      // the Bunker cannot deal with the attack themselves.
      bool homeHolds = false;
      if (state_ == State::Gather && holdPoint_.isValid())
      {
        std::vector<Unit> home;
        for (Unit u : army_)
          if (u->getDistance(ctx_.strategy->threatCenter()) < 25 * kTile)
            home.push_back(u);
        for (int id : bunkerCrew_)
          if (Unit m = Broodwar->getUnit(id))
            home.push_back(m);
        addTurretsNear(home, ctx_.strategy->threatCenter(), 25 * kTile);
        homeHolds = enemyStrengthOf(ctx_.strategy->baseThreats()) <=
                    strengthOf(home, enemyAirFraction(ctx_.strategy->baseThreats())) * 1.2;
      }
      if (state_ == State::Gather && !army_.empty() && ctx_.frame >= ignoreThreatUntilFrame_ && !homeHolds)
      {
        state_ = State::Defend;
        stateFrame_ = ctx_.frame;
        holdPoint_ = Positions::None;
        ctx_.log.info("Combat", concat("base under attack by ", ctx_.strategy->baseThreats().size(), " units: defending"));
      }
      else if (state_ == State::Attack && ctx_.config.recallArmyToDefend && ctx_.strategy->baseThreats().size() >= 2)
      {
        // Only when what is at home (units not in the wave, the Bunker crew) cannot
        // hold: one game recalled four waves for raids of 1-3 units.
        std::vector<Unit> waveUnits, home;
        for (Unit u : army_)
          (wave_.count(u->getID()) ? waveUnits : home).push_back(u);
        for (int id : bunkerCrew_)
          if (Unit m = Broodwar->getUnit(id))
            home.push_back(m);
        addTurretsNear(home, ctx_.strategy->threatCenter(), 25 * kTile);
        const double threat = enemyStrengthOf(ctx_.strategy->baseThreats());
        const double defense = strengthOf(home, enemyAirFraction(ctx_.strategy->baseThreats()));
        if (threat > defense * 1.2 && groupCenter(waveUnits).getApproxDistance(ctx_.map->mainPosition()) > 30 * kTile)
        {
          ctx_.log.info("Combat", concat("base attack too strong for home (", static_cast<int>(threat), " vs ",
                                         static_cast<int>(defense), "): recalling the wave"));
          endWave("recalled to defend the base", State::Defend);
        }
      }
    }

    switch (state_)
    {
    case State::Gather: gather(); break;
    case State::Attack: attack(); break;
    case State::Retreat: retreat(); break;
    case State::Defend: defend(); break;
    }
  }

  void CombatManager::gather()
  {
    // A Command Center being built away from the main is guarded by the army until
    // it stands (the natural's and a center base's were killed unfinished).
    Position guard = Positions::None;
    for (Unit cc : Broodwar->self()->getUnits())
      if (cc->getType().isResourceDepot() && !cc->isCompleted() && cc->getDistance(ctx_.map->mainPosition()) > 12 * kTile)
        guard = cc->getPosition();
    if (guard.isValid() && guard != guardedBase_)
      ctx_.log.info("Combat", concat("army guards the new base at ", guard.x / 32, ",", guard.y / 32, " until it stands"));
    guardedBase_ = guard;

    // Every level: the army does not sit out on the map. After a retreat it held in
    // the middle of the map for 3.5 minutes against Vultures, Mines and sieged Tanks
    // without answering (no defense there), and each new unit walked out to it
    // alone: 17 units became 3. It holds for 45 s at most, fights what comes at it
    // if it is the stronger side, and otherwise goes home to gather; units far from
    // the hold gather at home instead of walking out one by one.
    std::vector<Unit> atHold;
    if (holdPoint_.isValid())
    {
      for (Unit u : army_)
        if (u->getDistance(holdPoint_) < 20 * kTile)
          atHold.push_back(u);
      std::vector<Unit> enemies;
      for (Unit e : ctx_.info->visibleEnemiesNear(holdPoint_, 14 * kTile))
        if (!e->getType().isWorker() && canAttackGround(e->getType()))
          enemies.push_back(e);
      const double own = strengthOf(atHold, enemyAirFraction(enemies));
      const double enemy = enemyStrengthOf(enemies);
      const bool expired = ctx_.frame - holdSinceFrame_ > kHoldPointFrames;
      const bool outmatched = !enemies.empty() && defenseOutmatched(own, enemy);
      if (expired || outmatched || atHold.empty())
      {
        ctx_.log.info("Combat", outmatched ? concat("enemy at the hold point is stronger (", static_cast<int>(enemy), " vs ",
                                                   static_cast<int>(own), "): the army gathers at home")
                                           : std::string("hold over: the army gathers at home"));
        holdPoint_ = Positions::None;
        atHold.clear();
      }
      else if (!enemies.empty())
      {
        fightCenter_ = groupCenter(atHold);
        for (Unit u : atHold)
          if (isSupport(u))
            moveMedic(u, fightCenter_);
          else
            fight(u, enemies.front()->getPosition(), own, enemy);
        fightCenter_ = Positions::None;
        return;
      }
    }
    const Position rally = guard.isValid() ? guard : holdPoint_.isValid() ? holdPoint_ : ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(army_.size())) * kTile;
    int nearRally = 0;
    for (Unit u : army_)
    {
      if (dodging(u))
        continue;
      if ((isSiegeTank(u->getType()) && updateSiege(u) != TankMode::Mobile) || settledNear(u, rally, rallyRadius))
      {
        ++nearRally;
        continue;
      }
      // Far from the hold point: gather at home, not alone across the map.
      const bool farFromHold = holdPoint_.isValid() && !guard.isValid() && u->getDistance(holdPoint_) >= 20 * kTile;
      const Position to = farFromHold ? ctx_.map->rallyPoint() : rally;
      if (farFromHold && settledNear(u, to, rallyRadius))
        continue;
      if (isSupport(u))
        ctx_.commander->move(u, to);
      else
        ctx_.commander->attackMove(u, to);
    }
    if (army_.empty() || ctx_.frame < nextAttackAllowedFrame_)
      return;

    // Every level: enemy buildings at our doorstep are cleared as soon as the army is
    // clearly stronger than what defends them, without waiting for the attack size.
    // A Gateway, a Forge and three Cannons stood at our natural from 3:00 to 13:00 and
    // kept us on one base; Siege Tanks outrange the Cannons.
    if (auto door = doorstepTarget(true))
    {
      ctx_.log.info("Combat", concat("clearing our doorstep: ", door->reason));
      startWave(*door);
      doorstepWave_ = true;
      return;
    }

    AttackInputs in;
    in.armySize = shooterCount(army_);
    in.frame = ctx_.frame;
    // Against the enemy army we know, flyers included: Firebats and Vultures do not
    // count against its Mutalisks.
    in.ownStrength = strengthOf(army_, ctx_.info->knownEnemyAirFraction());
    in.knownEnemyStrength = ctx_.info->estimatedEnemyArmyStrength();
    in.useStrengthEstimate = ctx_.config.enemyInfoUsage >= 2;
    AttackParams p;
    p.requiredSize = ctx_.strategy->attackSize();
    p.earliestFrame = ctx_.config.earliestAttackFrame;
    p.requiredRatio = ctx_.strategy->attackStrengthRatio();
    p.maxSize = ctx_.config.maxAttackSize;
    if (!shouldAttack(in, p))
    {
      readySinceFrame_ = -1;
      return;
    }
    if (readySinceFrame_ < 0)
      readySinceFrame_ = ctx_.frame;

    // Move out together: wait (at most 45 s after the army became ready) until most
    // of it stands at the rally point.
    if (nearRally < static_cast<int>(army_.size() * 0.7) && ctx_.frame - readySinceFrame_ < 24 * 45)
      return;

    auto target = ctx_.strategy->chooseAttackTarget(rally);
    if (!target)
    {
      if (!loggedNoTarget_)
        ctx_.log.info("Combat", "army is ready but no reachable target is known; waiting");
      loggedNoTarget_ = true;
      return;
    }
    loggedNoTarget_ = false;
    readySinceFrame_ = -1;
    startWave(*target);
  }

  void CombatManager::startWave(const AttackTarget& target)
  {
    doorstepWave_ = false;
    wave_.clear();
    int marines = 0, medics = 0, firebats = 0, mech = 0;
    for (Unit u : army_)
    {
      wave_.insert(u->getID());
      if (u->getType() == UnitTypes::Terran_Marine) ++marines;
      else if (u->getType() == UnitTypes::Terran_Medic) ++medics;
      else if (isMechType(u->getType())) ++mech;
      else ++firebats;
    }
    waveStartSize_ = static_cast<int>(wave_.size());
    waveCommitted_ = shooterCount(army_);
    waveLost_ = waveValueLost_ = waveKilled_ = waveValueKilled_ = 0;
    target_ = target.position;
    targetReason_ = target.reason;
    const Position center = groupCenter(army_);
    bestDistanceToTarget_ = center.getApproxDistance(target_);
    bestLeadDistance_ = std::numeric_limits<int>::max();
    lastProgressCheckFrame_ = ctx_.frame;
    noProgressChecks_ = 0;
    regrouping_ = false;
    nextRegroupAllowedFrame_ = 0;
    regroups_ = 0;
    sieges_ = 0;
    state_ = State::Attack;
    stateFrame_ = ctx_.frame;
    ++ctx_.metrics.attackWaves;
    if (ctx_.metrics.firstAttackFrame < 0)
      ctx_.metrics.firstAttackFrame = ctx_.frame;
    ctx_.log.info("Combat", concat("attack wave ", ctx_.metrics.attackWaves, ": ", waveStartSize_, " units (", marines, " marines, ",
                                   medics, " medics, ", firebats, " firebats, ", mech, " mech) -> ", target_.x, ",", target_.y, " [", targetReason_,
                                   "] ownStrength=", static_cast<int>(strengthOf(army_, ctx_.info->knownEnemyAirFraction())),
                                   " knownEnemyStrength=", static_cast<int>(ctx_.info->knownEnemyArmyStrength()),
                                   " estimated=", static_cast<int>(ctx_.info->estimatedEnemyArmyStrength()),
                                   " requiredSize=", ctx_.strategy->attackSize()));
  }

  void CombatManager::holdPosition(Position p)
  {
    // The army waits there for the next target and the reinforcements, instead of
    // walking back across the map and out again. The spot is where a ground unit
    // of ours stands (the group center can be on a cliff; in 0.6.1 it never held).
    Unit closest = nullptr;
    for (Unit u : army_)
      if (!u->isFlying() && (!closest || u->getDistance(p) < closest->getDistance(p)))
        closest = u;
    if (closest && ctx_.map->groundReachable(ctx_.map->rallyPoint(), closest->getPosition()))
    {
      holdPoint_ = closest->getPosition();
      holdSinceFrame_ = ctx_.frame;
      ctx_.log.info("Combat", concat("army holds at ", holdPoint_.x, ",", holdPoint_.y, " instead of walking home"));
    }
  }

  void CombatManager::endWave(const char* reason, State next)
  {
    if (next != State::Gather)
      holdPoint_ = Positions::None;
    if (next == State::Retreat)
    {
      retreatFrom_ = groupCenter(army_);
      safeSinceFrame_ = -1;
    }
    ctx_.strategy->onWaveEnd(waveLost_, waveKilled_, waveValueLost_, waveValueKilled_, reason);
    wave_.clear();
    state_ = next;
    stateFrame_ = ctx_.frame;
    regrouping_ = false;
    ctx_.log.info("Combat", concat("wave over: ", reason, " -> ", stateName(), " (regrouped ", regroups_, " times, sieged ", sieges_,
                                   " times, ", kiteSteps_, " back-off steps from melee/outranged units, ", dodges_,
                                   " Scarab dodges)"));
    kiteSteps_ = 0;
    dodges_ = 0;
  }

  std::optional<AttackTarget> CombatManager::doorstepTarget(bool checkStrength) const
  {
    const Position main = ctx_.map->mainPosition();
    const int naturalIdx = ctx_.map->naturalIndex();
    const Position natural = naturalIdx >= 0 ? ctx_.map->bases()[naturalIdx].center : Positions::None;
    const EnemyRecord* best = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    for (const auto& [id, r] : ctx_.info->enemyBuildings())
    {
      const int d = r.position.getApproxDistance(main);
      const bool atDoor = d < 30 * kTile || (natural.isValid() && r.position.getApproxDistance(natural) < 12 * kTile);
      if (atDoor && d < bestDist && !ctx_.strategy->isMarkedUnreachable(r.position))
      {
        best = &r;
        bestDist = d;
      }
    }
    if (!best)
      return std::nullopt;
    if (checkStrength)
    {
      // What defends it: static defense next to it and enemy fighters seen there in
      // the last 30 s.
      std::vector<double> defenders;
      for (const auto& [id, r] : ctx_.info->enemyBuildings())
        if (canAttackGround(r.type) && r.position.getApproxDistance(best->position) < 9 * kTile)
          defenders.push_back(typeStrength(r.type));
      for (const auto& [id, e] : ctx_.info->enemyUnits())
        if (!e.type.isWorker() && canAttackGround(e.type) && ctx_.frame - e.lastSeenFrame < 24 * 30 &&
            e.position.getApproxDistance(best->position) < 12 * kTile)
          defenders.push_back(typeStrength(e.type));
      if (!canClearDoorstep(strengthOf(army_), combineStrength(defenders), shooterCount(army_)))
        return std::nullopt;
    }
    return AttackTarget{ best->position, concat("enemy ", best->type.c_str(), " at our doorstep") };
  }

  bool CombatManager::scanReady() const
  {
    if (!ctx_.config.useComsatScan)
      return false;
    for (Unit c : Broodwar->self()->getUnits())
      if (c->getType() == UnitTypes::Terran_Comsat_Station && c->isCompleted() &&
          c->getEnergy() >= TechTypes::Scanner_Sweep.energyCost())
        return true;
    return false;
  }

  bool CombatManager::staysSieged(Unit u)
  {
    return u->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode && updateSiege(u) != TankMode::Mobile;
  }

  bool CombatManager::pickNewTarget(Position from)
  {
    if (doorstepWave_)
    {
      // Clearing our doorstep only: the next building there, or the wave is over.
      auto d = doorstepTarget(false);
      if (!d)
        return false;
      target_ = d->position;
      targetReason_ = d->reason;
      bestDistanceToTarget_ = from.getApproxDistance(target_);
      bestLeadDistance_ = std::numeric_limits<int>::max();
      noProgressChecks_ = 0;
      lastProgressCheckFrame_ = ctx_.frame;
      ctx_.log.info("Combat", concat("new target ", target_.x, ",", target_.y, " [", targetReason_, "]"));
      return true;
    }
    auto t = ctx_.strategy->chooseAttackTarget(from);
    if (t && t->position.getApproxDistance(target_) < 3 * kTile && from.getApproxDistance(target_) < 6 * kTile)
    {
      // Same spot again although we are standing on it: put it aside for a minute.
      ctx_.strategy->markUnreachable(target_, 24 * 60);
      t = ctx_.strategy->chooseAttackTarget(from);
    }
    if (!t)
      return false;
    target_ = t->position;
    targetReason_ = t->reason;
    bestDistanceToTarget_ = from.getApproxDistance(target_);
    bestLeadDistance_ = std::numeric_limits<int>::max();
    noProgressChecks_ = 0;
    lastProgressCheckFrame_ = ctx_.frame;
    ctx_.log.info("Combat", concat("new target ", target_.x, ",", target_.y, " [", targetReason_, "]"));
    return true;
  }

  CombatManager::TankMode CombatManager::updateSiege(Unit tank)
  {
    // Siege when something that shoots at ground units is within siege range; pack
    // up only after nothing has been in range for a few seconds, so a tank does not
    // unsiege in the middle of a fight that briefly moved out of sight.
    if (!Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode))
      return TankMode::Mobile;
    const Order order = tank->getOrder();
    if (order == Orders::Sieging || order == Orders::Unsieging)
      return TankMode::Switching; // a second order while switching only fails
    int& lastThreat = tankThreatFrame_.try_emplace(tank->getID(), -kUnsiegeDelayFrames - 1).first->second;
    for (Unit e : ctx_.info->visibleEnemiesNear(tank->getPosition(), kSiegeRange + kTile))
      if (e->isDetected() && !e->isFlying() && canAttackGround(e->getType()) && tank->getDistance(e) <= kSiegeRange)
      {
        lastThreat = ctx_.frame;
        break;
      }
    const bool threat = ctx_.frame - lastThreat <= kUnsiegeDelayFrames;
    if (tank->getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode)
    {
      if (lastThreat != ctx_.frame)
        return TankMode::Mobile;
      if (ctx_.commander->siege(tank))
        ++sieges_;
      return TankMode::Switching;
    }
    if (threat)
      return TankMode::Sieged;
    ctx_.commander->unsiege(tank);
    return TankMode::Switching;
  }

  void CombatManager::moveMedic(Unit medic, Position center)
  {
    if (medic->getDistance(center) > 3 * kTile)
      ctx_.commander->move(medic, center);
  }

  void CombatManager::fight(Unit u, Position advanceTo, double localOwn, double localEnemy)
  {
    const UnitType type = u->getType();
    const bool hitsAir = type.airWeapon() != WeaponTypes::None;
    const bool sieged = type == UnitTypes::Terran_Siege_Tank_Siege_Mode;
    if (dodging(u))
      return;   // stepping aside from a Scarab
    // Every level: an attacker we cannot see (a Dark Templar without detection) is
    // not fought where it stands; units step 3 tiles away from it until a scan or a
    // Turret shows it. They stood and died to it.
    if (!u->isFlying() && !scanReady())
      for (Unit e : ctx_.info->visibleEnemiesNear(u->getPosition(), 3 * kTile))
        if (!e->isDetected() && canAttackGround(e->getType()) && !e->isFlying())
        {
          const Position away = toPosition(awayFrom(toPoint(u->getPosition()), toPoint(e->getPosition()), 3 * kTile));
          if (away.isValid())
          {
            ++hiddenSteps_;
            ctx_.commander->move(u, away, 8);
            return;
          }
        }
    if (isSiegeTank(type) && updateSiege(u) == TankMode::Switching)
      return;

    // Visible, targetable enemies around this unit (a sieged tank: only what it can hit).
    std::vector<Unit> enemies = ctx_.info->visibleEnemiesNear(u->getPosition(), sieged ? kSiegeRange : kTargetSearchRadius);
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](Unit e) {
                    return !e->isDetected() || (e->isFlying() && !hitsAir) || (sieged && !u->isInWeaponRange(e));
                  }), enemies.end());

    // Stim before a fight we intend to take.
    const bool canStim = type == UnitTypes::Terran_Marine || type == UnitTypes::Terran_Firebat;
    if (canStim && ctx_.config.useStim && Broodwar->self()->hasResearched(TechTypes::Stim_Packs) && u->getStimTimer() == 0 &&
        u->getHitPoints() >= 25 && localEnemy > 0.0 && localOwn >= localEnemy * 0.7)
    {
      for (Unit e : enemies)
        if (canAttackGround(e->getType()) && u->getDistance(e) < 6 * kTile)
        {
          ctx_.commander->useTech(u, TechTypes::Stim_Packs, 24 * 10);
          break;
        }
    }

    // Back off while the weapon reloads, shoot when it is ready:
    //  - from melee attackers (Marines stood next to Zealots and were cut down; Tanks
    //    walked in front of them). Tanks keep a wider distance;
    //  - from ranged units we outrange by a tile or more (a Tank, a Goliath or a
    //    U-238 Marine against Dragoons): stay just outside their range instead of
    //    trading shots at equal range. Upgrades on both sides count.
    // Every level: the same rule, the army that uses it differs.
    if (kitesMelee(type) && !enemies.empty())
    {
      const int meleeTrigger = type == UnitTypes::Terran_Siege_Tank_Tank_Mode ? 4 * kTile : type == UnitTypes::Terran_Vulture ? 3 * kTile : 2 * kTile + 16;
      const int ourRange = Broodwar->self()->weaponMaxRange(type.groundWeapon());
      Unit melee = nullptr;
      for (Unit e : enemies)
      {
        const UnitType et = e->getType();
        if (e->isFlying() || et.isBuilding() || et.isWorker() || !canAttackGround(et))
          continue;
        int trigger = -1;
        if (isMeleeAttacker(et))
          trigger = meleeTrigger;
        else if (et.groundWeapon() != WeaponTypes::None)
        {
          const int theirRange = e->getPlayer()->weaponMaxRange(et.groundWeapon());
          if (theirRange + kTile <= ourRange)
            trigger = theirRange + kTile / 2;
        }
        if (trigger >= 0 && u->getDistance(e) <= trigger && (!melee || u->getDistance(e) < u->getDistance(melee)))
          melee = e;
      }
      if (melee && u->getGroundWeaponCooldown() > 0)
      {
        const Position from = u->getPosition();
        const Position away = from - melee->getPosition();
        const double len = std::max(1.0, std::sqrt(static_cast<double>(away.x) * away.x + static_cast<double>(away.y) * away.y));
        Position step(from.x + static_cast<int>(away.x * 80 / len), from.y + static_cast<int>(away.y * 80 / len));
        if (!step.isValid() || !Broodwar->isWalkable(WalkPosition(step)))
        {
          // Wall behind: back off towards home instead.
          const Position home = ctx_.map->rallyPoint();
          const Position h = home - from;
          const double hl = std::max(1.0, std::sqrt(static_cast<double>(h.x) * h.x + static_cast<double>(h.y) * h.y));
          step = Position(from.x + static_cast<int>(h.x * 80 / hl), from.y + static_cast<int>(h.y * 80 / hl));
        }
        if (step.isValid())
        {
          ++kiteSteps_;
          ctx_.commander->move(u, step, 4);
          return;
        }
      }
    }

    // Tanks do not lead: while nothing is in reach, a tank ahead of its group waits
    // for it instead of walking into the enemy first.
    if (type == UnitTypes::Terran_Siege_Tank_Tank_Mode && enemies.empty() && fightCenter_.isValid() &&
        u->getDistance(advanceTo) + 3 * kTile < fightCenter_.getApproxDistance(advanceTo))
    {
      if (u->getDistance(fightCenter_) > 2 * kTile)
        ctx_.commander->move(u, fightCenter_);
      return;
    }

    // Focus fire: pick an explicit target when the difficulty uses it.
    if (ctx_.config.focusFireLevel > 0 && !enemies.empty())
    {
      std::vector<TargetInfo> infos;
      for (Unit e : enemies)
      {
        TargetInfo t;
        // A Shuttle or Dropship counts as what it may carry (a Reaver).
        const bool transport = isDropTransport(e->getType());
        t.canAttackUs = canAttackGround(e->getType()) || transport;
        t.isBuilding = e->getType().isBuilding();
        t.isWorker = e->getType().isWorker();
        t.hitPoints = e->getHitPoints() + e->getShields();
        const int maxHp = std::max(1, e->getType().maxHitPoints() + e->getType().maxShields());
        t.hpFraction = static_cast<double>(t.hitPoints) / maxHp;
        t.damagePerFrame = transport ? groundDamagePerFrame(UnitTypes::Protoss_Reaver) : groundDamagePerFrame(e->getType());
        t.distance = u->getDistance(e);
        t.inRange = u->isInWeaponRange(e);
        t.ourDamageFactor = damageFactor(e->isFlying() ? type.airWeapon() : type.groundWeapon(), e->getType().size());
        infos.push_back(t);
      }
      const int idx = chooseTarget(infos, ctx_.config.focusFireLevel);
      if (idx >= 0)
      {
        Unit target = enemies[idx];
        if (u->getTarget() != target && u->getOrderTarget() != target && !u->isAttackFrame())
          ctx_.commander->attackUnit(u, target, 12);
        return;
      }
    }

    // Default: attack-move. Do not interrupt an ongoing attack or repeat the same order.
    if (sieged || u->isAttackFrame() || u->getOrder() == Orders::AttackUnit)
      return;
    if (u->getDistance(advanceTo) < 2 * kTile)
      return;
    if (u->getOrder() == Orders::AttackMove && u->getOrderTargetPosition().getApproxDistance(advanceTo) < 3 * kTile)
      return;
    ctx_.commander->attackMove(u, advanceTo);
  }

  void CombatManager::attack()
  {
    std::vector<Unit> waveUnits, reinforcements;
    for (Unit u : army_)
      (wave_.count(u->getID()) ? waveUnits : reinforcements).push_back(u);
    if (waveUnits.empty())
    {
      endWave("wave destroyed", State::Gather);
      return;
    }

    // Reinforcements collect at the rally point and join in groups, so single
    // units do not walk into the enemy one by one. They defend the base if needed.
    const Position rally = ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(reinforcements.size())) * kTile;
    std::vector<Unit> ready;
    for (Unit r : reinforcements)
    {
      if (ctx_.strategy->baseUnderThreat())
      {
        fight(r, ctx_.strategy->threatCenter(), 0.0, 0.0);
        continue;
      }
      if (isSiegeTank(r->getType()) && updateSiege(r) != TankMode::Mobile)
        continue;
      if (settledNear(r, rally, rallyRadius))
        ready.push_back(r);
      else if (isSupport(r))
        ctx_.commander->move(r, rally);
      else
        ctx_.commander->attackMove(r, rally);
    }
    // Small groups only join while the wave is still near home. Once it is out on the
    // map, a few units walking after it get caught alone on the way, so they wait
    // until they are a full wave themselves (or the wave comes back and merges).
    const int ownWaveSize = shooterCount(ready);
    const bool waveNearHome = groupCenter(waveUnits).getApproxDistance(rally) <= kJoinNearHomeTiles * kTile;
    if (ownWaveSize >= kReinforcementBatch && (waveNearHome || ownWaveSize >= ctx_.strategy->attackSize()))
    {
      for (Unit r : ready)
      {
        wave_.insert(r->getID());
        waveUnits.push_back(r);
      }
      waveCommitted_ += ownWaveSize;
      ctx_.log.info("Combat", concat(ownWaveSize, " reinforcements join the wave",
                                     waveNearHome ? "" : " (as a group of their own)"));
    }

    const Position center = groupCenter(waveUnits);
    const int shooters = shooterCount(waveUnits);
    if (shooters == 0)
    {
      ctx_.log.info("Combat", concat("retreating: only ", waveUnits.size(), " medics left in the wave"));
      endWave("retreat", State::Retreat);
      return;
    }

    // The fight is where our units meet visible enemies, not around the wave center:
    // a strung-out wave can have nobody at its center (a 46-unit wave once retreated
    // from "0 vs 130"). Enemies near any wave unit, against our units that can join
    // that fight within a few seconds - not only the ones already in it (a 40-unit
    // wave once retreated because its first 3 marines met 9 enemies).
    std::vector<Unit> localEnemies;
    std::set<int> seenEnemies;
    for (Unit u : waveUnits)
      for (Unit e : ctx_.info->visibleEnemiesNear(u->getPosition(), kLocalRadius))
        if (seenEnemies.insert(e->getID()).second)
          localEnemies.push_back(e);
    std::vector<Unit> localOwnUnits;
    for (Unit u : waveUnits)
      for (Unit e : localEnemies)
        if (u->getDistance(e) < kJoinFightRadius)
        {
          localOwnUnits.push_back(u);
          break;
        }
    const double localAir = enemyAirFraction(localEnemies);
    const double localOwn = strengthOf(localOwnUnits, localAir);
    const double localEnemy = enemyStrengthOf(localEnemies);

    // Survivors are compared with every shooter sent (start + reinforcements), so a
    // wave that keeps being refilled still pulls back once it has lost most of them.
    // When only the front of a strung-out wave is outmatched but the whole wave is
    // not, nobody turns around: the rest is on its way into the same fight (a
    // 29-shooter wave once retreated because its first 2 units met a cannon, and
    // pulling the front back made the army walk back and forth).
    const bool waveCanWin = !shouldRetreat(strengthOf(waveUnits, localAir), localEnemy, ctx_.config.retreatStrengthRatio, shooters,
                                           waveCommitted_, ctx_.config.retreatArmyFraction);
    if (!waveCanWin && shouldRetreat(localOwn, localEnemy, ctx_.config.retreatStrengthRatio, shooters, waveCommitted_,
                                     ctx_.config.retreatArmyFraction))
    {
      ctx_.log.info("Combat", concat("retreating: wave ", shooters, "/", waveCommitted_, " shooters, local strength ",
                                     static_cast<int>(localOwn), " (", localOwnUnits.size(), " units) vs enemy ",
                                     static_cast<int>(localEnemy), " (", localEnemies.size(), " units)"));
      endWave("retreat", State::Retreat);
      return;
    }

    // The shooter closest to the target leads. Fights and the target itself are often
    // at the lead while the center is still far behind, so both are looked at.
    Unit lead = nullptr;
    int leadDistance = std::numeric_limits<int>::max();
    for (Unit u : waveUnits)
      if (!isSupport(u) && u->getDistance(target_) < leadDistance)
      {
        lead = u;
        leadDistance = u->getDistance(target_);
      }
    if (!lead)
      leadDistance = center.getApproxDistance(target_);
    const bool leadEngaged = lead && !ctx_.info->visibleEnemiesNear(lead->getPosition(), kTargetSearchRadius).empty();
    // Target reached / no progress / regroup look at the wave center as before.
    const std::vector<Unit> centerEnemies = ctx_.info->visibleEnemiesNear(center, kLocalRadius);
    const bool engaged = !centerEnemies.empty() || leadEngaged;

    // Reached the target and nothing is fighting us: choose the next target.
    if ((center.getApproxDistance(target_) < 6 * kTile && centerEnemies.empty()) || (leadDistance < 3 * kTile && !leadEngaged))
    {
      if (!pickNewTarget(center))
      {
        holdPosition(center);
        endWave("no more targets known", State::Gather);
        return;
      }
      leadDistance = lead ? lead->getDistance(target_) : leadDistance;
    }

    // No progress for a while (and no fight): the target is probably unreachable.
    // Progress of either the center or the lead counts; regrouping does not count.
    if (ctx_.frame - lastProgressCheckFrame_ >= kProgressCheckFrames)
    {
      lastProgressCheckFrame_ = ctx_.frame;
      const int d = center.getApproxDistance(target_);
      if (!regrouping_)
      {
        const bool progressed = d <= bestDistanceToTarget_ - 2 * kTile || leadDistance <= bestLeadDistance_ - 2 * kTile;
        if (!engaged && !progressed)
          ++noProgressChecks_;
        else
          noProgressChecks_ = 0;
      }
      bestDistanceToTarget_ = std::min(bestDistanceToTarget_, d);
      bestLeadDistance_ = std::min(bestLeadDistance_, leadDistance);
      if (noProgressChecks_ >= kMaxNoProgressChecks)
      {
        ++ctx_.metrics.stuckEvents;
        ctx_.log.warn("Combat", concat("no progress towards ", target_.x, ",", target_.y, " for ",
                                       kMaxNoProgressChecks * kProgressCheckFrames / 24, "s (center ", center.x, ",", center.y,
                                       " is ", d / kTile, " tiles away, lead ", leadDistance / kTile, " tiles, wave ", waveUnits.size(), ")"));
        ctx_.strategy->markUnreachable(target_, 24 * 180);
        if (!pickNewTarget(center))
        {
          holdPosition(center);
          endWave("all known targets unreachable", State::Gather);
          return;
        }
      }
    }

    // Cohesion: when the lead gets close to the target and the wave is strung out,
    // the lead waits for the rest. Not on the way across the map and never while
    // anyone is fighting; a regroup has a time limit and a cooldown.
    const int cohesionRadius = groupRadiusTiles(ctx_.config.regroupRadiusTiles, shooters) * kTile;
    int together = 0;
    for (Unit u : waveUnits)
      if (!isSupport(u) && u->getDistance(center) <= cohesionRadius)
        ++together;
    const double fraction = shooters > 0 ? static_cast<double>(together) / shooters : 1.0;
    // At most one regroup per wave, just before the target: walking back and forth
    // only gives the enemy time (the user saw waves stop again and again).
    if (!regrouping_ && regroups_ == 0 && fraction < 0.5 && !engaged && leadDistance <= kRegroupNearTargetTiles * kTile &&
        ctx_.frame >= nextRegroupAllowedFrame_)
    {
      regrouping_ = true;
      regroupStartFrame_ = ctx_.frame;
      ++regroups_;
      ctx_.log.debug("Combat", concat("regrouping (", together, "/", shooters, " together)"));
    }
    else if (regrouping_ && (fraction >= 0.75 || engaged || ctx_.frame - regroupStartFrame_ >= kMaxRegroupFrames))
    {
      if (fraction < 0.75 && !engaged)
        ctx_.log.debug("Combat", concat("regroup time limit (", together, "/", shooters, " together); advancing"));
      regrouping_ = false;
      nextRegroupAllowedFrame_ = ctx_.frame + kRegroupCooldownFrames;
    }
    // While regrouping the leading units wait and the rest catch up. Pulling the
    // front back to the center would undo the progress made through a narrow path.
    const int centerToTarget = center.getApproxDistance(target_);
    fightCenter_ = center;
    for (Unit u : waveUnits)
    {
      if (isSupport(u))
        moveMedic(u, center);
      else if (regrouping_ && u->getDistance(target_) < centerToTarget)
      {
        if (u->isMoving())
          ctx_.commander->stop(u);
      }
      else
        fight(u, target_, localOwn, localEnemy);
    }
    fightCenter_ = Positions::None;
  }

  void CombatManager::retreat()
  {
    // Pull back only as far as needed: towards home, but stop once nothing that can
    // hurt us has been near for a few seconds and we got at least 10 tiles away. The
    // army then regroups there with the reinforcements instead of in the main.
    const Position rally = ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(army_.size())) * kTile;
    for (Unit u : army_)
      if (!settledNear(u, rally, rallyRadius) && !packUp(*ctx_.commander, u))
        ctx_.commander->move(u, rally);
    const Position center = groupCenter(army_);
    bool threatened = false;
    for (Unit e : ctx_.info->visibleEnemiesNear(center, 14 * kTile))
      threatened = threatened || (canAttackGround(e->getType()) && !e->getType().isWorker());
    if (threatened)
      safeSinceFrame_ = -1;
    else if (safeSinceFrame_ < 0)
      safeSinceFrame_ = ctx_.frame;
    const bool safe = safeSinceFrame_ >= 0 && ctx_.frame - safeSinceFrame_ >= 24 * 3 && retreatFrom_.isValid() &&
                      center.getApproxDistance(retreatFrom_) >= 10 * kTile;
    if (army_.empty() || center.getApproxDistance(rally) < 6 * kTile || safe || ctx_.frame - stateFrame_ > 24 * 25)
    {
      state_ = State::Gather;
      stateFrame_ = ctx_.frame;
      nextAttackAllowedFrame_ = ctx_.frame + 24 * 15;
      if (safe && center.getApproxDistance(rally) > 15 * kTile)
      {
        ctx_.log.info("Combat", "retreat stops: out of danger");
        holdPosition(center);
      }
      else
        ctx_.log.info("Combat", "retreat finished; gathering");
    }
  }

  void CombatManager::defend()
  {
    if (!ctx_.strategy->baseUnderThreat())
    {
      if (ctx_.frame - lastThreatFrame_ > 24 * 4)
      {
        state_ = State::Gather;
        stateFrame_ = ctx_.frame;
        ctx_.log.info("Combat", concat("base is safe again; gathering", leashCalls_ > 0 ? concat(" (", leashCalls_, " units called back from chasing)") : std::string(),
                                       hiddenSteps_ > 0 ? concat(" (", hiddenSteps_, " steps away from undetected attackers)") : std::string()));
        leashed_.clear();
        leashCalls_ = 0;
        hiddenSteps_ = 0;
        holdingOutmatched_ = false;
      }
      return;
    }
    const Position threat = ctx_.strategy->threatCenter();
    // Do not chase far away from home. Home is every Command Center, also one being
    // built: measured from the main only, an attacked third base was never defended
    // (and the state flipped Gather/Defend every few frames).
    int homeDistance = threat.getApproxDistance(ctx_.map->mainPosition());
    for (Unit u : Broodwar->self()->getUnits())
      if (u->getType().isResourceDepot())
        homeDistance = std::min(homeDistance, threat.getApproxDistance(u->getPosition()));
    if (homeDistance > 35 * kTile && army_.size() > 0)
    {
      state_ = State::Gather;
      stateFrame_ = ctx_.frame;
      ignoreThreatUntilFrame_ = ctx_.frame + 24 * 5;
      return;
    }
    // While the manned Bunker at the entrance stands and the enemy is at it or
    // outside it, the army waits right behind it and shoots what comes into range.
    // Running out after zealots, it died a few units at a time in the open while the
    // Bunker stood (UAlbertaBot's zealot rush). Enemies past the Bunker are fought.
    const Position behindBunker = ctx_.map->bunkerRally();
    bool bunkerManned = false;
    for (Unit b : Broodwar->self()->getUnits())
      bunkerManned = bunkerManned || (b->getType() == UnitTypes::Terran_Bunker && b->isCompleted() && !b->getLoadedUnits().empty() &&
                                      b->getDistance(behindBunker) <= 6 * kTile);
    const Position main = ctx_.map->mainPosition();
    // Flyers do not come through the entrance: the hold looks at the ground attack,
    // and the units that shoot up go after flyers meanwhile (Mutalisks killed SCVs
    // and buildings while the army waited behind the Bunker for Zerglings).
    std::vector<Point> groundPts, airPts;
    for (Unit e : ctx_.strategy->baseThreats())
      if (e->exists())
        (e->isFlying() ? airPts : groundPts).push_back(toPoint(e->getPosition()));
    const Position groundThreat = groundPts.empty() ? Positions::None : toPosition(centroid(groundPts));
    if (!bunkerReleased_ && bunkerManned && behindBunker.isValid() && groundThreat.isValid() &&
        groundThreat.getApproxDistance(main) + 2 * kTile >= behindBunker.getApproxDistance(main))
    {
      if (!holdingAtBunker_)
        ctx_.log.info("Combat", "enemy at the Bunker: the army holds right behind it");
      holdingAtBunker_ = true;
      const Position airThreat = airPts.empty() ? Positions::None : toPosition(centroid(airPts));
      std::vector<Unit> antiAir;
      if (airThreat.isValid())
        for (Unit u : army_)
          if (canAttackAir(u->getType()))
            antiAir.push_back(u);
      if (!antiAir.empty() && !loggedAirInBase_)
        ctx_.log.info("Combat", concat("flyers in the base: ", antiAir.size(), " anti-air units go after them, the rest holds at the Bunker"));
      loggedAirInBase_ = loggedAirInBase_ || !antiAir.empty();
      const std::vector<Unit> airEnemies = airThreat.isValid() ? ctx_.info->visibleEnemiesNear(airThreat, kLocalRadius) : std::vector<Unit>{};
      const double airOwn = strengthOf(antiAir, 1.0);
      const double airEnemy = enemyStrengthOf(airEnemies);
      for (Unit u : army_)
      {
        if (airThreat.isValid() && canAttackAir(u->getType()))
        {
          fight(u, airThreat, airOwn, airEnemy);
          continue;
        }
        if (dodging(u) || staysSieged(u) || settledNear(u, behindBunker, 3 * kTile))
          continue;
        if (isSupport(u))
          ctx_.commander->move(u, behindBunker);
        else
          ctx_.commander->attackMove(u, behindBunker);
      }
      return;
    }
    holdingAtBunker_ = false;
    const std::vector<Unit> enemies = ctx_.info->visibleEnemiesNear(threat, kLocalRadius);
    const double localOwn = strengthOf(army_, enemyAirFraction(enemies));
    const double localEnemy = enemyStrengthOf(enemies);

    // Every level: the army does not go out to an enemy at the edge of the base that
    // is clearly stronger; it waits at the rally (behind the Bunker or at the
    // entrance) where reinforcements, the Bunker and repairs are. Zealots and Dragoons
    // poking the edge drew the whole army out again and again; it fell from 9 to 2
    // units before the Reavers and the main army came.
    const Position rally = ctx_.map->rallyPoint();
    const bool atEdge = rally.isValid() && threat.getApproxDistance(main) > rally.getApproxDistance(main) + 4 * kTile;
    if (atEdge && defenseOutmatched(localOwn, localEnemy))
    {
      if (!holdingOutmatched_)
        ctx_.log.info("Combat", concat("enemy at the edge of the base is stronger (", static_cast<int>(localEnemy), " vs ",
                                       static_cast<int>(localOwn), "): the army waits at the rally"));
      holdingOutmatched_ = true;
      for (Unit u : army_)
      {
        if (dodging(u) || staysSieged(u) || settledNear(u, rally, 3 * kTile))
          continue;
        if (isSupport(u))
          ctx_.commander->move(u, rally);
        else
          ctx_.commander->attackMove(u, rally);
      }
      return;
    }
    holdingOutmatched_ = false;

    // No chase away from home: a unit more than 10 tiles from every building of ours
    // walks back toward the rally and fights again once within 5 tiles. A Dragoon
    // backing off pulled units out of the base one at a time.
    std::vector<Unit> buildings;
    for (Unit b : Broodwar->self()->getUnits())
      if (b->getType().isBuilding() && !b->isLifted())
        buildings.push_back(b);
    const Position center = groupCenter(army_);
    fightCenter_ = center;
    for (Unit u : army_)
    {
      if (isSupport(u))
      {
        moveMedic(u, center);
        continue;
      }
      int nearest = std::numeric_limits<int>::max();
      for (Unit b : buildings)
        nearest = std::min(nearest, u->getDistance(b));
      const bool leashed = leashed_.count(u->getID()) > 0;
      if (!buildings.empty() && rally.isValid() && onLeash(nearest / kTile, leashed, 10, 5))
      {
        if (!leashed)
        {
          leashed_.insert(u->getID());
          ++leashCalls_;
        }
        if (!dodging(u) && !staysSieged(u))
          ctx_.commander->move(u, rally);
        continue;
      }
      leashed_.erase(u->getID());
      fight(u, threat, localOwn, localEnemy);
    }
    fightCenter_ = Positions::None;
  }
}
