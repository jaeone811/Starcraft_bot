#include "DifficultyConfig.h"

#include <algorithm>
#include <sstream>

namespace bb
{
  namespace
  {
    // Brood War runs ~23.8 logic frames per game second on "Fastest"; 24 is close enough for tuning.
    constexpr int seconds(int s) { return s * 24; }
    constexpr int minutes(int m) { return m * 60 * 24; }

    DifficultyConfig veryEasy()
    {
      DifficultyConfig c;
      c.level = 1;
      c.name = "VeryEasy";
      c.koreanName = u8"매우 쉬움";
      c.description = u8"단순한 빌드, 늦은 정찰과 공격, 집중 공격/후퇴 판단 제한";
      c.buildOrder = { {BuildItem::Barracks, 12}, {BuildItem::Refinery, 20},
                       {BuildItem::Barracks, 24}, {BuildItem::Academy, 30} };
      c.workersPerMineralPatch = 1.5;
      c.maxWorkers = 18;
      c.maxBarracks = 2;
      c.extraBarracksMineralBank = 0;
      c.supplyLookaheadFrames = 0;
      c.supplyFixedMargin = 2;
      c.maxConcurrentDepots = 1;
      c.strictPriority = false;
      c.armyFirstWhenThreatened = false;
      c.scoutAtSupply = 0;
      c.scoutAtFrame = minutes(5);
      c.inferEnemyStartByElimination = false;
      c.scoutObserveFrames = 0;
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 0;
      c.medicRatio = 0.15;
      c.firebatRatioVsMelee = 0.0;
      c.attackArmySize = 20;
      c.earliestAttackFrame = minutes(9);
      c.attackStrengthRatio = 0.0;
      c.regroupRadiusTiles = 10;
      c.recallArmyToDefend = false;
      c.focusFireLevel = 0;
      c.retreatStrengthRatio = 0.0;
      c.retreatArmyFraction = 0.25;
      c.useStim = false;
      c.useComsatScan = false;
      c.attackSizeIncreaseOnFail = 0;
      c.strengthRatioIncreaseOnFail = 0.0;
      c.maxAttackSize = 30;
      c.supplyMarginIncreaseOnBlock = 0;
      return c;
    }

    DifficultyConfig easy()
    {
      DifficultyConfig c;
      c.level = 2;
      c.name = "Easy";
      c.koreanName = u8"쉬움";
      c.description = u8"안정적인 운영, 기본 정찰/공격, 최적화되지 않은 조합과 타이밍";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 14},
                       {BuildItem::Barracks, 18}, {BuildItem::Academy, 22},
                       {BuildItem::Barracks, 30} };
      c.workersPerMineralPatch = 1.75;
      c.maxWorkers = 22;
      c.maxBarracks = 3;
      c.extraBarracksMineralBank = 0;
      c.supplyLookaheadFrames = 0;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 1;
      c.strictPriority = false;
      c.armyFirstWhenThreatened = false;
      c.scoutAtSupply = 15;
      c.scoutAtFrame = minutes(4);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = 0;
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 1;
      c.medicRatio = 0.2;
      c.firebatRatioVsMelee = 0.1;
      c.attackArmySize = 16;
      c.earliestAttackFrame = minutes(8);
      c.attackStrengthRatio = 0.0;
      c.regroupRadiusTiles = 9;
      c.recallArmyToDefend = false;
      c.focusFireLevel = 1;
      c.retreatStrengthRatio = 2.5;
      c.retreatArmyFraction = 0.3;
      c.useStim = false;
      c.useComsatScan = false;
      c.attackSizeIncreaseOnFail = 2;
      c.strengthRatioIncreaseOnFail = 0.0;
      c.maxAttackSize = 30;
      c.supplyMarginIncreaseOnBlock = 0;
      return c;
    }

    DifficultyConfig normal()
    {
      DifficultyConfig c;
      c.level = 3;
      c.name = "Normal";
      c.koreanName = u8"기본";
      c.description = u8"기본 AI 수준 목표: 표준 빌드, 기본 정찰, 집결 후 공격, 기본 집중 공격";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 16}, {BuildItem::Academy, 19},
                       {BuildItem::Barracks, 24} };
      c.workersPerMineralPatch = 2.0;
      c.maxWorkers = 26;
      c.maxBarracks = 3;
      c.extraBarracksMineralBank = 0;
      c.research = { ResearchItem::StimPacks };
      c.supplyLookaheadFrames = 700;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 1;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.scoutAtSupply = 12;
      c.scoutAtFrame = minutes(3);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(5);
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 1;
      c.medicRatio = 0.25;
      c.firebatRatioVsMelee = 0.25;
      c.attackArmySize = 12;
      c.earliestAttackFrame = minutes(6);
      c.attackStrengthRatio = 0.0;
      c.regroupRadiusTiles = 8;
      c.recallArmyToDefend = true;
      c.focusFireLevel = 2;
      c.retreatStrengthRatio = 1.8;
      c.retreatArmyFraction = 0.35;
      c.useStim = true;
      c.useComsatScan = true;
      c.attackSizeIncreaseOnFail = 2;
      c.strengthRatioIncreaseOnFail = 0.0;
      c.maxAttackSize = 30;
      c.supplyMarginIncreaseOnBlock = 1;
      return c;
    }

    DifficultyConfig hard()
    {
      DifficultyConfig c;
      c.level = 4;
      c.name = "Hard";
      c.koreanName = u8"어려움";
      c.description = u8"높은 가동률, 보급 예측, 정찰 정보 기반 조합/공격 시점, 효율적인 집중 공격과 후퇴";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 15}, {BuildItem::Academy, 17},
                       {BuildItem::Barracks, 20}, {BuildItem::EngineeringBay, 26},
                       {BuildItem::Barracks, 30} };
      c.workersPerMineralPatch = 2.2;
      c.maxWorkers = 34;
      c.maxBarracks = 6;
      c.extraBarracksMineralBank = 400;
      c.research = { ResearchItem::StimPacks, ResearchItem::U238Shells, ResearchItem::InfantryWeapons1 };
      c.supplyLookaheadFrames = 850;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 2;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.scoutAtSupply = 10;
      c.scoutAtFrame = minutes(2);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(15);
      c.rescoutAfterDeath = true;
      c.enemyInfoUsage = 2;
      c.medicRatio = 0.25;
      c.firebatRatioVsMelee = 0.35;
      c.attackArmySize = 12;
      c.earliestAttackFrame = minutes(5);
      c.attackStrengthRatio = 1.3;
      c.regroupRadiusTiles = 7;
      c.recallArmyToDefend = true;
      c.focusFireLevel = 3;
      c.retreatStrengthRatio = 1.4;
      c.retreatArmyFraction = 0.4;
      c.useStim = true;
      c.useComsatScan = true;
      c.attackSizeIncreaseOnFail = 4;
      c.strengthRatioIncreaseOnFail = 0.15;
      c.maxAttackSize = 30;
      c.supplyMarginIncreaseOnBlock = 2;
      return c;
    }

    DifficultyConfig veryHard()
    {
      DifficultyConfig c;
      c.level = 5;
      c.name = "VeryHard";
      c.koreanName = u8"매우 어려움";
      c.description = u8"구현된 전략 내 최대 효율: 앞마당 확장, 업그레이드, 위협 우선순위 집중 공격, 손실 기반 재판단";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 15}, {BuildItem::Academy, 16},
                       {BuildItem::Barracks, 19}, {BuildItem::EngineeringBay, 24},
                       {BuildItem::Barracks, 28} };
      c.workersPerMineralPatch = 2.5;
      c.maxWorkers = 50;
      c.maxBarracks = 8;
      c.extraBarracksMineralBank = 350;
      c.expand = true;
      c.expandAtWorkers = 20;
      c.research = { ResearchItem::StimPacks, ResearchItem::U238Shells,
                     ResearchItem::InfantryWeapons1, ResearchItem::InfantryArmor1 };
      c.supplyLookaheadFrames = 950;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 3;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.scoutAtSupply = 9;
      c.scoutAtFrame = minutes(2);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(25);
      c.rescoutAfterDeath = true;
      c.enemyInfoUsage = 2;
      c.medicRatio = 0.3;
      c.firebatRatioVsMelee = 0.4;
      c.attackArmySize = 10;
      c.earliestAttackFrame = minutes(4) + seconds(30);
      c.attackStrengthRatio = 1.2;
      c.regroupRadiusTiles = 6;
      c.recallArmyToDefend = true;
      c.focusFireLevel = 3;
      c.retreatStrengthRatio = 1.25;
      c.retreatArmyFraction = 0.4;
      c.useStim = true;
      c.useComsatScan = true;
      c.attackSizeIncreaseOnFail = 4;
      c.strengthRatioIncreaseOnFail = 0.2;
      c.maxAttackSize = 30;
      c.supplyMarginIncreaseOnBlock = 2;
      return c;
    }
  }

  const char* toString(BuildItem item)
  {
    switch (item)
    {
    case BuildItem::SupplyDepot: return "SupplyDepot";
    case BuildItem::Barracks: return "Barracks";
    case BuildItem::Refinery: return "Refinery";
    case BuildItem::Academy: return "Academy";
    case BuildItem::EngineeringBay: return "EngineeringBay";
    case BuildItem::CommandCenter: return "CommandCenter";
    }
    return "?";
  }

  const char* toString(ResearchItem item)
  {
    switch (item)
    {
    case ResearchItem::StimPacks: return "StimPacks";
    case ResearchItem::U238Shells: return "U238Shells";
    case ResearchItem::InfantryWeapons1: return "InfantryWeapons1";
    case ResearchItem::InfantryArmor1: return "InfantryArmor1";
    }
    return "?";
  }

  DifficultyConfig makeDifficultyConfig(int level)
  {
    switch (std::clamp(level, kMinDifficulty, kMaxDifficulty))
    {
    case 1: return veryEasy();
    case 2: return easy();
    case 4: return hard();
    case 5: return veryHard();
    default: return normal();
    }
  }

  std::string describe(const DifficultyConfig& c)
  {
    std::ostringstream ss;
    ss << "difficulty=" << c.level << " (" << c.name << " / " << c.koreanName << ")\n";
    ss << "  buildOrder:";
    for (const auto& step : c.buildOrder)
      ss << ' ' << toString(step.item) << '@' << step.supply;
    ss << "\n  research:";
    for (auto r : c.research)
      ss << ' ' << toString(r);
    ss << "\n  economy: workersPerPatch=" << c.workersPerMineralPatch << " maxWorkers=" << c.maxWorkers
       << " maxBarracks=" << c.maxBarracks << " extraBarracksBank=" << c.extraBarracksMineralBank
       << " expand=" << c.expand << "@" << c.expandAtWorkers << "workers";
    ss << "\n  supply: lookahead=" << c.supplyLookaheadFrames << " fixedMargin=" << c.supplyFixedMargin
       << " maxConcurrentDepots=" << c.maxConcurrentDepots << " marginOnBlock=+" << c.supplyMarginIncreaseOnBlock;
    ss << "\n  allocation: strictPriority=" << c.strictPriority << " armyFirstWhenThreatened=" << c.armyFirstWhenThreatened;
    ss << "\n  scouting: atSupply=" << c.scoutAtSupply << " atFrame=" << c.scoutAtFrame
       << " inferByElimination=" << c.inferEnemyStartByElimination << " observeFrames=" << c.scoutObserveFrames
       << " rescout=" << c.rescoutAfterDeath << " infoUsage=" << c.enemyInfoUsage;
    ss << "\n  composition: medicRatio=" << c.medicRatio << " firebatVsMelee=" << c.firebatRatioVsMelee;
    ss << "\n  attack: size=" << c.attackArmySize << " earliestFrame=" << c.earliestAttackFrame
       << " strengthRatio=" << c.attackStrengthRatio << " regroupRadius=" << c.regroupRadiusTiles
       << " recallToDefend=" << c.recallArmyToDefend;
    ss << "\n  combat: focusFire=" << c.focusFireLevel << " retreatStrengthRatio=" << c.retreatStrengthRatio
       << " retreatArmyFraction=" << c.retreatArmyFraction << " useStim=" << c.useStim << " comsatScan=" << c.useComsatScan;
    ss << "\n  adaptation: sizeOnFail=+" << c.attackSizeIncreaseOnFail << " ratioOnFail=+" << c.strengthRatioIncreaseOnFail
       << " maxAttackSize=" << c.maxAttackSize;
    return ss.str();
  }
}
