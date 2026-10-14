#include "Metrics.h"

#include <sstream>

namespace bb
{
  std::string metricsCsvHeader()
  {
    return "date,map,difficulty,enemy_race,result,frames,game_time,"
           "max_workers,workers_trained,army_trained,gathered_minerals,gathered_gas,"
           "supply_blocked_frames,supply_block_events,idle_production_frames,"
           "buildings_started,build_retries,build_failures,command_failures,"
           "stuck_events,unit_stuck_events,attack_waves,failed_waves,first_attack_frame,"
           "enemy_main_found_frame,scouts_sent,own_units_lost,enemy_units_killed,scans_used,exceptions";
  }

  std::string metricsCsvRow(const std::string& prefix, const Metrics& m)
  {
    std::ostringstream ss;
    ss << prefix << ',' << m.maxWorkers << ',' << m.workersTrained << ',' << m.armyTrained << ','
       << m.gatheredMinerals << ',' << m.gatheredGas << ','
       << m.supplyBlockedFrames << ',' << m.supplyBlockEvents << ',' << m.idleProductionFrames << ','
       << m.buildingsStarted << ',' << m.buildRetries << ',' << m.buildFailures << ',' << m.commandFailures << ','
       << m.stuckEvents << ',' << m.unitStuckEvents << ',' << m.attackWaves << ',' << m.failedWaves << ','
       << m.firstAttackFrame << ',' << m.enemyMainFoundFrame << ',' << m.scoutsSent << ','
       << m.ownUnitsLost << ',' << m.enemyUnitsKilled << ',' << m.scansUsed << ',' << m.exceptions;
    return ss.str();
  }

  std::string metricsSummary(const Metrics& m)
  {
    std::ostringstream ss;
    ss << "maxWorkers=" << m.maxWorkers << " workersTrained=" << m.workersTrained << " armyTrained=" << m.armyTrained
       << " minerals=" << m.gatheredMinerals << " gas=" << m.gatheredGas
       << " supplyBlockedFrames=" << m.supplyBlockedFrames << " (" << m.supplyBlockEvents << " events)"
       << " idleProductionFrames=" << m.idleProductionFrames << " buildingsStarted=" << m.buildingsStarted
       << " buildRetries=" << m.buildRetries << " buildFailures=" << m.buildFailures
       << " commandFailures=" << m.commandFailures << " stuck=" << m.stuckEvents << "/" << m.unitStuckEvents
       << " waves=" << m.attackWaves << " failedWaves=" << m.failedWaves
       << " lost=" << m.ownUnitsLost << " killed=" << m.enemyUnitsKilled << " scans=" << m.scansUsed
       << " exceptions=" << m.exceptions;
    return ss.str();
  }
}
