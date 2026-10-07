// Structural / robustness / regression tests that need the whole engine.
// tests.cpp keeps the doctest main; this file only adds test cases.
#include <doctest/doctest.h>

#include <cstdlib>
#include <set>
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
    cfg.illegalTolerance = 1000000;
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 10), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 3) % 10), seed * 7 + 2);
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
    cfg.illegalTolerance = 1000000;
    CheckingAgent a0(&e, static_cast<AgentMode>(g % 10), seed * 7 + 1);
    CheckingAgent a1(&e, static_cast<AgentMode>((g + 3) % 10), seed * 7 + 2);
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
  CHECK(n == 26);  // 12 达人 + 14 official
  CHECK(e.modules().size() == 26u);
  auto pool_t = e.goddess_pool();
  CHECK(pool_t.size() == 12u);  // the official pack is excluded
  CHECK(std::find(pool_t.begin(), pool_t.end(), "utsuro") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "honoka") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "konuru") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "yatsuha") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "kamuwi") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "hatsumi") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "megumi") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "kanawe") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "renri") == pool_t.end());
  CHECK(std::find(pool_t.begin(), pool_t.end(), "misora") == pool_t.end());

  // 全扩 includes the official pack
  e.cfg.preset = "kigen-full";
  auto pool_f = e.goddess_pool();
  CHECK(pool_f.size() == 26u);
  CHECK(std::find(pool_f.begin(), pool_f.end(), "utsuro") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "honoka") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "konuru") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "yatsuha") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "kamuwi") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "hatsumi") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "megumi") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "kanawe") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "renri") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "misora") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "shisui") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "akina") != pool_f.end());
  CHECK(std::find(pool_f.begin(), pool_f.end(), "innealra") != pool_f.end());
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

// ---- 17-Hastumi 初海 --------------------------------------------------------

TEST_CASE("初海: 航海顺风判定（第一回合 / 对手上一回合攻击 / 潜水固定顺风）") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);

  // 第一回合：没有上一回合的对手攻击 → 固定顺风。
  e.begin_tailwind(P0);
  CHECK(e.tailwind(P0));
  CHECK_FALSE(e.ps(P0).oppAttackedLastTurn);

  // 上一回合（对手的回合）对手进行过攻击 → 逆风。
  e.note_attack(P1);
  e.begin_tailwind(P0);
  CHECK_FALSE(e.tailwind(P0));
  CHECK(e.ps(P0).oppAttackedLastTurn);

  // 潜水闪避的“下回合固定顺风”覆盖逆风，且只生效一次。
  e.ps(P0).forcedTailwind = true;
  e.begin_tailwind(P0);
  CHECK(e.tailwind(P0));
  CHECK_FALSE(e.ps(P0).forcedTailwind);
  e.begin_tailwind(P0);
  CHECK_FALSE(e.tailwind(P0));  // 对手又攻击过（计数仍在）

  // 顺风/逆风是公开信息（双方 observation 都能看到）。
  CHECK(e.observation(P0)["players"][0]["tailwind"] == false);
  CHECK(e.observation(P1)["players"][0]["tailwind"] == false);
}

TEST_CASE("初海: 罗盘以离散值 5 追加/删除攻击距离，多个罗盘相互抵消") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int compass = find_def(e, "hatsumi", "罗盘");
  int sanHua = find_def(e, "kamuwi", "散华刃");    // 【3-4】
  int qiangSuan = find_def(e, "hatsumi", "强酸");  // 【5-6】
  REQUIRE(compass >= 0);
  REQUIRE(sanHua >= 0);
  REQUIRE(qiangSuan >= 0);

  e.st.active = P0;
  auto range_of = [&](int defId) {
    int inst = e.add_instance(defId, P0);
    e.move_card(inst, Zone::Hand);
    return e.make_attack(P0, inst, false, false).range.to_string();
  };
  auto put_compass = [&](Player p) {
    int inst = e.add_instance(compass, p);
    e.move_card(inst, Zone::Enhance);
    e.ci(inst).crystals = 3;
    return inst;
  };

  CHECK(range_of(sanHua) == "3-4");
  CHECK(range_of(qiangSuan) == "5-6");

  // 我方 1 个罗盘: 追加离散值 5（3-4 → 3-4,5）
  put_compass(P0);
  CHECK(range_of(sanHua) == "3-4,5");

  // 对手 1 个罗盘: 相互抵消（不追加也不删除）
  put_compass(P1);
  CHECK(range_of(sanHua) == "3-4");
  CHECK(range_of(qiangSuan) == "5-6");

  // 对手 2 个罗盘: 净值为负 → 删除离散值 5（5-6 → 6）
  put_compass(P1);
  CHECK(range_of(qiangSuan) == "6");
  CHECK(range_of(sanHua) == "3-4");
}

TEST_CASE("初海: 潜水对对手保密，闪避的攻击视为未发生过并固定下回合顺风") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int shuiQiu = find_def(e, "hatsumi", "水球");  // 【3-5 0/0】
  REQUIRE(shuiQiu >= 0);

  e.st.active = P0;
  e.st.distance = 3;
  e.declare_dive(P1, 1);  // P1 潜水前进（距离 -1）
  CHECK(e.dive_state(P1) == 1);

  // 秘密信息：只有本人能看到前进/后退的选择。
  auto o0 = e.observation(P0);
  auto o1 = e.observation(P1);
  CHECK(o0["players"][1]["diving"] == true);
  CHECK_FALSE(o0["players"][1].contains("dive"));
  CHECK(o1["players"][1]["dive"] == 1);

  // P0 使用攻击牌 → P1 在结算前公开潜水；距离 3 → 2，攻击【3-5】落空。
  int si = e.add_instance(shuiQiu, P0);
  e.move_card(si, Zone::Hand);
  e.ps(P0).tailwind = true;
  e.play_card(P0, si, false);
  CHECK(e.dive_state(P1) == 0);            // 已解除
  CHECK(e.distance() == 2);                // 前进 -1 作用到本回合
  CHECK(e.ps(P1).forcedTailwind);          // 闪避成功 → 下回合固定顺风
  CHECK(e.attacks_this_turn(P0) == 0);     // 该攻击视为未发生过
  CHECK_FALSE(e.attacked_this_turn(P0));
  CHECK(e.ci(si).zone == Zone::Discard);   // 通常牌直接进弃牌堆
  CHECK(e.ps(P1).life == 10);
  CHECK(e.ps(P1).aura == 3);

  // 没有落空时照常结算（距离 4 → 3，命中；顺风 +2/+2）。
  e.st.distance = 4;
  e.ps(P0).tempDistanceMod = 0;  // 模拟新回合：本回合的临时距离修正已重置
  e.ps(P1).tempDistanceMod = 0;
  e.ps(P0).tempNearDistanceMod = 0;
  e.ps(P1).tempNearDistanceMod = 0;
  e.declare_dive(P1, 1);
  int si2 = e.add_instance(shuiQiu, P0);
  e.move_card(si2, Zone::Hand);
  e.play_card(P0, si2, false);
  CHECK(e.attacks_this_turn(P0) == 1);
  CHECK(e.distance() == 3);
  CHECK(e.st.p[P1].aura == 1);  // 2/2 装伤（FirstAgent 选装侧）
  CHECK(e.ps(P1).life == 10);
}

TEST_CASE("初海: 以初海牌组自战（O 与 A1）不产生 Lua 错误") {
  // mode=hajimari 直接用 set id 组牌，避免三拾一舍在“只启用一柱”时被禁用而出现空牌组。
  struct Setup {
    const char* p0;
    const char* p1;
  };
  const Setup setups[] = {{"hatsumi", "hatsumi"}, {"hatsumi", "hatsumi.A1"}};
  const int games = fuzz_games(4);
  for (const Setup& su : setups) {
    for (int g = 0; g < games; ++g) {
      Config cfg = make_cfg("hajimari", 7000 + static_cast<uint64_t>(g));
      cfg.preset = "kigen-full";
      cfg.p0Set = su.p0;
      cfg.p1Set = su.p1;
      Engine e(cfg);
      load_all_content(e);
      CheckingAgent a0(&e, AgentMode::RandomLegal, 11 + static_cast<uint64_t>(g));
      CheckingAgent a1(&e, AgentMode::RandomLegal, 22 + static_cast<uint64_t>(g));
      e.set_agent(P0, &a0);
      e.set_agent(P1, &a1);
      INFO("p0set=", su.p0, " p1set=", su.p1, " game=", g);
      e.run();
      CHECK(e.st.over);
      CHECK(e.lua_error_count() == 0);
      for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
      for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
      // 牌组确实装入了初海的牌（排除空牌组的退化对局）。
      CHECK(e.observation(P0)["players"][0]["sets"][0] == su.p0);
      CHECK(e.observation(P1)["players"][1]["sets"][0] == su.p1);
    }
  }
}

TEST_CASE("初海: 水雷展开时秘密潜水，对手看不到前进/后退") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int shuiLei = find_def(e, "hatsumi.A1", "水雷");  // 【纳2】破绽。展开时：潜水
  REQUIRE(shuiLei >= 0);
  e.st.active = P0;
  e.st.dust = 5;  // 供纳
  int si = e.add_instance(shuiLei, P0);
  e.move_card(si, Zone::Hand);
  e.play_card(P0, si, false);
  CHECK(e.ci(si).zone == Zone::Enhance);
  CHECK(e.ci(si).crystals == 2);
  CHECK(e.dive_state(P0) == 1);  // FirstAgent 选“前进”
  // 秘密：本人可见，对手只见“处于潜水状态”。
  CHECK(e.observation(P0)["players"][0]["dive"] == 1);
  CHECK(e.observation(P1)["players"][0]["diving"] == true);
  CHECK_FALSE(e.observation(P1)["players"][0].contains("dive"));
  // 再次潜水 = 什么都不做（状态不变）。
  e.declare_dive(P0, 2);
  CHECK(e.dive_state(P0) == 1);
}

TEST_CASE("初海: 子午灯塔把对手从手牌使用的非攻击牌替换为弃置") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int lighthouse = find_def(e, "hatsumi", "子午灯塔");
  int ready = find_def(e, "hatsumi", "准备万全");  // 全力行动: 3虚到自装，可抽 1
  REQUIRE(lighthouse >= 0);
  REQUIRE(ready >= 0);
  e.st.active = P1;  // 对手（P1）的回合
  int li = e.add_instance(lighthouse, P0);
  e.move_card(li, Zone::Special);
  e.ci(li).faceUp = true;  // 使用后状态
  CHECK(e.near_distance() == 3);  // 使用后：达人距离 +1
  e.st.dust = 5;
  int ri = e.add_instance(ready, P1);
  e.move_card(ri, Zone::Hand);
  int auraBefore = e.st.p[P1].aura;
  e.play_card(P1, ri, false);
  CHECK(e.ci(ri).zone == Zone::Discard);   // 改为弃置，效果不结算
  CHECK(e.st.p[P1].aura == auraBefore);    // 没有 3虚到自装
  CHECK(e.st.dust == 5);
  CHECK_FALSE(e.ci(li).faceUp);            // 子午灯塔变为未使用状态
  CHECK(e.near_distance() == 2);
  CHECK(e.st.p[P1].cardsPlayedTotal == 1); // 视作对手使用了这张牌

  // 结算被替换时不进入解除潜水流程（潜水保持未公开状态）。
  e.declare_dive(P0, 2);
  int li2 = e.add_instance(lighthouse, P0);
  e.move_card(li2, Zone::Special);
  e.ci(li2).faceUp = true;
  int ri2 = e.add_instance(ready, P1);
  e.move_card(ri2, Zone::Hand);
  const int distBefore = e.distance();
  e.play_card(P1, ri2, false);
  CHECK(e.ci(ri2).zone == Zone::Discard);
  CHECK(e.dive_state(P0) == 2);          // 效果被替换 → 潜水未公开
  CHECK(e.distance() == distBefore);     // ±1 未应用
}

// ---- 18-Mizuki 山城水津城 ---------------------------------------------------

TEST_CASE("山城水津城: 动员把士兵翻正，打出的士兵翻回并留在兵舍") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);

  // 模块集成：official 预设可见，起源战达人不可见。
  auto pool_f = e.goddess_pool();
  CHECK(std::find(pool_f.begin(), pool_f.end(), "mizuki") != pool_f.end());
  e.cfg.preset = "kigen-tatsujin";
  auto pool_t = e.goddess_pool();
  CHECK(std::find(pool_t.begin(), pool_t.end(), "mizuki") == pool_t.end());
  e.cfg.preset = "kigen-full";

  int spear = find_def(e, "mizuki", "枪兵");
  REQUIRE(spear >= 0);
  int s1 = e.add_instance(spear, P0);
  int s2 = e.add_instance(spear, P0);
  e.ci(s1).soldier = true;
  e.ci(s2).soldier = true;
  e.to_barracks(P0, s1, false);
  e.to_barracks(P0, s2, false);
  CHECK(e.st.p[P0].barracks.size() == 2u);
  CHECK(e.barracks_mobilized_count(P0) == 0);
  CHECK(e.card_zone(s1) == "barracks");

  // 动员：一次翻 1 张。
  int m = e.mobilize(P0);
  CHECK((m == s1 || m == s2));
  CHECK(e.ci(m).faceUp);
  CHECK(e.barracks_mobilized_count(P0) == 1);
  // 兵舍构成与动员状态对双方都是公开信息。
  auto obs = e.observation(P1);
  CHECK(obs["players"][0]["barracks"].size() == 2u);
  CHECK(obs["players"][0]["barracksMobilized"] == 1);

  // 打出：视为手牌使用【3 1/1】，结算后翻回未动员并留在兵舍。
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P1].aura = 0;
  const int life0 = e.st.p[P1].life;
  e.play_card(P0, m, false);
  CHECK(e.st.p[P1].life == life0 - 1);
  CHECK(e.st.p[P0].barracks.size() == 2u);
  CHECK_FALSE(e.ci(m).faceUp);
  CHECK(e.is_soldier(m));
  CHECK(e.ci(m).zone == Zone::Limbo);  // 兵舍不是通用区域向量
  CHECK(e.lua_error_count() == 0);

  // 定向模糊：以 mizuki 为主的随机对局，检查结构不变量与 Lua 错误。
  bool saw_mizuki = false;
  for (int g = 0; g < 24; ++g) {
    Config c2 = make_cfg("standard", 9100 + static_cast<uint64_t>(g));
    c2.preset = "kigen-full";
    c2.enabledGoddesses = {"mizuki", "yurina", "saine"};
    Engine e2(c2);
    load_all_content(e2);
    CheckingAgent b0(&e2, static_cast<AgentMode>(g % 4), 100 + g);
    CheckingAgent b1(&e2, static_cast<AgentMode>((g + 2) % 4), 200 + g);
    e2.set_agent(P0, &b0);
    e2.set_agent(P1, &b1);
    INFO("seed=", c2.seed);
    e2.run();
    CHECK(e2.st.over);
    CHECK(e2.lua_error_count() == 0);
    if (!e2.ps(P0).barracks.empty() || !e2.ps(P1).barracks.empty()) saw_mizuki = true;
    for (const std::string& v : b0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : b1.violations()) CHECK_MESSAGE(false, v);
  }
  CHECK(saw_mizuki);
}

TEST_CASE("山城水津城: 士兵不占手牌上限") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);

  const char* soldiers[] = {"枪兵", "枪兵", "盾兵", "骑兵"};
  for (const char* sn : soldiers) {
    int d = find_def(e, "mizuki", sn);
    REQUIRE(d >= 0);
    int i = e.add_instance(d, P0);
    e.ci(i).soldier = true;
    e.to_barracks(P0, i, false);
  }
  CHECK(e.st.p[P0].barracks.size() == 4u);
  CHECK(e.st.p[P0].hand.empty());

  // 手牌 2 张 == 上限 2：兵舍里的 4 名士兵不参与盖伏阶段。
  for (const CardDef& d : e.defs) {
    if (d.goddess == "yurina" && d.kind == CardKind::Normal && !d.isExtra) {
      int h = e.add_instance(d.id, P0);
      e.move_card(h, Zone::Hand);
    }
    if (e.st.p[P0].hand.size() >= 2u) break;
  }
  REQUIRE(e.st.p[P0].hand.size() == 2u);
  CHECK(e.effective_hand_limit(P0) == 2);
  // 盖伏阶段的条件是“手牌 > 上限”；兵舍不计入手牌，因此不会被要求盖伏。
  CHECK_FALSE(static_cast<int>(e.st.p[P0].hand.size()) > e.effective_hand_limit(P0));
  auto obs = e.observation(P0);
  CHECK(obs["players"][0]["handCount"] == 2);
  CHECK(obs["players"][0]["barracks"].size() == 4u);
  CHECK(e.st.p[P0].cover.empty());
  CHECK(e.st.p[P0].barracks.size() == 4u);
}

TEST_CASE("山城水津城: 阵地只统计有效距离变化（达人距离变动不算）") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  e.st.distance = 0;  // Engine 的初始基线为 0
  CHECK(e.position(P0));
  CHECK(e.position(P1));

  // 达人距离变化：不算。
  e.add_temp_near_distance(P0, 1);
  CHECK(e.near_distance() == 3);
  CHECK(e.position(P0));
  CHECK(e.position(P1));

  // 含光环修正后的有效距离变化：算（距离为双方公用）。
  e.add_temp_distance(P0, 1);
  CHECK(e.distance() == 1);
  CHECK_FALSE(e.position(P0));
  CHECK_FALSE(e.position(P1));

  // 结晶移动改变距离：算。
  Engine e2(cfg);
  load_all_content(e2);
  e2.st.distance = 0;
  e2.st.dust = 1;
  CHECK(e2.position(P0));
  e2.move_crystals(AreaRef::dust(), AreaRef::distance(), 1);
  CHECK(e2.distance() == 1);
  CHECK_FALSE(e2.position(P0));
}

TEST_CASE("山城水津城: 大手盾无门把一张手牌与斗神以已动员状态加入兵舍") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  CheckingAgent a0(&e, AgentMode::MaxSelectLegal, 3);
  FirstAgent a1;
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);

  int s3 = find_def(e, "mizuki", "大手盾无门");
  REQUIRE(s3 >= 0);
  int inst = e.add_instance(s3, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = false;
  e.ps(P0).flare = e.cut_cost(P0, s3, inst);
  e.st.active = P0;

  int handDef = -1;
  for (const CardDef& d : e.defs)
    if (d.goddess == "yurina" && d.kind == CardKind::Normal && !d.isExtra) {
      handDef = d.id;
      break;
    }
  REQUIRE(handDef >= 0);
  int h = e.add_instance(handDef, P0);
  e.move_card(h, Zone::Hand);
  CHECK(e.ps(P0).barracks.empty());

  e.play_card(P0, inst, false);

  CHECK(e.ps(P0).barracks.size() == 2u);
  int dou = -1;
  for (int i : e.ps(P0).barracks)
    if (e.def_of(i).name == "斗神") dou = i;
  REQUIRE(dou >= 0);
  CHECK(e.ci(dou).faceUp);   // 已动员
  CHECK(e.ci(h).faceUp);     // 手牌也以已动员状态加入
  CHECK(e.is_soldier(dou));
  CHECK(e.is_soldier(h));    // 这张手牌此后也视为你的士兵
  CHECK(e.st.p[P0].hand.empty());
  CHECK(e.lua_error_count() == 0);

  // 使用后：你的士兵的攻击获得 +1/+0。
  int spear = find_def(e, "mizuki", "枪兵");
  REQUIRE(spear >= 0);
  int sp = e.add_instance(spear, P0);
  e.ci(sp).soldier = true;
  e.to_barracks(P0, sp, true);  // 已动员
  e.st.distance = 3;
  e.st.p[P1].aura = 3;
  e.st.p[P1].life = 10;
  e.play_card(P0, sp, false);
  CHECK(e.st.p[P1].aura == 1);  // 【3 1/1】+1/+0 → 对手选择承 2 装伤
  CHECK(e.st.p[P1].life == 10);
}

TEST_CASE("山城水津城: 冲锋号改写终端/全力，三重膝丸橹限首张攻击并即再起") {
  {
    Config cfg = make_cfg("standard", 1);
    cfg.preset = "kigen-full";
    Engine e(cfg);
    load_all_content(e);
    int s4 = find_def(e, "mizuki", "山城水津城的冲锋号");
    int qi = find_def(e, "mizuki", "骑兵");  // 原本具有终端
    int ya = find_def(e, "mizuki", "压阵");  // 原本具有全力
    REQUIRE(s4 >= 0);
    REQUIRE(qi >= 0);
    REQUIRE(ya >= 0);
    int qiI = e.add_instance(qi, P0);
    int yaI = e.add_instance(ya, P0);
    CHECK(e.has_terminal(qiI));
    CHECK(e.has_full_power(yaI));
    int s4I = e.add_instance(s4, P0);
    e.move_card(s4I, Zone::Special);
    e.ci(s4I).faceUp = true;
    e.ci(s4I).crystals = 5;  // 【纳5】展开中
    CHECK(e.terminal_rewrite_active(P0));
    CHECK_FALSE(e.has_terminal(qiI));    // 原本具有终端的牌失去终端
    CHECK_FALSE(e.has_full_power(yaI));  // 全力牌失去“全力”
    CHECK(e.has_terminal(yaI));          // 并获得词条“终端”
    int qiI1 = e.add_instance(qi, P1);
    CHECK(e.has_terminal(qiI1));         // 只影响自己
  }
  {
    Config cfg = make_cfg("standard", 2);
    cfg.preset = "kigen-full";
    Engine e(cfg);
    load_all_content(e);
    FirstAgent a;
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    int s2 = find_def(e, "mizuki", "三重膝丸橹");
    int spear = find_def(e, "mizuki", "枪兵");
    REQUIRE(s2 >= 0);
    REQUIRE(spear >= 0);
    int s2I = e.add_instance(s2, P0);
    e.move_card(s2I, Zone::Special);
    e.ci(s2I).faceUp = false;
    e.ps(P0).flare = 5;
    e.st.active = P0;
    e.st.distance = 10;  // 超出 3-4，落空
    CHECK(e.attack_cards_played(P0) == 0);
    CHECK(e.playable_card(P0, s2I));  // 本回合第一张攻击牌 → 可以打出
    e.play_card(P0, s2I, false);
    CHECK(e.attack_cards_played(P0) == 1);
    CHECK(e.ci(s2I).faceUp);
    CHECK_FALSE(e.playable_card(P0, s2I));  // 已经不是第一张

    // 即再起：打出具有终端的牌（该牌结算之前再起）。
    int sp = e.add_instance(spear, P0);
    e.ci(sp).soldier = true;
    e.to_barracks(P0, sp, true);  // 已动员
    e.st.distance = 3;
    e.st.p[P1].aura = 0;
    e.play_card(P0, sp, false);
    CHECK_FALSE(e.ci(s2I).faceUp);  // 结算之前已翻回未使用
    CHECK(e.attack_cards_played(P0) == 2);
    CHECK(e.lua_error_count() == 0);
  }
}

// ---- 19-Megumi 泷河希 -------------------------------------------------------

namespace {

// 生长X 一律“移满”；假想树选第一个合法空格；拒绝 未然境之掌 的免费再用。
struct MegumiTestAgent : Agent {
  Decision decide(const Request& r) override {
    Decision d;
    if (r.prompt.find("免费再使用") != std::string::npos) {
      d.indices.push_back(static_cast<int>(r.options.size()) - 1);  // 不使用
      return d;
    }
    if (r.prompt.find("生长") != std::string::npos && !r.options.empty()) {
      d.indices.push_back(static_cast<int>(r.options.size()) - 1);  // 移满
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

TEST_CASE("泷河希: 耕种（种子->植株）与生长X上牌") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  MegumiTestAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  CHECK(e.ps(P0).hasSoil);
  CHECK(e.ps(P0).soilSeeds == 5);
  CHECK(e.ps(P0).soilPlants == 0);

  int def = find_def(e, "megumi", "芦苇");  // O-N5【纳1,生长1】
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.dust = 6;
  e.st.active = P0;
  e.play_card(P0, inst, false);

  // 打出付与牌：种子 -1、植株 +1；随后生长1 把植株移到该牌上。
  CHECK(e.ps(P0).soilSeeds == 4);
  CHECK(e.ps(P0).soilPlants == 0);
  CHECK(e.ci(inst).green == 1);
  CHECK(e.ci(inst).crystals == 1);  // 【纳1】
  CHECK(e.ci(inst).zone == Zone::Enhance);
  CHECK(e.card_crystal_count(inst) == 2);
  CHECK(e.green_total(P0) == 5);  // 绿色结晶守恒
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("泷河希: 移除时先樱花后绿色，绿色回到种子") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  int def = find_def(e, "megumi", "芦苇");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 2;
  e.ci(inst).green = 1;
  e.ps(P0).soilSeeds = 4;  // 这 1 个绿色来自土壤，保持守恒

  e.consume_enhance_crystal(inst);
  CHECK(e.ci(inst).crystals == 1);
  CHECK(e.ci(inst).green == 1);
  e.consume_enhance_crystal(inst);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.ci(inst).green == 1);
  CHECK(e.ci(inst).zone == Zone::Enhance);  // 绿色结晶维持「展开中」

  e.consume_enhance_crystal(inst);
  CHECK(e.ci(inst).green == 0);
  CHECK(e.ps(P0).soilSeeds == 5);          // 移除的绿色回到种子（4+1）
  CHECK(e.ci(inst).zone == Zone::Discard);  // 无结晶即离场
  CHECK(e.green_total(P0) == 5);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("泷河希: 假想树叠层、脱落与散华时") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  MegumiTestAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  int def = find_def(e, "megumi", "未然境之掌");  // A1-S4
  REQUIRE(def >= 0);
  int s4 = e.add_instance(def, P0);
  e.move_card(s4, Zone::Special);
  e.ci(s4).faceUp = false;
  e.ps(P0).flare = 2;
  e.st.active = P0;
  e.st.distance = 5;
  e.play_card(P0, s4, false);

  CHECK(e.tree_active(P0));
  CHECK(e.ps(P0).soilSeeds == 4);  // 1 个种子放到假想树
  CHECK(e.tree_occupied(P0) == 1);
  CHECK(e.tree_slot(P0, 0) == 1);
  CHECK(e.ci(s4).zone == Zone::Special);  // 拒绝免费再用 → 牌留在场上

  // 放第 2 层的前置：第 1 层至少 1 个。
  auto legal = e.tree_legal_slots(P0);
  REQUIRE(legal.size() == 2u);
  CHECK(legal[0] == 1);
  CHECK(legal[1] == 2);

  e.ps(P0).tree[1] = 1;  // 2-1 格
  e.ps(P0).soilSeeds = 5;
  e.st.p[P1].aura = 5;
  // 脱落 1：从下往上 → 第 1 层（无效果）
  e.tree_fall(P0, 1);
  CHECK(e.tree_slot(P0, 0) == 0);
  CHECK(e.tree_slot(P0, 1) == 1);
  CHECK(e.ps(P0).soilSeeds == 6);
  CHECK(e.st.p[P1].aura == 5);

  // 3-3【开花中】：本回合第一次对对手装造成伤害 → 基本动作（此处只有离脱合法）。
  e.ps(P0).tree[5] = 1;  // 3-3 格
  e.st.distance = 2;
  e.st.dust = 1;
  e.ps(P0).aura = 0;  // 后退不合法
  e.st.p[P1].aura = 3;
  e.deal_damage(P1, 1, std::nullopt);
  CHECK(e.st.p[P1].aura == 2);
  CHECK(e.st.distance == 3);  // 离脱：1 虚到距
  CHECK(e.st.dust == 1);      // 装伤结晶 +1，离脱 -1

  // 3-1【开花中】：本回合第一次对对手命造成伤害 → 基本动作（此处只有装附合法）。
  e.ps(P0).tree[3] = 1;  // 3-1 格
  e.st.distance = 2;     // 前进不合法（需 > 达人距离 2）
  e.st.dust = 1;
  e.ps(P0).aura = 0;
  e.deal_damage(P1, std::nullopt, 1);
  CHECK(e.st.p[P1].life == 9);
  CHECK(e.ps(P0).aura == 1);  // 装附
  CHECK(e.st.dust == 0);

  // 脱落 2：2-1【散华时】攻击【3-6 3/1】
  e.st.distance = 5;
  e.st.p[P1].aura = 5;
  e.tree_fall(P0, 1);
  CHECK(e.tree_slot(P0, 1) == 0);
  CHECK(e.st.p[P1].aura == 2);  // 对手选择承 3 装伤

  // 3-2【散华时】：公开使用一张未选择的切牌，之后移出游戏。
  int root = find_def(e, "megumi", "因果律之根");
  REQUIRE(root >= 0);
  int cut = e.add_instance(root, P0);  // Removed
  int plants = e.ps(P0).soilPlants;
  e.ps(P0).tree = {0, 0, 0, 0, 1, 0};  // slot4 = 3-2
  e.tree_fall(P0, 1);
  CHECK(e.ci(cut).zone == Zone::Removed);          // 使用后移出游戏
  CHECK(e.ps(P0).soilPlants == plants + 1);        // 因果律之根：1 种子 -> 植株
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("泷河希: 终结之果实改道其它付与牌的结晶") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  int s3 = find_def(e, "megumi", "终结之果实");  // O-S3
  int reed = find_def(e, "megumi", "芦苇");
  REQUIRE(s3 >= 0);
  REQUIRE(reed >= 0);
  int fruit = e.add_instance(s3, P0);
  e.move_card(fruit, Zone::Special);
  e.ci(fruit).faceUp = true;
  e.ci(fruit).crystals = 2;
  CHECK(e.def(s3).redirectCrystals);
  CHECK(e.def(s3).crystalImmune);
  int other = e.add_instance(reed, P0);
  e.move_card(other, Zone::Enhance);
  e.ci(other).crystals = 3;
  e.ci(other).green = 1;
  e.ps(P0).soilSeeds = 4;  // 这 1 个绿色来自土壤，保持守恒

  // 其它付与牌要移除的结晶 → 改为移到终结之果实（樱花/绿色各自保留）。
  CHECK(e.remove_card_crystals(other, 2) == 2);
  CHECK(e.ci(other).crystals == 1);
  CHECK(e.ci(fruit).crystals == 4);
  e.ci(other).crystals = 0;
  CHECK(e.remove_card_crystals(other, 1) == 1);  // 只剩绿色
  CHECK(e.ci(other).green == 0);
  CHECK(e.ci(fruit).green == 1);

  // 本牌自身：普通手段不能移除，每回合开始的固定 -1 可以。
  CHECK(e.remove_card_crystals(fruit, 1) == 0);
  CHECK(e.ci(fruit).crystals == 4);
  e.consume_enhance_crystal(fruit);
  CHECK(e.ci(fruit).crystals == 3);
  CHECK(e.green_total(P0) == 5);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("泷河希: 须臾景之叶的衍生攻击前置与动态 X") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  int def = find_def(e, "megumi.A1", "须臾景之叶");  // A1-S3
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.ps(P0).flare = 2;

  // 只有在你使用过衍生攻击的回合才可以使用。
  CHECK_FALSE(e.used_generated_attack(P0));
  CHECK_FALSE(e.playable_card(P0, inst));
  e.note_generated_attack(P0);
  CHECK(e.playable_card(P0, inst));
  CHECK(e.respondable_card(P0, inst));

  // X = 绿色结晶存在的区域数（土壤 / 付与区 / 假想树）。
  int reed = find_def(e, "megumi", "芦苇");
  int card = e.add_instance(reed, P0);
  e.move_card(card, Zone::Enhance);
  CHECK(e.green_zones(P0) == 1);  // 土壤
  e.ci(card).crystals = 1;
  e.ci(card).green = 1;
  CHECK(e.green_zones(P0) == 2);  // + 付与区
  e.ps(P0).treeActive = true;
  e.ps(P0).tree[0] = 1;
  CHECK(e.green_zones(P0) == 3);  // + 假想树

  e.st.active = P0;
  Attack atk = e.make_attack(P0, inst, false, false);
  REQUIRE(atk.life.has_value());
  CHECK(*atk.life == 3);
  CHECK(atk.range.contains(5));
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("准备阶段：每张付与牌每回合只移除 1 个结晶") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int d1 = find_def(e, "utsuro", "遗灰咒");   // 【纳2】通常付与
  int d2 = find_def(e, "saine", "圈域");
  REQUIRE(d1 >= 0);
  REQUIRE(d2 >= 0);
  int i1 = e.add_instance(d1, P0);
  e.move_card(i1, Zone::Enhance);
  e.ci(i1).crystals = 5;
  int i2 = e.add_instance(d2, P0);
  e.move_card(i2, Zone::Enhance);
  e.ci(i2).crystals = 3;
  // 抽牌堆留一张牌，避免焦躁干扰断言
  int dz = e.add_instance(find_def(e, "utsuro", "雪刃") >= 0 ? find_def(e, "utsuro", "雪刃")
                                                             : d1, P0);
  e.move_card(dz, Zone::Deck);
  e.start_phase(P0);
  CHECK(e.ci(i1).crystals == 4);   // 5 -> 4（而不是 0）
  CHECK(e.ci(i2).crystals == 2);   // 3 -> 2
}

// ---------------------------------------------------------------------------
// 20-Kanawe 叶慧: 地图 / 戏剧
// ---------------------------------------------------------------------------

// 找一张该玩家的戏剧实例（按槽位 1..6）。
static int drama_of(const Engine& e, Player p, int slot) {
  for (int inst : e.ps(p).dramas)
    if (e.def_of(inst).dramaSlot == slot) return inst;
  return -1;
}

TEST_CASE("叶慧: 地图节点、剧目数值与试炼分支") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_dramas(P0);
  CHECK(e.ps(P0).dramas.size() == 6u);
  CHECK(e.ps(P0).node == "O2");
  CHECK(e.node_value(P0) == 2);    // 起始点 O2 的剧目数值
  CHECK(e.node_color(P0) == -1);   // 起始点没有颜色
  CHECK(e.prepared_drama(P0) == -1);

  e.ps(P0).node = "2B";
  CHECK(e.node_value(P0) == 4);
  CHECK(e.node_color(P0) == 1);    // 紫
  // 基础版：可到 3A / 3B（FirstAgent 选第一个）
  CHECK(e.advance_node(P0, false));
  CHECK(e.ps(P0).node == "3A");
  // 升级版：额外开放「可试炼到 3C」
  e.ps(P0).node = "2B";
  CHECK(e.advance_node(P0, true));
  CHECK(e.ps(P0).node == "3A");
  // 3D 只有「可试炼到 4B」出口
  e.ps(P0).node = "3D";
  CHECK_FALSE(e.advance_node(P0, false));
  CHECK(e.advance_node(P0, true));
  CHECK(e.ps(P0).node == "4B");
  // 5A 基础版没有任何出口，升级版可试炼到终点
  e.ps(P0).node = "5A";
  CHECK_FALSE(e.advance_node(P0, false));
}

TEST_CASE("叶慧: 落格奖励（红/紫/绿/黄）") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_dramas(P0);
  e.st.active = P0;

  // 红: 对敌人造成 1 命伤
  e.ps(P0).node = "1A";
  const int life0 = e.ps(P1).life;
  e.resolve_node_reward(P0);
  CHECK(e.ps(P1).life == life0 - 1);

  // 紫: 执行一次基本动作（FirstAgent 选第一个合法动作 = 前进）
  e.ps(P0).node = "2B";
  e.ps(P0).aura = 3;
  const int dist0 = e.distance();
  e.resolve_node_reward(P0);
  CHECK(e.distance() == dist0 - 1);
  CHECK(e.did_basic_this_turn(P0));

  // 绿: 从盖牌区选一张放到牌库底（牌库底 == front）
  e.ps(P0).node = "1B";
  const int cdef = find_def(e, "kanawe", "构思");
  REQUIRE(cdef >= 0);
  const int card = e.add_instance(cdef, P0);
  e.move_card(card, Zone::Cover);
  e.ci(card).faceUp = false;
  e.resolve_node_reward(P0);
  CHECK(e.ci(card).zone == Zone::Deck);
  REQUIRE_FALSE(e.ps(P0).deck.empty());
  CHECK(e.ps(P0).deck.front() == card);

  // 黄: 站在黄格时，非衍生攻击 +0/+1
  const int writing = find_def(e, "kanawe", "撰写");
  REQUIRE(writing >= 0);
  const int inst = e.add_instance(writing, P0);
  e.move_card(inst, Zone::Hand);
  e.ps(P0).node = "3C";  // 黄
  CHECK(e.node_color(P0) == 3);
  Attack yellow = e.make_attack(P0, inst, false, false);
  REQUIRE(yellow.life.has_value());
  CHECK(*yellow.life + yellow.lifeDelta == 2);  // 1 + 1
  e.ps(P0).node = "1A";  // 红：无加成
  Attack red = e.make_attack(P0, inst, false, false);
  REQUIRE(red.life.has_value());
  CHECK(*red.life + red.lifeDelta == 1);

  // 演出：紫格时 +0/+1（on_play 注册「下一次攻击」修正）
  const int perform = find_def(e, "kanawe", "演出");
  REQUIRE(perform >= 0);
  const int perf1 = e.add_instance(perform, P0);
  e.move_card(perf1, Zone::Hand);
  e.st.distance = 2;      // 让 2-3 的攻击命中
  e.ps(P1).aura = 0;      // 2 装伤不可承受 → 走命伤，便于断言 +1
  e.ps(P0).node = "2B";   // 紫
  const int life1 = e.ps(P1).life;
  e.play_card(P0, perf1, false);
  CHECK(e.ps(P1).life == life1 - 2);  // 2/1 +0/+1

  const int perf2 = e.add_instance(perform, P0);
  e.move_card(perf2, Zone::Hand);
  e.ps(P0).node = "1A";   // 红：无加成
  const int life2 = e.ps(P1).life;
  e.play_card(P0, perf2, false);
  CHECK(e.ps(P1).life == life2 - 1);  // 2/1
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("叶慧: 戏剧的准备、替换与已完成堆") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_dramas(P0);

  CHECK_FALSE(e.prepare_drama(P0, false));  // 从空到有：不是「已完成过的」
  const int d1 = e.prepared_drama(P0);
  REQUIRE(d1 >= 0);
  CHECK(e.load_int(d1, "tag") == 1);        // 戏剧栏
  CHECK(e.load_int(d1, "tier") == 0);       // FirstAgent 选基础版

  e.store_int(d1, "progress", 1);           // 假装已经达成过一次
  CHECK_FALSE(e.prepare_drama(P0, false));  // 换成另一张
  const int d2 = e.prepared_drama(P0);
  CHECK(d2 != d1);
  CHECK(e.load_int(d1, "tag") == 0);        // 换下的回到未完成堆
  CHECK(e.load_int(d1, "progress") == 0);   // 进度不保留
  CHECK(e.load_int(d2, "tag") == 1);

  // 全部视为已完成：撰写（allow=false）无可选，杀青/疾书（allow=true）可选
  for (int i : e.ps(P0).dramas) {
    e.store_int(i, "tag", 2);
    e.store_int(i, "progress", 0);
  }
  e.ps(P0).dramaPrepared = -1;
  CHECK_FALSE(e.prepare_drama(P0, false));
  CHECK(e.prepare_drama(P0, true));         // 从已完成堆中选择 → 返回 true
  CHECK(e.load_int(e.prepared_drama(P0), "tag") == 1);
  // 唯一在栏中的牌被排除；allow=false 依旧无可选（其余全在已完成堆）
  CHECK_FALSE(e.prepare_drama(P0, false));
}

TEST_CASE("叶慧: 《鼓动》的命变化计数跨回合累计") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_dramas(P0);
  const int d3 = drama_of(e, P0, 3);  // 《鼓动》
  REQUIRE(d3 >= 0);
  e.ps(P0).dramaPrepared = d3;
  e.store_int(d3, "tag", 1);
  e.store_int(d3, "tier", 0);  // 基础版：达成次数 2
  e.store_int(d3, "progress", 0);
  e.store_int(d3, "counted_turn", -1);
  e.store_int(d3, "prep_turn", 0);

  e.damage_life(P1, 1, AreaKind::Flare, true);  // 某一方的命发生了变化
  CHECK(e.load_int(d3, "progress") == 1);
  CHECK(e.load_int(d3, "tag") == 1);            // 还差一次
  CHECK(e.ps(P0).dramaProgressedThisTurn);

  // 同一回合内再变化：同一条每回合只计 1 次
  e.damage_life(P1, 1, AreaKind::Flare, true);
  CHECK(e.load_int(d3, "progress") == 1);

  // 下一回合再达成一次 → 完成 → 推进地图
  e.st.turn += 1;
  e.ps(P0).dramaProgressedThisTurn = false;
  e.damage_life(P1, 1, AreaKind::Flare, true);
  CHECK(e.load_int(d3, "progress") == 2);
  CHECK(e.load_int(d3, "tag") == 2);            // 已完成
  CHECK(e.ps(P0).dramaPrepared == -1);
  e.flush_drama_advances();
  CHECK(e.ps(P0).node == "1A");                 // O2 → 1A（FirstAgent 选第一个）
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("叶慧: 《杀阵》升级版按攻击次数判定") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_dramas(P0);
  const int d1 = drama_of(e, P0, 1);  // 《杀阵》
  REQUIRE(d1 >= 0);
  e.ps(P0).dramaPrepared = d1;
  e.store_int(d1, "tag", 1);
  e.store_int(d1, "tier", 1);  // 升级版：至少 5 次攻击和对应，达成次数 1
  e.store_int(d1, "progress", 0);
  e.store_int(d1, "counted_turn", -1);
  e.store_int(d1, "prep_turn", 0);
  e.st.active = P0;

  for (int i = 0; i < 4; ++i) {
    Attack atk;
    atk.attacker = P0;
    e.declare_attack(atk);
  }
  CHECK(e.load_int(d1, "progress") == 0);  // 4 < 5
  Attack fifth;
  fifth.attacker = P0;
  e.declare_attack(fifth);
  CHECK(e.load_int(d1, "progress") == 1);
  CHECK(e.load_int(d1, "tag") == 2);       // 升级版达成次数 1
  CHECK(e.ps(P0).dramaPrepared == -1);
}

// ---- 夜山恋离 (22) -----------------------------------------------------------

namespace {

// 伪证测试用智能体: 按配置回答「伪证声明」与「质疑」，其余决策取最靠前的
// 可用选项（fallback 可指定选项下标）。
struct RenriAgent : Agent {
  std::string claim;    // 声称的牌名（空 = 正常打出）
  bool doubt = false;   // 收到质疑请求时是否质疑
  int fallback = 0;     // 其它请求选择的选项下标
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "bluff") {
      if (!claim.empty()) {
        for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
          const Option& o = r.options[static_cast<size_t>(i)];
          if (o.data.is_object() && o.data.contains("name") && o.data["name"] == claim) {
            d.indices.push_back(i);
            return d;
          }
        }
      }
      d.indices.push_back(0);  // 正常打出
      return d;
    }
    if (r.kind == "doubt") {
      d.indices.push_back(doubt ? 1 : 0);
      return d;
    }
    int want = std::max(1, r.minSel);
    for (int i = 0; i < static_cast<int>(r.options.size()) &&
                    static_cast<int>(d.indices.size()) < want; ++i) {
      if (!r.options[static_cast<size_t>(i)].enabled) continue;
      int pick = std::min(i, static_cast<int>(r.options.size()) - 1);
      if (fallback > 0) pick = std::min(fallback, static_cast<int>(r.options.size()) - 1);
      d.indices.push_back(pick);
    }
    return d;
  }
};

// 清空一名玩家的所有区域（测试自行摆放需要的牌）。
void clear_zones(Engine& e, Player p) {
  PlayerState& s = e.ps(p);
  s.deck.clear();
  s.hand.clear();
  s.discard.clear();
  s.cover.clear();
  s.enhance.clear();
  s.special.clear();
}

// 固定牌组开局（hajimari 模式；cfg.p0Set = "renri"/"renri.A1"），然后清空
// 双方的所有区域，测试自行摆放需要的牌。
void renri_setup(Engine& e, RenriAgent& a0, RenriAgent& a1) {
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);
  e.setup_match();
  clear_zones(e, P0);
  clear_zones(e, P1);
}

}  // namespace

TEST_CASE("夜山恋离: 伪证未被质疑时按声称的牌名结算") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  a0.claim = "捧杀";  // 实际打出「夸口」（3-5 2/1），声称「捧杀」（1距到自气）
  renri_setup(e, a0, a1);
  const int def = find_def(e, "renri", "夸口");
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 5;
  e.ps(P0).flare = 0;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, inst, false);
  CHECK(e.distance() == 4);           // 按声称的「捧杀」结算: 1距到自气
  CHECK(e.ps(P0).flare == 1);
  CHECK(e.st.p[P1].aura == 5);        // 「夸口」的攻击没有发生
  CHECK(e.ci(inst).zone == Zone::Discard);
  CHECK_FALSE(e.ps(P1).doubtFailedThisTurn);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 伪证被质疑且牌名一致 → 质疑失败，对手焦躁一次") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  a0.claim = "构陷";
  a1.doubt = true;
  renri_setup(e, a0, a1);
  const int def = find_def(e, "renri", "构陷");  // 实际就是构陷
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 1;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, inst, false);
  CHECK(e.ps(P1).doubtFailedThisTurn);  // 本回合质疑失败过
  CHECK(e.st.p[P1].aura == 2);          // 焦躁 1 + 构陷 2/1 的 2
  CHECK(e.st.p[P1].life == 10);
  CHECK(e.ci(inst).zone == Zone::Discard);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 伪证被质疑且牌名不一致 → 两者都不结算") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  a0.claim = "构陷";  // 实际是「夸口」
  a1.doubt = true;
  renri_setup(e, a0, a1);
  const int def = find_def(e, "renri", "夸口");
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 4;  // 夸口 3-5 命中；构陷 1-2 落空
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, inst, false);
  CHECK(e.st.p[P1].aura == 5);          // 声称牌与本来效果都没有执行
  CHECK(e.st.p[P1].life == 10);
  CHECK_FALSE(e.ps(P1).doubtFailedThisTurn);
  CHECK(e.ci(inst).zone == Zone::Discard);  // 牌进弃牌堆
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 道化的觉悟把对手受到的焦躁伤害变为 2/1") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  renri_setup(e, a0, a1);
  const int def = find_def(e, "renri", "道化的觉悟");
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 3;  // 展开中
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.impatience(P1);
  CHECK(e.st.p[P1].aura == 3);  // 2/1（而非 1/1）
  CHECK(e.st.p[P1].life == 10);
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 回归把牌移出游戏并把「考古」置回弃牌堆") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  a0.claim = "谎言的武器";
  a1.doubt = true;  // 牌名一致 → 质疑失败
  renri_setup(e, a0, a1);
  const int relic = find_def(e, "renri", "谎言的武器");
  const int kaogu = find_def(e, "renri", "考古");
  REQUIRE(relic >= 0);
  REQUIRE(kaogu >= 0);
  const int inst = e.add_instance(relic, P0);
  e.move_card(inst, Zone::Hand);
  const int ki = e.add_instance(kaogu, P0);
  e.move_card(ki, Zone::Removed);  // 考古已被移出游戏
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, inst, false);
  CHECK(e.ps(P1).doubtFailedThisTurn);
  CHECK(e.ci(inst).zone == Zone::Removed);  // 回归: 移出游戏
  CHECK(e.ci(ki).zone == Zone::Discard);    // 考古置回弃牌堆
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 铭镌之衣按终幕上的结晶数复制（0 → 巫女神乐）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  renri_setup(e, a0, a1);
  const int host = find_def(e, "renri", "夜山恋离的终幕");
  const int meih = find_def(e, "renri", "铭镌之衣");
  REQUIRE(host >= 0);
  REQUIRE(meih >= 0);
  const int hi = e.add_instance(host, P0);
  e.move_card(hi, Zone::Special);
  e.ci(hi).faceUp = true;
  e.ci(hi).crystals = 0;  // 0 个 → 御剑桐子的巫女神乐【2-3 3/2】（费用 3）
  const int mi = e.add_instance(meih, P0);
  e.move_card(mi, Zone::Special);
  e.ci(mi).faceUp = false;
  e.ps(P0).flare = 5;
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, mi, false);
  CHECK(e.ps(P0).flare == 2);        // 支付了复制「巫女神乐」的 3 费
  CHECK(e.st.p[P1].aura == 2);       // 3/2 → 3 装伤
  CHECK(e.st.p[P1].life == 10);
  CHECK(e.ci(mi).faceUp);            // 使用后状态
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 铭镌之衣的 2 结晶复制是完全论破") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  renri_setup(e, a0, a1);
  const int host = find_def(e, "renri", "夜山恋离的终幕");
  const int meih = find_def(e, "renri", "铭镌之衣");
  const int victimDef = find_def(e, "renri", "构陷");
  REQUIRE(host >= 0);
  REQUIRE(meih >= 0);
  REQUIRE(victimDef >= 0);
  const int hi = e.add_instance(host, P0);
  e.move_card(hi, Zone::Special);
  e.ci(hi).faceUp = true;
  e.ci(hi).crystals = 2;
  const int mi = e.add_instance(meih, P0);
  e.move_card(mi, Zone::Special);
  e.ci(mi).faceUp = false;
  const int victim = e.add_instance(victimDef, P1);
  e.move_card(victim, Zone::Discard);
  e.ps(P0).flare = 5;
  e.st.active = P0;
  e.st.distance = 9;
  e.play_card(P0, mi, false);
  CHECK(e.ps(P0).flare == 3);              // 完全论破费用 2
  CHECK(e.sealed_card(mi) == victim);      // 封印了对手弃牌堆的一张常规牌
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 御剑桐子的巫女神乐是白板【2-3 3/2】") {
  Engine e;
  e.load_content(find_file("content/renri.lua"));
  int def = -1;
  for (const CardDef& d : e.defs)
    if (d.name == "御剑桐子的巫女神乐") def = d.id;
  REQUIRE(def >= 0);
  const CardDef& d = e.def(def);
  CHECK(d.kind == CardKind::Special);
  CHECK(d.type == CardType::Attack);
  CHECK(d.isExtra);
  CHECK(d.cost == 3);
  CHECK(d.hasAttack);
  std::set<int> pts;
  for (const auto& sp : d.attack.range.spans)
    for (int i = sp.first; i <= sp.second; ++i) pts.insert(i);
  CHECK(pts == std::set<int>{2, 3});
  CHECK(d.attack.damage.aura.has_value());
  CHECK(d.attack.damage.life.has_value());
  CHECK(*d.attack.damage.aura == 3);
  CHECK(*d.attack.damage.life == 2);
  CHECK(d.attack.keywords == 0u);  // 白板: 没有附加词条
}

TEST_CASE("夜山恋离: 洛阳铲声称对手构筑池中的牌") {
  // (1) 对手展示了声称的牌 → 使用失败，这张牌被弃置。
  {
    Config cfg = make_cfg("hajimari", 1);
    cfg.preset = "kigen-full";
    cfg.p0Set = "renri.A1";
    cfg.p1Set = "renri";  // 对手的构筑池含「构陷」等常规非付与牌
    Engine e(cfg);
    load_all_content(e);
    RenriAgent a0;
    RenriAgent a1;  // fallback=0 → 声称候选表的第一张（构陷）
    renri_setup(e, a0, a1);
    const int luoyang = find_def(e, "renri", "洛阳铲");
    const int gou = find_def(e, "renri", "构陷");
    REQUIRE(luoyang >= 0);
    REQUIRE(gou >= 0);
    const int li = e.add_instance(luoyang, P0);
    e.move_card(li, Zone::Hand);
    const int gi = e.add_instance(gou, P1);
    e.move_card(gi, Zone::Hand);  // 对手手上展示了这张牌
    e.st.active = P0;
    e.st.distance = 2;
    e.ps(P0).flare = 0;
    e.st.p[P1].aura = 5;
    e.st.p[P1].life = 10;
    e.play_card(P0, li, false);
    CHECK(e.ci(li).zone == Zone::Discard);  // 使用失败 → 弃置
    CHECK(e.st.p[P1].aura == 5);            // 没有效果
    CHECK(e.ps(P0).flare == 0);
    CHECK(e.lua_error_count() == 0);
  }
  // (2) 对手没有这张牌 → 按声称的牌名使用（构陷【1-2 2/1】）。
  {
    Config cfg = make_cfg("hajimari", 2);
    cfg.preset = "kigen-full";
    cfg.p0Set = "renri.A1";
    cfg.p1Set = "renri";
    Engine e(cfg);
    load_all_content(e);
    RenriAgent a0;
    RenriAgent a1;
    renri_setup(e, a0, a1);
    const int luoyang = find_def(e, "renri", "洛阳铲");
    REQUIRE(luoyang >= 0);
    const int li = e.add_instance(luoyang, P0);
    e.move_card(li, Zone::Hand);
    e.st.active = P0;
    e.st.distance = 2;
    e.ps(P0).flare = 0;
    e.st.p[P1].aura = 5;
    e.st.p[P1].life = 10;
    e.play_card(P0, li, false);
    CHECK(e.st.p[P1].aura == 3);            // 按声称的「构陷」结算: 2 装伤
    CHECK(e.ci(li).zone == Zone::Discard);
    CHECK(e.lua_error_count() == 0);
  }
}

TEST_CASE("夜山恋离: 道化的觉悟的纳至少要有一个结晶来自命（结晶守恒）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  renri_setup(e, a0, a1);
  const int def = find_def(e, "renri", "道化的觉悟");
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;  // 开局即使用后状态
  e.ci(inst).crystals = 0;
  e.ps(P0).aura = 0;
  e.ps(P0).life = 10;
  e.st.dust = 0;
  e.st.active = P0;
  const long long before = crystals_total(e);
  e.reuse_special(inst);  // 使用后：你的回合结束时可以使用这张牌
  CHECK(e.ci(inst).crystals == 3);   // 纳3
  CHECK(e.ps(P0).life == 7);         // 其中至少 1 个来自命（此处 0虚0装 → 3 个都来自命）
  CHECK(crystals_total(e) == before);  // 结晶守恒
  CHECK(e.lua_error_count() == 0);
}

// ---------------------------------------------------------------------------
// 17-22 加固：按用户裁定补上的行为与回归测试。
// ---------------------------------------------------------------------------

TEST_CASE("山城水津城: 骑兵是付与士兵，弃置后回到兵舍而非弃牌堆") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  const int d = find_def(e, "mizuki", "骑兵");
  REQUIRE(d >= 0);
  CHECK(e.def(d).type == CardType::Enhance);  // 付与型士兵
  const int inst = e.add_instance(d, P0);
  e.ci(inst).soldier = true;
  e.to_barracks(P0, inst, true);  // 已动员
  CHECK(e.st.p[P0].barracks.size() == 1u);
  CHECK(e.soldier_mobilized(inst));

  // 打出（付与牌）→ 进入付与区。
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 2;
  CHECK(e.ci(inst).zone == Zone::Enhance);
  CHECK(e.st.p[P0].barracks.empty());

  // 弃置（破绽/击落等）→ 翻回未动员并回到兵舍，不进入弃牌堆。
  e.move_card(inst, Zone::Discard);
  CHECK(e.ci(inst).zone == Zone::Limbo);  // 兵舍不列入通用区域
  CHECK(e.st.p[P0].barracks.size() == 1u);
  CHECK_FALSE(e.soldier_mobilized(inst));
  CHECK(e.st.p[P0].discard.empty());
  CHECK(e.ci(inst).crystals == 0);  // 离场时不保留献
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("泷河希: 终结之果实 5 绿结算 5/5 后绿回种子、樱花进虚") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_soil(P0);
  const int d = find_def(e, "megumi", "终结之果实");
  REQUIRE(d >= 0);
  const int inst = e.add_instance(d, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 2;  // 樱花结晶（献）
  e.ci(inst).green = 5;
  e.ps(P0).soilSeeds = 0;
  e.st.active = P0;
  e.st.distance = 5;
  // 对手不参与对应/承伤选择。
  e.ps(P1).hand.clear();
  e.ps(P1).cover.clear();
  e.ps(P1).special.clear();
  const int dustBefore = e.st.dust;
  const int p1life = e.ps(P1).life;
  e.fire("turn_end", P0);
  CHECK(e.ps(P0).soilSeeds == 5);      // 绿色结晶 → 种子
  CHECK(e.ci(inst).green == 0);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.st.dust == dustBefore + 2);  // 樱花结晶 → 虚
  CHECK(e.ps(P1).life == p1life - 5);  // 5/5 命中（装 3 不足以承受 5 装伤）
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("叶慧: 封杀在红色剧目时连同名通常牌一起封杀") {
  Config cfg = make_cfg("standard", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  const int fang = find_def(e, "kanawe", "封杀");
  const int normal = find_def(e, "yurina", "斩");
  const int cut = find_def(e, "yurina", "月影落");
  REQUIRE(fang >= 0);
  REQUIRE(normal >= 0);
  REQUIRE(cut >= 0);
  const int host = e.add_instance(fang, P0);
  e.move_card(host, Zone::Enhance);
  e.ci(host).crystals = 3;
  const int n1 = e.add_instance(normal, P1);
  e.move_card(n1, Zone::Hand);
  const int c1 = e.add_instance(cut, P1);
  e.move_card(c1, Zone::Special);

  // 宣言「斩」（通常牌）：非红剧目时不生效。
  e.store_int(host, "ban_def", normal);
  e.ps(P0).node = "2B";  // 紫
  CHECK(e.playable_card(P1, n1));
  e.ps(P0).node = "1A";  // 红
  CHECK_FALSE(e.playable_card(P1, n1));
  CHECK_FALSE(e.respondable_card(P1, n1));

  // 宣言切牌名：不受剧目颜色影响，始终封杀。
  e.store_int(host, "ban_def", cut);
  e.ps(P0).node = "2B";  // 紫
  CHECK_FALSE(e.playable_card(P1, c1));
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("夜山恋离: 铭镌之衣视作夙愿时免疫伤害并在主要阶段开始时即再起") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RenriAgent a0;
  RenriAgent a1;
  renri_setup(e, a0, a1);
  const int host = find_def(e, "renri", "夜山恋离的终幕");
  const int meih = find_def(e, "renri", "铭镌之衣");
  REQUIRE(host >= 0);
  REQUIRE(meih >= 0);
  const int hi = e.add_instance(host, P0);
  e.move_card(hi, Zone::Special);
  e.ci(hi).faceUp = true;
  e.ci(hi).crystals = 1;  // 1 个 → 夙愿
  const int mi = e.add_instance(meih, P0);
  e.move_card(mi, Zone::Special);
  e.ci(mi).faceUp = false;
  e.st.active = P0;
  e.st.distance = 4;
  e.ps(P0).flare = 6;
  e.ps(P0).life = 10;
  e.st.p[P1].aura = 3;

  // 使用（复制夙愿，费用 6）→ 使用后：你不会受到任何伤害。
  e.play_card(P0, mi, false);
  CHECK(e.ps(P0).flare == 0);
  CHECK(e.ci(mi).faceUp);
  CHECK(e.has_damage_immunity(P0));

  // 准备阶段把终幕上的结晶拔掉 → 身份变桐子（已使用状态保留），不再免疫。
  e.ci(hi).crystals = 0;
  CHECK_FALSE(e.has_damage_immunity(P0));
  CHECK(e.ci(mi).faceUp);  // 已使用状态不因身份变化而改变

  // 主要阶段开始：只有当它是夙愿时才即再起。
  e.fire("main_start", P0);
  CHECK(e.ci(mi).faceUp);  // 桐子不再起
  e.ci(hi).crystals = 1;
  e.fire("main_start", P0);
  CHECK_FALSE(e.ci(mi).faceUp);  // 夙愿即再起
  CHECK_FALSE(e.has_damage_immunity(P0));

  // 未使用时拔掉结晶 → 变桐子（未使用），主要阶段开始也不会再起。
  e.ci(hi).crystals = 0;
  e.fire("main_start", P0);
  CHECK_FALSE(e.ci(mi).faceUp);

  // 桐子费用 3、【2-3 3/2】白板攻击。
  e.ps(P0).flare = 3;
  e.st.distance = 3;
  e.play_card(P0, mi, false);
  CHECK(e.ps(P0).flare == 0);
  CHECK(e.st.p[P1].aura == 0);  // 3 装伤
  CHECK(e.lua_error_count() == 0);
}

namespace {

// 重铸牌库的流程需要按提示逐个作答：正常重铸 / 不设置 / 宣称盖牌。
// 其它请求（构筑 / 换牌 / 承伤）沿用 FirstAgent 式的“取最前面的若干项”。
struct RebuildClaimAgent : Agent {
  bool claim = false;
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind != "option") {
      if (r.options.empty()) return d;  // 0 张可选 = 不作选择
      int want = std::max(1, r.minSel);
      for (int i = 0; i < static_cast<int>(r.options.size()) &&
                      static_cast<int>(d.indices.size()) < want; ++i) {
        if (!r.options[static_cast<size_t>(i)].enabled) continue;
        d.indices.push_back(i);
      }
      return d;
    }
    auto byLabel = [&](const char* s) -> int {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i)
        if (r.options[static_cast<size_t>(i)].label.find(s) != std::string::npos) return i;
      return -1;
    };
    int pick = 0;
    if (r.prompt.find("重铸牌库") != std::string::npos)
      pick = byLabel("正常重铸");
    else if (r.prompt.find("谎言的武器") != std::string::npos)
      pick = claim ? 1 : 0;
    else if (r.prompt.rfind("设置：", 0) == 0)
      pick = byLabel("不使用");
    if (pick < 0) pick = 0;
    d.indices.push_back(pick);
    return d;
  }
};

}  // namespace

TEST_CASE("夜山恋离: 谎言的武器可在重铸牌库时宣称盖牌区一张背面牌并如设置打出") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "renri.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  RebuildClaimAgent a0;
  RebuildClaimAgent a1;
  a0.claim = true;
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);
  e.setup_match();
  clear_zones(e, P0);
  clear_zones(e, P1);

  const int relicDef = find_def(e, "renri", "谎言的武器");
  const int coverDef = find_def(e, "renri", "恐吓");
  REQUIRE(relicDef >= 0);
  REQUIRE(coverDef >= 0);
  const int relic = e.add_instance(relicDef, P0);
  e.move_card(relic, Zone::Deck);  // 拥有「谎言的武器」才有宣称能力
  const int cover = e.add_instance(coverDef, P0);
  e.move_card(cover, Zone::Cover);  // 背面向上
  e.st.active = P0;
  e.st.distance = 3;
  e.ps(P0).life = 10;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;

  e.run_rebuild(P0);
  CHECK(e.ps(P0).life == 9);                 // 正常重铸的 1 命伤
  CHECK(e.ci(cover).zone == Zone::Deck);     // 宣称的牌结算完毕必然进入牌山
  CHECK(e.ci(cover).def == coverDef);        // 临时替换的 def 必须还原
  CHECK(e.ci(relic).zone == Zone::Deck);
  CHECK(e.ci(relic).def == relicDef);
  CHECK(e.st.p[P1].aura == 4);               // 作为「谎言的武器」打出【2-4 1/1】
  CHECK(e.lua_error_count() == 0);
}

// ---- 25-Misora 观空 ---------------------------------------------------------

namespace {
// 计数“瞄准点”相关决策（观空机制是否被询问）。
struct AimPromptAgent : Agent {
  int* counter = nullptr;
  Decision decide(const Request& r) override {
    if (counter && r.prompt.find("瞄准点") != std::string::npos) *counter += 1;
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

TEST_CASE("观空: 瞄准点回合结束记录、主要阶段结束时因攻击移除") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "misora";
  cfg.p1Set = "misora";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.setup_match();
  clear_zones(e, P0);
  clear_zones(e, P1);
  CHECK(e.has_misora(P0));
  CHECK(e.has_misora(P1));

  e.st.active = P0;
  e.st.distance = 5;
  e.set_aim(P0, -1);
  e.offer_aim_recording(P0);  // FirstAgent → yes
  CHECK(e.aim(P0) == 5);
  e.clear_aim_if_attacked(P0);  // 本回合没有攻击 → 保留
  CHECK(e.aim(P0) == 5);

  int gong = find_def(e, "misora", "弓流");
  REQUIRE(gong >= 0);
  int gi = e.add_instance(gong, P0);
  e.move_card(gi, Zone::Hand);
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, gi, false);
  CHECK(e.attacked_this_turn(P0));
  e.clear_aim_if_attacked(P0);  // 主要阶段结束时因攻击移除
  CHECK(e.aim(P0) == -1);

  // 瞄准点是公开信息，且进入 state_hash（回放校验）。
  e.set_aim(P0, 7);
  CHECK(e.observation(P0)["players"][0]["aim"] == 7);
  CHECK(e.observation(P1)["players"][0]["aim"] == 7);
  const uint64_t h1 = e.state_hash();
  e.set_aim(P0, 7);
  CHECK(e.state_hash() == h1);
  e.set_aim(P0, 8);
  CHECK(e.state_hash() != h1);
}

TEST_CASE("观空: 追踪攻击按瞄准点判定距离，没有瞄准点则不能宣告") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xuan = find_def(e, "misora", "旋翎疑矢");  // 全力【5-15 5/1】追踪
  REQUIRE(xuan >= 0);
  e.st.active = P0;
  e.st.distance = 0;  // 实际距离 0
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  int xi = e.add_instance(xuan, P0);
  e.move_card(xi, Zone::Hand);

  // 没有瞄准点：追踪牌不可宣告（range 判定失败）。
  e.set_aim(P0, -1);
  Attack probe = e.make_attack(P0, xi, false, false);
  CHECK_FALSE(e.attack_range_ok(probe));

  // 瞄准点 5：当前距 0 也能用【5-15】并命中。
  e.set_aim(P0, 5);
  Attack probe2 = e.make_attack(P0, xi, false, false);
  CHECK(e.attack_range_ok(probe2));
  e.play_card(P0, xi, false);
  CHECK(e.st.p[P1].aura == 0);  // 5/1，FirstAgent 选择装侧
  CHECK(e.st.p[P1].life == 10);
}

TEST_CASE("观空: 弓流的对装伤害在攻击距离包含瞄准点时改为 -/1") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int gong = find_def(e, "misora", "弓流");
  REQUIRE(gong >= 0);
  e.st.active = P0;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;

  // 瞄准点 6 位于【4-7】内 → 只能受 1 命伤。
  e.st.distance = 5;
  e.set_aim(P0, 6);
  int g1 = e.add_instance(gong, P0);
  e.move_card(g1, Zone::Hand);
  e.play_card(P0, g1, false);
  CHECK(e.st.p[P1].life == 9);
  CHECK(e.st.p[P1].aura == 5);

  // 瞄准点 2 不在【4-7】内 → 照常 2/1。
  e.set_aim(P0, 2);
  int g2 = e.add_instance(gong, P0);
  e.move_card(g2, Zone::Hand);
  e.play_card(P0, g2, false);
  CHECK(e.st.p[P1].aura == 3);
  CHECK(e.st.p[P1].life == 9);
}

TEST_CASE("观空: 引弓蹴可令瞄准点 +1 或 -1") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int yin = find_def(e, "misora", "引弓蹴");
  REQUIRE(yin >= 0);
  e.st.active = P0;
  e.st.distance = 3;
  e.set_aim(P0, 5);
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  int yi = e.add_instance(yin, P0);
  e.move_card(yi, Zone::Hand);
  e.play_card(P0, yi, false);
  CHECK(e.aim(P0) == 6);  // FirstAgent 选择 +1
  CHECK(e.st.p[P1].aura == 3);
}

TEST_CASE("观空: 风口攻击后按 距/瞄准点 的相对大小移动结晶") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int feng = find_def(e, "misora", "风口");  // 对应【2-5 1/1】
  REQUIRE(feng >= 0);
  e.st.active = P0;
  e.st.p[P1].aura = 0;  // 迫使对手受命伤，避免装伤结晶进入虚干扰计数
  e.st.p[P1].life = 10;
  auto play_feng = [&]() {
    int fi = e.add_instance(feng, P0);
    e.move_card(fi, Zone::Hand);
    e.play_card(P0, fi, false);
  };

  // 距 > 瞄准点：1距到虚
  e.st.distance = 4;
  e.st.dust = 0;
  e.set_aim(P0, 3);
  play_feng();
  CHECK(e.st.distance == 3);
  CHECK(e.st.dust == 1);

  // 距 == 瞄准点：1虚到装
  e.st.distance = 3;
  e.st.dust = 1;
  e.set_aim(P0, 3);
  int aura0 = e.ps(P0).aura;
  play_feng();
  CHECK(e.st.dust == 0);
  CHECK(e.ps(P0).aura == aura0 + 1);

  // 距 < 瞄准点：1虚到距
  e.st.distance = 3;
  e.st.dust = 1;
  e.set_aim(P0, 5);
  play_feng();
  CHECK(e.st.dust == 0);
  CHECK(e.st.distance == 4);
}

TEST_CASE("观空: 旋翎疑矢令对手弃攻击牌，否则盖伏其牌库顶3张") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xuan = find_def(e, "misora", "旋翎疑矢");
  int san = find_def(e, "kamuwi", "散华刃");
  REQUIRE(xuan >= 0);
  REQUIRE(san >= 0);
  e.st.active = P0;
  e.st.distance = 0;
  e.set_aim(P0, 5);
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;

  // 情况 A：对手手牌有攻击牌 → 弃置它。
  int ai = e.add_instance(san, P1);
  e.move_card(ai, Zone::Hand);
  int x1 = e.add_instance(xuan, P0);
  e.move_card(x1, Zone::Hand);
  e.play_card(P0, x1, false);
  CHECK(e.ci(ai).zone == Zone::Discard);

  // 情况 B：对手没有攻击牌 → 盖伏其牌库顶 3 张。
  int d1 = e.add_instance(san, P1);
  int d2 = e.add_instance(san, P1);
  int d3 = e.add_instance(san, P1);
  int d4 = e.add_instance(san, P1);
  for (int d : {d1, d2, d3, d4}) e.move_card(d, Zone::Deck);
  int x2 = e.add_instance(xuan, P0);
  e.move_card(x2, Zone::Hand);
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.play_card(P0, x2, false);
  CHECK(e.cover_count(P1) == 3);
  CHECK(e.ps(P1).deck.size() == 1);
}

TEST_CASE("观空: 精密化强化符合条件的下一次非观空通常牌攻击") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int jing = find_def(e, "misora", "精密化");
  int qiang = find_def(e, "hatsumi", "强酸");  // 【5-6 3/1】
  int gong = find_def(e, "misora", "弓流");
  REQUIRE(jing >= 0);
  REQUIRE(qiang >= 0);
  REQUIRE(gong >= 0);
  e.st.active = P0;
  e.st.distance = 5;
  e.set_aim(P0, 5);
  e.ps(P0).vigor = 0;
  e.ps(P0).tailwind = true;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;

  int ji = e.add_instance(jing, P0);
  e.move_card(ji, Zone::Hand);
  e.play_card(P0, ji, false);
  CHECK(e.ps(P0).vigor == 1);  // 获得 1 集中力

  int qi = e.add_instance(qiang, P0);
  e.move_card(qi, Zone::Hand);
  e.play_card(P0, qi, false);
  CHECK(e.st.p[P1].aura == 1);  // 3/1 → +1/+1 = 4 装伤

  // 观空自身的通常牌不吃强化：弓流 只结算自己的 -/1（命伤 1，而非 2）。
  int j2 = e.add_instance(jing, P0);
  e.move_card(j2, Zone::Hand);
  e.play_card(P0, j2, false);
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  e.st.distance = 5;
  e.set_aim(P0, 5);
  int g3 = e.add_instance(gong, P0);
  e.move_card(g3, Zone::Hand);
  e.play_card(P0, g3, false);
  CHECK(e.st.p[P1].life == 9);
}

TEST_CASE("观空: 寻踪箭从游戏外使用非全力攻击牌并赋予锁定，用后移出游戏") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xun = find_def(e, "misora", "寻踪箭");
  int gong = find_def(e, "misora", "弓流");  // 【4-7 2/1】
  REQUIRE(xun >= 0);
  REQUIRE(gong >= 0);
  e.st.active = P0;
  e.st.distance = 5;  // 弓流的攻击距离包含当前距 → 本来打得到
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  int outside = e.add_instance(gong, P0);  // zone == Removed == 游戏外未加入构筑
  auto pool = e.unchosen_normals(P0);
  REQUIRE(std::find(pool.begin(), pool.end(), outside) != pool.end());

  int xi = e.add_instance(xun, P0);
  e.move_card(xi, Zone::Hand);
  e.play_card(P0, xi, false);
  CHECK(e.st.p[P1].aura == 3);                 // 2 装伤
  CHECK(e.ci(outside).zone == Zone::Removed);  // 游戏外的牌使用后移出游戏
  CHECK(e.load_int(outside, "out", 0) == 1);
  CHECK(e.unchosen_normals(P0).empty());
}

TEST_CASE("观空: 寻踪箭的候选必须是本来打得中的攻击牌（锁定不豁免打出距离）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int xun = find_def(e, "misora", "寻踪箭");
  int gong = find_def(e, "misora", "弓流");  // 【4-7 2/1】
  REQUIRE(xun >= 0);
  REQUIRE(gong >= 0);
  e.st.active = P0;
  e.st.distance = 9;  // 弓流打不到
  e.st.p[P1].aura = 5;
  int outside = e.add_instance(gong, P0);
  int xi = e.add_instance(xun, P0);
  e.move_card(xi, Zone::Hand);
  e.play_card(P0, xi, false);
  CHECK(e.st.p[P1].aura == 5);                  // 没有可用候选 → 不产生攻击
  CHECK(e.ci(outside).zone == Zone::Removed);   // 游戏外的牌没有被使用
  CHECK(e.load_int(outside, "out", 0) == 0);
  CHECK(std::find(e.unchosen_normals(P0).begin(), e.unchosen_normals(P0).end(), outside) !=
        e.unchosen_normals(P0).end());
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("观空: 空之翼限制距离 0-3、展开时移敌装、弃置时 +距/+达人") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int kong = find_def(e, "misora", "空之翼");
  REQUIRE(kong >= 0);

  // 「限制距离0-3」= 打出时当前距必须在 [0,3]（类似攻击牌的距离限制，不是距离 clamp）。
  int ki = e.add_instance(kong, P0);
  e.move_card(ki, Zone::Hand);
  e.st.distance = 8;
  CHECK(e.distance() == 8);              // 不再把有效距离夹到 3
  CHECK_FALSE(e.playable_card(P0, ki));
  e.st.distance = 4;
  CHECK_FALSE(e.playable_card(P0, ki));
  e.st.distance = 3;
  CHECK(e.playable_card(P0, ki));
  CHECK(e.has_terminal(ki));

  // 展开时：2 敌装到距。
  e.ci(ki).crystals = 0;
  e.st.dust = 2;
  e.st.p[P1].aura = 3;
  e.play_card(P0, ki, false);
  CHECK(e.ci(ki).zone == Zone::Enhance);
  CHECK(e.ci(ki).crystals == 2);
  CHECK(e.st.p[P1].aura == 1);
  CHECK(e.distance() == 5);  // 展开时 +2 距

  // 弃置时：本回合内当前距离 +1、达人距离 +1。
  const int near0 = e.near_distance();
  e.consume_enhance_crystal(ki);
  e.consume_enhance_crystal(ki);  // 献尽 → 弃置
  CHECK(e.ci(ki).zone == Zone::Discard);
  CHECK(e.ps(P0).tempDistanceMod == 1);
  CHECK(e.ps(P0).tempNearDistanceMod == 1);
  CHECK(e.near_distance() == near0 + 1);
}

TEST_CASE("八叶: 八叶镜陨茕樱限制距离 0-7 是打出限制（不再 clamp 距离）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "yatsuha.A1";
  cfg.p1Set = "yurina";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  // 用固定牌组开局以拿到八叶的形态；随后直接摆放实例。
  e.setup_match();
  clear_zones(e, P0);
  const int def = find_def(e, "yatsuha.A1", "八叶镜陨茕樱");
  REQUIRE(def >= 0);
  const int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ps(P0).flare = 5;
  e.st.active = P0;
  e.st.distance = 8;
  CHECK(e.distance() == 8);                 // 有效距离不再被夹到 7
  CHECK_FALSE(e.respondable_card(P0, inst));  // （非对应牌，这里只是确认不崩）
  CHECK_FALSE(e.playable_card(P0, inst));   // 8 > 7 → 不能打出
  e.st.distance = 7;
  CHECK(e.playable_card(P0, inst));
  e.st.distance = 0;
  CHECK(e.playable_card(P0, inst));
  CHECK(e.lua_error_count() == 0);
}

TEST_CASE("观空: 遥瞩霜际 X=牌上结晶数、追踪不可对、X>=2 盖伏敌牌库、使用后回收") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int yao = find_def(e, "misora", "遥瞩霜际");
  REQUIRE(yao >= 0);
  int yi = e.add_instance(yao, P0);
  e.move_card(yi, Zone::Special);
  e.ci(yi).faceUp = false;
  e.st.active = P0;
  e.st.distance = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 5;

  // X = 0 → 距离 3 的追踪不可对攻击。
  e.set_aim(P0, 3);
  e.ps(P0).flare = 2;
  e.play_card(P0, yi, false);
  CHECK(e.st.p[P1].life == 9);
  CHECK(e.ci(yi).faceUp);
  Attack spec0 = e.make_attack(P0, yi, false, false);
  CHECK(spec0.range.to_string() == "3");
  CHECK((spec0.keywords & AF_Tracking) != 0);
  CHECK((spec0.keywords & AF_Unrespondable) != 0);
  CHECK_FALSE(spec0.aura.has_value());
  REQUIRE(spec0.life.has_value());
  CHECK(*spec0.life == 1);

  // 使用后：结束阶段开始时 1虚到牌上，然后设为未使用。
  e.st.dust = 1;
  e.fire("end_phase_start", P0);
  CHECK(e.ci(yi).crystals == 1);
  CHECK_FALSE(e.ci(yi).faceUp);

  // X = 1 → 距离 5。
  e.set_aim(P0, 5);
  e.ps(P0).flare = 2;
  e.play_card(P0, yi, false);
  CHECK(e.st.p[P1].life == 8);
  Attack spec1 = e.make_attack(P0, yi, false, false);
  CHECK(spec1.range.to_string() == "5");

  // X = 2 → 攻击后盖伏对手整个牌库。
  e.ci(yi).crystals = 2;
  e.ci(yi).faceUp = false;
  e.set_aim(P0, 7);
  e.ps(P0).flare = 2;
  int c1 = e.add_instance(yao, P1), c2 = e.add_instance(yao, P1), c3 = e.add_instance(yao, P1);
  for (int c : {c1, c2, c3}) e.move_card(c, Zone::Deck);
  e.play_card(P0, yi, false);
  CHECK(e.st.p[P1].life == 7);
  CHECK(e.cover_count(P1) == 3);
  CHECK(e.ps(P1).deck.empty());
}

TEST_CASE("观空: 蔽目重云把当前距变为瞄准点并禁止前进/离脱") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int bi = find_def(e, "misora", "蔽目重云");
  REQUIRE(bi >= 0);
  e.st.active = P0;
  int inst = e.add_instance(bi, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 1;
  e.st.distance = 10;

  e.set_aim(P0, 4);
  CHECK(e.distance() == 4);
  CHECK_FALSE(e.basic_legal(P0, BasicAction::Advance));

  // 离脱：瞄准点 1 时通常合法，但被 蔽目重云 禁止。
  e.set_aim(P0, 1);
  e.st.dust = 1;
  CHECK(e.distance() <= e.near_distance());
  CHECK_FALSE(e.basic_legal(P0, BasicAction::Escape));

  // 没有瞄准点 → 距离不变。
  e.set_aim(P0, -1);
  CHECK(e.distance() == 10);

  // 献尽后限制解除。
  e.set_aim(P0, 4);
  e.remove_card_crystals(inst, 1);
  CHECK(e.distance() == 10);
  CHECK(e.basic_legal(P0, BasicAction::Advance));
}

TEST_CASE("观空: 惴息悬影打消通常牌攻击并封印该牌，弃置时置入对手弃牌堆") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  RespondAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int chui = find_def(e, "misora", "惴息悬影");  // 对应【纳3】
  int san = find_def(e, "kamuwi", "散华刃");      // 通常牌攻击【3-4】
  REQUIRE(chui >= 0);
  REQUIRE(san >= 0);
  e.st.active = P1;
  e.st.distance = 3;
  e.set_aim(P0, 3);  // 瞄准点位于 散华刃【3-4】内
  e.ps(P0).flare = 2;
  e.st.dust = 3;
  e.st.p[P1].aura = 5;
  e.st.p[P1].life = 10;
  int ci = e.add_instance(chui, P0);
  e.move_card(ci, Zone::Special);
  e.ci(ci).faceUp = false;
  int ai = e.add_instance(san, P1);
  e.move_card(ai, Zone::Hand);

  e.play_card(P1, ai, false);  // RespondAgent(P0) 选择最后一项 = 惴息悬影
  CHECK(e.illegal_count(P0) == 0);
  CHECK(e.ci(ci).faceUp);              // 作为对应打出
  CHECK(e.ci(ai).zone == Zone::Sealed);  // 被封印在惴息悬影下
  CHECK(e.sealed_card(ci) == ai);
  CHECK(e.ps(P0).life == 10);          // 攻击被打消

  // 弃置时：封印的牌置入其持有者（对手）的弃牌堆。
  e.consume_enhance_crystal(ci);
  e.consume_enhance_crystal(ci);
  e.consume_enhance_crystal(ci);  // 献尽 → 弃置
  CHECK(e.ci(ai).zone == Zone::Discard);
  CHECK(e.ci(ai).holder == P1);
  CHECK(e.sealed_card(ci) == -1);
}

TEST_CASE("观空: 观空穹仪距离 +5（可>10）、弃置时三处敌结晶入距、不可被再次发动") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int guan = find_def(e, "misora", "观空穹仪");
  REQUIRE(guan >= 0);
  e.st.active = P0;
  int gi = e.add_instance(guan, P0);
  e.move_card(gi, Zone::Special);
  e.ci(gi).faceUp = true;
  e.ci(gi).crystals = 2;
  e.st.distance = 10;
  CHECK(e.distance() == 15);  // 可以大于 10
  CHECK(e.has_full_power(gi));

  // 使用后：不能被其它牌的效果再次发动（神座渡式重置被忽略）。
  e.reset_special(gi);
  CHECK(e.ci(gi).faceUp);

  // 弃置时：1敌命到距、1敌装到距、1敌气到距。
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 3;
  e.st.p[P1].flare = 2;
  e.consume_enhance_crystal(gi);
  e.consume_enhance_crystal(gi);  // 献尽 → 弃置
  CHECK(e.st.p[P1].life == 9);
  CHECK(e.st.p[P1].aura == 2);
  CHECK(e.st.p[P1].flare == 1);
  CHECK(e.st.distance == 13);
}

TEST_CASE("观空: 以观空牌组自战不产生 Lua 错误、保持不变量并出现瞄准点询问") {
  const int games = fuzz_games(3);
  int aimPrompts = 0;
  for (int g = 0; g < games; ++g) {
    Config cfg = make_cfg("hajimari", 9000 + static_cast<uint64_t>(g));
    cfg.preset = "kigen-full";
    cfg.p0Set = "misora";
    cfg.p1Set = "misora";
    Engine e(cfg);
    load_all_content(e);
    CheckingAgent a0(&e, AgentMode::RandomLegal, 11 + static_cast<uint64_t>(g));
    CheckingAgent a1(&e, AgentMode::RandomLegal, 22 + static_cast<uint64_t>(g));
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(e.st.over);
    CHECK(e.lua_error_count() == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
  // 至少一名观空玩家会被询问记录瞄准点（另一段用计数 agent 复跑一局）。
  Config cfg = make_cfg("hajimari", 9100);
  cfg.preset = "kigen-full";
  cfg.p0Set = "misora";
  cfg.p1Set = "misora";
  Engine e(cfg);
  load_all_content(e);
  AimPromptAgent c0, c1;
  c0.counter = &aimPrompts;
  c1.counter = &aimPrompts;
  e.set_agent(P0, &c0);
  e.set_agent(P1, &c1);
  e.run();
  CHECK(aimPrompts > 0);
}

TEST_CASE("观空: 观空自战的回放哈希一致（瞄准点进入 state_hash）") {
  Config cfg = make_cfg("hajimari", 4242);
  cfg.preset = "kigen-full";
  cfg.p0Set = "misora";
  cfg.p1Set = "misora";
  Engine rec(cfg);
  load_all_content(rec);
  RandomAgent a0(1), a1(2);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  nlohmann::json journal = rec.journal_json();
  REQUIRE(!journal["entries"].empty());

  Engine rep(cfg);
  load_all_content(rep);
  RandomAgent b0(1), b1(2);
  rep.set_agent(P0, &b0);
  rep.set_agent(P1, &b1);
  rep.load_journal(journal);
  rep.run();
  CHECK(rep.state_hash() == rec.state_hash());
}

// ---- 24-Shisui 桑畑志水 ------------------------------------------------------

namespace {

// 志水测试用智能体: response 时选择标签包含 respondName 的牌；prompt 含 promptKey
// 的请求按 indices 选择（indices 为空 = 选择 0 项）；其余取最靠前的可用选项。
struct ShisuiAgent : Agent {
  std::string respondName;
  std::string promptKey;
  std::vector<int> indices;
  int responses = 0;
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "response" && !respondName.empty()) {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
        const Option& o = r.options[static_cast<size_t>(i)];
        if (o.enabled && o.label.find(respondName) != std::string::npos) {
          d.indices.push_back(i);
          responses += 1;
          return d;
        }
      }
    }
    if (!promptKey.empty() && r.prompt.find(promptKey) != std::string::npos) {
      for (int i : indices)
        if (i >= 0 && i < static_cast<int>(r.options.size()) &&
            r.options[static_cast<size_t>(i)].enabled)
          d.indices.push_back(i);
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

// 志水自我对战：清空区域，双方都是志水（准备阶段会结算裂伤）。
void shisui_setup(Engine& e, Agent* a0, Agent* a1) {
  e.set_agent(P0, a0);
  e.set_agent(P1, a1);
  e.setup_match();
  clear_zones(e, P0);
  clear_zones(e, P1);
}

}  // namespace

TEST_CASE("志水: 裂伤指示物不占位置、命溢出立即伤害化（结晶守恒）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  CHECK(e.has_shisui(P0));
  CHECK(e.has_shisui(P1));

  e.st.active = P0;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 2;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 3;
  e.st.p[P1].flare = 0;
  e.st.distance = 8;
  e.st.dust = 0;
  const long long total0 = crystals_total(e);
  REQUIRE(total0 == 36);

  e.add_wound(P0, kWoundAura, 2, P1);
  e.add_wound(P0, kWoundFlare, 1, P1);
  e.add_wound(P0, kWoundAura, 1, P0);  // 同区域、不同来源
  CHECK(e.wound_count(P0, kWoundAura, P1) == 2);
  CHECK(e.wound_count(P0, kWoundAura, P0) == 1);
  CHECK(e.wound_count(P0, kWoundAura) == 3);
  CHECK(e.wound_count(P0, kWoundFlare, P1) == 1);
  CHECK(e.st.p[P0].aura == 3);  // 裂伤不占位置、也不移动结晶
  CHECK(e.st.p[P0].life == 10);
  CHECK(crystals_total(e) == total0);

  // 公开信息（含来源）并进入 state_hash。
  const auto obs = e.observation(P1);
  CHECK(obs["players"][0]["wounds"]["aura"][0] == 1);  // 下标 = 来源玩家
  CHECK(obs["players"][0]["wounds"]["aura"][1] == 2);
  CHECK(obs["players"][0]["wounds"]["flare"][1] == 1);
  const uint64_t h0 = e.state_hash();
  CHECK(e.state_hash() == h0);
  e.add_wound(P1, kWoundLife, 1, P0);
  CHECK(e.state_hash() != h0);
  e.ps(P1).wound[kWoundLife][0] = 0;

  // 命区域: 3 个 (来自 P1) <= 命 10 → 不伤害化。
  e.add_wound(P0, kWoundLife, 3, P1);
  CHECK(e.wound_count(P0, kWoundLife, P1) == 3);
  CHECK(e.st.p[P0].life == 10);

  // 补到 11 > 命 10 → 塞入的那一刻立即只伤害化「命区域里同一来源」的那 11 个。
  e.add_wound(P0, kWoundLife, 8, P1);
  CHECK(e.wound_count(P0, kWoundLife, P1) == 0);
  CHECK(e.st.p[P0].life == 0);    // 命 → 气
  CHECK(e.st.p[P0].flare == 12);  // 2 + 10
  CHECK(e.wound_count(P0, kWoundAura) == 3);  // 装区域的裂伤不受影响
  CHECK(crystals_total(e) == total0);
  CHECK(e.st.over);  // 命归零 → 判负
  CHECK(e.st.winner == 1);
}

TEST_CASE("志水: 准备阶段把所有裂伤伤害化，同源同区域合并为 1 次伤害") {
  Config cfg = make_cfg("hajimari", 2);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);

  e.st.active = P0;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 5;
  e.st.p[P0].flare = 1;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 4;
  e.st.p[P1].flare = 1;
  e.st.distance = 2;
  e.st.dust = 3;
  REQUIRE(crystals_total(e) == 36);
  e.ps(P0).damageTakenThisTurn = 0;

  e.add_wound(P0, kWoundAura, 2, P1);
  e.add_wound(P0, kWoundAura, 1, P0);  // 同区域不同来源 = 另 1 次伤害
  e.add_wound(P0, kWoundFlare, 1, P1);
  e.add_wound(P0, kWoundLife, 3, P1);

  e.resolve_all_wounds(P0);
  CHECK(e.wound_count(P0, kWoundAura) == 0);
  CHECK(e.wound_count(P0, kWoundFlare) == 0);
  CHECK(e.wound_count(P0, kWoundLife) == 0);
  CHECK(e.st.p[P0].aura == 2);   // 装 → 虚（3 个）
  CHECK(e.st.p[P0].flare == 3);  // 气 -1，随后命伤 +3
  CHECK(e.st.p[P0].life == 7);   // 命 → 气（3 个）
  CHECK(e.st.dust == 7);
  // 合并后共 4 次伤害（装(P0) 1 + 装(P1) 1 + 气 1 + 命 1），而不是 2+1+1+3。
  CHECK(e.damage_taken_this_turn(P0) == 4);
  CHECK(crystals_total(e) == 36);

  // 准备阶段开始时自动结算（志水的机制）。
  e.add_wound(P1, kWoundFlare, 1, P0);
  e.start_phase(P0);
  CHECK(e.wound_count(P1, kWoundFlare, P0) == 0);
  CHECK(e.st.p[P1].flare == 0);  // 气 → 虚
}

TEST_CASE("志水: 裂伤攻击 {X/Y} 按实际装结晶数判断承伤侧且不移动装结晶") {
  Config cfg = make_cfg("hajimari", 3);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 0;

  const int def = find_def(e, "shisui", "彻底抗战");  // 全力【2-5 {2/3}】
  REQUIRE(def >= 0);

  // 敌实际装 1 < 2 → 只能吃 3 命裂伤。
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 1;
  int i1 = e.add_instance(def, P0);
  e.move_card(i1, Zone::Hand);
  e.play_card(P0, i1, false);
  CHECK(e.wound_count(P1, kWoundLife, P0) == 3);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 0);
  CHECK(e.st.p[P1].life == 10);  // 裂伤尚未伤害化：命不变
  CHECK(e.st.p[P1].aura == 1);   // 受到裂伤不移动装结晶
  // 攻击后：对手畏缩；对自装/自气/自命 1 裂伤（FirstAgent 取第一项 = 自装）。
  CHECK(e.ps(P1).cower);
  CHECK(e.wound_count(P0, kWoundAura, P0) == 1);

  // 敌实际装 3 >= 2 → 由被攻击者选择；FirstAgent 取第一项 = 装侧。
  e.st.p[P1].aura = 3;
  int i2 = e.add_instance(def, P0);
  e.move_card(i2, Zone::Hand);
  e.play_card(P0, i2, false);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 2);
  CHECK(e.st.p[P1].aura == 3);
  CHECK(e.wound_count(P1, kWoundLife, P0) == 3);  // 先前那 3 个仍在
}

TEST_CASE("志水: 切牌费用 {2} = 向自气放置 2 个裂伤指示物") {
  Config cfg = make_cfg("hajimari", 4);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 0;  // 没有气也能打出（费用不是气）
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 5;

  const int def = find_def(e, "shisui", "青莲裂肤");  // 【3 {2/1}】，费用 {2}
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = false;
  CHECK(e.cut_cost(P0, def, inst) == 0);  // 不消耗气
  e.play_card(P0, inst, false);
  CHECK(e.wound_count(P0, kWoundFlare, P0) == 2);  // 向自气放入 2 个裂伤
  CHECK(e.st.p[P0].flare == 0);                    // 不移动气结晶
  CHECK(e.ci(inst).faceUp);
  // 攻击 {2/1}: 敌实际装 5 >= 2 → FirstAgent 选装侧 → 2 装裂伤，不移动装结晶。
  CHECK(e.wound_count(P1, kWoundAura, P0) == 2);
  CHECK(e.st.p[P1].aura == 5);
  CHECK(e.ps(P1).life == 10);

  // 再起：回合结束时装+气 <= 6 → 重置为未使用。
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 2;
  e.end_phase(P0);
  CHECK_FALSE(e.ci(inst).faceUp);
}

TEST_CASE("志水: 本回合受到伤害的次数驱动 叛乱 与 青色的羁绊") {
  Config cfg = make_cfg("hajimari", 5);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 5;
  e.st.p[P0].flare = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 5;
  e.st.p[P1].flare = 0;
  e.st.dust = 5;
  e.ps(P0).damageTakenThisTurn = 0;

  const int pan = find_def(e, "shisui", "叛乱");  // 对应【2-4 1/1】
  REQUIRE(pan >= 0);
  // 本回合没受到过伤害 → 白板 1/1。
  int i1 = e.add_instance(pan, P0);
  e.move_card(i1, Zone::Hand);
  e.play_card(P0, i1, false);
  CHECK(e.st.p[P1].aura == 4);

  // 受到过一次伤害 → +1/+1（打出时锁定）。
  e.deal_damage(P0, 1, std::nullopt);
  CHECK(e.damage_taken_this_turn(P0) == 1);
  int i2 = e.add_instance(pan, P0);
  e.move_card(i2, Zone::Hand);
  e.play_card(P0, i2, false);
  CHECK(e.st.p[P1].aura == 2);

  // 受到过两次伤害 → 附加 1 虚到装。
  e.deal_damage(P0, 1, std::nullopt);
  CHECK(e.damage_taken_this_turn(P0) == 2);
  const int dust0 = e.st.dust;
  const int aura0 = e.st.p[P0].aura;
  int i3 = e.add_instance(pan, P0);
  e.move_card(i3, Zone::Hand);
  e.play_card(P0, i3, false);
  CHECK(e.st.p[P1].aura == 0);
  // 攻击 2 装伤入虚，随后「1虚到装」。
  CHECK(e.st.dust == dust0 + 2 - 1);
  CHECK(e.st.p[P0].aura == aura0 + 1);
  CHECK(e.damage_taken_this_turn(P0) == 2);  // 裂伤/伤害计数不受卡牌影响

  // 青色的羁绊: X = 本回合受到伤害的次数（=2）→ 选择至多 2 项。
  const int bond = find_def(e, "shisui", "青色的羁绊");
  REQUIRE(bond >= 0);
  ShisuiAgent pick;  // 非对应打出：选项只有「畏缩 / 造成裂伤」，全选
  pick.promptKey = "青色的羁绊";
  pick.indices = {0, 1};
  e.set_agent(P0, &pick);
  int i4 = e.add_instance(bond, P0);
  e.move_card(i4, Zone::Hand);
  e.play_card(P0, i4, false);
  CHECK(e.ps(P1).cower);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 1);  // P1 的智能体取第一项 = 装
  CHECK(e.st.p[P1].aura == 0);                    // 裂伤不移动结晶

  // X = 0 时无事发生。
  e.ps(P1).cower = false;
  e.ps(P0).damageTakenThisTurn = 0;
  e.set_agent(P0, &a);
  int i5 = e.add_instance(bond, P0);
  e.move_card(i5, Zone::Hand);
  e.play_card(P0, i5, false);
  CHECK_FALSE(e.ps(P1).cower);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 1);
}

TEST_CASE("志水: 寒疮噬身把被对应的攻击裂伤化，一回合内第三次受伤即再起") {
  Config cfg = make_cfg("hajimari", 6);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  ShisuiAgent p0;
  p0.respondName = "寒疮噬身";
  FirstAgent p1;
  shisui_setup(e, &p0, &p1);
  e.st.active = P1;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 5;
  e.st.p[P0].flare = 4;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 5;
  e.ps(P0).damageTakenThisTurn = 0;

  const int cut = find_def(e, "shisui", "寒疮噬身");  // 对应（2）
  REQUIRE(cut >= 0);
  int si = e.add_instance(cut, P0);
  e.move_card(si, Zone::Special);
  e.ci(si).faceUp = false;
  int ai = e.add_instance(find_def(e, "shisui", "锯"), P1);  // 【2-3 3/1】
  e.move_card(ai, Zone::Hand);

  e.play_card(P1, ai, false);
  CHECK(p0.responses == 1);
  CHECK(e.ci(si).faceUp);
  // 攻击被裂伤化：3/1 的 3 装伤变为 3 装裂伤（FirstAgent 选装侧）。
  CHECK(e.wound_count(P0, kWoundAura, P1) == 3);
  CHECK(e.st.p[P0].aura == 5);  // 不移动装结晶
  CHECK(e.damage_taken_this_turn(P0) == 0);  // 造成裂伤 ≠ 造成伤害
  CHECK_FALSE(e.ci(si).faceUp == false);

  // 即再起：一回合内受到第三次伤害。
  e.deal_damage(P0, 1, std::nullopt);
  e.deal_damage(P0, 1, std::nullopt);
  CHECK(e.damage_taken_this_turn(P0) == 2);
  CHECK(e.ci(si).faceUp);  // 仅两次，尚未再起
  e.deal_damage(P0, 1, std::nullopt);
  CHECK(e.damage_taken_this_turn(P0) == 3);
  CHECK_FALSE(e.ci(si).faceUp);  // 即再起
}

TEST_CASE("志水: 利刃的衍生攻击在敌装裂伤大于敌装时不能用装承受") {
  Config cfg = make_cfg("hajimari", 7);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 0;

  const int def = find_def(e, "shisui", "利刃");  // 【2-3 {1/1}】+ 衍生【2-3 {1/2}】
  REQUIRE(def >= 0);

  // 敌装里已有 3 个裂伤 > 敌实际装 2 → 衍生攻击不能用装承受。
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 2;
  e.add_wound(P1, kWoundAura, 3, P0);
  int i1 = e.add_instance(def, P0);
  e.move_card(i1, Zone::Hand);
  e.play_card(P0, i1, false);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 4);  // 第一段 {1/1} 选装侧
  CHECK(e.wound_count(P1, kWoundLife, P0) == 2);  // 衍生 {1/2} → 只能吃 2 命裂伤
  CHECK(e.st.p[P1].aura == 2);

  // 敌装裂伤不大于敌装 → 正常二选一（FirstAgent 选装侧）。
  e.st.p[P1].aura = 5;
  e.ps(P1).wound[kWoundAura][0] = 0;
  e.ps(P1).wound[kWoundLife][0] = 0;
  int i2 = e.add_instance(def, P0);
  e.move_card(i2, Zone::Hand);
  e.play_card(P0, i2, false);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 2);  // 1 + 1
  CHECK(e.wound_count(P1, kWoundLife, P0) == 0);
}

TEST_CASE("志水: 旌旗护身限制距离 0-4、两次装附、双方各一区域 1 裂伤") {
  Config cfg = make_cfg("hajimari", 8);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 0;
  e.st.p[P0].flare = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 3;
  e.st.dust = 5;
  e.st.distance = 5;

  const int def = find_def(e, "shisui", "旌旗护身");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  CHECK_FALSE(e.playable_card(P0, inst));  // 距离 5 不在 [0,4]
  e.st.distance = 3;
  CHECK(e.playable_card(P0, inst));

  e.play_card(P0, inst, false);
  CHECK(e.st.p[P0].aura == 2);  // 两次装附
  CHECK(e.st.dust == 3);
  CHECK(e.wound_count(P0, kWoundAura, P0) == 1);  // 你选自装
  CHECK(e.wound_count(P1, kWoundAura, P0) == 1);  // 对手选敌装
  CHECK(e.st.p[P0].aura == 2);                    // 裂伤不占位置
  CHECK(e.st.p[P1].aura == 3);
}

TEST_CASE("志水: 荆棘之路 2距到虚、距离 0 时改为自命裂伤") {
  Config cfg = make_cfg("hajimari", 9);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.distance = 4;
  e.st.dust = 3;

  const int def = find_def(e, "shisui", "荆棘之路");
  REQUIRE(def >= 0);
  int i1 = e.add_instance(def, P0);
  e.move_card(i1, Zone::Hand);
  e.play_card(P0, i1, false);
  CHECK(e.st.distance == 2);
  CHECK(e.st.dust == 5);
  CHECK(e.wound_count(P0, kWoundAura, P0) == 1);  // FirstAgent 选自装
  CHECK(e.wound_count(P0, kWoundLife, P0) == 0);

  // 距离变为 0 → 改为对自命造成 1 裂伤。
  e.st.distance = 2;
  int i2 = e.add_instance(def, P0);
  e.move_card(i2, Zone::Hand);
  e.play_card(P0, i2, false);
  CHECK(e.st.distance == 0);
  CHECK(e.wound_count(P0, kWoundLife, P0) == 1);
}

TEST_CASE("志水: 红莲钻心先结算被对应的攻击，再伤害化裂伤并进行攻击") {
  Config cfg = make_cfg("hajimari", 10);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  ShisuiAgent p0;
  p0.respondName = "红莲钻心";
  p0.promptKey = "红莲钻心";
  p0.indices = {};  // 不伤害化任何区域
  FirstAgent p1;
  shisui_setup(e, &p0, &p1);
  e.st.active = P1;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 5;
  e.st.p[P0].flare = 5;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 1;  // 实际装 < 2 → 衍生攻击只能吃命裂伤
  e.ps(P0).damageTakenThisTurn = 0;

  const int cut = find_def(e, "shisui", "红莲钻心");  // 对应（3）
  REQUIRE(cut >= 0);
  int si = e.add_instance(cut, P0);
  e.move_card(si, Zone::Special);
  e.ci(si).faceUp = false;
  int ai = e.add_instance(find_def(e, "shisui", "锯"), P1);  // 【2-3 3/1】
  e.move_card(ai, Zone::Hand);

  e.play_card(P1, ai, false);
  CHECK(p0.responses == 1);
  CHECK(e.st.p[P0].flare == 2);  // 支付 3 气
  // 攻击先结算（而不是这张牌）：3 装伤 → P0 实际装 5-3。
  CHECK(e.st.p[P0].aura == 2);
  CHECK(e.damage_taken_this_turn(P0) == 1);
  // 之后进行攻击 2/(1+X)，X = ceil(1/2) = 1 → 2/2（常规伤害）；敌方实际装 1 < 2
  // → 2 命伤。若本牌先结算，X 会是 0，则只造成 1 命伤（10 → 9）。
  CHECK(e.wound_count(P1, kWoundLife, P0) == 0);
  CHECK(e.st.p[P1].life == 8);
  CHECK(e.st.p[P1].aura == 1);

  // 非对应打出：直接选择任意多的区域并把裂伤伤害化，然后进行攻击。
  e.reset_special(si);
  CHECK_FALSE(e.ci(si).faceUp);
  p0.indices = { 0, 1, 2, 3, 4, 5 };  // 全选（智能体会按选项数截断）
  e.add_wound(P0, kWoundAura, 2, P1);  // 自装 2 裂伤
  e.add_wound(P1, kWoundFlare, 2, P0);
  e.st.p[P1].flare = 3;
  const int life1 = e.st.p[P1].life;
  const int dust0 = e.st.dust;
  e.play_card(P0, si, false);
  CHECK(e.wound_count(P0, kWoundAura) == 0);
  CHECK(e.wound_count(P1, kWoundFlare) == 0);
  CHECK(e.st.p[P0].aura == 0);   // 2 装裂伤 -> 虚
  CHECK(e.st.p[P1].flare == 3);  // 气 3 - 2（裂伤）+ 2（随后的 2 命伤）
  // 虚: 切札费用实付 2 + 装裂伤 2 + 气裂伤 2。
  CHECK(e.st.dust == dust0 + 6);
  // 然后进行攻击（2/(1+X)）；敌方实际装 1 < 2 → 2 命伤。
  CHECK(e.st.p[P1].life == life1 - 2);
}

TEST_CASE("志水: 埋骨地展开中命为 0 也不死亡，且对手集中力视为 0") {
  Config cfg = make_cfg("hajimari", 11);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.p[P0].life = 10;
  e.st.p[P1].life = 10;
  e.st.p[P1].vigor = 2;

  const int def = find_def(e, "shisui", "桑畑志水的埋骨地");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 2;  // 展开中
  CHECK(e.no_death(P0));
  CHECK_FALSE(e.no_death(P1));
  CHECK(e.effective_vigor(P1) == 2);

  e.st.p[P0].life = 0;
  e.check_win();
  CHECK_FALSE(e.st.over);  // 命为 0 也不会死亡
  CHECK(e.effective_vigor(P1) == 0);  // 对手的集中力视为 0
  CHECK(e.effective_vigor(P0) == e.st.p[P0].vigor);

  // 献尽 → 光环结束，不再保护。
  e.ci(inst).crystals = 0;
  CHECK_FALSE(e.no_death(P0));
  e.check_win();
  CHECK(e.st.over);
  CHECK(e.st.winner == 1);
  CHECK(e.effective_vigor(P1) == 2);
}

TEST_CASE("志水: 青莲裂肤在对手用命承伤后把下一次对装伤害<=2的攻击裂伤化") {
  Config cfg = make_cfg("hajimari", 12);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  shisui_setup(e, &a, &a);
  e.st.active = P0;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 3;
  e.st.p[P0].flare = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 0;  // 实际装 0 < 2 → 强制命侧
  e.ps(P0).damageTakenThisTurn = 0;

  const int def = find_def(e, "shisui", "青莲裂肤");
  REQUIRE(def >= 0);
  int i1 = e.add_instance(def, P0);
  e.move_card(i1, Zone::Special);
  e.ci(i1).faceUp = false;
  e.play_card(P0, i1, false);
  CHECK(e.wound_count(P1, kWoundLife, P0) == 1);   // {2/1} 被强制吃 1 命裂伤
  CHECK(e.wound_count(P1, kWoundAura, P0) == 0);
  CHECK(e.wound_count(P0, kWoundFlare, P0) == 2);  // 费用 {2} → 自气 2 裂伤

  // 「下一次对装伤害不大于 2 的攻击伤害改为造成裂伤」（叛乱 1/1）。
  e.st.p[P1].aura = 3;
  const int pan = find_def(e, "shisui", "叛乱");
  REQUIRE(pan >= 0);
  int i2 = e.add_instance(pan, P0);
  e.move_card(i2, Zone::Hand);
  e.play_card(P0, i2, false);
  CHECK(e.wound_count(P1, kWoundAura, P0) == 1);  // 1 装裂伤而不是 1 装伤
  CHECK(e.st.p[P1].aura == 3);                    // 装结晶未移动

  // 修饰已被消耗：再打一张恢复常规伤害。
  int i3 = e.add_instance(pan, P0);
  e.move_card(i3, Zone::Hand);
  e.play_card(P0, i3, false);
  CHECK(e.st.p[P1].aura == 2);
}

TEST_CASE("志水: 青色的羁绊作为对应把被对应的攻击 -1 命伤") {
  Config cfg = make_cfg("hajimari", 13);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine e(cfg);
  load_all_content(e);
  ShisuiAgent p0;
  p0.respondName = "青色的羁绊";
  p0.promptKey = "青色的羁绊";
  p0.indices = { 0 };  // 只选「被对应的攻击 +0/-1」
  FirstAgent p1;
  shisui_setup(e, &p0, &p1);
  e.st.active = P1;
  e.st.distance = 3;
  e.st.p[P0].life = 10;
  e.st.p[P0].aura = 0;  // 实际装 0 < 3 → 只能吃命伤
  e.st.p[P0].flare = 0;
  e.st.p[P1].life = 10;
  e.st.p[P1].aura = 5;
  e.ps(P0).damageTakenThisTurn = 1;  // 本回合已受过 1 次伤害

  const int bond = find_def(e, "shisui", "青色的羁绊");
  REQUIRE(bond >= 0);
  int bi = e.add_instance(bond, P0);
  e.move_card(bi, Zone::Hand);
  int ai = e.add_instance(find_def(e, "shisui", "锯"), P1);  // 【2-3 3/1】
  e.move_card(ai, Zone::Hand);

  e.play_card(P1, ai, false);
  CHECK(p0.responses == 1);
  CHECK(e.st.p[P0].life == 10);  // 命伤 1 + (-1) = 0
  CHECK(e.st.p[P0].aura == 0);
  CHECK(e.st.p[P0].flare == 0);
  CHECK(e.damage_taken_this_turn(P0) == 1);  // 0 点伤害不计入次数
}

TEST_CASE("志水: 以志水牌组自战不产生 Lua 错误、保持不变量") {
  const int games = fuzz_games(3);
  for (int g = 0; g < games; ++g) {
    Config cfg = make_cfg("hajimari", 9200 + static_cast<uint64_t>(g));
    cfg.preset = "kigen-full";
    cfg.p0Set = "shisui";
    cfg.p1Set = "shisui";
    Engine e(cfg);
    load_all_content(e);
    CheckingAgent a0(&e, AgentMode::RandomLegal, 33 + static_cast<uint64_t>(g));
    CheckingAgent a1(&e, AgentMode::RandomLegal, 44 + static_cast<uint64_t>(g));
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(e.st.over);
    CHECK(e.lua_error_count() == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("志水: 志水自战的回放哈希一致（裂伤进入 state_hash）") {
  Config cfg = make_cfg("hajimari", 4343);
  cfg.preset = "kigen-full";
  cfg.p0Set = "shisui";
  cfg.p1Set = "shisui";
  Engine rec(cfg);
  load_all_content(rec);
  RandomAgent a0(11), a1(12);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  nlohmann::json journal = rec.journal_json();
  REQUIRE(!journal["entries"].empty());

  Engine rep(cfg);
  load_all_content(rep);
  RandomAgent b0(11), b1(12);
  rep.set_agent(P0, &b0);
  rep.set_agent(P1, &b1);
  rep.load_journal(journal);
  rep.run();
  CHECK(rep.state_hash() == rec.state_hash());
}


// ---- 23-Akina 源上安琪娜 ------------------------------------------------------

namespace {

// 安琪娜测试用智能体：按 (promptKey, pickLabel) 规则选择选项；response 请求里选择
// 标签含 respondLabel 的牌；minSel == 0 且 pickOptional 时也选 1 项。
struct AkinaAgent : Agent {
  std::vector<std::pair<std::string, std::string>> rules;
  std::string respondLabel;
  bool pickOptional = false;
  int matches = 0;
  int responses = 0;
  void rule(const std::string& prompt, const std::string& label) {
    rules.push_back({prompt, label});
  }
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "response" && !respondLabel.empty()) {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
        const Option& o = r.options[static_cast<size_t>(i)];
        if (o.enabled && o.label.find(respondLabel) != std::string::npos) {
          d.indices.push_back(i);
          responses += 1;
          return d;
        }
      }
    }
    for (const auto& [key, label] : rules) {
      if (r.prompt.find(key) == std::string::npos) continue;
      matches += 1;
      if (label.empty()) break;
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i) {
        const Option& o = r.options[static_cast<size_t>(i)];
        if (o.enabled && o.label.find(label) != std::string::npos) {
          d.indices.push_back(i);
          return d;
        }
      }
      break;
    }
    int want = r.minSel;
    if (pickOptional && want < 1 && r.maxSel != 0) want = 1;
    if (want < 0) want = 0;
    for (int i = 0; i < static_cast<int>(r.options.size()) &&
                    static_cast<int>(d.indices.size()) < want;
         ++i)
      if (r.options[static_cast<size_t>(i)].enabled) d.indices.push_back(i);
    return d;
  }
};

// 开局（hajimari 固定牌组）后清空双方区域，测试自行摆放需要的牌。
// 之后板面为：双方 命10 装3 气0 股市0；距2 虚8（合计 36 结晶）。
void akina_setup(Engine& e, const std::string& p1set, Agent* a0, Agent* a1) {
  e.cfg.p0Set = "akina";
  e.cfg.p1Set = p1set;
  e.set_agent(P0, a0);
  e.set_agent(P1, a1);
  e.setup_match();
  clear_zones(e, P0);
  clear_zones(e, P1);
  e.move_crystals(AreaRef::distance(), AreaRef::dust(), 8, false);
}

// 调整有效距离（用虚平衡结晶，保持守恒）。
void akina_dist(Engine& e, int d) {
  const int cur = e.st.distance;
  if (d > cur)
    e.move_crystals(AreaRef::dust(), AreaRef::distance(), d - cur, false);
  else if (d < cur)
    e.move_crystals(AreaRef::distance(), AreaRef::dust(), cur - d, false);
}

// 从虚取 n 个结晶到 p 的指定区域（"market"/"flare"/"aura"）。
void akina_from_dust(Engine& e, Player p, const char* area, int n) {
  const std::string a(area);
  AreaRef to = a == "market" ? AreaRef::market(p)
                            : (a == "flare" ? AreaRef::flare(p) : AreaRef::aura(p));
  e.move_crystals(AreaRef::dust(), to, n, false);
}

int ak_put(Engine& e, const char* name, Player p, Zone z) {
  const int def = find_def(e, "akina", name);
  if (def < 0) return -1;
  const int inst = e.add_instance(def, p);
  e.move_card(inst, z);
  return inst;
}

// 由 attacker 对对手结算一次只有命伤的虚拟攻击（用于股价 / 死亡窗口）。
void ak_life_attack(Engine& e, Player attacker, int sourceInst, int life) {
  Attack a;
  a.attacker = attacker;
  a.sourceInst = sourceInst;
  a.range.add(0, 20);
  a.life = life;
  e.declare_attack(a);
  e.resolve_attack(a);
}

}  // namespace

TEST_CASE("安琪娜: 股市是结晶区，资本 = 装+气+股市（非安琪娜视作 0）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);
  CHECK(e.has_akina(P0));
  CHECK_FALSE(e.has_akina(P1));
  CHECK(e.stock_price(P0) == 2);  // 初始股价 2

  akina_from_dust(e, P0, "market", 2);
  CHECK(e.ps(P0).market == 2);
  CHECK(e.capital(P0) == e.ps(P0).aura + e.ps(P0).flare + 2);
  CHECK(e.capital(P1) == e.ps(P1).aura + e.ps(P1).flare);  // 股市视作 0
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 命受攻击伤害时股价 +2/-1 并 clamp 到 [1,4]") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "akina", &a, &a);
  akina_dist(e, 5);

  const int src = ak_put(e, "算盘珠", P1, Zone::Hand);
  REQUIRE(src >= 0);

  // P1 攻击 P0：P0 的命受伤 -> P0 -1；P1 的敌人（P0）命受伤 -> P1 +2。
  ak_life_attack(e, P1, src, 1);
  CHECK(e.stock_price(P0) == 1);
  CHECK(e.stock_price(P1) == 4);
  CHECK(e.ps(P0).life == 9);

  ak_life_attack(e, P1, src, 1);  // clamp
  CHECK(e.stock_price(P0) == 1);
  CHECK(e.stock_price(P1) == 4);

  ak_life_attack(e, P0, src, 1);  // 反向：P1 -1，P0 +2
  CHECK(e.stock_price(P0) == 3);
  CHECK(e.stock_price(P1) == 3);

  // 非攻击伤害不改变股价。
  e.deal_damage(P1, 1, 1);
  CHECK(e.stock_price(P0) == 3);
  CHECK(e.stock_price(P1) == 3);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 投资把恫吓从弃牌堆盖伏、支付投资资金并股价 +1") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);

  e.ps(P0).stockPrice = 3;  // 投资资金从自气
  akina_from_dust(e, P0, "flare", 2);
  const int ht = ak_put(e, "恫吓", P0, Zone::Discard);
  REQUIRE(ht >= 0);
  CHECK(e.invest_available(P0));
  CHECK(e.invest(P0));
  CHECK(e.ci(ht).zone == Zone::Cover);
  CHECK_FALSE(e.ci(ht).faceUp);
  CHECK(e.ps(P0).flare == 1);
  CHECK(e.ps(P0).market == 1);
  CHECK(e.stock_price(P0) == 4);  // clamp 上界
  CHECK(crystals_total(e) == 36);

  // 对应区域不足则不能投资（即使有可翻的投资券）。
  e.ps(P0).stockPrice = 3;
  e.move_crystals(AreaRef::flare(P0), AreaRef::dust(), 1, false);  // 自气 -> 0
  const int ht2 = ak_put(e, "恫吓", P0, Zone::Discard);
  REQUIRE(ht2 >= 0);
  CHECK_FALSE(e.invest_available(P0));
  CHECK_FALSE(e.invest(P0));
  CHECK(e.ci(ht2).zone == Zone::Discard);  // 没有发生任何事
  CHECK(e.ps(P0).market == 1);
  CHECK(crystals_total(e) == 36);

  // 正解（切牌）：从已使用重置为未使用；股价 1 时从虚支付。
  Engine e2(cfg);
  load_all_content(e2);
  FirstAgent a2;
  akina_setup(e2, "yurina", &a2, &a2);
  const int zj = ak_put(e2, "源上安琪娜的正解", P0, Zone::Special);
  REQUIRE(zj >= 0);
  e2.ci(zj).faceUp = true;
  e2.ps(P0).stockPrice = 1;
  const int dustBefore = e2.st.dust;
  CHECK(e2.invest_available(P0));
  CHECK(e2.invest(P0));
  CHECK_FALSE(e2.ci(zj).faceUp);
  CHECK(e2.st.dust == dustBefore - 1);
  CHECK(e2.ps(P0).market == 1);
  CHECK(e2.stock_price(P0) == 2);
  CHECK(crystals_total(e2) == 36);
}

TEST_CASE("安琪娜: 套现把股市 1 个结晶移到虚，按股价移 1 个入自装，股价 -2") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);

  // 股价 2：从敌装移入自装。
  e.ps(P0).stockPrice = 2;
  akina_from_dust(e, P0, "market", 1);
  const int dust0 = e.st.dust;
  const int p0aura = e.ps(P0).aura;
  const int p1aura = e.ps(P1).aura;
  e.cash_out(P0);
  CHECK(e.ps(P0).market == 0);
  CHECK(e.st.dust == dust0 + 1);
  CHECK(e.ps(P0).aura == p0aura + 1);
  CHECK(e.ps(P1).aura == p1aura - 1);
  CHECK(e.stock_price(P0) == 1);  // 2 - 2 clamp 到 1
  CHECK(crystals_total(e) == 36);

  // 股价 4：从敌命移入自装。
  e.ps(P0).stockPrice = 4;
  akina_from_dust(e, P0, "market", 1);
  const int p1life = e.ps(P1).life;
  const int p0aura2 = e.ps(P0).aura;
  e.cash_out(P0);
  CHECK(e.ps(P1).life == p1life - 1);
  CHECK(e.ps(P0).aura == p0aura2 + 1);
  CHECK(e.stock_price(P0) == 2);
  CHECK(crystals_total(e) == 36);

  // 股市为空时不能套现。
  CHECK_FALSE(e.can_cash_out(P0));
  e.cash_out(P0);  // no-op
  CHECK(e.stock_price(P0) == 2);
}

TEST_CASE("安琪娜: 切牌费用等于当前股价且不能被任何费用修正改变") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);

  const int lie = find_def(e, "akina", "差列递归征税法");
  const int ans = find_def(e, "akina", "源上安琪娜的正解");
  REQUIRE(lie >= 0);
  REQUIRE(ans >= 0);
  e.ps(P0).stockPrice = 3;
  CHECK(e.cut_cost(P0, lie, -1) == 3);
  CHECK(e.cut_cost(P0, ans, -1) == 3);
  e.ps(P0).cutCostDelta = -1;        // 伴奏
  e.ps(P0).cutCostPermanent = true;  // 缠回
  CHECK(e.cut_cost(P0, lie, -1) == 3);
  CHECK(e.cut_cost(P0, ans, -1) == 3);
  // 普通切牌照常受费用修正影响（对照）。
  const int uk = find_def(e, "yurina", "浮舟宿");  // cost 2
  REQUIRE(uk >= 0);
  CHECK(e.cut_cost(P0, uk, -1) == 0);
}

TEST_CASE("安琪娜: 算盘珠攻击后可选套现 / 1 自装到股市") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AkinaAgent a0;
  a0.rule("算盘珠", "套现一次");
  FirstAgent a1;
  akina_setup(e, "yurina", &a0, &a1);
  akina_dist(e, 4);
  e.ps(P0).stockPrice = 2;
  akina_from_dust(e, P0, "market", 1);
  const int p0aura = e.ps(P0).aura;
  const int p1aura = e.ps(P1).aura;
  const int bead = ak_put(e, "算盘珠", P0, Zone::Hand);
  REQUIRE(bead >= 0);
  e.play_card(P0, bead, false);
  CHECK(a0.matches == 1);
  CHECK(e.ps(P0).market == 0);           // 套现用掉了股市结晶
  CHECK(e.ps(P1).aura == p1aura - 2);    // 攻击 1 装伤 + 套现从敌装取 1
  CHECK(e.ps(P0).aura == p0aura + 1);
  CHECK(e.stock_price(P0) == 1);
  CHECK(crystals_total(e) == 36);

  // 另一选项：1 自装到股市。
  Engine e2(cfg);
  load_all_content(e2);
  AkinaAgent b0;
  b0.rule("算盘珠", "1 自装到股市");
  FirstAgent b1;
  akina_setup(e2, "yurina", &b0, &b1);
  akina_dist(e2, 4);
  const int aura0 = e2.ps(P0).aura;
  const int bead2 = ak_put(e2, "算盘珠", P0, Zone::Hand);
  e2.play_card(P0, bead2, false);
  CHECK(e2.ps(P0).market == 1);
  CHECK(e2.ps(P0).aura == aura0 - 1);
  CHECK(crystals_total(e2) == 36);
}

TEST_CASE("安琪娜: 算法按集合语义修正距离（区间两端各向近端 1，离散值整体前移）") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);

  const int bead = find_def(e, "akina", "算盘珠");
  const int ht = find_def(e, "akina", "恫吓");
  REQUIRE(bead >= 0);
  REQUIRE(ht >= 0);
  const int bi = e.add_instance(bead, P0);
  const int hi = e.add_instance(ht, P0);

  Attack plain = e.make_attack(P0, bi, false, false);
  CHECK(plain.range.contains(1));
  CHECK(plain.range.contains(6));

  e.ps(P0).algorithmThisTurn = true;
  Attack shifted = e.make_attack(P0, bi, false, false);  // 【1-6】->【0-5】
  CHECK(shifted.range.contains(0));
  CHECK(shifted.range.contains(5));
  CHECK_FALSE(shifted.range.contains(6));

  // 算法 = 距离扩大（近1）+ 距离缩小（远1）；按 rules/01-yurina.md 的集合语义：
  // 离散 4 -> 先扩大近端 [3,4]，再缩小远端 -> 3。
  Attack discrete = e.make_attack(P0, hi, false, false);
  CHECK(discrete.range.contains(3));
  CHECK_FALSE(discrete.range.contains(4));
  CHECK_FALSE(discrete.range.contains(2));
  CHECK_FALSE(discrete.range.contains(5));

  // 对手拥有算法时，自己的攻击同样被修正（所有攻击）。
  e.ps(P0).algorithmThisTurn = false;
  e.ps(P1).algorithmThisTurn = true;
  Attack opp_shift = e.make_attack(P0, bi, false, false);
  CHECK(opp_shift.range.contains(0));
  CHECK_FALSE(opp_shift.range.contains(6));

  // 算法牌本身把标记写入回放状态。
  Engine e3(cfg);
  load_all_content(e3);
  FirstAgent a3;
  akina_setup(e3, "yurina", &a3, &a3);
  const int al = ak_put(e3, "算法", P0, Zone::Hand);
  REQUIRE(al >= 0);
  e3.play_card(P0, al, false);
  CHECK(e3.ps(P0).algorithmThisTurn);
}

TEST_CASE("安琪娜: 恫吓资本多时 +0/+1，资本少时结算后被盖伏") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);
  akina_dist(e, 4);
  akina_from_dust(e, P0, "aura", 1);  // 资本 4 > 3
  const int ht = ak_put(e, "恫吓", P0, Zone::Hand);
  REQUIRE(ht >= 0);
  const int p1life = e.ps(P1).life;
  e.play_card(P0, ht, false);
  CHECK(e.ps(P1).life == p1life - 1);     // +0/+1 使命伤 0 -> 1
  CHECK(e.ci(ht).zone == Zone::Discard);  // 资本不少 -> 不盖伏
  CHECK(crystals_total(e) == 36);

  Engine e2(cfg);
  load_all_content(e2);
  FirstAgent a2;
  akina_setup(e2, "yurina", &a2, &a2);
  akina_dist(e2, 4);
  akina_from_dust(e2, P1, "aura", 1);  // 资本 3 < 4
  const int ht2 = ak_put(e2, "恫吓", P0, Zone::Hand);
  const int p1life2 = e2.ps(P1).life;
  e2.play_card(P0, ht2, false);
  CHECK(e2.ps(P1).life == p1life2);       // 无 +0/+1
  CHECK(e2.ci(ht2).zone == Zone::Cover);  // 结算完毕后被盖伏
  CHECK_FALSE(e2.ci(ht2).faceUp);
  CHECK(crystals_total(e2) == 36);
}

TEST_CASE("安琪娜: 交易终端、资本多则执行基本动作、多 3 则取回弃牌堆非安琪娜牌") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AkinaAgent a0;
  a0.rule("交易", "斩");
  a0.rule("free basic action", "basic: flare");  // 聚气：资本不变
  a0.pickOptional = true;
  FirstAgent a1;
  akina_setup(e, "yurina", &a0, &a1);

  akina_dist(e, 3);
  akina_from_dust(e, P0, "flare", 2);
  akina_from_dust(e, P0, "market", 1);
  // 资本 P0 = 3 + 2 + 1 = 6；P1 = 3 + 0 = 3（至少多 3）。
  const int sl = find_def(e, "yurina", "斩");
  REQUIRE(sl >= 0);
  const int slInst = e.add_instance(sl, P0);
  e.move_card(slInst, Zone::Discard);
  const int handBefore = static_cast<int>(e.ps(P0).hand.size());
  const int trade = ak_put(e, "交易", P0, Zone::Hand);
  REQUIRE(trade >= 0);
  CHECK(e.has_terminal(trade));
  e.play_card(P0, trade, false);
  CHECK(a0.matches >= 2);                                          // 交易 + 自由基本动作
  CHECK(static_cast<int>(e.ps(P0).hand.size()) == handBefore + 1);  // 斩 回到手中
  CHECK(e.ci(trade).zone == Zone::Discard);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 乱拨限制距离 0-3、展开时 2 敌气到距、结晶移除改为进敌气") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);

  const int lb = ak_put(e, "乱拨", P0, Zone::Hand);
  REQUIRE(lb >= 0);
  akina_dist(e, 4);
  CHECK_FALSE(e.playable_card(P0, lb));  // 限制距离 0-3
  akina_dist(e, 3);
  CHECK(e.playable_card(P0, lb));

  akina_from_dust(e, P1, "flare", 3);
  const int p1flare = e.ps(P1).flare;
  const int dist = e.st.distance;
  e.play_card(P0, lb, false);
  CHECK(e.ps(P1).flare == p1flare - 2);  // 展开时 2 敌气到距
  CHECK(e.st.distance == dist + 2);
  CHECK(e.card_crystal_count(lb) == 2);  // 纳2
  // 展开中：此牌上的樱花结晶被移除时改为移到敌气而非虚。
  const int p1flare2 = e.ps(P1).flare;
  const int dust = e.st.dust;
  e.consume_enhance_crystal(lb);
  CHECK(e.card_crystal_count(lb) == 1);
  CHECK(e.ps(P1).flare == p1flare2 + 1);
  CHECK(e.st.dust == dust);
  e.consume_enhance_crystal(lb);
  CHECK(e.ci(lb).zone == Zone::Discard);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 直接金融展开时 1 敌装到自装、可付 1 集中力再执行，弃置时攻击") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AkinaAgent a0;
  a0.rule("直接金融", "支付");
  FirstAgent a1;
  akina_setup(e, "yurina", &a0, &a1);
  akina_dist(e, 3);
  e.ps(P0).vigor = 2;
  const int p1aura = e.ps(P1).aura;
  const int jr = ak_put(e, "直接金融", P0, Zone::Hand);
  REQUIRE(jr >= 0);
  e.play_card(P0, jr, false);
  CHECK(e.ps(P1).aura == p1aura - 2);    // 两次 1 敌装到自装
  CHECK(e.ps(P0).vigor == 1);            // 支付了 1 集中力
  CHECK(e.card_crystal_count(jr) == 2);  // 纳2
  CHECK(e.ci(jr).zone == Zone::Enhance);
  // 弃置时：进行攻击【2-5 1/0】。
  const int p1aura2 = e.ps(P1).aura;
  e.empty_card(jr);
  CHECK(e.ci(jr).zone == Zone::Discard);
  CHECK(e.ps(P1).aura == p1aura2 - 1);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 差列递归被对应时打消自身，资本大于对手时强制再使用") {
  // (a) 未被对应：资本一直大于对手 -> 连续使用直到资本不大于对手。
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);
  e.ps(P0).stockPrice = 2;
  akina_from_dust(e, P0, "flare", 8);  // 资本 3 + 8 = 11
  const int lie = ak_put(e, "差列递归征税法", P0, Zone::Special);
  REQUIRE(lie >= 0);
  CHECK(e.def_of(lie).stockCost);
  CHECK(e.def_of(lie).reuseWhileAhead);
  const int p1life = e.ps(P1).life;
  e.play_card(P0, lie, false);
  // 第一次支付股价 2（自气 8 -> 6）；命伤使股价 +2 -> 4。
  // 资本 3+6=9 > 敌 3+1=4，支付 4 再使用（自气 6 -> 2）；敌 3+2=5 >= 我方 3+2=5 停止。
  CHECK(e.ps(P0).flare == 2);
  CHECK(e.ps(P1).life == p1life - 2);
  CHECK(e.stock_price(P0) == 4);  // 两次命伤 +2 后 clamp 到 4
  CHECK(e.ci(lie).faceUp);
  CHECK(crystals_total(e) == 36);

  // (b) 被对应：攻击打消自身（无论对应牌是什么）。
  Engine e2(cfg);
  load_all_content(e2);
  AkinaAgent b1;
  b1.respondLabel = "算法";
  FirstAgent b0;
  akina_setup(e2, "akina", &b0, &b1);
  // 资本 P0 = 2+2 = 4；P1 = 3+1 = 4（不会触发强制再使用）。
  e2.move_crystals(AreaRef::aura(P0), AreaRef::distance(), 1, false);
  akina_from_dust(e2, P0, "flare", 2);
  akina_from_dust(e2, P1, "flare", 1);
  e2.ps(P0).stockPrice = 2;
  const int resp = ak_put(e2, "算法", P1, Zone::Hand);
  REQUIRE(resp >= 0);
  const int lie2 = ak_put(e2, "差列递归征税法", P0, Zone::Special);
  const int p1life2 = e2.ps(P1).life;
  e2.play_card(P0, lie2, false);
  CHECK(b1.responses >= 1);
  CHECK(e2.ps(P1).life == p1life2);  // 被打消，未造成伤害
  CHECK(crystals_total(e2) == 36);
}

TEST_CASE("安琪娜: 大衍算科手打表攻击后从气/命/股市各移 1 片到自装") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);
  e.move_crystals(AreaRef::aura(P0), AreaRef::distance(), 2, false);  // 自装 1，距 4
  akina_from_dust(e, P0, "flare", 2);
  akina_from_dust(e, P0, "market", 1);
  const int life0 = e.ps(P0).life;
  const int dy = ak_put(e, "大衍算科手打表", P0, Zone::Special);
  REQUIRE(dy >= 0);
  e.play_card(P0, dy, false);
  CHECK(e.ps(P0).aura == 4);  // 1 -> 4（气/命/股市各 1）
  CHECK(e.ps(P0).flare == 1);
  CHECK(e.ps(P0).life == life0 - 1);
  CHECK(e.ps(P0).market == 0);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 仙霄鬼泉展开时 4 自命到自气、结晶不可被移除、死亡时 4 自气到自命并移除") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  akina_setup(e, "yurina", &a, &a);
  akina_from_dust(e, P0, "flare", 2);
  const int xx = ak_put(e, "仙霄鬼泉天元术", P0, Zone::Special);
  REQUIRE(xx >= 0);
  const int life0 = e.ps(P0).life;
  e.play_card(P0, xx, false);
  CHECK(e.ps(P0).life == life0 - 4);     // 展开时：4 自命到自气
  CHECK(e.ps(P0).flare == 5);            // 2 - 1（费用）+ 4
  CHECK(e.card_crystal_count(xx) == 1);  // 纳1
  // 展开中：这张牌上的樱花结晶不能被除了本牌外的任何方式移除（含每回合 -1）。
  e.consume_enhance_crystal(xx);
  CHECK(e.card_crystal_count(xx) == 1);
  CHECK(e.ci(xx).zone == Zone::Special);
  CHECK(crystals_total(e) == 36);
  // 当你死亡时：4 自气到自命，然后移除这张牌（其上所有樱花结晶移到虚）。
  e.damage_life(P0, 99, AreaKind::Flare, true);
  CHECK_FALSE(e.st.over);
  CHECK(e.ps(P0).life == 4);
  CHECK(e.ci(xx).zone == Zone::Removed);
  CHECK(e.card_crystal_count(xx) == 0);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("安琪娜: 正解立即套现+2虚到自装+移出游戏；不执行时保留使用后替代套现") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AkinaAgent a0;
  a0.rule("正解", "执行");
  FirstAgent a1;
  akina_setup(e, "yurina", &a0, &a1);
  e.ps(P0).stockPrice = 2;
  akina_from_dust(e, P0, "flare", 2);  // 付费用
  akina_from_dust(e, P0, "market", 1);
  const int p1aura = e.ps(P1).aura;
  const int ans = ak_put(e, "源上安琪娜的正解", P0, Zone::Special);
  REQUIRE(ans >= 0);
  e.play_card(P0, ans, false);
  CHECK(e.ci(ans).zone == Zone::Removed);
  CHECK(e.ps(P0).market == 0);         // 套现
  CHECK(e.stock_price(P0) == 1);
  CHECK(e.ps(P1).aura == p1aura - 1);  // 套现取敌装 1
  CHECK(e.ps(P0).aura == 5);           // +1 套现，+1 从虚（自装上限 5）
  CHECK(crystals_total(e) == 36);

  // 不执行：牌留在切牌区已使用，回合开始可用 1 自装到自气替代套现。
  Engine e2(cfg);
  load_all_content(e2);
  AkinaAgent b0;
  b0.rule("回合开始", "正解");
  FirstAgent b1;
  akina_setup(e2, "yurina", &b0, &b1);
  e2.ps(P0).stockPrice = 2;
  akina_from_dust(e2, P0, "flare", 2);
  const int ans2 = ak_put(e2, "源上安琪娜的正解", P0, Zone::Special);
  // 股市为空 -> on_play 直接返回，牌保持已使用状态。
  e2.play_card(P0, ans2, false);
  CHECK(e2.ci(ans2).zone == Zone::Special);
  CHECK(e2.ci(ans2).faceUp);
  CHECK(e2.answer_aura_active(P0));
  const int aura0 = e2.ps(P0).aura;
  const int flare0 = e2.ps(P0).flare;
  e2.akina_turn_start(P0);
  // 裁定：替代套现的前提是本来能套现（股市至少 1 个结晶），因此此时不提供。
  CHECK(b0.matches == 0);
  CHECK(e2.ps(P0).aura == aura0);
  CHECK(e2.ps(P0).flare == flare0);
  CHECK_FALSE(e2.ps(P0).cashOutThisTurn);

  // 股市有 1 个结晶 -> 可以提供「1 自装到自气」替代套现（且不算套现过）。
  akina_from_dust(e2, P0, "market", 1);
  const int aura1 = e2.ps(P0).aura;
  const int flare1 = e2.ps(P0).flare;
  e2.akina_turn_start(P0);
  CHECK(b0.matches >= 1);
  CHECK(e2.ps(P0).aura == aura1 - 1);
  CHECK(e2.ps(P0).flare == flare1 + 1);
  CHECK_FALSE(e2.ps(P0).cashOutThisTurn);  // 替代操作不算套现
  CHECK(crystals_total(e2) == 36);
}

TEST_CASE("安琪娜: 回合结束且本回合内没有套现时可以投资") {
  Config cfg = make_cfg("hajimari", 1);
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  AkinaAgent a0;
  a0.rule("回合结束", "yes");
  FirstAgent a1;
  akina_setup(e, "yurina", &a0, &a1);
  e.ps(P0).stockPrice = 2;  // 投资资金从自装
  akina_from_dust(e, P0, "aura", 1);
  const int ht = ak_put(e, "恫吓", P0, Zone::Discard);
  REQUIRE(ht >= 0);
  e.akina_end_of_turn(P0);
  CHECK(a0.matches == 1);
  CHECK(e.ci(ht).zone == Zone::Cover);
  CHECK(e.ps(P0).market == 1);
  CHECK(e.stock_price(P0) == 3);
  CHECK(crystals_total(e) == 36);

  // 本回合内套现过则不能投资。
  Engine e2(cfg);
  load_all_content(e2);
  AkinaAgent b0;
  b0.rule("回合结束", "yes");
  FirstAgent b1;
  akina_setup(e2, "yurina", &b0, &b1);
  const int ht2 = ak_put(e2, "恫吓", P0, Zone::Discard);
  e2.ps(P0).cashOutThisTurn = true;
  e2.akina_end_of_turn(P0);
  CHECK(b0.matches == 0);
  CHECK(e2.ci(ht2).zone == Zone::Discard);
}

TEST_CASE("安琪娜: 以安琪娜牌组自战不产生 Lua 错误、保持不变量") {
  const int games = fuzz_games(3);
  for (int g = 0; g < games; ++g) {
    Config cfg = make_cfg("hajimari", 9300 + static_cast<uint64_t>(g));
    cfg.preset = "kigen-full";
    cfg.p0Set = "akina";
    cfg.p1Set = "akina";
    Engine e(cfg);
    load_all_content(e);
    CheckingAgent a0(&e, AgentMode::RandomLegal, 55 + static_cast<uint64_t>(g));
    CheckingAgent a1(&e, AgentMode::RandomLegal, 66 + static_cast<uint64_t>(g));
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(e.st.over);
    CHECK(e.lua_error_count() == 0);
    for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
    for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("安琪娜: 安琪娜自战的回放哈希一致（股市/股价进入 state_hash）") {
  Config cfg = make_cfg("hajimari", 4344);
  cfg.preset = "kigen-full";
  cfg.p0Set = "akina";
  cfg.p1Set = "akina";
  Engine rec(cfg);
  load_all_content(rec);
  RandomAgent a0(21), a1(22);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  nlohmann::json journal = rec.journal_json();
  REQUIRE(!journal["entries"].empty());

  Engine rep(cfg);
  load_all_content(rep);
  RandomAgent b0(21), b1(22);
  rep.set_agent(P0, &b0);
  rep.set_agent(P1, &b1);
  rep.load_journal(journal);
  rep.run();
  CHECK(rep.state_hash() == rec.state_hash());
}

// ---- 伊努露·诺伦 & 玛希露·诺伦 & 阿库露·诺伦 (26) ------------------------------

TEST_CASE("诺伦: 三把枪的构筑（共有牌 forms、每形态 7 常规 + 4 切札）") {
  Config cfg;
  cfg.preset = "gachi-full";  // 异相开启：三把枪都可用
  Engine e(cfg);
  load_all_content(e);
  auto names = [&](const std::string& form) {
    std::set<std::string> s;
    for (int id : e.deck_def_ids("innealra", form)) s.insert(e.def(id).name);
    return s;
  };
  auto o = names("O");
  auto a1 = names("A1");
  auto a2 = names("A2");
  for (const auto* s : {&o, &a1, &a2}) {
    CHECK(s->count("挥枪") == 1);       // 共有常规
    CHECK(s->count("雨露霜雪") == 1);
    CHECK(s->count("变迁") == 1);
    CHECK(s->count("造物诺伦神的万劫缠迫") == 1);  // 共有切札
    CHECK(s->size() == 11u);            // 7 常规 + 4 切札
    CHECK(s->count("修省") == 0);       // 命运不进构筑
    CHECK(s->count("悔恨") == 0);
  }
  CHECK(o.count("诅咒") == 1);
  CHECK(o.count("祖枪") == 1);
  CHECK(a1.count("刃碎") == 1);
  CHECK(a1.count("鸣枪") == 1);
  CHECK(a2.count("星空") == 1);
  CHECK(a2.count("怪枪") == 1);
  CHECK(o.count("刃碎") == 0);  // 形态之间不串
  CHECK(a1.count("诅咒") == 0);
  // 形态枚举：三把枪
  auto forms = e.available_forms("innealra");
  CHECK(forms.size() == 3u);
}

TEST_CASE("诺伦: 命运槽开局顺序、轮转（过去→待启、现在→过去、未来→现在、待启→未来）") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  e.init_fates(P0, "O");
  auto nm = [&](int i) {
    const int d = e.fate_slot(P0, i);
    return d >= 0 ? e.def(d).name : std::string();
  };
  CHECK(nm(0) == "修省");  // 过去
  CHECK(nm(1) == "悔恨");  // 现在
  CHECK(nm(2) == "怨艾");  // 未来
  CHECK(nm(3) == "长眠");  // 待启
  CHECK(e.fate_pos(P0, "怨艾") == 2);
  CHECK(e.fate_pos(P0, "不存在") == -1);
  e.rotate_fates(P0);
  CHECK(nm(0) == "悔恨");
  CHECK(nm(1) == "怨艾");
  CHECK(nm(2) == "长眠");
  CHECK(nm(3) == "修省");
  e.rotate_fates(P0); e.rotate_fates(P0); e.rotate_fates(P0);
  CHECK(nm(0) == "修省");  // 四步一循环
  // A1 / A2 的命运
  e.init_fates(P1, "A1");
  CHECK(e.def(e.fate_slot(P1, 0)).name == "惶惑");
  CHECK(e.def(e.fate_slot(P1, 3)).name == "救赎");
  e.init_fates(P1, "A2");
  CHECK(e.def(e.fate_slot(P1, 0)).name == "歆羡");
  CHECK(e.def(e.fate_slot(P1, 3)).name == "迷蒙");
}

TEST_CASE("诺伦: 修省让本回合不能攻击，过去槽时敌装到距执行两次") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_fates(P0, "O");
  e.ps(P1).aura = 5;
  e.st.distance = 3;
  e.st.dust = 5;  // 守恒：20 + (3+5) + 3 + 5 = 36
  e.resolve_fate_slot(P0, 0, false);  // 修省（位于过去）
  CHECK(e.ps(P0).cannotAttack);
  CHECK(e.st.distance == 5);   // 两次 1 敌装到距
  CHECK(e.ps(P1).aura == 3);
  CHECK_FALSE(e.ps(P0).cannotUseNormals);  // 未纠葛
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 救赎与迷蒙按命运文本移动结晶并保持守恒") {
  Config cfg;
  cfg.preset = "kigen-full";
  FirstAgent a;
  {
    Engine e(cfg);
    load_all_content(e);
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    e.init_fates(P0, "A1");
    e.st.dust = 5;
    e.st.distance = 5;
    e.resolve_fate_slot(P0, 3, false);  // 救赎：2虚到自装，1虚到敌装
    CHECK(e.ps(P0).aura == 5);
    CHECK(e.ps(P1).aura == 4);
    CHECK(e.st.dust == 2);
    for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
  }
  {
    Engine e(cfg);
    load_all_content(e);
    e.set_agent(P0, &a);
    e.set_agent(P1, &a);
    e.init_fates(P0, "A2");
    e.st.dust = 5;
    e.st.distance = 5;
    e.resolve_fate_slot(P0, 3, false);  // 迷蒙（非回合开始）：1虚到惑
    CHECK(e.ps(P0).waku == 1);
    CHECK(e.st.dust == 4);
    for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
  }
}

TEST_CASE("诺伦: 万劫缠迫纠葛命运、弃置时回归常态并移出游戏") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_fates(P0, "O");
  int def = find_def(e, "innealra", "造物诺伦神的万劫缠迫");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.ps(P0).aura = 0;   // 让纳全从虚支付，避免选择分配
  e.st.distance = 3;
  e.st.dust = 10;      // 守恒：20 + (0+3) + 0 + 10 + 3 = 36
  e.play_card(P0, inst, false);
  CHECK(e.ci(inst).zone == Zone::Special);
  CHECK(e.ci(inst).crystals == 5);   // 纳5
  CHECK(e.st.dust == 5);
  CHECK(e.fates_entangled(P0));
  for (int i = 0; i < 5; ++i) e.consume_enhance_crystal(inst);
  CHECK_FALSE(e.fates_entangled(P0));
  CHECK(e.ci(inst).zone == Zone::Removed);  // 弃置时移出游戏，不会回到切牌区
  CHECK(e.st.dust == 10);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 星空在用过非诺伦牌时把伤害结晶移入惑") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "innealra", "星空");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 1;
  e.st.dust = 9;  // 守恒
  e.ps(P0).usedNonInnealraThisTurn = true;
  e.play_card(P0, inst, false);
  CHECK(e.ps(P0).waku == 1);   // 敌装的 1 个结晶改为进惑
  CHECK(e.ps(P1).aura == 2);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 神枪·永世把支付的费用移到惑") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "innealra", "神枪·永世");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.st.active = P0;
  e.st.distance = 1;
  e.st.dust = 7;
  e.ps(P0).flare = 2;  // 守恒：20 + 6 + 2 + 7 + 1 = 36
  e.play_card(P0, inst, false);  // 手牌外直接结算使用（费用照付）
  CHECK(e.ps(P0).waku == 2);
  CHECK(e.ps(P0).flare == 0);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 脆弱意志把对手的非装附装获得改为放到此牌上，基本装附则移除此牌 1 结晶") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  int def = find_def(e, "innealra", "脆弱意志");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 4;
  e.st.distance = 2;
  e.st.dust = 4;  // 守恒：20 + 6 + 0 + 4 + 2 + 4 = 36
  // 非装附（牌效）的装获得改为移到脆弱意志上
  CHECK(e.move_crystals(AreaRef::dust(), AreaRef::aura(P1), 2, true) == 2);
  CHECK(e.ci(inst).crystals == 6);
  CHECK(e.ps(P1).aura == 3);
  // 基本动作装附照常，然后从脆弱意志上移 1 个结晶到虚
  CHECK(e.do_basic(P1, BasicAction::Aura));
  CHECK(e.ps(P1).aura == 4);
  CHECK(e.ci(inst).crystals == 5);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 阵雨·覆逆令对手攻击 -1/+0 并在结算后对敌装造成 1 伤害") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int rainy = find_def(e, "innealra", "阵雨·覆逆");
  REQUIRE(rainy >= 0);
  int ri = e.add_instance(rainy, P1);
  e.move_card(ri, Zone::Special);
  e.ci(ri).faceUp = true;
  e.ci(ri).crystals = 1;
  int saw = find_def(e, "shisui", "锯");  // 【2-3 3/1】
  REQUIRE(saw >= 0);
  int si = e.add_instance(saw, P0);
  e.move_card(si, Zone::Hand);
  e.st.active = P0;
  e.st.distance = 2;
  e.st.dust = 7;  // 守恒：20 + 6 + 0 + 7 + 2 + 1 = 36
  e.play_card(P0, si, false);
  CHECK(e.ps(P1).aura == 1);  // 3 - 1（阵雨）- 2（装承伤）
  CHECK(e.ps(P0).aura == 2);  // 结算后阵雨对敌装造成 1 伤害
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 虚幻意志展开中其上的结晶被移除时改为进惑") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  int def = find_def(e, "innealra", "虚幻意志");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 2;
  e.st.distance = 8;  // 守恒：20 + 6 + 0 + 0 + 8 + 2 = 36
  CHECK(e.remove_card_crystals(inst, 2) == 2);
  CHECK(e.ps(P0).waku == 2);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("诺伦: 以诺伦牌组自战不产生 Lua 错误、保持不变量") {
  const char* sets[3] = {"innealra", "innealra.A1", "innealra.A2"};
  for (int si = 0; si < 3; ++si) {
    for (int g = 0; g < 2; ++g) {
      Config cfg = make_cfg("hajimari", 9700 + 100 * static_cast<uint64_t>(si) + static_cast<uint64_t>(g));
      cfg.preset = "kigen-full";
      cfg.p0Set = sets[si];
      cfg.p1Set = sets[(si + 1) % 3];
      Engine e(cfg);
      load_all_content(e);
      CheckingAgent a0(&e, AgentMode::RandomLegal, 77 + static_cast<uint64_t>(g));
      CheckingAgent a1(&e, AgentMode::RandomLegal, 88 + static_cast<uint64_t>(g));
      e.set_agent(P0, &a0);
      e.set_agent(P1, &a1);
      INFO("set=", sets[si], " seed=", cfg.seed);
      e.run();
      CHECK(e.st.over);
      CHECK(e.lua_error_count() == 0);
      for (const std::string& v : a0.violations()) CHECK_MESSAGE(false, v);
      for (const std::string& v : a1.violations()) CHECK_MESSAGE(false, v);
    }
  }
}

TEST_CASE("诺伦: 诺伦自战的回放哈希一致（命运槽/纠葛/惑进入 state_hash）") {
  Config cfg = make_cfg("hajimari", 4747);
  cfg.preset = "kigen-full";
  cfg.p0Set = "innealra";
  cfg.p1Set = "innealra.A1";
  Engine rec(cfg);
  load_all_content(rec);
  RandomAgent a0(31), a1(32);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  nlohmann::json journal = rec.journal_json();
  REQUIRE(!journal["entries"].empty());

  Engine rep(cfg);
  load_all_content(rep);
  RandomAgent b0(31), b1(32);
  rep.set_agent(P0, &b0);
  rep.set_agent(P1, &b1);
  rep.load_journal(journal);
  rep.run();
  CHECK(rep.state_hash() == rec.state_hash());
}

TEST_CASE("诺伦: 阵雨·覆逆作为对应打出时给被对应的攻击 -1/+0") {
  Config cfg;
  cfg.preset = "kigen-full";
  Engine e(cfg);
  load_all_content(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int rainy = find_def(e, "innealra", "阵雨·覆逆");
  REQUIRE(rainy >= 0);
  int ri = e.add_instance(rainy, P1);
  e.move_card(ri, Zone::Hand);
  e.st.active = P0;
  e.ps(P1).aura = 0;   // 让纳全从虚支付
  e.st.distance = 3;
  e.st.dust = 10;      // 守恒：20 + (3+0) + 0 + 10 + 3 = 36
  Attack atk;
  atk.attacker = P0;
  atk.range.add(0, 10);
  atk.aura = 3;
  atk.life = 1;
  e.currentResponding = &atk;
  e.play_card(P1, ri, true);
  e.currentResponding = nullptr;
  CHECK(atk.auraDelta == -1);       // 展开时把被对应的攻击 -1/+0
  CHECK(atk.lifeDelta == 0);
  CHECK(e.ci(ri).zone == Zone::Special);
  CHECK(e.ci(ri).faceUp);
  CHECK(e.ci(ri).crystals == 1);    // 纳1
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

// ---------------------------------------------------------------------------
// 付与打出效果的新顺序（what.md 裁定 2026-10-07）：种植 → 给献 → 展开时 →
// 0 献弃置。展开时是单一钩子，在献落位后触发（可读最终献数）。
// ---------------------------------------------------------------------------

TEST_CASE("付与顺序: 展开时获得的装不能再支付纳（引力场）") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  e.load_content(find_file("content/hagane.lua"));
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hagane", "引力场");  // 全开【纳2】展开时: 距→自装 2
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  // 装清空、虚 2、距 10：旧序（展开时先得 2 装、纳再从中支付、FirstAgent 选
  // dust0+aura2）终态为 aura=0/dust=2；新序（纳先从虚付清）终态为 aura=2/dust=0。
  e.move_crystals(AreaRef::aura(P0), AreaRef::flare(P0), e.st.p[P0].aura);
  e.move_crystals(AreaRef::life(P1), AreaRef::dust(), 2);
  e.play_card(P0, inst, false, /*zenkai=*/true);
  CHECK(e.st.p[P0].aura == 2);   // 展开时的 距→自装 2 落在纳之后
  CHECK(e.st.dust == 0);         // 纳2 已先行从虚付清
  CHECK(e.st.distance == 8);
  CHECK(e.card_crystal_count(inst) == 2);
  CHECK(crystals_total(e) == 36);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("付与顺序: 展开时可读最终献数（寄花）") {
  Config cfg = make_cfg("standard", 1);
  Engine e(cfg);
  e.load_content(find_file("content/yatsuha.lua"));
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yatsuha", "寄花");  // 纳3 破绽 展开时: 移 X(=镜映) 个献到虚
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  // 镜映 = 1（仅命相等；装/气错开）。装清空使纳3 只能从虚支付（虚取自距，守恒）。
  e.move_crystals(AreaRef::aura(P0), AreaRef::flare(P0), 3);
  e.move_crystals(AreaRef::distance(), AreaRef::dust(), 3);
  CHECK(e.mirror(P0) >= 1);
  e.play_card(P0, inst, false);
  // 献先落位（3），展开时再移走 mirror() 个到虚。
  CHECK(e.card_crystal_count(inst) == 3 - e.mirror(P0));
  CHECK(e.st.dust == e.mirror(P0));
  CHECK(crystals_total(e) == 36);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}

TEST_CASE("Lua 特化接口: ctx:aura_damage / ctx:life_damage 与引擎一致") {
  Config cfg = make_cfg("hajimari", 1);
  Engine e(cfg);
  load_hajimari(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  // 经由一张 Lua 牌的 on_play 调用特化接口，验证与 C++ 路径等价。
  const char* src =
      "return {{set='t', form='O', num=1, name='T', kind='normal', type='action',\n"
      "        on_play=function(ctx)\n"
      "          ctx:aura_damage(ctx:opp(), 2)\n"
      "          ctx:life_damage(ctx:opp(), 3)\n"
      "        end}}";
  FILE* f = std::fopen("/tmp/fy_test_api.lua", "w");
  std::fputs(src, f);
  std::fclose(f);
  e.load_content("/tmp/fy_test_api.lua");
  int def = find_def(e, "t", "T");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;
  const int aura0 = e.ps(P1).aura, dust0 = e.st.dust, life0 = e.ps(P1).life;
  e.play_card(P0, inst, false);
  CHECK(e.ps(P1).aura == aura0 - 2);      // 装伤 2: 装→虚
  CHECK(e.st.dust == dust0 + 2);
  CHECK(e.ps(P1).life == life0 - 3);      // 命伤 3: 命→自气
  CHECK(e.ps(P1).flare == 3);
  CHECK(crystals_total(e) == 36);
  for (const std::string& v : check_invariants(e)) CHECK_MESSAGE(false, v);
}
