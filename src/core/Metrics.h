#pragma once
// Counters collected during a game for difficulty validation (see docs/VALIDATION.md).

#include <string>

namespace bb
{
  struct Metrics
  {
    int maxWorkers = 0;
    int workersTrained = 0;
    int armyTrained = 0;
    int supplyBlockedFrames = 0;
    int supplyBlockEvents = 0;
    int idleProductionFrames = 0;   // barracks/CC idle while we could have afforded a unit
    int buildingsStarted = 0;
    int buildRetries = 0;
    int buildFailures = 0;
    int commandFailures = 0;
    int stuckEvents = 0;            // army made no progress towards its target
    int unitStuckEvents = 0;        // single unit reported isStuck() or kept receiving the same order
    int attackWaves = 0;
    int failedWaves = 0;
    int firstAttackFrame = -1;
    int enemyMainFoundFrame = -1;
    int scoutsSent = 0;
    int ownUnitsLost = 0;
    int enemyUnitsKilled = 0;
    int scansUsed = 0;
    int exceptions = 0;
    int gatheredMinerals = 0;       // copied from BWAPI at the end of the game
    int gatheredGas = 0;
  };

  std::string metricsCsvHeader();
  // `prefix` holds the already-formatted leading columns (date, map, ...).
  std::string metricsCsvRow(const std::string& prefix, const Metrics& m);
  std::string metricsSummary(const Metrics& m);
}
