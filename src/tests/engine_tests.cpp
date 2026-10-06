// Structural / robustness / regression tests that need the whole engine.
// tests.cpp keeps the doctest main; this file only adds test cases.
#include <doctest/doctest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"
#include "tests/agents.hpp"
#include "tests/test_util.hpp"

using namespace fy;
using namespace fy::test;

namespace {

int fuzz_games(int fallback) {
  if (const char* s = std::getenv("FY_FUZZ_GAMES")) {
    int v = std::atoi(s);
    if (v > 0) return v;
  }
  return fallback;
}

Config make_cfg(const std::string& mode, uint64_t seed) {
  Config cfg;
  cfg.seed = seed;
  cfg.mode = mode;
  return cfg;
}

}  // namespace

TEST_CASE("invariants hold over randomized self-play (hajimari)") {
  const int games = fuzz_games(25);
  for (int g = 0; g < games; ++g) {
    uint64_t seed = 1000 + static_cast<uint64_t>(g);
    Engine e(make_cfg("hajimari", seed));
    load_hajimari(e);
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 4), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 2) % 4), seed * 7 + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    INFO("seed=", seed, " mode=", agent_mode_name(static_cast<AgentMode>(g % 4)));
    e.run();
    CHECK(e.st.over);
    CHECK(e.illegal_count(P0) == 0);  // legal agents must never be punished
    CHECK(e.illegal_count(P1) == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("invariants hold over randomized self-play (standard)") {
  const int games = fuzz_games(25);
  for (int g = 0; g < games; ++g) {
    uint64_t seed = 2000 + static_cast<uint64_t>(g);
    Engine e(make_cfg("standard", seed));
    load_standard(e);
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 4), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 2) % 4), seed * 7 + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    INFO("seed=", seed, " mode=", agent_mode_name(static_cast<AgentMode>(g % 4)));
    e.run();
    CHECK(e.st.over);
    CHECK(e.illegal_count(P0) == 0);
    CHECK(e.illegal_count(P1) == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("illegal decisions forfeit once the tolerance is exceeded") {
  Config cfg = make_cfg("hajimari", 7);
  cfg.illegalTolerance = 1;  // one free mistake, the second forfeits
  Engine e(cfg);
  load_hajimari(e);
  CheckingAgent bad(&e, AgentMode::OutOfRange, 1);
  FirstAgent good;
  e.set_agent(P0, &bad);
  e.set_agent(P1, &good);
  e.run();
  CHECK(e.st.over);
  CHECK(e.forfeited());
  CHECK(e.st.winner == 1);
  CHECK(e.illegal_count(P0) >= 2);
  CHECK(e.illegal_count(P1) == 0);
}

TEST_CASE("replay rejects a corrupted journal") {
  Config cfg = make_cfg("hajimari", 11);
  Engine rec(cfg);
  load_hajimari(rec);
  RandomAgent a0(1), a1(2);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  nlohmann::json journal = rec.journal_json();
  REQUIRE(!journal["entries"].empty());
  journal["entries"][0]["indices"] = nlohmann::json::array({999999});

  Engine rep(cfg);
  load_hajimari(rep);
  rep.load_journal(journal);
  CHECK_THROWS(rep.run());
}

TEST_CASE("invariants hold over randomized self-play (all official content)") {
  const int games = fuzz_games(25);
  for (int g = 0; g < games; ++g) {
    uint64_t seed = 3000 + static_cast<uint64_t>(g);
    Config cfg = make_cfg("standard", seed);
    cfg.preset = "kigen-full";  // 达人 + 官方, 异相 off
    Engine e(cfg);
    load_all_content(e);
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 4), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 2) % 4), seed * 7 + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    INFO("seed=", seed, " mode=", agent_mode_name(static_cast<AgentMode>(g % 4)));
    e.run();
    CHECK(e.st.over);
    CHECK(e.lua_error_count() == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("invariants hold over randomized self-play (异相 on, all content)") {
  const int games = fuzz_games(25);
  for (int g = 0; g < games; ++g) {
    uint64_t seed = 5000 + static_cast<uint64_t>(g);
    Config cfg = make_cfg("standard", seed);
    cfg.preset = "gachi-full";  // 达人 + 官方, 异相 on (A1/AA1 forms drafted)
    Engine e(cfg);
    load_all_content(e);
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 4), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 2) % 4), seed * 7 + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    INFO("seed=", seed);
    e.run();
    CHECK(e.st.over);
    CHECK(e.lua_error_count() == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("engine tolerates protocol-violating decisions") {
  const AgentMode modes[] = {AgentMode::RandomAny, AgentMode::OutOfRange, AgentMode::Negative,
                             AgentMode::Empty,     AgentMode::OverSelect, AgentMode::Duplicate};
  const int games = fuzz_games(4);
  for (AgentMode m : modes) {
    for (int g = 0; g < games; ++g) {
      uint64_t seed = 5000 + static_cast<uint64_t>(g) * 13 + static_cast<uint64_t>(m);
      Config cfg = make_cfg("hajimari", seed);
      cfg.illegalTolerance = 1000000;  // never forfeit: keep exercising repaired paths
      Engine e(cfg);
      load_hajimari(e);
      CheckingAgent a0(&e, m, seed + 1);
      CheckingAgent a1(&e, m, seed + 2);
      e.set_agent(P0, &a0);
      e.set_agent(P1, &a1);
      INFO("mode=", agent_mode_name(m), " seed=", seed);
      e.run();
      CHECK(e.st.over);
      for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    }
  }
}

// ---------------------------------------------------------------------------
// Regressions for bugs found by reading / fuzzing the engine.
// ---------------------------------------------------------------------------

TEST_CASE("a played attack is counted exactly once") {
  Config cfg = make_cfg("hajimari", 1);
  Engine e(cfg);
  load_hajimari(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hajimari.ukiro", "投射");  // 【5-9 3/1】
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 7;
  CHECK(e.attacks_this_turn(P0) == 0);
  e.play_card(P0, inst, false);
  CHECK(e.attacks_this_turn(P0) == 1);
}

TEST_CASE("倒车 (to_distance) routes the life side into 距") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.active = P0;
  e.st.distance = 5;
  e.st.dust = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 0;
  e.st.p[P1].flare = 0;
  Attack atk;
  atk.attacker = P0;
  atk.sourceInst = -1;
  atk.range.add(0, 10);
  atk.aura = 3;
  atk.life = 2;
  atk.keywords = AF_ToDistance;
  e.resolve_attack(atk);
  CHECK(e.st.p[P1].life == 8);
  CHECK(e.st.p[P1].flare == 0);
  CHECK(e.st.distance == 7);
  CHECK(e.st.dust == 0);
}

TEST_CASE("state_hash is sensitive to the whole state (deck order)") {
  Config cfg = make_cfg("standard", 3);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.setup_match();
  REQUIRE(e.st.p[0].deck.size() >= 2);
  uint64_t h0 = e.state_hash();
  std::swap(e.st.p[0].deck[0], e.st.p[0].deck[1]);
  uint64_t h1 = e.state_hash();
  CHECK(h0 != h1);
}

// ---------------------------------------------------------------------------
// Regressions for defects found by the rules audit / invariant fuzzing.
// ---------------------------------------------------------------------------

TEST_CASE("poisons placed on the opponent keep exactly one owner and zone") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_bag(P0);
  REQUIRE(!e.ps(P0).bag.empty());
  int pz = e.ps(P0).bag[0];
  e.place_poison(pz, P1, Zone::Hand);
  CHECK(e.ci(pz).owner == P1);
  CHECK(e.ci(pz).zone == Zone::Hand);
  CHECK(std::find(e.ps(P0).bag.begin(), e.ps(P0).bag.end(), pz) == e.ps(P0).bag.end());
  CHECK(std::find(e.ps(P1).hand.begin(), e.ps(P1).hand.end(), pz) != e.ps(P1).hand.end());
  CHECK(check_invariants(e).empty());

  e.ci(pz).bagOwner = P0;
  e.return_poison(pz);
  CHECK(e.ci(pz).owner == P0);
  CHECK(e.ci(pz).zone == Zone::Bag);
  CHECK(std::find(e.ps(P1).hand.begin(), e.ps(P1).hand.end(), pz) == e.ps(P1).hand.end());
  CHECK(std::find(e.ps(P0).bag.begin(), e.ps(P0).bag.end(), pz) != e.ps(P0).bag.end());
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("破绽 on a 切札付与 returns it unused, never to the cover pile") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "chikage", "暗昏千影的信条");  // 全力【纳4】破绽 (切札付与)
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  // give it 献 taken from 距 so crystals are still conserved
  e.st.distance -= 4;
  e.ci(inst).crystals = 4;
  int dust0 = e.st.dust;
  e.lose_life(P0, 1, true);
  CHECK(e.ci(inst).zone == Zone::Special);  // 切札 must not enter cover/deck
  CHECK_FALSE(e.ci(inst).faceUp);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.st.dust == dust0 + 4);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("a 视作装 enhancement leaves play when its last crystal is spent") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine", "无音壁");  // 全力【纳5】armor_as_crystals
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.st.distance -= 2;  // keep 36 crystals while creating the 献
  e.ci(inst).crystals = 2;
  e.st.dust += e.st.p[P0].aura;
  e.st.p[P0].aura = 0;
  int dust0 = e.st.dust;
  e.deal_damage(P0, 2, std::nullopt, 0);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.ci(inst).zone == Zone::Discard);
  CHECK(e.st.dust == dust0 + 2);
}

TEST_CASE("二重奏 blocks basic actions, including effect-granted ones") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.dust = 3;
  CHECK(e.basic_legal(P0, BasicAction::Aura));
  e.ps(P0).cannotBasic = true;
  CHECK_FALSE(e.basic_legal(P0, BasicAction::Aura));
  CHECK_FALSE(e.do_basic(P0, BasicAction::Aura));
}

TEST_CASE("即再起 declared on an event resets the cut (阿尔法之刃 on 气动)") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "thallya", "阿尔法之刃");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ps(P0).thallya = true;
  e.ps(P0).steamEngine = 1;
  e.pneumatic(P0);  // fires the "pneumatic" event
  CHECK_FALSE(e.ci(inst).faceUp);
}

TEST_CASE("气动 distance lasts until the owner's own next turn") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).thallya = true;
  e.ps(P0).steamEngine = 2;
  e.st.distance = 5;
  e.pneumatic(P0);  // FirstAgent picks "距离 +1"
  int d0 = e.distance();
  CHECK(d0 == 6);
  e.reset_steam_at_turn_start(P1);  // the opponent's turn starts
  CHECK(e.distance() == d0);        // 气动 is still in effect
  e.reset_steam_at_turn_start(P0);  // the owner's turn starts
  CHECK(e.distance() == d0 - 1);
}

TEST_CASE("问答: taking the 0-life side makes the defender cover their top 3") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int card = find_def(e, "yurina", "斩");
  REQUIRE(card >= 0);
  for (int i = 0; i < 5; ++i) {
    int c = e.add_instance(card, P1);
    e.move_card(c, Zone::Deck);
  }
  int def = find_def(e, "yurina.A2", "问答");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.dust += e.st.distance - 3;  // keep 36 crystals while moving to 距 3
  e.st.distance = 3;
  e.st.dust += e.st.p[P1].aura;  // keep 36 crystals while emptying the aura
  e.st.p[P1].aura = 0;           // cannot take the 3 aura side -> forced life (0)
  e.st.p[P1].life = 10;
  int deck0 = static_cast<int>(e.ps(P1).deck.size());
  e.play_card(P0, inst, false);
  CHECK(e.st.p[P1].life == 10);                                   // 0 life damage
  CHECK(static_cast<int>(e.ps(P1).cover.size()) == 3);            // top 3 covered
  CHECK(static_cast<int>(e.ps(P1).deck.size()) == deck0 - 3);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("二重奏 即再起 sees the damage side of the current hit") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine.A1", "二重奏·弹奏冰瞑");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ps(P0).flare = 5;
  e.st.p[P0].aura = 0;  // forced to take life damage
  e.st.p[P0].life = 10;
  e.st.distance = 3;
  e.st.active = P1;
  Attack atk;
  atk.attacker = P1;
  atk.sourceInst = -1;
  atk.range.add(0, 10);
  atk.aura = 2;
  atk.life = 1;
  e.resolve_attack(atk);
  CHECK(e.st.p[P0].life == 9);
  CHECK_FALSE(e.ci(inst).faceUp);  // 即再起: "因为攻击而受到命伤"
}

TEST_CASE("达人距离 (近身距离) is shared and modified by 圈域/引力场") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.dust += e.st.distance - 3;  // keep 36 crystals while moving to 距 3
  e.st.distance = 3;
  CHECK(e.near_distance() == 2);
  CHECK(e.basic_legal(P0, BasicAction::Advance));   // 距 3 > 达人距 2
  CHECK_FALSE(e.basic_legal(P0, BasicAction::Escape));  // 距 3 > 2

  int quanyu = find_def(e, "saine", "圈域");
  REQUIRE(quanyu >= 0);
  int qi = e.add_instance(quanyu, P0);
  e.move_card(qi, Zone::Enhance);
  e.ci(qi).crystals = 2;
  CHECK(e.near_distance() == 3);                     // 圈域: 达人距离 +1
  CHECK_FALSE(e.basic_legal(P0, BasicAction::Advance));  // 距 3 > 3 false
  CHECK(e.basic_legal(P0, BasicAction::Escape));         // 距 3 <= 3 true
  CHECK(e.basic_legal(P1, BasicAction::Escape));         // shared attribute
}

TEST_CASE("A1 变形池继承未被替换的 O 变形 (娜迦)") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_transforms(P0, "A1");
  REQUIRE(e.ps(P0).parts.empty());  // no parts for thallya
  std::vector<std::string> names;
  for (int inst : e.transform_cards(P0)) names.push_back(e.def_of(inst).name);
  auto has = [&](const char* n) {
    return std::find(names.begin(), names.end(), n) != names.end();
  };
  CHECK(names.size() == 4);  // 紧那罗 / 娜迦 / 阿修罗 / 提婆
  CHECK(has("娜迦"));
  CHECK(has("紧那罗"));
  CHECK(has("阿修罗"));
  CHECK(has("提婆"));
  CHECK_FALSE(has("夜叉"));
  CHECK_FALSE(has("迦楼罗"));
}

TEST_CASE("萨利亚的杰作: a burn card needs enough 用尽 to be played") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).thallya = true;
  e.ps(P0).steamEngine = 0;
  e.ps(P0).steamExhausted = 1;
  CHECK_FALSE(e.can_burn(P0, 2));  // engines empty: nothing to burn normally
  int def = find_def(e, "thallya", "萨利亚的杰作");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 3;
  CHECK(e.can_burn(P0, 1));         // 1 exhausted -> can recover 1
  CHECK_FALSE(e.can_burn(P0, 2));   // only 1 can be recovered
  e.ps(P0).steamExhausted = 2;
  CHECK(e.can_burn(P0, 2));
}

// ---- 2026-10 需求方裁定对应的回归测试 ----------------------------------------

TEST_CASE("an attack that is negated still counts as resolved (圆环轮回旋)") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "raira", "圆环轮回旋");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P1);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 3;
  e.ps(P1).raira = true;
  e.st.active = P0;
  e.st.distance = 5;
  Attack atk;
  atk.attacker = P0;
  atk.sourceInst = -1;
  atk.range.add(0, 10);
  atk.aura = 1;
  atk.negated = true;  // 被打消也是一种结算
  e.resolve_attack(atk);
  CHECK(e.ps(P1).wind == 1);  // FirstAgent picks 风+1
}

TEST_CASE("迟缓毒 forbids using attack cards but not generated attacks") {
  // (a) using an attack card is refused
  {
    Config cfg = make_cfg("standard", 1);
    Engine e(cfg);
    load_standard(e);
    FirstAgent a;
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    int poison = find_def(e, "chikage", "迟缓毒");
    REQUIRE(poison >= 0);
    int pz = e.add_instance(poison, P0);
    e.move_card(pz, Zone::Enhance);
    e.ci(pz).crystals = 3;
    int atk = find_def(e, "yurina", "斩");
    int ai = e.add_instance(atk, P0);
    e.move_card(ai, Zone::Hand);
    e.st.active = P0;
    e.st.distance = 3;
    int aura0 = e.st.p[P1].aura;
    e.play_card(P0, ai, false);
    CHECK(e.st.p[P1].aura == aura0);       // no attack happened
    CHECK(e.attacks_this_turn(P0) == 0);  // not even counted
  }
  // (b) an attack generated by a card effect still resolves
  {
    Config cfg = make_cfg("standard", 1);
    Engine e(cfg);
    load_standard(e);
    FirstAgent a;
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    int poison = find_def(e, "chikage", "迟缓毒");
    int pz = e.add_instance(poison, P0);
    e.move_card(pz, Zone::Enhance);
    e.ci(pz).crystals = 3;
    int def = find_def(e, "yurina", "气合斩");  // 弃置时 生成不可对攻击
    REQUIRE(def >= 0);
    int gi = e.add_instance(def, P0);
    e.move_card(gi, Zone::Hand);
    // dust + aura == 0 -> 0 献 -> immediate discard -> on_discard attack
    e.st.p[P1].flare += e.st.p[P0].aura + e.st.dust;
    e.st.p[P0].aura = 0;
    e.st.dust = 0;
    e.st.distance = 3;
    int aura0 = e.st.p[P1].aura;
    e.play_card(P0, gi, false);
    CHECK(e.st.p[P1].aura < aura0);  // generated attacks are not "using a card"
  }
}

TEST_CASE("二重奏 blocks generated attacks as well") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yurina", "气合斩");
  int gi = e.add_instance(def, P0);
  e.move_card(gi, Zone::Hand);
  e.st.p[P1].flare += e.st.p[P0].aura + e.st.dust;
  e.st.p[P0].aura = 0;
  e.st.dust = 0;
  e.ps(P0).cannotAttack = true;
  e.st.distance = 3;
  int aura0 = e.st.p[P1].aura;
  e.play_card(P0, gi, false);
  CHECK(e.st.p[P1].aura == aura0);
}

TEST_CASE("天地反驳 swaps damage before numeric modifiers") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "shinra", "天地反驳");
  REQUIRE(def >= 0);
  int ti = e.add_instance(def, P0);
  e.move_card(ti, Zone::Special);
  e.ci(ti).faceUp = true;
  e.ci(ti).crystals = 5;
  int atk = find_def(e, "yurina", "斩");  // 【3-4 3/1】 -> swapped to 1/3
  int ai = e.add_instance(atk, P0);
  e.move_card(ai, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P1].aura = 3;
  e.st.p[P1].life = 10;
  e.play_card(P0, ai, false);
  CHECK(e.st.p[P1].aura == 2);  // took the 1-aura side (FirstAgent prefers aura)
  CHECK(e.st.p[P1].life == 10);
}

TEST_CASE("诡辩: a borrowed 付与 is on the user's side, then returns home") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine", "圈域");  // an enhancement in the opponent's discard
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P1);
  e.move_card(inst, Zone::Discard);
  e.st.distance -= 2;  // conserve crystals for the 献
  e.use_foreign_card(P0, inst);
  CHECK(e.ci(inst).holder == P0);   // on the user's field while in use
  CHECK(e.ci(inst).owner == P1);    // but still the opponent's card
  CHECK(e.ci(inst).zone == Zone::Enhance);
  CHECK(std::find(e.ps(P0).enhance.begin(), e.ps(P0).enhance.end(), inst) !=
        e.ps(P0).enhance.end());
  CHECK(e.near_distance() == 3);  // the aura benefits the user

  // Once its 献 are gone it leaves play and returns to the opponent's discard.
  e.consume_enhance_crystal(inst);
  e.consume_enhance_crystal(inst);
  CHECK(e.ci(inst).zone == Zone::Discard);
  CHECK(e.ci(inst).holder == P1);
  CHECK(std::find(e.ps(P1).discard.begin(), e.ps(P1).discard.end(), inst) !=
        e.ps(P1).discard.end());
}

TEST_CASE("紧那罗 covers the opponent's whole deck") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int card = find_def(e, "yurina", "斩");
  for (int i = 0; i < 3; ++i) {
    int c = e.add_instance(card, P1);
    e.move_card(c, Zone::Deck);
  }
  REQUIRE(e.ps(P1).deck.size() == 3);
  e.cover_deck(P1);
  CHECK(e.ps(P1).deck.empty());
  CHECK(e.ps(P1).cover.size() == 3);
  for (int inst : e.ps(P1).cover) CHECK_FALSE(e.ci(inst).faceUp);
}

TEST_CASE("夜叉: the OPPONENT's next start phase draws one fewer") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).thallya = true;
  e.ps(P0).steamEngine = 5;
  e.transform(P0, "夜叉");
  CHECK(e.ps(P1).nextDrawOne);   // the opponent is the one affected
  CHECK_FALSE(e.ps(P0).nextDrawOne);
}

TEST_CASE("炼成攻击 cannot be sealed or picked by the opponent") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hagane", "炼成攻击");
  REQUIRE(def >= 0);
  CHECK(e.def(def).unsealable);
  CHECK(e.def(def).noOpponentPick);
  int victim = e.add_instance(def, P1);
  e.move_card(victim, Zone::Discard);
  int host = e.add_instance(find_def(e, "shinra", "论破"), P0);
  e.seal_card(host, victim);
  CHECK(e.ci(victim).zone == Zone::Discard);  // refused
  CHECK(e.ci(host).sealed.empty());
}

TEST_CASE("a stolen 论破 returns itself and the sealed card to its owner") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int lun = find_def(e, "shinra", "论破");            // 常规付与【纳4】
  int victim = find_def(e, "yurina", "斩");
  REQUIRE(lun >= 0);
  int li = e.add_instance(lun, P1);
  e.move_card(li, Zone::Discard);
  int vi = e.add_instance(victim, P1);
  e.move_card(vi, Zone::Discard);
  e.st.distance -= 4;
  e.st.dust += 4;  // conserve crystals and fund the 献

  e.use_foreign_card(P0, li);  // 诡辩 borrows the opponent's 论破
  CHECK(e.ci(li).holder == P0);
  CHECK(e.ci(li).owner == P1);
  CHECK(e.ci(li).zone == Zone::Enhance);
  CHECK(e.ci(vi).zone == Zone::Sealed);  // 论破 sealed one of ITS owner's cards

  while (e.ci(li).crystals > 0) e.consume_enhance_crystal(li);
  CHECK(e.ci(li).zone == Zone::Discard);
  CHECK(e.ci(li).holder == P1);          // 论破 itself goes home
  CHECK(e.ci(vi).zone == Zone::Discard);  // and so does the sealed card
  CHECK(std::find(e.ps(P1).discard.begin(), e.ps(P1).discard.end(), li) !=
        e.ps(P1).discard.end());
  CHECK(std::find(e.ps(P1).discard.begin(), e.ps(P1).discard.end(), vi) !=
        e.ps(P1).discard.end());
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

// ---- 内容模块 / 预设规则包 ---------------------------------------------------

TEST_CASE("ruleset presets select packs and whether 异相 is allowed") {
  struct Case {
    const char* preset;
    bool variants;
  };
  const Case cases[] = {{"kigen-tatsujin", false}, {"kigen-full", false},
                        {"gachi-tatsujin", true},  {"gachi-full", true},
                        {"起源战达人", false},      {"完全战达人", true}};
  for (const Case& c : cases) {
    Config cfg;
    cfg.preset = c.preset;
    Engine e(cfg);
    load_standard(e);
    INFO("preset=", c.preset);
    CHECK(e.variants_allowed() == c.variants);
    CHECK(e.available_forms("yurina").size() == (c.variants ? 3u : 1u));
    CHECK(e.available_forms("himika").size() == (c.variants ? 2u : 1u));
    CHECK(e.goddess_pool().size() == 12u);  // only the bundled 达人 modules
  }
}

TEST_CASE("dynamic modules: custom pack, manifest and 异相 control") {
  Config cfg;
  cfg.preset = "kigen-tatsujin";  // 起源战达人: 异相 off, 全扩 off
  cfg.allowCustom = true;
  Engine e(cfg);
  load_standard(e);
  CHECK(e.goddess_pool().size() == 12u);
  CHECK(e.pack_allowed("custom"));  // custom enabled by allowCustom

  // load a custom goddess module from a separate directory with an explicit pack
  const std::string mod = find_file("src/tests/fixtures/custom_goddess.lua");
  e.load_content(mod, "custom");
  CHECK(e.modules().size() == 13u);
  CHECK(e.goddess_pool().size() == 13u);
  auto cpool = e.goddess_pool();
  CHECK(std::find(cpool.begin(), cpool.end(), "customx") != cpool.end());
  CHECK(e.available_forms("customx").size() == 1u);  // 起源战: no 异相

  // switching to a 完全战 preset enables the custom 异相
  e.cfg.preset = "gachi-tatsujin";
  CHECK(e.available_forms("customx").size() == 2u);
  CHECK(e.available_forms("customx")[1] == "A1");

  // without allowCustom the custom pack is excluded again
  e.cfg.allowCustom = false;
  CHECK_FALSE(e.pack_allowed("custom"));
  CHECK(e.goddess_pool().size() == 12u);
}

TEST_CASE("a match can be played with a restricted (custom) goddess pool") {
  Config cfg;
  cfg.preset = "kigen-full";
  cfg.allowCustom = true;
  cfg.mode = "standard";
  cfg.enabledGoddesses = {"customx", "yurina"};
  Engine e(cfg);
  load_standard(e);
  e.load_content(find_file("src/tests/fixtures/custom_goddess.lua"), "custom");
  CHECK(e.goddess_pool().size() == 2u);
  CheckingAgent a0(&e, AgentMode::RandomLegal, 11);
  CheckingAgent a1(&e, AgentMode::RandomLegal, 22);
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);
  e.run();
  CHECK(e.st.over);
  for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
  for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("the bundled packs manifest loads the 达人 and 官方 modules") {
  Config cfg;
  cfg.preset = "kigen-tatsujin";  // 达人 only
  Engine e(cfg);
  const int n = e.load_manifest(find_file("content/packs.json"));
  CHECK(n == 17);  // 12 达人 + 5 official
  CHECK(e.modules().size() == 17u);
  auto pool_t = e.goddess_pool();
  CHECK(pool_t.size() == 12u);  // the official pack is excluded
  CHECK(std::find(pool_t.begin(), pool_t.end(), "utsuro") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "honoka") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "konuru") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "yatsuha") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "kamuwi") == pool_t.end());

  // 全扩 includes the official pack
  e.cfg.preset = "kigen-full";
  auto pool_f = e.goddess_pool();
  CHECK(pool_f.size() == 17u);
  CHECK(std::find(pool_f.begin(), pool_f.end(), "utsuro") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "honoka") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "konuru") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "yatsuha") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "kamuwi") != pool_f.end());
}

TEST_CASE("a borrowed 切札 never enters the discard pile") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "thallya", "最终燃烧");  // used 切札 in the opponent's zone
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P1);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.st.distance -= 5;  // keep 36 crystals while granting 气
  e.ps(P0).flare = 5;
  e.st.active = P0;
  e.use_foreign_card(P0, inst);  // 神涉装置 borrows it
  CHECK(e.ci(inst).zone == Zone::Special);  // 切牌不进弃牌堆
  CHECK(e.ci(inst).holder == P1);
  CHECK(e.ci(inst).faceUp);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

// ---- 虚路 (13) ---------------------------------------------------------------

namespace {
// Drafts a fixed pair of goddesses and bans the spare one, so a specific
// two-goddess combination reaches setup_player (for the combo-ban test).
struct DraftPairAgent : Agent {
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "draft_pick") {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
        const auto& dt = r.options[static_cast<size_t>(i)].data;
        std::string g = dt.is_null() ? "" : dt.value("goddess", "");
        if (g == "himika" || g == "utsuro") d.indices.push_back(i);
      }
      for (int i = 0; i < static_cast<int>(r.options.size()) &&
                      static_cast<int>(d.indices.size()) < r.minSel;
           ++i)
        if (std::find(d.indices.begin(), d.indices.end(), i) == d.indices.end())
          d.indices.push_back(i);
    } else if (r.kind == "draft_ban") {
      int pick = 0;
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
        const auto& dt = r.options[static_cast<size_t>(i)].data;
        std::string g = dt.is_null() ? "" : dt.value("goddess", "");
        if (g != "himika" && g != "utsuro") {
          pick = i;
          break;
        }
      }
      d.indices.push_back(pick);
    } else {
      int want = std::max(0, r.minSel);
      for (int i = 0; i < static_cast<int>(r.options.size()) &&
                      static_cast<int>(d.indices.size()) < want;
           ++i)
        if (r.options[static_cast<size_t>(i)].enabled) d.indices.push_back(i);
    }
    return d;
  }
};
}  // namespace

TEST_CASE("虚路: 灰尘 (虚>=12) empowers 圆月") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "utsuro", "圆月");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.st.distance = 6;
  Attack plain = e.make_attack(P0, inst, false, false);
  CHECK(plain.range.contains(5));
  CHECK_FALSE(plain.range.contains(4));
  CHECK(plain.aura.has_value());
  CHECK(plain.life.value_or(0) == 2);

  e.st.dust += 12;  // 灰尘 (test setup)
  Attack jin = e.make_attack(P0, inst, false, false);
  CHECK(jin.range.contains(4));            // 距离扩大（近1）
  CHECK_FALSE(jin.aura.has_value());       // 对装伤害变为 -
  CHECK(jin.life.value_or(0) == 2);
}

TEST_CASE("铳镰组合禁用真红凶弹 via the combo-ban table") {
  for (int withBan = 0; withBan < 2; ++withBan) {
    Config cfg = make_cfg("standard", 5);
    cfg.preset = "kigen-full";
    cfg.enabledGoddesses = {"himika", "utsuro", "yurina"};
    Engine e(cfg);
    e.load_manifest(find_file("content/packs.json"));
    if (withBan) e.cfg.comboBans.push_back({"himika", "utsuro", "真红凶弹"});
    DraftPairAgent a;
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    e.setup_match();
    bool found = false;
    for (const auto& ci : e.st.insts)
      if (e.def_of(ci.inst).name == "真红凶弹") found = true;
    INFO("withBan=", withBan);
    CHECK(found == (withBan == 0));
  }
}

TEST_CASE("影飞翅 raises 距 and 达人距离 until end of turn") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "utsuro", "影飞翅");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 5;
  CHECK(e.distance() == 5);
  CHECK(e.near_distance() == 2);
  e.play_card(P0, inst, false);
  CHECK(e.distance() == 7);
  CHECK(e.near_distance() == 4);
}

TEST_CASE("虚伪 reduces the 纳 of the opponent's newly expanded 付与") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xu = find_def(e, "utsuro", "虚伪");
  REQUIRE(xu >= 0);
  int xi = e.add_instance(xu, P0);
  e.move_card(xi, Zone::Special);
  e.ci(xi).faceUp = true;
  e.ci(xi).crystals = 3;
  int en = find_def(e, "saine", "圈域");  // 【纳2】
  REQUIRE(en >= 0);
  int ei = e.add_instance(en, P1);
  e.move_card(ei, Zone::Hand);
  e.st.active = P1;
  e.st.distance -= 4;
  e.st.dust += 4;  // fund the 献
  e.play_card(P1, ei, false);
  CHECK(e.ci(ei).zone == Zone::Enhance);
  CHECK(e.ci(ei).crystals == 1);  // 纳2 - 1
}

TEST_CASE("夙愿 grants immunity to all damage") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int w = find_def(e, "utsuro.A1", "夙愿");
  REQUIRE(w >= 0);
  int inst = e.add_instance(w, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  CHECK(e.has_damage_immunity(P0));
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 0;
  e.deal_damage(P0, std::nullopt, 3, 0);
  CHECK(e.st.p[P0].life == 10);
  e.deal_damage(P0, 3, std::nullopt, 0);
  CHECK(e.st.p[P0].aura == 3);
  CHECK_FALSE(e.has_damage_immunity(P1));
}

TEST_CASE("终末 empties itself when its controller takes attack damage") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "utsuro", "终末");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 3;
  e.st.p[P0].aura = 3;
  e.st.distance = 5;
  e.st.active = P1;
  int dust0 = e.st.dust;
  Attack atk;
  atk.attacker = P1;
  atk.sourceInst = -1;
  atk.range.add(0, 10);
  atk.aura = 2;
  e.resolve_attack(atk);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.st.dust == dust0 + 3 + 2);  // 3 献 -> 虚, plus the 2 aura damage
  CHECK(e.ci(inst).zone == Zone::Special);  // 切札付与 stays used
}

TEST_CASE("灰灭 costs 24 minus the current 虚") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "utsuro", "灰灭");
  REQUIRE(def >= 0);
  e.st.dust = 10;
  CHECK(e.cut_cost(P0, def, -1) == 14);
  e.st.dust = 30;
  CHECK(e.cut_cost(P0, def, -1) == 0);
}

// ---- 仄佳 (14) ---------------------------------------------------------------

TEST_CASE("仄佳: 绽放 replaces the card with its EX upgrade") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "honoka", "精灵式");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 5;
  e.play_card(P0, inst, false);
  CHECK(e.ci(inst).zone == Zone::Removed);  // 绽放: 移出这张牌
  bool upgraded = false;
  for (const auto& c : e.st.insts)
    if (e.def_of(c.inst).name == "守护灵式") upgraded = true;
  CHECK(upgraded);  // 获得「守护灵式」
}

namespace {
// Picks the "装到这张牌上" answer for 双掌生花's 装附 redirect question.
struct AuraRedirectAgent : Agent {
  Decision decide(const Request& r) override {
    Decision d;
    if (r.prompt.find("装附：") != std::string::npos && r.options.size() >= 2) {
      d.indices.push_back(static_cast<int>(r.options.size()) - 1);
      return d;
    }
    int want = std::max(0, r.minSel);
    for (int i = 0; i < static_cast<int>(r.options.size()) &&
                    static_cast<int>(d.indices.size()) < want;
         ++i)
      if (r.options[static_cast<size_t>(i)].enabled) d.indices.push_back(i);
    return d;
  }
};
}  // namespace

TEST_CASE("仄佳: 双掌生花 blooms into 新幕来临 at exactly 5 crystals") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AuraRedirectAgent a;  // redirects each 装附 onto the card
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "honoka", "双掌生花");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.st.dust += 5;  // fuel for 装附
  e.st.p[P0].flare = 0;
  for (int k = 0; k < 5; ++k) e.do_basic(P0, BasicAction::Aura);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.st.p[P0].flare == 5);  // 恰好5 → 全部移到自气
  CHECK(e.ci(inst).zone == Zone::Removed);
  bool next_act = false;
  for (int i : e.ps(P0).special)
    if (e.def_of(i).name == "新幕来临") next_act = true;
  CHECK(next_act);
}

TEST_CASE("仄佳: 新幕来临 attack damage is the count of 5-crystal areas") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  // areas with exactly 5: P0 aura(=5), distance(=5) -> 2
  e.st.p[P0].aura = 5;
  e.st.distance = 5;
  CHECK(e.areas_with(5) == 2);
  e.st.p[P1].life = 5;
  CHECK(e.areas_with(5) == 3);
}

TEST_CASE("仄佳: 漫天的花道 returns removed 献 to the controller") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "honoka", "漫天的花道");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.st.distance -= 5;
  e.ci(inst).crystals = 5;
  e.st.p[P0].aura = 1;  // room for the first removal
  e.st.p[P0].flare = 0;
  e.consume_enhance_crystal(inst);
  CHECK(e.st.p[P0].aura == 2);       // 结晶 -> 装
  e.st.p[P0].aura = 5;               // 装已满
  e.consume_enhance_crystal(inst);
  CHECK(e.st.p[P0].flare == 1);      // 改为 -> 气
  CHECK(e.ci(inst).crystals == 3);
}

TEST_CASE("仄佳: 熠熠见繁樱 uses the crystals sitting on it") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "honoka", "熠熠见繁樱");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).crystals = 3;
  Attack atk = e.make_attack(P0, inst, false, false);
  CHECK(atk.aura.value_or(0) == 3);
  CHECK(atk.life.value_or(0) == 2);
  CHECK((atk.keywords & AF_Overwhelm) != 0);
}

TEST_CASE("仄佳: 踽踽虚路行 removes itself and skips the opponent's main phase") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "honoka", "踽踽虚路行");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = false;
  e.ps(P0).flare = 3;
  e.st.active = P0;
  e.play_card(P0, inst, false);
  CHECK(e.ps(P1).skipMainPhase);
  CHECK(e.ci(inst).zone == Zone::Removed);
}

TEST_CASE("仄佳: 四季轮回 resets when a 追加牌 leaves the 追加牌区") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int ji = find_def(e, "honoka", "四季轮回");
  int xi = find_def(e, "honoka", "心胸所念");
  REQUIRE(ji >= 0);
  REQUIRE(xi >= 0);
  int j = e.add_instance(ji, P0);
  e.move_card(j, Zone::Special);
  e.ci(j).faceUp = true;
  e.ci(j).crystals = 0;
  int x = e.add_instance(xi, P0);
  e.move_card(x, Zone::Special);
  e.ci(x).faceUp = false;
  e.ps(P0).flare = 5;
  e.st.active = P0;
  e.play_card(P0, x, false);  // 心胸所念 → 绽放, gain_extra fires extra_gained
  CHECK_FALSE(e.ci(j).faceUp);  // 四季轮回 returned to unused
  CHECK(e.ps(P0).flare == 0);   // 心胸所念 cost 5
  bool gained = false;
  for (int i : e.ps(P0).special)
    if (e.def_of(i).name == "双掌生花") gained = true;
  CHECK(gained);
}

namespace {
struct CountingAgent : Agent {
  int orderPrompts = 0;
  Decision decide(const Request& r) override {
    if (r.prompt.find("选择下一个结算的触发") != std::string::npos) orderPrompts++;
    Decision d;
    int want = std::max(1, r.minSel);
    for (int i = 0; i < static_cast<int>(r.options.size()) &&
                    static_cast<int>(d.indices.size()) < want;
         ++i)
      if (r.options[static_cast<size_t>(i)].enabled) d.indices.push_back(i);
    return d;
  }
};
}  // namespace

TEST_CASE("simultaneous end-phase triggers are ordered by the current player") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  CountingAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int c1 = find_def(e, "honoka", "指挥");     // turn_end: attack 【1-5 1/1】不可对
  int c2 = find_def(e, "kururu", "大～魔像");  // turn_end trigger
  REQUIRE(c1 >= 0);
  REQUIRE(c2 >= 0);
  int i1 = e.add_instance(c1, P0);
  e.move_card(i1, Zone::Special);
  e.ci(i1).faceUp = true;
  e.ci(i1).crystals = 3;
  int i2 = e.add_instance(c2, P0);
  e.move_card(i2, Zone::Special);
  e.ci(i2).faceUp = true;
  e.st.active = P0;
  e.fire("turn_end", P0);
  CHECK(a.orderPrompts >= 1);  // the active player chose the order
}

// ---- 凝努 (15) ---------------------------------------------------------------

TEST_CASE("凝努: 冻结 fills 装 slots and blocks 装附/前进") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P1].aura = 0;
  e.st.p[P1].flare = 0;
  e.st.p[P1].life = 10;
  e.st.distance = 5;
  CHECK(e.aura_free(P1) == 5);
  CHECK_FALSE(e.frozen(P1));
  CHECK(e.freeze(P1, 2) == 2);          // 2 ice crystals
  CHECK(e.ice_count(P1) == 2);
  CHECK(e.frozen(P1));
  CHECK(e.aura_free(P1) == 3);
  CHECK_FALSE(e.armor_full(P1));
  // 装 3 个樱花结晶后装被填满（樱花 + 冰晶共用上限）
  e.st.distance -= 4;   // distance 5 -> 1, near 2 -> advance illegal by distance;
  e.add_crystals(AreaRef::aura(P1), 3);
  e.st.distance = 4;
  CHECK(e.ps(P1).aura == 3);
  CHECK(e.armor_full(P1));
  CHECK(e.freeze(P1, 1) == 0);          // 无空位
  CHECK_FALSE(e.basic_legal(P1, BasicAction::Aura));
  CHECK_FALSE(e.basic_legal(P1, BasicAction::Advance));
}

TEST_CASE("凝努: 被冻结时 聚气 改为移除冰晶") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P0].aura = 3;
  e.freeze(P0, 2);
  CHECK(e.basic_legal(P0, BasicAction::Flare));
  CHECK(e.do_basic(P0, BasicAction::Flare));
  CHECK(e.ice_count(P0) == 1);          // 冰晶 -1
  CHECK(e.ps(P0).aura == 3);            // 装不变
  CHECK(e.ps(P0).flare == 0);
}

TEST_CASE("凝努: 冰晶不能承伤（不算装）") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P1].aura = 0;
  e.st.p[P1].life = 10;
  e.freeze(P1, 3);
  CHECK(e.ice_count(P1) == 3);
  CHECK(e.effective_armor(P1) == 0);    // 3 冰晶不承伤
  e.deal_damage(P1, 3, 1, 0);  // 装不足 3 → 只能以命承伤（1 点）
  CHECK(e.st.p[P1].life == 9);
  CHECK(e.st.p[P1].aura == 0);  // 冰晶没有被当作装消耗
  CHECK(e.ice_count(P1) == 3);
}

TEST_CASE("凝努: 冰凌包覆 makes own 冰晶 count as 装") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "konuru.A1", "冰凌包覆");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.st.p[P0].aura = 0;
  e.freeze(P0, 2);
  CHECK(e.effective_armor(P0) == 2);    // 冰晶视作装
}

TEST_CASE("凝努: 冻僵 blocks the opponent's 聚气") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "konuru", "冻僵");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.st.p[P1].aura = 3;
  CHECK_FALSE(e.basic_legal(P1, BasicAction::Flare));
  CHECK(e.basic_legal(P0, BasicAction::Flare));
}

TEST_CASE("凝努: 残烛式 纳 is dynamic (6 + 敌命 - 双方冰晶)") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "konuru.A1", "残烛式");
  REQUIRE(def >= 0);
  e.st.p[P1].life = 4;
  CHECK(e.nagi_value(def, P0, -1) == 10);   // 6 + 4 - 0
  e.freeze(P0, 2);
  e.freeze(P1, 1);
  CHECK(e.nagi_value(def, P0, -1) == 7);    // 6 + 4 - 3
}

TEST_CASE("凝努: 吹雪式 即再起 triggers when the opponent's 装 becomes full") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "konuru", "吹雪式");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;  // 使用后
  e.st.p[P1].aura = 4;
  e.ice_count(P1);  // no-op
  e.freeze(P1, 1);  // 敌装变满（4+1=5）
  CHECK_FALSE(e.ci(inst).faceUp);   // 回到未使用
}

TEST_CASE("凝努: 在此旗 redirects damage crystals onto the chosen 付与") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int qi = find_def(e, "honoka", "在此旗的名义之下");
  int ho = find_def(e, "honoka", "指挥");
  REQUIRE(qi >= 0);
  REQUIRE(ho >= 0);
  int host = e.add_instance(ho, P0);
  e.move_card(host, Zone::Enhance);
  e.ci(host).crystals = 3;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.st.p[P1].flare = 0;
  e.st.active = P0;
  e.st.distance = 5;
  e.ps(P0).flare = 10;
  int atk = e.add_instance(qi, P0);
  e.move_card(atk, Zone::Special);
  e.ci(atk).faceUp = false;
  const int dust0 = e.st.dust;
  e.play_card(P0, atk, false);
  // FirstAgent answers "是" to the redirect question.
  CHECK(e.ci(host).crystals == 3 + 3);   // 3 aura damage crystals went onto the card
  CHECK(e.st.dust == dust0 + 4);         // only the 4-flare 切札 cost reached 虚
  CHECK(e.st.p[P1].aura == 2);
}

// ---- 八叶 (16) ---------------------------------------------------------------

TEST_CASE("八叶: 镜映 counts areas equal to the opponent's") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P0].aura = 3; e.st.p[P1].aura = 3;   // same
  e.st.p[P0].flare = 2; e.st.p[P1].flare = 5; // differ
  e.st.p[P0].life = 10; e.st.p[P1].life = 10; // same
  CHECK(e.mirror(P0) == 2);
  CHECK(e.mirror(P1) == 2);
  e.st.p[P1].life = 9;
  CHECK(e.mirror(P0) == 1);
  CHECK(e.mirror(P1) == 1);
}

TEST_CASE("八叶: 昏神颚 damage uses 镜映") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yatsuha", "昏神颚");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.st.p[P0].flare = 1; e.st.p[P1].flare = 1;
  e.st.p[P0].aura = 2; e.st.p[P1].aura = 2;
  e.st.p[P0].life = 7; e.st.p[P1].life = 7;
  CHECK(e.mirror(P0) == 3);
  Attack atk = e.make_attack(P0, inst, false, false);
  CHECK(atk.life.value_or(0) == 4);   // 1 + 3
  CHECK(atk.aura.value_or(0) == 3);
}

TEST_CASE("八叶: 完全态 upgrades a card in place") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yatsuha", "星云爪");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  CHECK(e.can_upgrade(inst));
  CHECK(e.upgrade_card(inst));
  CHECK(e.def_of(inst).name == "星辰之利爪");   // 完全态
  CHECK(e.ci(inst).zone == Zone::Hand);         // 区域不变
  CHECK_FALSE(e.can_upgrade(inst));             // 已是完全态
}

TEST_CASE("八叶: 寄花 drains 镜映 crystals when expanded (破绽)") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yatsuha", "寄花");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  // 让 纳3 从虚支付，装保持 0，使结算时 镜映 == 3
  e.st.p[P0].flare = 0; e.st.p[P0].aura = 0; e.st.p[P0].life = 10;
  e.st.p[P1].flare = 0; e.st.p[P1].aura = 0; e.st.p[P1].life = 10;
  e.st.distance = 5;
  e.st.distance -= 3;
  e.st.dust += 3;
  CHECK(e.mirror(P0) == 3);
  e.st.active = P0;
  e.play_card(P0, inst, false);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.ci(inst).zone == Zone::Discard);   // 破绽: 无结晶即弃置
}

TEST_CASE("八叶: AA1 is a third form with a single 切札 line") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "gachi-full";  // 异相 on
  Engine e(cfg);
  load_all_content(e);
  auto forms = e.available_forms("yatsuha");
  CHECK(std::find(forms.begin(), forms.end(), "O") != forms.end());
  CHECK(std::find(forms.begin(), forms.end(), "A1") != forms.end());
  CHECK(std::find(forms.begin(), forms.end(), "AA1") != forms.end());
  // A1 / AA1 只有 1 张切牌（O 有 4 张）
  auto o = e.deck_def_ids("yatsuha", "O");
  auto a1 = e.deck_def_ids("yatsuha", "A1");
  auto aa1 = e.deck_def_ids("yatsuha", "AA1");
  auto specials = [&](const std::vector<int>& v) {
    int n = 0;
    for (int d : v)
      if (e.def(d).kind == CardKind::Special) n++;
    return n;
  };
  CHECK(specials(o) == 4);
  CHECK(specials(a1) == 1);
  CHECK(specials(aa1) == 1);
}

TEST_CASE("八叶: 回忆区 hides contents from the opponent") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yatsuha", "星云爪");
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.to_memory(inst);
  CHECK(e.ci(inst).zone == Zone::Memory);
  CHECK(e.memory_size(P0) == 1);
  auto obs_self = e.observation(P0);
  auto obs_opp = e.observation(P1);
  CHECK(obs_self["players"][0]["memory"].size() == 1);
  CHECK(obs_opp["players"][0]["memoryCount"] == 1);
  CHECK(obs_opp["players"][0].contains("memory") == false);  // 对对手保密
  CHECK(e.memory_draw(P0, 1) == 1);
  CHECK(e.ci(inst).zone == Zone::Hand);
}

TEST_CASE("八叶: 双叶镜的祟神 precisely copies the responded attack") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int atkDef = find_def(e, "yatsuha", "星云爪");  // 【3-4 3/2】攻击后：1自装到敌气
  REQUIRE(atkDef >= 0);
  int atkInst = e.add_instance(atkDef, P1);
  e.move_card(atkInst, Zone::Hand);
  e.st.active = P1;
  e.st.distance = 3;
  e.st.p[P1].aura = 3; e.st.p[P1].flare = 0; e.st.p[P1].life = 10;
  e.st.p[P0].aura = 2; e.st.p[P0].flare = 0; e.st.p[P0].life = 5;  // 自命 < 敌命
  e.st.dust = 0;
  Attack atk = e.make_attack(P1, atkInst, false, true);
  e.currentResponding = &atk;
  int shen = find_def(e, "yatsuha", "双叶镜的祟神");
  REQUIRE(shen >= 0);
  int si = e.add_instance(shen, P0);
  e.move_card(si, Zone::Special);
  e.ci(si).faceUp = false;
  e.ps(P0).flare = 10;
  e.play_card(P0, si, true, false);
  CHECK(atk.negated);                       // 原攻击被对应掉
  CHECK(e.st.p[P1].aura == 0);              // 复制品 3 点装伤命中
  CHECK(e.st.p[P0].aura == 1);              // 复制的"攻击后：1自装到敌气"也执行了
  CHECK(e.st.p[P1].flare == 1);
}

TEST_CASE("八叶: 散樱代的旅途 advances on both players' turns") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int trav = find_def(e, "yatsuha.AA1", "散樱代的旅途");
  REQUIRE(trav >= 0);
  int ti = e.add_instance(trav, P0);
  e.move_card(ti, Zone::Special);
  e.ci(ti).faceUp = true;
  e.st.active = P0;
  e.play_card(P0, ti, false);   // 选择起点奖励（FirstAgent 选第 1 项）→ k = 1
  // 旅途中 on_play 自己也算一次；此后每个回合（双方）推进
  const int k0 = 1;
  (void)k0;
  e.st.active = P1;
  e.fire("turn_start", P1);
  e.fire("turn_start", P0);
  // 两次 turn_start 都应推进：k 从 1 → 3（未触发终幕）
  CHECK(e.lua_error_count() == 0);
  CHECK(e.ci(ti).zone == Zone::Special);   // 仍在场（未到终幕）
}

// ---- 神居 (21) ---------------------------------------------------------------

namespace {
// Always answers a response request with the last option (i.e. actually responds).
struct RespondAgent : Agent {
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "response" && r.options.size() > 1) {
      d.indices.push_back(static_cast<int>(r.options.size()) - 1);
      return d;
    }
    int want = std::max(1, r.minSel);
    for (int i = 0; i < static_cast<int>(r.options.size()) &&
                    static_cast<int>(d.indices.size()) < want;
         ++i)
      if (r.options[static_cast<size_t>(i)].enabled) d.indices.push_back(i);
    return d;
  }
};
}  // namespace

TEST_CASE("神居: 诅咒 >= 16 即死亡（并保持结晶守恒）") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P0].hasCurse = true;
  int total0 = 0;
  for (const auto& ci : e.st.insts) total0 += e.ci(ci.inst).crystals;
  e.add_curse(P0, 15);
  CHECK(e.curse(P0) == 15);
  CHECK_FALSE(e.st.over);
  e.add_curse(P0, 1);  // 16
  CHECK(e.st.over);
  CHECK(e.st.winner == P1);
  int total1 = 0;
  for (const auto& ci : e.st.insts) total1 += e.ci(ci.inst).crystals;
  CHECK(total1 == total0);  // 命 -> 气，不灭失
}

TEST_CASE("神居: 血飞沫 redirects crystals away from the enemy's 装") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int hp = find_def(e, "kamuwi", "血飞沫");
  REQUIRE(hp >= 0);
  int hi = e.add_instance(hp, P0);
  e.move_card(hi, Zone::Enhance);
  e.ci(hi).crystals = 2;
  e.st.p[P1].aura = 0;
  e.st.distance -= 3;
  e.st.dust += 3;  // 供 3 个结晶从虚出发
  const int dust0 = e.st.dust;
  e.move_crystals(AreaRef::dust(), AreaRef::aura(P1), 3, true);
  CHECK(e.st.p[P1].aura == 0);          // 没有进入敌装
  CHECK(e.st.dust == dust0 + 1);        // 该牌上 1 个献移到虚
  CHECK(e.ci(hi).crystals == 1);
}

TEST_CASE("神居: 阡 keeps the opponent alive until it is discarded") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int sen = find_def(e, "kamuwi", "阡");
  REQUIRE(sen >= 0);
  int si = e.add_instance(sen, P0);
  e.move_card(si, Zone::Enhance);
  e.ci(si).crystals = 4;
  e.st.p[P1].life = 0;
  e.check_win();
  CHECK_FALSE(e.st.over);              // 对手不会死亡
  e.move_card(si, Zone::Discard);      // 本牌弃置
  e.check_win();
  CHECK(e.st.over);
  CHECK(e.st.winner == 0);
}

TEST_CASE("神居: 晓 prevents the response and shrinks by -1/+0") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  RespondAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xiao = find_def(e, "kamuwi", "晓");       // 【3-7 6/4】超克, 防止对应
  int bi = find_def(e, "utsuro", "影之壁");      // 通常的对应牌
  REQUIRE(xiao >= 0);
  REQUIRE(bi >= 0);
  int xi = e.add_instance(xiao, P0);
  e.move_card(xi, Zone::Special);
  e.ci(xi).faceUp = false;
  e.ps(P0).flare = 10;
  int ri = e.add_instance(bi, P1);
  e.move_card(ri, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 5;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.st.p[P1].flare = 3;
  e.play_card(P0, xi, false);
  CHECK(e.ci(ri).zone == Zone::Discard);   // 用于对应的牌进入弃牌堆
  CHECK(e.st.p[P1].aura == 0);             // 6/4 变 5/4 → 装可以承伤
  CHECK(e.st.p[P1].life == 10);            // 因此没有受到命伤
  CHECK(e.ci(xi).zone == Zone::Removed);   // 攻击后将晓移出游戏
}
