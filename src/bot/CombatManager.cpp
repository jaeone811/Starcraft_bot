#include "CombatManager.h"

#include "BwapiUtil.h"
#include "Commander.h"
#include "InformationManager.h"
#include "MapInfo.h"
#include "Planning.h"

#include <algorithm>

using namespace BWAPI;

namespace bb
{
  namespace
  {
    constexpr int kTile = 32;
    constexpr int kLocalRadius = 12 * kTile;     // fight evaluation radius around the wave
    constexpr int kTargetSearchRadius = 8 * kTile;
    constexpr int kReinforcementBatch = 4;
    constexpr int kProgressCheckFrames = 24 * 10;
    constexpr int kMaxNoProgressChecks = 3;
    constexpr int kRallyRadiusTiles = 5;              // smallest "at the rally point" radius
    constexpr int kMaxRegroupFrames = 24 * 8;         // a regroup that takes longer is given up
    constexpr int kRegroupCooldownFrames = 24 * 6;    // advance at least this long before regrouping again

    bool isMedic(Unit u) { return u->getType() == UnitTypes::Terran_Medic; }

    // Close enough to the rally point. A unit that has stopped a bit further out is
    // as close as the crowd (or the buildings around the spot) lets it get; ordering
    // it there again would only repeat the same command.
    bool settledNear(Unit u, Position rally, int radius)
    {
      const int d = u->getDistance(rally);
      return d <= radius || (d <= 2 * radius && !u->isMoving());
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
      if (isOurArmyType(u->getType()) && u->isCompleted() && u->exists() && !u->isLoaded())
        army_.push_back(u);
  }

  Position CombatManager::groupCenter(const std::vector<Unit>& units) const
  {
    std::vector<Point> shooters, all;
    for (Unit u : units)
    {
      all.push_back(toPoint(u->getPosition()));
      if (!isMedic(u))
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
    useScans();

    if (ctx_.strategy->baseUnderThreat())
    {
      lastThreatFrame_ = ctx_.frame;
      if (state_ == State::Gather && !army_.empty())
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
      if (settledNear(u, rally, rallyRadius))
      {
        ++nearRally;
        continue;
      }
      if (isMedic(u))
        ctx_.commander->move(u, rally);
      else
        ctx_.commander->attackMove(u, rally);
    }
    if (army_.empty() || ctx_.frame < nextAttackAllowedFrame_)
      return;

    AttackInputs in;
    in.armySize = static_cast<int>(army_.size());
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
    int marines = 0, medics = 0, firebats = 0;
    for (Unit u : army_)
    {
      wave_.insert(u->getID());
      if (u->getType() == UnitTypes::Terran_Marine) ++marines;
      else if (u->getType() == UnitTypes::Terran_Medic) ++medics;
      else ++firebats;
    }
    waveStartSize_ = static_cast<int>(wave_.size());
    waveLost_ = waveValueLost_ = waveKilled_ = waveValueKilled_ = 0;
    target_ = target.position;
    targetReason_ = target.reason;
    const Position center = groupCenter(army_);
    bestDistanceToTarget_ = center.getApproxDistance(target_);
    lastProgressCheckFrame_ = ctx_.frame;
    noProgressChecks_ = 0;
    regrouping_ = false;
    nextRegroupAllowedFrame_ = 0;
    state_ = State::Attack;
    stateFrame_ = ctx_.frame;
    ++ctx_.metrics.attackWaves;
    if (ctx_.metrics.firstAttackFrame < 0)
      ctx_.metrics.firstAttackFrame = ctx_.frame;
    ctx_.log.info("Combat", concat("attack wave ", ctx_.metrics.attackWaves, ": ", waveStartSize_, " units (", marines, " marines, ",
                                   medics, " medics, ", firebats, " firebats) -> ", target_.x, ",", target_.y, " [", targetReason_,
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
    ctx_.log.info("Combat", concat("wave over: ", reason, " -> ", stateName()));
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
    noProgressChecks_ = 0;
    lastProgressCheckFrame_ = ctx_.frame;
    ctx_.log.info("Combat", concat("new target ", target_.x, ",", target_.y, " [", targetReason_, "]"));
    return true;
  }

  void CombatManager::moveMedic(Unit medic, Position center)
  {
    if (medic->getDistance(center) > 3 * kTile)
      ctx_.commander->move(medic, center);
  }

  void CombatManager::fight(Unit u, Position advanceTo, double localOwn, double localEnemy)
  {
    const UnitType type = u->getType();
    const bool isFirebat = type == UnitTypes::Terran_Firebat;

    // Visible, targetable enemies around this unit.
    std::vector<Unit> enemies = ctx_.info->visibleEnemiesNear(u->getPosition(), kTargetSearchRadius);
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [&](Unit e) {
                    return !e->isDetected() || (isFirebat && e->isFlying());
                  }), enemies.end());

    // Stim before a fight we intend to take.
    if (ctx_.config.useStim && Broodwar->self()->hasResearched(TechTypes::Stim_Packs) && u->getStimTimer() == 0 &&
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
    if (u->isAttackFrame() || u->getOrder() == Orders::AttackUnit)
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
      if (settledNear(r, rally, rallyRadius))
        ready.push_back(r);
      else if (isMedic(r))
        ctx_.commander->move(r, rally);
      else
        ctx_.commander->attackMove(r, rally);
    }
    if (static_cast<int>(ready.size()) >= kReinforcementBatch)
    {
      for (Unit r : ready)
      {
        wave_.insert(r->getID());
        waveUnits.push_back(r);
      }
      ctx_.log.info("Combat", concat(ready.size(), " reinforcements join the wave"));
    }

    const Position center = groupCenter(waveUnits);
    const std::vector<Unit> localEnemies = ctx_.info->visibleEnemiesNear(center, kLocalRadius);
    std::vector<Unit> localOwnUnits;
    for (Unit u : waveUnits)
      if (u->getDistance(center) < 10 * kTile)
        localOwnUnits.push_back(u);
    const double localOwn = strengthOf(localOwnUnits);
    const double localEnemy = enemyStrengthOf(localEnemies);

    if (shouldRetreat(localOwn, localEnemy, ctx_.config.retreatStrengthRatio, static_cast<int>(waveUnits.size()),
                      waveStartSize_, ctx_.config.retreatArmyFraction))
    {
      ctx_.log.info("Combat", concat("retreating: wave ", waveUnits.size(), "/", waveStartSize_, " units, local strength ",
                                     static_cast<int>(localOwn), " vs enemy ", static_cast<int>(localEnemy)));
      endWave("retreat", State::Retreat);
      return;
    }

    // Reached the target and nothing is fighting us: choose the next target.
    if (center.getApproxDistance(target_) < 6 * kTile && localEnemies.empty())
    {
      if (!pickNewTarget(center))
      {
        endWave("no more targets known", State::Gather);
        return;
      }
    }

    // No progress for a while (and no fight): the target is probably unreachable.
    // Standing still to regroup is intended and does not count.
    if (ctx_.frame - lastProgressCheckFrame_ >= kProgressCheckFrames)
    {
      lastProgressCheckFrame_ = ctx_.frame;
      const int d = center.getApproxDistance(target_);
      if (!regrouping_)
      {
        if (localEnemies.empty() && d > bestDistanceToTarget_ - 2 * kTile)
          ++noProgressChecks_;
        else
          noProgressChecks_ = 0;
      }
      bestDistanceToTarget_ = std::min(bestDistanceToTarget_, d);
      if (noProgressChecks_ >= kMaxNoProgressChecks)
      {
        ++ctx_.metrics.stuckEvents;
        ctx_.log.warn("Combat", concat("no progress towards ", target_.x, ",", target_.y, " for ",
                                       kMaxNoProgressChecks * kProgressCheckFrames / 24, "s"));
        ctx_.strategy->markUnreachable(target_, 24 * 180);
        if (!pickNewTarget(center))
        {
          endWave("all known targets unreachable", State::Gather);
          return;
        }
      }
    }

    // Cohesion: stop and regroup when the wave is strung out (outside of fights).
    // A long column in a narrow passage may never meet the cohesion test, so a
    // regroup has a time limit and the wave then advances for a while.
    int shooters = 0;
    for (Unit u : waveUnits)
      if (!isMedic(u))
        ++shooters;
    const int cohesionRadius = groupRadiusTiles(ctx_.config.regroupRadiusTiles, shooters) * kTile;
    int together = 0;
    for (Unit u : waveUnits)
      if (!isMedic(u) && u->getDistance(center) <= cohesionRadius)
        ++together;
    const double fraction = shooters > 0 ? static_cast<double>(together) / shooters : 1.0;
    if (!regrouping_ && fraction < 0.6 && localEnemies.empty() && ctx_.frame >= nextRegroupAllowedFrame_)
    {
      regrouping_ = true;
      regroupStartFrame_ = ctx_.frame;
      ctx_.log.debug("Combat", concat("regrouping (", together, "/", shooters, " together)"));
    }
    else if (regrouping_ && (fraction >= 0.85 || !localEnemies.empty() || ctx_.frame - regroupStartFrame_ >= kMaxRegroupFrames))
    {
      if (fraction < 0.85 && localEnemies.empty())
        ctx_.log.debug("Combat", concat("regroup time limit (", together, "/", shooters, " together); advancing"));
      regrouping_ = false;
      nextRegroupAllowedFrame_ = ctx_.frame + kRegroupCooldownFrames;
    }

    const Position advance = regrouping_ ? center : target_;
    for (Unit u : waveUnits)
    {
      if (isMedic(u))
        moveMedic(u, center);
      else
        fight(u, advance, localOwn, localEnemy);
    }
  }

  void CombatManager::retreat()
  {
    const Position rally = ctx_.map->rallyPoint();
    const int rallyRadius = groupRadiusTiles(kRallyRadiusTiles, static_cast<int>(army_.size())) * kTile;
    for (Unit u : army_)
      if (!settledNear(u, rally, rallyRadius))
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
    if (threat.getApproxDistance(ctx_.map->mainPosition()) > 35 * kTile && army_.size() > 0)
    {
      // Do not chase far away from home.
      state_ = State::Gather;
      stateFrame_ = ctx_.frame;
      return;
    }
    const std::vector<Unit> enemies = ctx_.info->visibleEnemiesNear(threat, kLocalRadius);
    const double localOwn = strengthOf(army_);
    const double localEnemy = enemyStrengthOf(enemies);
    const Position center = groupCenter(army_);
    for (Unit u : army_)
    {
      if (isMedic(u))
        moveMedic(u, center);
      else
        fight(u, threat, localOwn, localEnemy);
    }
  }
}
