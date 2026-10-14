#include "Commander.h"

#include "BwapiUtil.h"

using namespace BWAPI;

namespace bb
{
  template <typename Send>
  bool Commander::issue(Unit u, const CommandKey& key, int cooldown, const char* what, Send send)
  {
    lastSuppressed_ = false;
    if (!isValid(u))
      return false;
    const int id = u->getID();
    if (!ctx_.history.shouldIssue(id, key, ctx_.frame, cooldown))
    {
      ctx_.history.noteSuppressed();
      lastSuppressed_ = true;
      return false;
    }
    const bool ok = send();
    const int inARow = ctx_.history.record(id, key, ctx_.frame);
    if (!ok)
    {
      ++ctx_.metrics.commandFailures;
      ctx_.log.debug("Command", concat(what, " failed for ", u->getType().c_str(), "#", id, ": ",
                                       Broodwar->getLastError().c_str()));
    }
    // The same order over and over usually means the unit cannot do it (blocked path,
    // unreachable target). Report it once per streak so it shows up in validation.
    const bool producerCommand = key.kind == CommandKind::Train || key.kind == CommandKind::Research ||
                                 key.kind == CommandKind::Upgrade || key.kind == CommandKind::SetRally ||
                                 key.kind == CommandKind::UseTech;
    if (!producerCommand && inARow == 8)
    {
      ++ctx_.metrics.unitStuckEvents;
      ctx_.log.warn("Command", concat(u->getType().c_str(), "#", id, " received the same ", what,
                                      " command 8 times in a row (possible stuck unit)"));
    }
    return ok;
  }

  bool Commander::move(Unit u, Position p, int cooldown)
  {
    return issue(u, { CommandKind::Move, -1, p.x, p.y, 0 }, cooldown, "move", [&] { return u->move(p); });
  }

  bool Commander::attackMove(Unit u, Position p, int cooldown)
  {
    return issue(u, { CommandKind::AttackMove, -1, p.x, p.y, 0 }, cooldown, "attack-move", [&] { return u->attack(p); });
  }

  bool Commander::attackUnit(Unit u, Unit target, int cooldown)
  {
    if (!isValid(target))
      return false;
    return issue(u, { CommandKind::AttackUnit, target->getID(), 0, 0, 0 }, cooldown, "attack",
                 [&] { return u->attack(target); });
  }

  bool Commander::gather(Unit worker, Unit resource, int cooldown)
  {
    if (!isValid(resource))
      return false;
    return issue(worker, { CommandKind::Gather, resource->getID(), 0, 0, 0 }, cooldown, "gather",
                 [&] { return worker->gather(resource); });
  }

  bool Commander::returnCargo(Unit worker, int cooldown)
  {
    return issue(worker, { CommandKind::ReturnCargo, -1, 0, 0, 0 }, cooldown, "return-cargo",
                 [&] { return worker->returnCargo(); });
  }

  bool Commander::build(Unit worker, UnitType type, TilePosition tile, int cooldown)
  {
    return issue(worker, { CommandKind::Build, -1, tile.x, tile.y, type.getID() }, cooldown, "build",
                 [&] { return worker->build(type, tile); });
  }

  bool Commander::resumeConstruction(Unit worker, Unit building, int cooldown)
  {
    if (!isValid(building))
      return false;
    return issue(worker, { CommandKind::RightClickUnit, building->getID(), 0, 0, 0 }, cooldown, "resume-construction",
                 [&] { return worker->rightClick(building); });
  }

  bool Commander::train(Unit producer, UnitType type, int cooldown)
  {
    return issue(producer, { CommandKind::Train, -1, 0, 0, type.getID() }, cooldown, "train",
                 [&] { return producer->train(type); });
  }

  bool Commander::buildAddon(Unit producer, UnitType type, int cooldown)
  {
    return issue(producer, { CommandKind::Build, -1, 0, 0, type.getID() }, cooldown, "build-addon",
                 [&] { return producer->buildAddon(type); });
  }

  bool Commander::research(Unit building, TechType tech, int cooldown)
  {
    return issue(building, { CommandKind::Research, -1, 0, 0, tech.getID() }, cooldown, "research",
                 [&] { return building->research(tech); });
  }

  bool Commander::upgrade(Unit building, UpgradeType upgrade, int cooldown)
  {
    return issue(building, { CommandKind::Upgrade, -1, 0, 0, upgrade.getID() }, cooldown, "upgrade",
                 [&] { return building->upgrade(upgrade); });
  }

  bool Commander::useTech(Unit u, TechType tech, int cooldown)
  {
    return issue(u, { CommandKind::UseTech, -1, 0, 0, tech.getID() }, cooldown, "use-tech",
                 [&] { return u->useTech(tech); });
  }

  bool Commander::useTechAt(Unit u, TechType tech, Position p, int cooldown)
  {
    return issue(u, { CommandKind::UseTech, -1, p.x, p.y, tech.getID() }, cooldown, "use-tech-at",
                 [&] { return u->useTech(tech, p); });
  }

  bool Commander::setRally(Unit building, Position p, int cooldown)
  {
    return issue(building, { CommandKind::SetRally, -1, p.x, p.y, 0 }, cooldown, "set-rally",
                 [&] { return building->setRallyPoint(p); });
  }

  bool Commander::stop(Unit u, int cooldown)
  {
    return issue(u, { CommandKind::Stop, -1, 0, 0, 0 }, cooldown, "stop", [&] { return u->stop(); });
  }

  void Commander::forget(Unit u)
  {
    if (u)
      ctx_.history.forget(u->getID());
  }
}
