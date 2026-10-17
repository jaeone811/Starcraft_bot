#include "CommandHistory.h"

#include <cstdlib>

namespace bb
{
  bool CommandHistory::sameCommand(const CommandKey& a, const CommandKey& b, int positionTolerance)
  {
    if (a.kind != b.kind || a.targetId != b.targetId || a.extra != b.extra)
      return false;
    return std::abs(a.x - b.x) <= positionTolerance && std::abs(a.y - b.y) <= positionTolerance;
  }

  bool CommandHistory::shouldIssue(int unitId, const CommandKey& cmd, int frame, int cooldownFrames, int positionTolerance) const
  {
    auto it = last_.find(unitId);
    if (it == last_.end())
      return true;
    if (!sameCommand(it->second.cmd, cmd, positionTolerance))
      return true;
    return frame - it->second.frame >= cooldownFrames;
  }

  int CommandHistory::record(int unitId, const CommandKey& cmd, int frame)
  {
    Entry& e = last_[unitId];
    if (e.sameInARow > 0 && sameCommand(e.cmd, cmd, 0))
    {
      ++e.sameInARow;
      ++repeated_;
    }
    else
    {
      e.sameInARow = 1;
    }
    e.cmd = cmd;
    e.frame = frame;
    return e.sameInARow;
  }

  void CommandHistory::forget(int unitId)
  {
    last_.erase(unitId);
  }
}
