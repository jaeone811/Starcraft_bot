// Unit tests for the BWAPI-independent logic: difficulty presets, settings,
// INI editing, command de-duplication and the planning rules.

#include "TestFramework.h"

#include "BotSettings.h"
#include "CommandHistory.h"
#include "DifficultyConfig.h"
#include "IniFile.h"
#include "Metrics.h"
#include "Planning.h"

#include <algorithm>

using namespace bb;

// ------------------------------------------------------------------ difficulty
TEST_CASE("difficulty presets exist for 1..5 and clamp outside")
{
  for (int l = 1; l <= 5; ++l)
  {
    const DifficultyConfig c = makeDifficultyConfig(l);
    CHECK_EQ(c.level, l);
    CHECK(!c.name.empty());
    CHECK(!c.koreanName.empty());
    CHECK(!c.description.empty());
    CHECK(!c.buildOrder.empty());
  }
  CHECK_EQ(makeDifficultyConfig(0).level, 1);
  CHECK_EQ(makeDifficultyConfig(99).level, 5);
  CHECK_EQ(kDefaultDifficulty, 3);
}

TEST_CASE("higher difficulty never plays worse on the tuned axes")
{
  for (int l = 1; l < 5; ++l)
  {
    const DifficultyConfig lo = makeDifficultyConfig(l), hi = makeDifficultyConfig(l + 1);
    CHECK(hi.workersPerMineralPatch >= lo.workersPerMineralPatch);
    CHECK(hi.maxWorkers >= lo.maxWorkers);
    CHECK(hi.maxBarracks >= lo.maxBarracks);
    CHECK(hi.scoutAtFrame <= lo.scoutAtFrame);
    CHECK(hi.enemyInfoUsage >= lo.enemyInfoUsage);
    CHECK(hi.attackArmySize <= lo.attackArmySize);
    CHECK(hi.earliestAttackFrame <= lo.earliestAttackFrame);
    CHECK(hi.focusFireLevel >= lo.focusFireLevel);
    CHECK(hi.regroupRadiusTiles <= lo.regroupRadiusTiles);
    CHECK(hi.retreatArmyFraction >= lo.retreatArmyFraction);
    CHECK(hi.attackSizeIncreaseOnFail >= lo.attackSizeIncreaseOnFail);
    CHECK(hi.supplyMarginIncreaseOnBlock >= lo.supplyMarginIncreaseOnBlock);
    CHECK(hi.maxConcurrentDepots >= lo.maxConcurrentDepots);
    CHECK(hi.research.size() >= lo.research.size());
    CHECK(hi.mechShare >= lo.mechShare);
    CHECK(hi.maxMechShare >= lo.maxMechShare);
    CHECK(hi.maxFactories >= lo.maxFactories);
    CHECK(hi.maxBases >= lo.maxBases);
    CHECK(hi.airShare >= lo.airShare);
    CHECK(hi.maxScienceVessels >= lo.maxScienceVessels);
    CHECK(hi.useBattlecruisers >= lo.useBattlecruisers);
    CHECK(hi.earlyDefenseArmy >= lo.earlyDefenseArmy);
    CHECK(hi.earlyBunker >= lo.earlyBunker);
    CHECK(hi.expandEarliestFrame <= lo.expandEarliestFrame);
  }
}

TEST_CASE("build orders are sorted by supply and every level keeps fallbacks")
{
  for (int l = 1; l <= 5; ++l)
  {
    const DifficultyConfig c = makeDifficultyConfig(l);
    for (size_t i = 1; i < c.buildOrder.size(); ++i)
      CHECK(c.buildOrder[i].supply >= c.buildOrder[i - 1].supply);
    // Every level must build at least one Barracks (army production) and never retreat below 0.
    CHECK(std::any_of(c.buildOrder.begin(), c.buildOrder.end(), [](const BuildStep& s) { return s.item == BuildItem::Barracks; }));
    CHECK(c.retreatArmyFraction > 0.0);
    CHECK(c.maxAttackSize >= c.attackArmySize);
    // Researching something the bot never uses would waste money.
    const bool stim = std::find(c.research.begin(), c.research.end(), ResearchItem::StimPacks) != c.research.end();
    CHECK_EQ(stim, c.useStim);
    // Upgrades from the Engineering Bay need the bay in the build order.
    const bool needsBay = std::any_of(c.research.begin(), c.research.end(), [](ResearchItem r) {
      return r == ResearchItem::InfantryWeapons1 || r == ResearchItem::InfantryArmor1;
    });
    const bool hasBay = std::any_of(c.buildOrder.begin(), c.buildOrder.end(), [](const BuildStep& s) { return s.item == BuildItem::EngineeringBay; });
    CHECK_EQ(needsBay, hasBay);
    // Medics need an Academy, which needs a Refinery for its gas units.
    if (c.medicRatio > 0.0)
      CHECK(std::any_of(c.buildOrder.begin(), c.buildOrder.end(), [](const BuildStep& s) { return s.item == BuildItem::Academy; }));
    // Mech needs a Factory and Siege Mode; a Factory without mech units would be wasted.
    const bool hasFactory = std::any_of(c.buildOrder.begin(), c.buildOrder.end(), [](const BuildStep& s) { return s.item == BuildItem::Factory; });
    const bool siege = std::find(c.research.begin(), c.research.end(), ResearchItem::SiegeMode) != c.research.end();
    CHECK_EQ(hasFactory, c.mechShare > 0.0);
    CHECK_EQ(siege, c.mechShare > 0.0);
    CHECK_EQ(c.maxFactories > 0, c.mechShare > 0.0);
    CHECK(c.maxMechShare >= c.mechShare);
    // Upper tech: each unit or upgrade has its building in the build order.
    auto has = [&](BuildItem item) {
      return std::any_of(c.buildOrder.begin(), c.buildOrder.end(), [item](const BuildStep& s) { return s.item == item; });
    };
    auto researches = [&](ResearchItem r) { return std::find(c.research.begin(), c.research.end(), r) != c.research.end(); };
    if (researches(ResearchItem::VehicleWeapons1) || researches(ResearchItem::VehiclePlating1) || researches(ResearchItem::CharonBoosters))
      CHECK(has(BuildItem::Armory));
    CHECK_EQ(c.airShare > 0.0 || c.maxScienceVessels > 0, has(BuildItem::Starport));
    CHECK_EQ(c.maxScienceVessels > 0 || c.useBattlecruisers, has(BuildItem::ScienceFacility));
    if (has(BuildItem::Starport))
      CHECK(has(BuildItem::Factory));
  }
}

TEST_CASE("describe() names the applied difficulty")
{
  const std::string d = describe(makeDifficultyConfig(4));
  CHECK(d.find("difficulty=4") != std::string::npos);
  CHECK(d.find("Hard") != std::string::npos);
}

// ------------------------------------------------------------------ ini
TEST_CASE("ini get handles sections, case, comments and CRLF")
{
  IniFile ini;
  ini.parse("\xEF\xBB\xBF; comment\r\n[Bot]\r\nDifficulty = 4 ; inline\r\nname=x\r\n\r\n[log]\r\nlevel=debug\r\n");
  CHECK(ini.get("bot", "difficulty").value_or("") == "4");
  CHECK(ini.getInt("BOT", "DIFFICULTY", 0) == 4);
  CHECK(ini.getString("log", "level", "") == "debug");
  CHECK(!ini.get("log", "difficulty").has_value());
  CHECK(ini.getInt("bot", "name", 7) == 7); // not a number -> fallback
}

TEST_CASE("ini set replaces in place and appends new keys/sections")
{
  IniFile ini;
  ini.parse("; keep me\n[ai]\nai = old.dll\n\n[auto_menu]\nrace = Zerg\n; trailing comment\n\n[config]\nx=1\n");
  ini.set("ai", "ai", "bwapi-data/AI/BeginnerBot.dll");
  ini.set("auto_menu", "race", "Terran");
  ini.set("auto_menu", "map", "maps/test.scx");
  ini.set("new", "k", "v");
  const std::string out = ini.toString();
  CHECK(out.find("; keep me") != std::string::npos);
  CHECK(out.find("ai = bwapi-data/AI/BeginnerBot.dll") != std::string::npos);
  CHECK(out.find("old.dll") == std::string::npos);
  CHECK(ini.getString("auto_menu", "race", "") == "Terran");
  CHECK(ini.getString("auto_menu", "map", "") == "maps/test.scx");
  CHECK(ini.getString("config", "x", "") == "1");
  CHECK(ini.getString("new", "k", "") == "v");
  // The new auto_menu key must land inside [auto_menu], before [config].
  CHECK(out.find("map = maps/test.scx") < out.find("[config]"));
}

// ------------------------------------------------------------------ settings
TEST_CASE("settings defaults and validation")
{
  BotSettings s = parseBotSettings("");
  CHECK_EQ(s.difficulty, 3);
  CHECK(s.race == "Terran");

  s = parseBotSettings("[bot]\ndifficulty = 5\n[game]\nlocal_speed = 0\ndebug_draw = off\n[log]\nlevel = DEBUG\n");
  CHECK_EQ(s.difficulty, 5);
  CHECK_EQ(s.localSpeed, 0);
  CHECK(!s.debugDraw);
  CHECK(s.logLevel == "debug");
  CHECK(s.warnings.empty());

  s = parseBotSettings("[bot]\ndifficulty = 9\nrace = Zerg\n");
  CHECK_EQ(s.difficulty, 3);
  CHECK(s.race == "Terran");
  CHECK_EQ(static_cast<int>(s.warnings.size()), 2);

  s = parseBotSettings("[bot]\ndifficulty = hard\n");
  CHECK_EQ(s.difficulty, 3);
  CHECK_EQ(static_cast<int>(s.warnings.size()), 1);
}

TEST_CASE("missing settings file falls back to defaults with a warning")
{
  BotSettings s = loadBotSettings({ "does/not/exist.ini" });
  CHECK_EQ(s.difficulty, 3);
  CHECK(s.loadedFrom.empty());
  CHECK(!s.warnings.empty());
}

// ------------------------------------------------------------------ command history
TEST_CASE("identical commands are suppressed during the cooldown")
{
  CommandHistory h;
  CommandKey move{ CommandKind::Move, -1, 100, 100, 0 };
  CHECK(h.shouldIssue(1, move, 0, 48));
  h.record(1, move, 0);
  CHECK(!h.shouldIssue(1, move, 10, 48));
  CommandKey nearby{ CommandKind::Move, -1, 120, 90, 0 };
  CHECK(!h.shouldIssue(1, nearby, 10, 48));          // within tolerance
  CommandKey far{ CommandKind::Move, -1, 600, 100, 0 };
  CHECK(h.shouldIssue(1, far, 10, 48));              // different target
  CommandKey attack{ CommandKind::AttackMove, -1, 100, 100, 0 };
  CHECK(h.shouldIssue(1, attack, 10, 48));           // different kind
  CHECK(h.shouldIssue(2, move, 10, 48));             // different unit
  CHECK(h.shouldIssue(1, move, 48, 48));             // cooldown over
}

TEST_CASE("repeat counter detects the same order sent again and again")
{
  CommandHistory h;
  CommandKey g{ CommandKind::Gather, 77, 0, 0, 0 };
  CHECK_EQ(h.record(5, g, 0), 1);
  CHECK_EQ(h.record(5, g, 100), 2);
  CHECK_EQ(h.record(5, g, 200), 3);
  CommandKey other{ CommandKind::Gather, 78, 0, 0, 0 };
  CHECK_EQ(h.record(5, other, 300), 1);
  CHECK_EQ(h.repeatedCount(), 2);
  h.forget(5);
  CHECK(h.shouldIssue(5, other, 301, 1000));
}

// ------------------------------------------------------------------ supply
TEST_CASE("supply: fixed margin")
{
  std::vector<Producer> none;
  SupplyState s{ 16, 20, 0, 0 };                     // 8/10, 2 free
  CHECK(needSupplyDepot(s, none, 0, 2, 0, 1, false));
  s.used = 14;                                       // 7/10, 3 free
  CHECK(!needSupplyDepot(s, none, 0, 2, 0, 1, false));
  CHECK(needSupplyDepot(s, none, 0, 2, 1, 1, false)); // extra margin after a block
}

TEST_CASE("supply: concurrency limit, pending supply and max supply")
{
  std::vector<Producer> none;
  SupplyState s{ 18, 20, 16, 1 };                    // one depot already coming
  CHECK(!needSupplyDepot(s, none, 0, 2, 0, 1, false));
  s.pending = 0;
  s.depotsInProgress = 1;                            // e.g. planned but not counted yet
  CHECK(!needSupplyDepot(s, none, 0, 2, 0, 1, false));
  CHECK(needSupplyDepot(s, none, 0, 2, 0, 2, false));
  SupplyState maxed{ 390, 400, 0, 0 };
  CHECK(!needSupplyDepot(maxed, none, 0, 2, 0, 3, true));
}

TEST_CASE("supply: a block with nothing pending always gets a depot")
{
  std::vector<Producer> none;
  SupplyState s{ 20, 20, 0, 1 };
  CHECK(needSupplyDepot(s, none, 0, 2, 0, 1, true));
}

TEST_CASE("supply: forecast grows with production capacity")
{
  std::vector<Producer> one{ { 300, 2 } };
  std::vector<Producer> many{ { 300, 2 }, { 360, 2 }, { 360, 2 }, { 360, 2 } };
  const int small = desiredSupplyMargin(one, 900, 2, 0);
  const int big = desiredSupplyMargin(many, 900, 2, 0);
  CHECK(big > small);
  CHECK(small >= 4);                                 // never below the fixed margin
  CHECK_EQ(desiredSupplyMargin(one, 0, 3, 0), 6);
}

TEST_CASE("mech share adapts to the enemy army")
{
  EnemyMix none;
  CHECK_EQ(targetMechShare(none, 0.25, 0.5, true), 0.25);          // nothing seen yet
  EnemyMix gateway;                                                 // zealots + dragoons
  gateway.melee = 6;
  gateway.heavy = 6;
  CHECK_EQ(targetMechShare(gateway, 0.25, 0.5, true), 0.5);        // all mech-friendly: maximum
  CHECK_EQ(targetMechShare(gateway, 0.25, 0.5, false), 0.25);      // no adaptation at this level
  EnemyMix hydras;
  hydras.light = 10;
  CHECK_EQ(targetMechShare(hydras, 0.25, 0.5, true), 0.25);        // infantry is fine: stay at the base
  EnemyMix mixed;
  mixed.heavy = 4;
  mixed.light = 4;
  const double m = targetMechShare(mixed, 0.25, 0.5, true);
  CHECK(m > 0.25 && m < 0.5);
}

TEST_CASE("factory unit choice")
{
  EnemyMix none;
  CHECK(chooseFactoryUnit(0, 0, 0, none, true, false) == FactoryUnit::SiegeTank);   // first unit: a tank
  CHECK(chooseFactoryUnit(0, 0, 0, none, false, false) == FactoryUnit::Vulture);    // no Machine Shop yet
  CHECK(chooseFactoryUnit(0, 4, 0, none, true, false) == FactoryUnit::Vulture);     // a few vultures next to tanks
  CHECK(chooseFactoryUnit(2, 4, 0, none, true, false) == FactoryUnit::SiegeTank);
  EnemyMix melee;
  melee.melee = 10;
  CHECK(chooseFactoryUnit(2, 4, 0, melee, true, false) == FactoryUnit::Vulture);    // vultures against zealots/lings
  EnemyMix air;
  air.air = 5;
  air.heavy = 5;
  CHECK(chooseFactoryUnit(0, 4, 0, air, true, true) == FactoryUnit::Goliath);       // air seen and Armory ready
  CHECK(chooseFactoryUnit(0, 4, 0, air, true, false) != FactoryUnit::Goliath);      // no Armory: no goliaths
  CHECK(chooseFactoryUnit(0, 4, 0, none, true, true) == FactoryUnit::Goliath);      // Armory: some goliaths against ground too
  CHECK(chooseFactoryUnit(5, 11, 6, none, true, true) == FactoryUnit::SiegeTank);   // ...about a quarter
}

TEST_CASE("race prior shapes the opening until the enemy army is seen")
{
  EnemyMix none;
  const EnemyMix p = withRacePrior(none, EnemyRace::Protoss);
  const EnemyMix z = withRacePrior(none, EnemyRace::Zerg);
  const EnemyMix t = withRacePrior(none, EnemyRace::Terran);
  CHECK(p.melee > 0 && z.melee > 0 && t.melee == 0);
  // Mech share: highest against Protoss, lowest against Zerg/Terran bio.
  CHECK(targetMechShare(p, 0.3, 0.7, true) > targetMechShare(z, 0.3, 0.7, true));
  CHECK(targetMechShare(p, 0.3, 0.7, true) > targetMechShare(t, 0.3, 0.7, true));
  // Against Protoss the first Factory unit is a Vulture (zealots), against Terran a tank.
  CHECK(chooseFactoryUnit(0, 0, 0, p, true, false) == FactoryUnit::Vulture);
  CHECK(chooseFactoryUnit(0, 0, 0, t, true, false) == FactoryUnit::SiegeTank);
  // Once enough is seen, only what was seen counts.
  EnemyMix seen;
  seen.light = 6;
  CHECK_EQ(withRacePrior(seen, EnemyRace::Protoss).melee, 0);
  CHECK_EQ(withRacePrior(none, EnemyRace::Unknown).total(), 0);
}

TEST_CASE("starport unit choice")
{
  CHECK(chooseStarportUnit(0, 0, 0, 1, false, false) == StarportUnit::Wraith);        // nothing unlocked yet
  CHECK(chooseStarportUnit(2, 0, 0, 1, true, false) == StarportUnit::ScienceVessel);  // a vessel for detection first
  CHECK(chooseStarportUnit(2, 1, 0, 1, true, false) == StarportUnit::Wraith);         // vessel cap reached
  CHECK(chooseStarportUnit(1, 2, 0, 2, true, true) == StarportUnit::Battlecruiser);
  CHECK(chooseStarportUnit(1, 2, 2, 2, true, true) == StarportUnit::Wraith);          // a Wraith per Battlecruiser
}

TEST_CASE("a build step waits for its supply unless supply stopped growing")
{
  CHECK(buildStepDue(12, 12, false, false));
  CHECK(!buildStepDue(11, 12, false, false));
  // Level 1 on Boxer: 11 workers is the target and nothing trains -> Barracks@12 still comes.
  CHECK(buildStepDue(11, 12, true, false));
  // Only the first waiting step is released.
  CHECK(!buildStepDue(11, 20, true, true));
  CHECK(buildStepDue(20, 20, true, true));
}

TEST_CASE("factories keep up with the barracks at the mech share")
{
  // A mix of tanks and vultures takes about 1.8x as long as a marine.
  CHECK_EQ(factoriesWanted(0.0, 3, 1.8, 0), 0);
  CHECK_EQ(factoriesWanted(0.0, 3, 1.8, 4), 0);
  CHECK_EQ(factoriesWanted(0.15, 3, 1.8, 1), 1);
  // 25% mech next to 3 Barracks needs about two Factories, not one.
  CHECK_EQ(factoriesWanted(0.25, 3, 1.8, 2), 2);
  CHECK_EQ(factoriesWanted(0.25, 1, 1.8, 4), 1);
  // More Barracks or a higher share need more Factories, up to the cap.
  CHECK(factoriesWanted(0.3, 6, 1.8, 10) > factoriesWanted(0.3, 3, 1.8, 10));
  CHECK(factoriesWanted(0.5, 3, 1.8, 10) > factoriesWanted(0.25, 3, 1.8, 10));
  CHECK_EQ(factoriesWanted(0.8, 3, 1.8, 4), 4);
  CHECK_EQ(factoriesWanted(1.0, 3, 1.8, 4), 4);
}

TEST_CASE("group radius grows with the army but never below the minimum")
{
  CHECK_EQ(groupRadiusTiles(8, 0), 8);
  CHECK_EQ(groupRadiusTiles(8, 20), 8);   // a dozen or two units: the difficulty radius applies
  CHECK_EQ(groupRadiusTiles(5, 18), 7);
  CHECK_EQ(groupRadiusTiles(8, 79), 11);  // the 79-unit wave of the first real game
  CHECK(groupRadiusTiles(6, 60) >= groupRadiusTiles(6, 30));
}

// ------------------------------------------------------------------ economy / budget
TEST_CASE("worker target")
{
  CHECK_EQ(workerTarget(8, 1, 2.0, 50), 19);
  CHECK_EQ(workerTarget(8, 1, 2.0, 18), 18);
  CHECK_EQ(workerTarget(0, 0, 2.0, 30), 0);
  CHECK_EQ(workerTarget(9, 0, 1.5, 50), 14);
}

TEST_CASE("budget: greedy lets cheaper items through, strict saves up")
{
  Budget greedy(100, 0, 20, false);
  CHECK(!greedy.tryReserve({ 150, 0, 0 }));          // barracks: too expensive
  CHECK(greedy.tryReserve({ 50, 0, 2 }));            // marine still bought
  Budget strict(100, 0, 20, true);
  CHECK(!strict.tryReserve({ 150, 0, 0 }));
  CHECK(!strict.tryReserve({ 50, 0, 2 }));           // money is held for the barracks
  Budget supplyBlocked(500, 0, 0, true);
  CHECK(!supplyBlocked.tryReserve({ 50, 0, 2 }));    // supply problem: do not hoard money
  CHECK(supplyBlocked.tryReserve({ 100, 0, 0 }));    // the depot can still be bought
}

TEST_CASE("early defense army grows until 6:00 and ends at 8:00")
{
  CHECK_EQ(earlyDefenseTarget(24 * 120, 10, 0), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 180, 10, 0), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 0), 5);
  CHECK_EQ(earlyDefenseTarget(24 * 360, 10, 0), 10);
  CHECK_EQ(earlyDefenseTarget(24 * 479, 10, 0), 10);
  CHECK_EQ(earlyDefenseTarget(24 * 480, 10, 0), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 360, 0, 0), 0);
}

TEST_CASE("early defense answers the enemy army that was seen")
{
  CHECK_EQ(earlyDefenseTarget(24 * 150, 10, 3), 3);   // zealots seen before 3:00
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 8), 8);   // more than the ramp
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 2), 5);   // the ramp is higher
  CHECK_EQ(earlyDefenseTarget(24 * 480, 10, 8), 0);   // over after 8:00
}

TEST_CASE("only what makes the early army is built while it is short")
{
  CHECK(makesEarlyArmy(BuildItem::SupplyDepot));
  CHECK(makesEarlyArmy(BuildItem::Barracks));
  CHECK(makesEarlyArmy(BuildItem::Refinery));
  CHECK(makesEarlyArmy(BuildItem::Academy));
  CHECK(makesEarlyArmy(BuildItem::Factory));
  CHECK(!makesEarlyArmy(BuildItem::EngineeringBay));
  CHECK(!makesEarlyArmy(BuildItem::CommandCenter));
  CHECK(!makesEarlyArmy(BuildItem::Armory));
  CHECK(!makesEarlyArmy(BuildItem::Starport));
  CHECK(!makesEarlyArmy(BuildItem::ScienceFacility));
}

TEST_CASE("budget: reserveFirst saves up even at the greedy levels")
{
  Budget greedy(100, 0, 10, false);
  CHECK(!greedy.reserveFirst({ 400, 0, 0 }));   // a Command Center is not affordable yet...
  CHECK(!greedy.canAfford({ 50, 0, 2 }));       // ...and marines may not take the money meanwhile
  Budget rich(500, 0, 10, false);
  CHECK(rich.reserveFirst({ 400, 0, 0 }));
  CHECK_EQ(rich.minerals(), 100);
}

TEST_CASE("expansion: one rule, the difficulty sets when and how far")
{
  ExpansionParams p{ 2, 24 * 600, 16 };
  ExpansionInputs in;
  in.frame = 24 * 600;
  in.bases = 1;
  in.workers = 16;
  in.workerTarget = 15;
  CHECK(shouldExpand(in, p));
  in.frame = 24 * 599;
  CHECK(!shouldExpand(in, p));                  // too early
  in.frame = 24 * 600;
  in.underThreat = true;
  CHECK(!shouldExpand(in, p));
  in.underThreat = false;
  in.workers = 12;
  CHECK(!shouldExpand(in, p));                  // neither enough workers...
  in.workerTarget = 12;
  CHECK(shouldExpand(in, p));                   // ...but the main is saturated
  in.bases = 2;
  CHECK(!shouldExpand(in, p));                  // maxBases reached
  p.maxBases = 3;
  in.basesBuilding = 1;
  CHECK(!shouldExpand(in, p));                  // one at a time
  in.basesBuilding = 0;
  in.lastExpansionFrame = in.frame - kExpansionIntervalFrames + 1;
  CHECK(!shouldExpand(in, p));
  in.lastExpansionFrame = in.frame - kExpansionIntervalFrames;
  CHECK(shouldExpand(in, p));
}

TEST_CASE("infantry mix")
{
  CHECK(chooseInfantry(10, 0, 0, 0.25, 0.0, false) == Infantry::Marine); // no academy
  CHECK(chooseInfantry(10, 0, 0, 0.25, 0.0, true) == Infantry::Medic);
  CHECK(chooseInfantry(10, 3, 0, 0.25, 0.0, true) == Infantry::Marine);
  CHECK(chooseInfantry(8, 2, 0, 0.25, 0.3, true) == Infantry::Firebat);
  CHECK(chooseInfantry(2, 0, 0, 0.25, 0.0, true) == Infantry::Marine);   // too few to need a medic
}

// ------------------------------------------------------------------ attack / retreat
TEST_CASE("attack decision")
{
  AttackParams p;
  p.requiredSize = 12;
  p.earliestFrame = 1000;
  p.requiredRatio = 1.3;
  p.maxSize = 30;
  AttackInputs in;
  in.armySize = 12;
  in.frame = 999;
  CHECK(!shouldAttack(in, p));                       // too early
  in.frame = 1000;
  CHECK(shouldAttack(in, p));                        // no strength estimate in use
  in.useStrengthEstimate = true;
  in.ownStrength = 100;
  in.knownEnemyStrength = 100;
  CHECK(!shouldAttack(in, p));                       // not strong enough
  in.ownStrength = 131;
  CHECK(shouldAttack(in, p));
  in.knownEnemyStrength = 0;                         // nothing known: size decides
  in.ownStrength = 0;
  CHECK(shouldAttack(in, p));
  in.armySize = 11;
  CHECK(!shouldAttack(in, p));
  in.armySize = 30;
  in.frame = 0;
  CHECK(shouldAttack(in, p));                        // maxed army always moves out

  // No suicide attacks, even without the strength estimate (difficulty 1).
  in.armySize = 20;
  in.frame = 1000;
  in.useStrengthEstimate = false;
  in.ownStrength = 6145;
  in.knownEnemyStrength = 42007;                     // difficulty 1 game: 20 marines vs cannons and army
  CHECK(!shouldAttack(in, p));
  in.knownEnemyStrength = 3 * 6145;
  CHECK(shouldAttack(in, p));
  in.armySize = 30;
  in.knownEnemyStrength = 42007;
  CHECK(shouldAttack(in, p));                        // a maxed army still goes
}

TEST_CASE("retreat decision")
{
  CHECK(shouldRetreat(100, 0, 0.0, 2, 10, 0.25));    // wave shrank below 25%
  CHECK(!shouldRetreat(100, 0, 0.0, 3, 10, 0.25));
  CHECK(!shouldRetreat(100, 300, 0.0, 10, 10, 0.25)); // ratio 0: never by strength
  CHECK(shouldRetreat(100, 150, 1.4, 10, 10, 0.25));
  CHECK(!shouldRetreat(100, 130, 1.4, 10, 10, 0.25));
}

TEST_CASE("strength: square law favours numbers")
{
  const double one = combineStrength({ 10.0 });
  const double two = combineStrength({ 10.0, 10.0 });
  CHECK_NEAR(two, 4 * one, 1e-9);
  CHECK_NEAR(unitStrength(40, 0, 0.4), 16.0, 1e-9);
}

// ------------------------------------------------------------------ focus fire
TEST_CASE("target choice per focus fire level")
{
  TargetInfo building;     building.isBuilding = true; building.hitPoints = 500; building.distance = 10; building.inRange = true;
  TargetInfo worker;       worker.isWorker = true; worker.canAttackUs = true; worker.hitPoints = 60; worker.damagePerFrame = 0.1; worker.distance = 50; worker.inRange = true;
  TargetInfo weakFighter;  weakFighter.canAttackUs = true; weakFighter.hitPoints = 20; weakFighter.damagePerFrame = 0.4; weakFighter.distance = 150;
  TargetInfo strongFighter; strongFighter.canAttackUs = true; strongFighter.hitPoints = 160; strongFighter.damagePerFrame = 0.7; strongFighter.distance = 60; strongFighter.inRange = true;
  std::vector<TargetInfo> c{ building, worker, weakFighter, strongFighter };

  CHECK_EQ(chooseTarget(c, 0), -1);                  // plain attack-move
  CHECK_EQ(chooseTarget(c, 1), 1);                   // closest unit that can fight back
  CHECK_EQ(chooseTarget(c, 2), 1);                   // in range first, then lowest hp
  CHECK_EQ(chooseTarget(c, 3), 3);                   // most damage per hp among in-range threats
  CHECK_EQ(chooseTarget({ building }, 2), 0);
  CHECK_EQ(chooseTarget({}, 3), -1);
}

// ------------------------------------------------------------------ geometry
TEST_CASE("mineral clustering")
{
  std::vector<Point> pts{ {0, 0}, {32, 0}, {64, 16}, {1000, 1000}, {1032, 1000}, {3000, 0} };
  auto clusters = clusterPoints(pts, 192);
  CHECK_EQ(static_cast<int>(clusters.size()), 3);
  size_t largest = 0;
  for (const auto& c : clusters)
    largest = std::max(largest, c.size());
  CHECK_EQ(static_cast<int>(largest), 3);
  Point c = centroid({ {0, 0}, {10, 20} });
  CHECK_EQ(c.x, 5);
  CHECK_EQ(c.y, 10);
  CHECK_EQ(countWithin(pts, { 0, 0 }, 100), 3);
}

// ------------------------------------------------------------------ metrics
TEST_CASE("metrics csv header and row have the same number of columns")
{
  Metrics m;
  const std::string header = metricsCsvHeader();
  const std::string row = metricsCsvRow("d,m,3,Protoss,win,100,00:04", m);
  CHECK_EQ(std::count(header.begin(), header.end(), ','), std::count(row.begin(), row.end(), ','));
}

int main()
{
  return bbtest::runAll();
}
