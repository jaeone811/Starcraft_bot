#pragma once
// Remembers the last command sent to each unit so the bot does not spam the same
// order every frame (which in Brood War resets the unit's action and makes it
// stutter). Also counts suspicious repetition for the validation metrics.

#include <unordered_map>

namespace bb
{
  enum class CommandKind
  {
    Move, AttackMove, AttackUnit, Gather, ReturnCargo, Build, RightClickUnit,
    Train, Research, Upgrade, UseTech, Stop, SetRally
  };

  struct CommandKey
  {
    CommandKind kind = CommandKind::Stop;
    int targetId = -1;   // unit target, -1 if none
    int x = 0, y = 0;    // position target (pixels), or tile for Build
    int extra = 0;       // unit/tech/upgrade type id
  };

  class CommandHistory
  {
  public:
    // True when `cmd` differs from the unit's last command, or the same command is
    // older than `cooldownFrames`. Positions closer than `positionTolerance` pixels
    // count as the same target.
    bool shouldIssue(int unitId, const CommandKey& cmd, int frame, int cooldownFrames, int positionTolerance = 64) const;

    // Records a sent command. Returns the number of times in a row this unit got
    // the same command (1 for a new command).
    int record(int unitId, const CommandKey& cmd, int frame);
    void forget(int unitId);

    void noteSuppressed() { ++suppressed_; }
    int issuedCount() const { return issued_; }
    int suppressedCount() const { return suppressed_; }
    int repeatedCount() const { return repeated_; }

    static bool sameCommand(const CommandKey& a, const CommandKey& b, int positionTolerance);

  private:
    struct Entry
    {
      CommandKey cmd;
      int frame = 0;
      int sameInARow = 0;
    };
    std::unordered_map<int, Entry> last_;
    int issued_ = 0;
    int suppressed_ = 0;
    int repeated_ = 0; // same command re-sent after the cooldown (possible stuck unit)
  };
}
