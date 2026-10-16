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

  CHECK(parseBotSettings("").opening == "liquipedia");
  CHECK(parseBotSettings("[bot]\nopening = Classic\n").opening == "classic");
  s = parseBotSettings("[bot]\nopening = rush\n");
  CHECK(s.opening == "liquipedia");
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
  Budget gasShort(300, 40, 20, true);
  CHECK(!gasShort.tryReserve({ 150, 100, 4 }));      // a Tank waits for gas...
  CHECK(gasShort.tryReserve({ 50, 0, 2 }));          // ...but the minerals still buy a Marine
  CHECK(!gasShort.tryReserve({ 50, 25, 2 }));        // gas is held for the Tank
}

TEST_CASE("early defense army grows until 6:00 and ends at 8:00")
{
  CHECK_EQ(earlyDefenseTarget(24 * 120, 10, 0, false), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 180, 10, 0, false), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 0, false), 5);
  CHECK_EQ(earlyDefenseTarget(24 * 360, 10, 0, false), 10);
  CHECK_EQ(earlyDefenseTarget(24 * 479, 10, 0, false), 10);
  CHECK_EQ(earlyDefenseTarget(24 * 480, 10, 0, false), 0);
  CHECK_EQ(earlyDefenseTarget(24 * 360, 0, 0, false), 0);
}

TEST_CASE("early defense answers the enemy army that was seen")
{
  CHECK_EQ(earlyDefenseTarget(24 * 150, 10, 3, false), 3);   // zealots seen before 3:00
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 8, false), 8);   // more than the ramp
  CHECK_EQ(earlyDefenseTarget(24 * 270, 10, 2, false), 5);   // the ramp is higher
  CHECK_EQ(earlyDefenseTarget(24 * 480, 10, 8, false), 0);   // over after 8:00
}

TEST_CASE("a seen rush wants the whole early army at once")
{
  CHECK_EQ(earlyDefenseTarget(24 * 150, 10, 1, true), 10);
  CHECK_EQ(earlyDefenseTarget(24 * 150, 10, 12, true), 12);
  CHECK_EQ(earlyDefenseTarget(24 * 480, 10, 0, true), 0);
  EarlyScouting twoGates;
  twoGates.frame = 24 * 141;
  twoGates.productionBuildings = 2;
  CHECK(rushSigns(twoGates));
  EarlyScouting normal;
  normal.frame = 24 * 141;
  normal.productionBuildings = 1;
  normal.combatUnits = 1;
  CHECK(!rushSigns(normal));
  EarlyScouting pool;
  pool.frame = 24 * 140;
  pool.spawningPool = true;
  CHECK(rushSigns(pool));
  pool.frame = 24 * 170;
  CHECK(!rushSigns(pool));            // a pool seen late is a normal opening
  EarlyScouting late = twoGates;
  late.frame = 24 * 200;
  CHECK(!rushSigns(late));            // two Gateways after 3:00 are normal
  EarlyScouting proxy;
  proxy.frame = 24 * 172;
  proxy.proxyBuildings = 1;
  CHECK(rushSigns(proxy));            // a Gateway at our natural
  proxy.frame = 24 * 280;
  CHECK(rushSigns(proxy));            // still before 5:00
  proxy.frame = 24 * 320;
  CHECK(!rushSigns(proxy));           // later: a forward base, not a rush
}

TEST_CASE("counters: a dominant enemy unit type is answered in mass")
{
  CHECK(!dominantForce(5, 6));          // too few to matter
  CHECK(dominantForce(6, 10));          // 6 Dragoons in an army of 10
  CHECK(!dominantForce(6, 20));         // 30 %: a mixed army
  CHECK(dominantForce(12, 30));
  CHECK(!dominantForce(5, 11));               // 45 % but fewer than 6: not yet
  CHECK(dominantForce(5, 11, true));          // already massing: keep going
  CHECK(!dominantForce(3, 8, true));          // fewer than 4: stop
  CHECK(!dominantForce(4, 20, true));         // 20 %: stop
  CHECK(isFactoryCounter(Counter::SiegeTanks));
  CHECK(!isFactoryCounter(Counter::Firebats));
  CHECK(!isFactoryCounter(Counter::None));
  using R = ResearchItem;
  CHECK(researchServesCounter(R::SiegeMode, Counter::SiegeTanks));
  CHECK(!researchServesCounter(R::StimPacks, Counter::SiegeTanks));
  CHECK(!researchServesCounter(R::CharonBoosters, Counter::SiegeTanks));
  CHECK(researchServesCounter(R::CharonBoosters, Counter::Goliaths));
  CHECK(researchServesCounter(R::StimPacks, Counter::Firebats));
  CHECK(!researchServesCounter(R::SiegeMode, Counter::Marines));
  CHECK(researchServesCounter(R::InfantryArmor1, Counter::None));
}

TEST_CASE("detection: Turrets before the Dark Templar arrive")
{
  CHECK(!detectionWanted(false, false, 0, false, 24 * 600));
  CHECK(detectionWanted(true, false, 0, false, 24 * 100));    // a hidden attacker seen
  CHECK(detectionWanted(false, true, 0, false, 24 * 100));    // Templar Archives seen
  CHECK(detectionWanted(false, false, 2, false, 24 * 100));   // Mutalisks
  CHECK(!detectionWanted(false, false, 0, true, 24 * 300));   // Protoss, 5:00: not yet
  CHECK(detectionWanted(false, false, 0, true, 24 * 360));    // Protoss, 6:00
}

TEST_CASE("doorstep: enemy buildings near our bases are cleared when the army is clearly stronger")
{
  CHECK(canClearDoorstep(3000.0, 0.0, 4));        // an undefended Pylon / Gateway
  CHECK(!canClearDoorstep(3000.0, 0.0, 3));       // too few shooters
  CHECK(!canClearDoorstep(3000.0, 2500.0, 10));   // three Cannons and a Dragoon
  CHECK(canClearDoorstep(9000.0, 2500.0, 10));    // Siege Tanks and the army
}

TEST_CASE("SCVs for the next base come before it")
{
  CHECK_EQ(workersInAdvance(1, 3, 24 * 300, 24 * 390), 6);   // 90 s before the earliest expansion
  CHECK_EQ(workersInAdvance(1, 3, 24 * 200, 24 * 390), 0);   // too early
  CHECK_EQ(workersInAdvance(3, 3, 24 * 600, 24 * 390), 0);   // all bases taken
}

TEST_CASE("levels 3-5 research Spider Mines, levels 1-2 do not")
{
  for (int level = 1; level <= 5; ++level)
  {
    const DifficultyConfig c = makeDifficultyConfig(level);
    const bool mines = std::find(c.research.begin(), c.research.end(), ResearchItem::SpiderMines) != c.research.end();
    CHECK_EQ(mines, level >= 3);
  }
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

TEST_CASE("anti-air: strength counts only what can shoot the enemy")
{
  // A Firebat (ground only) is worth nothing against flyers, a Goliath its air weapon.
  CHECK_EQ(mixedDamagePerFrame(1.0, 0.0, 1.0), 0.0);
  CHECK_EQ(mixedDamagePerFrame(1.0, 0.0, 0.0), 1.0);
  CHECK_EQ(mixedDamagePerFrame(0.5, 1.0, 0.5), 0.75);
  CHECK_EQ(mixedDamagePerFrame(1.0, 0.0, 2.0), 0.0);   // clamped
  CHECK(!antiAirShort(0.0, 0.0));                     // no enemy air
  CHECK(antiAirShort(5000.0, 1000.0));
  CHECK(!antiAirShort(5000.0, 5000.0));
  CHECK(helpsAgainstAir(ResearchItem::U238Shells));
  CHECK(helpsAgainstAir(ResearchItem::CharonBoosters));
  CHECK(helpsAgainstAir(ResearchItem::StimPacks));
  CHECK(!helpsAgainstAir(ResearchItem::SpiderMines));
  CHECK(!helpsAgainstAir(ResearchItem::SiegeMode));
  CHECK(!helpsAgainstAir(ResearchItem::VehicleWeapons1));
}

TEST_CASE("build order: Factory first against Protoss")
{
  using B = BuildItem;
  const std::vector<BuildStep> level4{ {B::Barracks, 11}, {B::Refinery, 12}, {B::Barracks, 15}, {B::Academy, 17},
                                       {B::Factory, 19}, {B::Barracks, 20}, {B::EngineeringBay, 26}, {B::Barracks, 30},
                                       {B::Armory, 50} };
  const std::vector<BuildStep> p = factoryFirst(level4, 3);
  CHECK_EQ(p.size(), level4.size() - 1);
  CHECK(p[2].item == B::Factory);
  CHECK_EQ(p[2].supply, 15);
  CHECK(p[3].item == B::Academy);
  CHECK(p[4].item == B::Factory);          // the Barracks@20 became the second Factory
  CHECK_EQ(p[4].supply, 20);
  CHECK(p[6].item == B::Barracks);         // the Barracks@30 stays
  int barracks = 0, factories = 0;
  for (const auto& s : p)
  {
    barracks += s.item == B::Barracks ? 1 : 0;
    factories += s.item == B::Factory ? 1 : 0;
  }
  CHECK_EQ(barracks, 2);
  CHECK_EQ(factories, 2);
  // One Factory at most: only the swap.
  const std::vector<BuildStep> level2{ {B::Barracks, 11}, {B::Refinery, 14}, {B::Barracks, 18}, {B::Academy, 22},
                                       {B::Barracks, 30}, {B::Factory, 32} };
  const std::vector<BuildStep> q = factoryFirst(level2, 1);
  CHECK(q[2].item == B::Factory);
  CHECK(q[4].item == B::Barracks);
  CHECK_EQ(q.size(), level2.size() - 1);
  // No Factory in the build order: unchanged.
  const std::vector<BuildStep> level1{ {B::Barracks, 12}, {B::Refinery, 20}, {B::Barracks, 24} };
  CHECK_EQ(factoryFirst(level1, 0).size(), level1.size());
  CHECK(factoryFirst(level1, 0)[2].item == B::Barracks);
}

TEST_CASE("openings per enemy race (Liquipedia)")
{
  using B = BuildItem;
  const std::vector<BuildStep> level4{ {B::Barracks, 11}, {B::Refinery, 12}, {B::Barracks, 15}, {B::Academy, 17},
                                       {B::Factory, 19}, {B::Barracks, 20}, {B::EngineeringBay, 26}, {B::Barracks, 30},
                                       {B::Armory, 50} };
  // Zerg: Engineering Bay where the Factory was, the Factory where the Engineering Bay was.
  const std::vector<BuildStep> z = raceOpening(level4, EnemyRace::Zerg, 3);
  CHECK(z[4].item == B::EngineeringBay);
  CHECK_EQ(z[4].supply, 19);
  CHECK(z[6].item == B::Factory);
  CHECK_EQ(z[6].supply, 26);
  CHECK_EQ(z.size(), level4.size());
  // Terran and Protoss: Factory first.
  CHECK(raceOpening(level4, EnemyRace::Terran, 3)[2].item == B::Factory);
  CHECK(raceOpening(level4, EnemyRace::Protoss, 3)[2].item == B::Factory);
  // Unknown race: unchanged.
  CHECK(raceOpening(level4, EnemyRace::Unknown, 3)[2].item == B::Barracks);
  // No Engineering Bay (level 2): unchanged against Zerg.
  const std::vector<BuildStep> level2{ {B::Barracks, 11}, {B::Refinery, 14}, {B::Barracks, 18}, {B::Factory, 32} };
  CHECK(raceOpening(level2, EnemyRace::Zerg, 1)[3].item == B::Factory);

  using R = ResearchItem;
  const std::vector<R> research{ R::StimPacks, R::SiegeMode, R::SpiderMines, R::U238Shells, R::InfantryWeapons1,
                                 R::InfantryArmor1, R::VehicleWeapons1 };
  const std::vector<R> zr = raceResearch(research, EnemyRace::Zerg);
  CHECK(zr[0] == R::StimPacks);
  CHECK(zr[1] == R::U238Shells);
  CHECK(zr[2] == R::InfantryWeapons1);
  CHECK(zr[3] == R::InfantryArmor1);
  CHECK(zr[4] == R::SiegeMode);
  const std::vector<R> tr = raceResearch(research, EnemyRace::Terran);
  CHECK(tr[0] == R::SiegeMode);
  CHECK(tr[1] == R::U238Shells);
  CHECK(raceResearch(research, EnemyRace::Unknown) == research);
}

TEST_CASE("research: range first against heavy ranged enemies")
{
  const std::vector<ResearchItem> list{ ResearchItem::StimPacks, ResearchItem::SiegeMode, ResearchItem::SpiderMines,
                                        ResearchItem::U238Shells, ResearchItem::InfantryWeapons1 };
  CHECK(researchOrder(list, false) == list);
  const std::vector<ResearchItem> first = researchOrder(list, true);
  CHECK(first[0] == ResearchItem::SiegeMode);
  CHECK(first[1] == ResearchItem::U238Shells);
  CHECK(first[2] == ResearchItem::StimPacks);
  CHECK(first[3] == ResearchItem::SpiderMines);
  CHECK(extendsRange(ResearchItem::U238Shells));
  CHECK(!extendsRange(ResearchItem::StimPacks));
}

TEST_CASE("defense: no chase away from the base, no fight against a stronger enemy outside")
{
  CHECK(!defenseOutmatched(1000.0, 0.0));
  CHECK(!defenseOutmatched(1000.0, 1100.0));     // about even: fight
  CHECK(defenseOutmatched(1000.0, 1500.0));      // clearly stronger: wait at the rally
  CHECK(!onLeash(8, false, 10, 5));              // close to our buildings
  CHECK(onLeash(11, false, 10, 5));              // chased too far: called back
  CHECK(onLeash(7, true, 10, 5));                // on the way back: keeps walking
  CHECK(!onLeash(4, true, 10, 5));               // back home: fights again
}

TEST_CASE("geometry: stepping away from a Scarab's target")
{
  const Point p = awayFrom({ 100, 100 }, { 70, 100 }, 96);
  CHECK_EQ(p.x, 196);
  CHECK_EQ(p.y, 100);
  const Point q = awayFrom({ 100, 100 }, { 100, 100 }, 96);   // on the spot: sideways
  CHECK_EQ(q.x, 196);
  const Point r = awayFrom({ 0, 0 }, { 30, 40 }, 50);
  CHECK_EQ(r.x, -30);
  CHECK_EQ(r.y, -40);
}

TEST_CASE("expansion: a base is contested only by enemies the army cannot handle")
{
  CHECK(!baseContested(0.0, 0.0));          // nobody seen there
  CHECK(baseContested(500.0, 0.0));         // no army to guard it
  CHECK(!baseContested(500.0, 20000.0));    // two Zerglings next to a 20-unit army
  CHECK(baseContested(12000.0, 20000.0));   // the enemy army stands there
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
  in.knownEnemyStrength = 5000;
  in.ownArmyStrength = 4000;
  CHECK(!shouldExpand(in, p));                  // a base the army cannot cover
  in.ownArmyStrength = 5000;
  CHECK(shouldExpand(in, p));
  in.bases = 1;
  in.ownArmyStrength = 0;
  in.nextIsNatural = true;
  CHECK(shouldExpand(in, p));                   // the natural does not wait for the army
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

TEST_CASE("targets: our damage type counts (Firebat: Zealot before Dragoon)")
{
  TargetInfo zealot;
  zealot.canAttackUs = true;
  zealot.hitPoints = 160;
  zealot.damagePerFrame = 16.0 / 22;
  zealot.inRange = true;
  zealot.ourDamageFactor = 1.0;       // small: full concussive damage
  TargetInfo dragoon = zealot;
  dragoon.hitPoints = 120;            // a hurt Dragoon looks weaker...
  dragoon.damagePerFrame = 20.0 / 30;
  dragoon.ourDamageFactor = 0.25;     // ...but it is large: a quarter of concussive damage
  CHECK_EQ(chooseTarget({ dragoon, zealot }, 2), 1);
  CHECK_EQ(chooseTarget({ dragoon, zealot }, 3), 1);
  dragoon.ourDamageFactor = 1.0;      // a Marine's normal damage
  CHECK_EQ(chooseTarget({ dragoon, zealot }, 2), 0);
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
