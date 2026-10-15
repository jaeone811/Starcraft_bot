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
      c.maxBases = 2;
      c.expandEarliestFrame = 24 * 60 * 13;
      c.expandAtWorkers = 14;
      c.maxBarracks = 2;
      c.extraBarracksMineralBank = 0;
      c.supplyLookaheadFrames = 0;
      c.supplyFixedMargin = 2;
      c.maxConcurrentDepots = 1;
      c.strictPriority = false;
      c.armyFirstWhenThreatened = false;
      c.earlyDefenseArmy = 4;
      c.earlyBunker = false;
      c.scoutAtSupply = 0;
      c.scoutAtFrame = minutes(5);
      c.inferEnemyStartByElimination = false;
      c.scoutObserveFrames = 0;
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 0;
      c.medicRatio = 0.15;
      c.firebatRatioVsMelee = 0.0;
      c.mechShare = 0.0;
      c.maxMechShare = 0.0;
      c.maxFactories = 0;
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
                       {BuildItem::Barracks, 30}, {BuildItem::Factory, 32} };
      c.workersPerMineralPatch = 1.75;
      c.maxWorkers = 22;
      c.maxBases = 2;
      c.expandEarliestFrame = 24 * 60 * 10 + 24 * 30;
      c.expandAtWorkers = 16;
      c.maxBarracks = 3;
      c.extraBarracksMineralBank = 0;
      c.research = { ResearchItem::SiegeMode };
      c.supplyLookaheadFrames = 0;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 1;
      c.strictPriority = false;
      c.armyFirstWhenThreatened = false;
      c.earlyDefenseArmy = 6;
      c.earlyBunker = false;
      c.scoutAtSupply = 15;
      c.scoutAtFrame = minutes(4);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = 0;
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 1;
      c.medicRatio = 0.2;
      c.firebatRatioVsMelee = 0.1;
      c.mechShare = 0.15;
      c.maxMechShare = 0.15;
      c.maxFactories = 1;
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
      c.description = u8"기본 AI 수준 목표: 바이오닉 + 메카닉(상대에 따라 비중 조절), 기본 정찰, 집결 후 공격, 기본 집중 공격";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 16}, {BuildItem::Academy, 19},
                       {BuildItem::Factory, 22}, {BuildItem::Barracks, 24}, {BuildItem::EngineeringBay, 30} };
      c.workersPerMineralPatch = 2.0;
      c.maxWorkers = 26;
      c.maxBases = 3;
      c.expandEarliestFrame = 24 * 60 * 8;
      c.expandAtWorkers = 20;
      c.maxBarracks = 3;
      c.extraBarracksMineralBank = 0;
      c.research = { ResearchItem::StimPacks, ResearchItem::SiegeMode, ResearchItem::SpiderMines, ResearchItem::U238Shells,
                     ResearchItem::InfantryWeapons1 };
      c.supplyLookaheadFrames = 700;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 1;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.earlyDefenseArmy = 8;
      c.earlyBunker = true;
      c.scoutAtSupply = 12;
      c.scoutAtFrame = minutes(3);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(5);
      c.rescoutAfterDeath = false;
      c.enemyInfoUsage = 1;
      c.medicRatio = 0.25;
      c.firebatRatioVsMelee = 0.25;
      c.mechShare = 0.25;
      c.maxMechShare = 0.5;
      c.maxFactories = 2;
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
      c.description = u8"높은 가동률, 보급 예측, 정찰 정보 기반 조합/공격 시점, 상위 테크(골리앗·레이스·사이언스 베슬)";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 15}, {BuildItem::Academy, 17},
                       {BuildItem::Factory, 19}, {BuildItem::Barracks, 20}, {BuildItem::EngineeringBay, 26},
                       {BuildItem::Barracks, 30}, {BuildItem::Armory, 50}, {BuildItem::Starport, 70},
                       {BuildItem::ScienceFacility, 95} };
      c.workersPerMineralPatch = 2.2;
      c.maxWorkers = 34;
      c.maxBases = 3;
      c.expandEarliestFrame = 24 * 60 * 6 + 24 * 30;
      c.expandAtWorkers = 20;
      c.maxBarracks = 6;
      c.extraBarracksMineralBank = 400;
      c.research = { ResearchItem::StimPacks, ResearchItem::SiegeMode, ResearchItem::SpiderMines, ResearchItem::U238Shells,
                     ResearchItem::InfantryWeapons1, ResearchItem::InfantryArmor1, ResearchItem::VehicleWeapons1,
                     ResearchItem::CharonBoosters };
      c.supplyLookaheadFrames = 850;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 2;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.earlyDefenseArmy = 10;
      c.earlyBunker = true;
      c.scoutAtSupply = 10;
      c.scoutAtFrame = minutes(2);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(15);
      c.rescoutAfterDeath = true;
      c.enemyInfoUsage = 2;
      c.medicRatio = 0.25;
      c.firebatRatioVsMelee = 0.35;
      c.mechShare = 0.3;
      c.maxMechShare = 0.7;
      c.maxFactories = 3;
      c.airShare = 0.1;
      c.maxScienceVessels = 1;
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
      c.description = u8"구현된 전략 내 최대 효율: 다중 확장, 업그레이드, 최상위 테크(배틀크루저), 위협 우선순위 집중 공격";
      c.buildOrder = { {BuildItem::Barracks, 11}, {BuildItem::Refinery, 12},
                       {BuildItem::Barracks, 15}, {BuildItem::Academy, 16},
                       {BuildItem::Factory, 18}, {BuildItem::Barracks, 19}, {BuildItem::EngineeringBay, 24},
                       {BuildItem::Barracks, 28}, {BuildItem::Armory, 42}, {BuildItem::Starport, 60},
                       {BuildItem::ScienceFacility, 80} };
      c.workersPerMineralPatch = 2.5;
      c.maxWorkers = 50;
      c.maxBarracks = 8;
      c.extraBarracksMineralBank = 350;
      c.maxBases = 4;
      c.expandEarliestFrame = 24 * 60 * 4 + 24 * 30;
      c.expandAtWorkers = 20;
      c.research = { ResearchItem::StimPacks, ResearchItem::SiegeMode, ResearchItem::SpiderMines, ResearchItem::U238Shells,
                     ResearchItem::InfantryWeapons1, ResearchItem::InfantryArmor1, ResearchItem::VehicleWeapons1,
                     ResearchItem::CharonBoosters, ResearchItem::VehiclePlating1 };
      c.supplyLookaheadFrames = 950;
      c.supplyFixedMargin = 4;
      c.maxConcurrentDepots = 3;
      c.strictPriority = true;
      c.armyFirstWhenThreatened = true;
      c.earlyDefenseArmy = 10;
      c.earlyBunker = true;
      c.scoutAtSupply = 9;
      c.scoutAtFrame = minutes(2);
      c.inferEnemyStartByElimination = true;
      c.scoutObserveFrames = seconds(25);
      c.rescoutAfterDeath = true;
      c.enemyInfoUsage = 2;
      c.medicRatio = 0.3;
      c.firebatRatioVsMelee = 0.4;
      c.mechShare = 0.3;
      c.maxMechShare = 0.8;
      c.maxFactories = 4;
      c.airShare = 0.15;
      c.maxScienceVessels = 2;
      c.useBattlecruisers = true;
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

  bool makesEarlyArmy(BuildItem item)
  {
    switch (item)
    {
    case BuildItem::SupplyDepot:
    case BuildItem::Barracks:
    case BuildItem::Refinery:
    case BuildItem::Academy:
    case BuildItem::Factory:
      return true;
    default:
      return false;
    }
  }

  std::vector<BuildStep> factoryFirst(const std::vector<BuildStep>& base, int maxFactories)
  {
    std::vector<BuildStep> out(base);
    auto nth = [&out](BuildItem item, int n) {
      int seen = 0;
      for (int i = 0; i < static_cast<int>(out.size()); ++i)
        if (out[i].item == item && ++seen == n)
          return i;
      return -1;
    };
    const int factory = nth(BuildItem::Factory, 1);
    if (factory < 0 || maxFactories <= 0)
      return out;
    const int secondBarracks = nth(BuildItem::Barracks, 2);
    if (secondBarracks >= 0 && secondBarracks < factory)
    {
      out[secondBarracks].item = BuildItem::Factory;
      out.erase(out.begin() + factory);
    }
    if (maxFactories >= 2 && nth(BuildItem::Factory, 2) < 0)
      for (int i = nth(BuildItem::Factory, 1) + 1; i < static_cast<int>(out.size()); ++i)
        if (out[i].item == BuildItem::Barracks)
        {
          out[i].item = BuildItem::Factory;
          break;
        }
    return out;
  }

  std::vector<BuildStep> bioFirst(const std::vector<BuildStep>& base)
  {
    std::vector<BuildStep> out(base);
    int factory = -1, ebay = -1;
    for (int i = 0; i < static_cast<int>(out.size()); ++i)
    {
      if (factory < 0 && out[i].item == BuildItem::Factory)
        factory = i;
      if (ebay < 0 && out[i].item == BuildItem::EngineeringBay)
        ebay = i;
    }
    if (factory >= 0 && ebay > factory)
      std::swap(out[factory].item, out[ebay].item);
    return out;
  }

  std::vector<BuildStep> raceOpening(const std::vector<BuildStep>& base, EnemyRace race, int maxFactories)
  {
    switch (race)
    {
    case EnemyRace::Protoss:
    case EnemyRace::Terran:
      return factoryFirst(base, maxFactories);
    case EnemyRace::Zerg:
      return bioFirst(base);
    case EnemyRace::Unknown:
      break;
    }
    return base;
  }

  std::vector<ResearchItem> raceResearch(const std::vector<ResearchItem>& list, EnemyRace race)
  {
    if (race == EnemyRace::Protoss || race == EnemyRace::Terran)
      return researchOrder(list, true);
    std::vector<ResearchItem> out(list);
    if (race == EnemyRace::Zerg)
      std::stable_partition(out.begin(), out.end(), [](ResearchItem i) {
        return i == ResearchItem::StimPacks || i == ResearchItem::U238Shells || i == ResearchItem::InfantryWeapons1 ||
               i == ResearchItem::InfantryArmor1;
      });
    return out;
  }

  bool extendsRange(ResearchItem item)
  {
    return item == ResearchItem::SiegeMode || item == ResearchItem::U238Shells;
  }

  std::vector<ResearchItem> researchOrder(const std::vector<ResearchItem>& list, bool rangeFirst)
  {
    std::vector<ResearchItem> out(list);
    if (rangeFirst)
      std::stable_partition(out.begin(), out.end(), [](ResearchItem i) { return extendsRange(i); });
    return out;
  }

  bool helpsAgainstAir(ResearchItem item)
  {
    switch (item)
    {
    case ResearchItem::StimPacks:
    case ResearchItem::U238Shells:
    case ResearchItem::InfantryWeapons1:
    case ResearchItem::CharonBoosters:
      return true;
    default:
      return false;
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
    case BuildItem::Factory: return "Factory";
    case BuildItem::Armory: return "Armory";
    case BuildItem::Starport: return "Starport";
    case BuildItem::ScienceFacility: return "ScienceFacility";
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
    case ResearchItem::SiegeMode: return "SiegeMode";
    case ResearchItem::VehicleWeapons1: return "VehicleWeapons1";
    case ResearchItem::VehiclePlating1: return "VehiclePlating1";
    case ResearchItem::CharonBoosters: return "CharonBoosters";
    case ResearchItem::SpiderMines: return "SpiderMines";
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
       << " bases<=" << c.maxBases << " expand>=" << c.expandEarliestFrame << "f@" << c.expandAtWorkers << "workers";
    ss << "\n  supply: lookahead=" << c.supplyLookaheadFrames << " fixedMargin=" << c.supplyFixedMargin
       << " maxConcurrentDepots=" << c.maxConcurrentDepots << " marginOnBlock=+" << c.supplyMarginIncreaseOnBlock;
    ss << "\n  allocation: strictPriority=" << c.strictPriority << " armyFirstWhenThreatened=" << c.armyFirstWhenThreatened
       << " earlyDefenseArmy=" << c.earlyDefenseArmy << " earlyBunker=" << c.earlyBunker;
    ss << "\n  scouting: atSupply=" << c.scoutAtSupply << " atFrame=" << c.scoutAtFrame
       << " inferByElimination=" << c.inferEnemyStartByElimination << " observeFrames=" << c.scoutObserveFrames
       << " rescout=" << c.rescoutAfterDeath << " infoUsage=" << c.enemyInfoUsage;
    ss << "\n  composition: medicRatio=" << c.medicRatio << " firebatVsMelee=" << c.firebatRatioVsMelee << " mechShare=" << c.mechShare << "-" << c.maxMechShare << " maxFactories=" << c.maxFactories
       << " airShare=" << c.airShare << " scienceVessels=" << c.maxScienceVessels << " battlecruisers=" << c.useBattlecruisers;
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
