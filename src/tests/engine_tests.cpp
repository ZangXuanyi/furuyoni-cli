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
