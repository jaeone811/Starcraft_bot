#include "CombatManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "Planning.h"

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

    double strengthOf(const std::vector<Unit>& units)
    {
      std::vector<double> s;
      for (Unit u : units)
        if (u->exists())
          s.push_back(unitStrengthNow(u));
      return combineStrength(s);
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
      return;
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
      return;
    }
    // Fill it from the marines at home (never from a wave that is out).
    while (bunkerCrew_.size() < 4)
    {
      Unit best = nullptr;
      for (Unit u : army_)
        if (u->getType() == UnitTypes::Terran_Marine && !wave_.count(u->getID()) && !bunkerCrew_.count(u->getID()) &&
            (!best || u->getDistance(bunker) < best->getDistance(bunker)))
          best = u;
      if (!best)
        break;
      bunkerCrew_.insert(best->getID());
      army_.erase(std::remove(army_.begin(), army_.end(), best), army_.end());
    }
    for (int id : bunkerCrew_)
    {
      Unit m = Broodwar->getUnit(id);
      if (isValid(m) && !m->isLoaded())
        ctx_.commander->enterBunker(m, bunker);
    }
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

  void CombatManager::update()
  {
    collectArmy();
    manageBunker();
    useScans();

    if (ctx_.strategy->baseUnderThreat())
    {
      lastThreatFrame_ = ctx_.frame;
      if (state_ == State::Gather && !army_.empty() && ctx_.frame >= ignoreThreatUntilFrame_)
      {
        state_ = State::Defend;
        stateFrame_ = ctx_.frame;
        ctx_.log.info("Combat", concat("base under attack by ", ctx_.strategy->baseThreats().size(), " units: defending"));
      }
      else if (state_ == State::Attack && ctx_.config.recallArmyToDefend && ctx_.strategy->baseThreats().size() >= 2)
      {
        std::vector<Unit> waveUnits;
        for (Unit u : army_)
          if (wave_.count(u->getID()))
            waveUnits.push_back(u);
        if (groupCenter(waveUnits).getApproxDistance(ctx_.map->mainPosition()) > 30 * kTile)
          endWave("recalled to defend the base", State::Defend);
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
    const Position rally = ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(army_.size())) * kTile;
    int nearRally = 0;
    for (Unit u : army_)
    {
      if ((isSiegeTank(u->getType()) && updateSiege(u) != TankMode::Mobile) || settledNear(u, rally, rallyRadius))
      {
        ++nearRally;
        continue;
      }
      if (isSupport(u))
        ctx_.commander->move(u, rally);
      else
        ctx_.commander->attackMove(u, rally);
    }
    if (army_.empty() || ctx_.frame < nextAttackAllowedFrame_)
      return;

    AttackInputs in;
    in.armySize = shooterCount(army_);
    in.frame = ctx_.frame;
    in.ownStrength = strengthOf(army_);
    in.knownEnemyStrength = ctx_.info->knownEnemyArmyStrength();
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
    fallingBack_ = false;
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
                                   "] ownStrength=", static_cast<int>(strengthOf(army_)),
                                   " knownEnemyStrength=", static_cast<int>(ctx_.info->knownEnemyArmyStrength()),
                                   " requiredSize=", ctx_.strategy->attackSize()));
  }

  void CombatManager::endWave(const char* reason, State next)
  {
    ctx_.strategy->onWaveEnd(waveLost_, waveKilled_, waveValueLost_, waveValueKilled_, reason);
    wave_.clear();
    state_ = next;
    stateFrame_ = ctx_.frame;
    regrouping_ = false;
    ctx_.log.info("Combat", concat("wave over: ", reason, " -> ", stateName(), " (regrouped ", regroups_, " times, sieged ", sieges_, " times)"));
  }

  bool CombatManager::pickNewTarget(Position from)
  {
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

    // Focus fire: pick an explicit target when the difficulty uses it.
    if (ctx_.config.focusFireLevel > 0 && !enemies.empty())
    {
      std::vector<TargetInfo> infos;
      for (Unit e : enemies)
      {
        TargetInfo t;
        t.canAttackUs = canAttackGround(e->getType());
        t.isBuilding = e->getType().isBuilding();
        t.isWorker = e->getType().isWorker();
        t.hitPoints = e->getHitPoints() + e->getShields();
        const int maxHp = std::max(1, e->getType().maxHitPoints() + e->getType().maxShields());
        t.hpFraction = static_cast<double>(t.hitPoints) / maxHp;
        t.damagePerFrame = groundDamagePerFrame(e->getType());
        t.distance = u->getDistance(e);
        t.inRange = u->isInWeaponRange(e);
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
    std::set<int> inContact;
    for (Unit u : waveUnits)
      for (Unit e : localEnemies)
        if (u->getDistance(e) < kJoinFightRadius)
        {
          localOwnUnits.push_back(u);
          if (u->getDistance(e) < kLocalRadius)
            inContact.insert(u->getID());
          break;
        }
    const double localOwn = strengthOf(localOwnUnits);
    const double localEnemy = enemyStrengthOf(localEnemies);

    // Survivors are compared with every shooter sent (start + reinforcements), so a
    // wave that keeps being refilled still pulls back once it has lost most of them.
    // When only the front of a strung-out wave is outmatched but the whole wave is
    // not, the front falls back to the others instead of the wave going home (a
    // 29-shooter wave once retreated because its first 2 units met a cannon).
    bool fallBack = false;
    if (shouldRetreat(localOwn, localEnemy, ctx_.config.retreatStrengthRatio, shooters, waveCommitted_,
                      ctx_.config.retreatArmyFraction) &&
        !shouldRetreat(strengthOf(waveUnits), localEnemy, ctx_.config.retreatStrengthRatio, shooters, waveCommitted_,
                       ctx_.config.retreatArmyFraction))
    {
      fallBack = true;
      if (!fallingBack_)
        ctx_.log.info("Combat", concat("front falls back to the wave: ", inContact.size(), " units in contact (",
                                       static_cast<int>(localOwn), " vs enemy ", static_cast<int>(localEnemy), "), wave ",
                                       static_cast<int>(strengthOf(waveUnits))));
    }
    fallingBack_ = fallBack;
    if (!fallBack && shouldRetreat(localOwn, localEnemy, ctx_.config.retreatStrengthRatio, shooters, waveCommitted_,
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
    if (!regrouping_ && fraction < 0.5 && !engaged && leadDistance <= kRegroupNearTargetTiles * kTile &&
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
    for (Unit u : waveUnits)
    {
      if (isSupport(u))
        moveMedic(u, center);
      else if (fallBack && inContact.count(u->getID()) && !(isSiegeTank(u->getType()) && updateSiege(u) != TankMode::Mobile))
        ctx_.commander->move(u, center);
      else if (regrouping_ && u->getDistance(target_) < centerToTarget)
      {
        if (u->isMoving())
          ctx_.commander->stop(u);
      }
      else
        fight(u, target_, localOwn, localEnemy);
    }
  }

  void CombatManager::retreat()
  {
    const Position rally = ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(army_.size())) * kTile;
    for (Unit u : army_)
      if (!settledNear(u, rally, rallyRadius) && !packUp(*ctx_.commander, u))
        ctx_.commander->move(u, rally);
    const Position center = groupCenter(army_);
    if (army_.empty() || center.getApproxDistance(rally) < 6 * kTile || ctx_.frame - stateFrame_ > 24 * 25)
    {
      state_ = State::Gather;
      stateFrame_ = ctx_.frame;
      nextAttackAllowedFrame_ = ctx_.frame + 24 * 15;
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
        ctx_.log.info("Combat", "base is safe again; gathering");
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
    const std::vector<Unit> enemies = ctx_.info->visibleEnemiesNear(threat, kLocalRadius);
    const double localOwn = strengthOf(army_);
    const double localEnemy = enemyStrengthOf(enemies);
    const Position center = groupCenter(army_);
    for (Unit u : army_)
    {
      if (isSupport(u))
        moveMedic(u, center);
      else
        fight(u, threat, localOwn, localEnemy);
    }
  }
}
