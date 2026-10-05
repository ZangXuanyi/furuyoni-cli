#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"

using namespace fy;

static std::string find_file(const std::string& rel) {
  for (const char* pre : {"", "../", "../../"}) {
    std::string p = std::string(pre) + rel;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
      std::fclose(f);
      return p;
    }
  }
  return rel;
}

static std::string content_path() { return find_file("content/hajimari.lua"); }

static void load_standard(Engine& e) {
  for (const char* f : {"content/yurina.lua", "content/saine.lua", "content/himika.lua",
                        "content/tokoyo.lua"})
    e.load_content(find_file(f));
}

TEST_CASE("range membership") {
  Range r;
  r.add(5, 9);
  CHECK(r.contains(5));
  CHECK(r.contains(9));
  CHECK_FALSE(r.contains(4));
  CHECK_FALSE(r.contains(10));
  r.add(1, 3);
  CHECK(r.contains(2));
  r.extend_far(1);
  CHECK(r.contains(10));
}

TEST_CASE("content loads 26 cards") {
  Engine e;
  e.load_content(content_path());
  CHECK(e.defs.size() == 26);
  int ukiro = 0, okika = 0;
  for (auto& d : e.defs) {
    if (d.set == "hajimari.ukiro") ukiro++;
    if (d.set == "hajimari.okika") okika++;
  }
  CHECK(ukiro == 13);
  CHECK(okika == 13);
}

TEST_CASE("simple resource moves") {
  Engine e;
  CHECK(e.amount(AreaRef::distance()) == 10);
  CHECK(e.amount(AreaRef::aura(P0)) == 3);
  e.move_crystals(AreaRef::distance(), AreaRef::aura(P0), 1);
  CHECK(e.amount(AreaRef::distance()) == 9);
  CHECK(e.amount(AreaRef::aura(P0)) == 4);
  // aura is capped at 5
  for (int i = 0; i < 10; ++i) e.move_crystals(AreaRef::dust(), AreaRef::aura(P0), 1);
  CHECK(e.amount(AreaRef::aura(P0)) <= 5);
}

TEST_CASE("full game completes with built-in agents") {
  for (uint64_t seed : {1ull, 7ull, 42ull, 123456ull}) {
    Config cfg;
    cfg.seed = seed;
    Engine e(cfg);
    e.load_content(content_path());
    FirstAgent a0, a1;
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(e.st.over);
    for (int i = 0; i < 2; ++i) {
      CHECK(e.st.p[i].life >= 0);
      CHECK(e.st.p[i].life <= 10);
      CHECK(e.st.p[i].aura >= 0);
      CHECK(e.st.p[i].aura <= 5);
      CHECK(e.st.p[i].flare >= 0);
      CHECK(e.st.p[i].vigor >= 0);
    }
  }
}

static long long crystals_total(const Engine& e) {
  long long t = 0;
  for (int i = 0; i < 2; ++i) {
    t += e.st.p[i].life + e.st.p[i].aura + e.st.p[i].flare;
  }
  t += e.st.distance + e.st.dust;
  for (const auto& ci : e.st.insts) t += ci.crystals;
  return t;
}

TEST_CASE("setup produces legal zones") {
  Config cfg;
  cfg.seed = 5;
  Engine e(cfg);
  e.load_content(content_path());
  FirstAgent a0, a1;
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);
  e.setup_match();
  for (int i = 0; i < 2; ++i) {
    // 7 normal cards total, 3 drawn into hand, 3 special cards face down.
    CHECK(e.st.p[i].deck.size() + e.st.p[i].hand.size() == 7);
    CHECK(e.st.p[i].special.size() == 3);
    CHECK(e.st.p[i].hand.size() == 3);
    for (int inst : e.st.p[i].special) CHECK_FALSE(e.ci(inst).faceUp);
  }
  // 36 crystals are always in play (dropped 献 falls to 虚, never vanishes).
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("crystals are conserved over full random games") {
  for (uint64_t seed = 0; seed < 30; ++seed) {
    Config cfg;
    cfg.seed = seed;
    Engine e(cfg);
    e.load_content(content_path());
    RandomAgent a0(seed + 1000), a1(seed + 2000);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(crystals_total(e) == 36);
  }
}

static int find_def(Engine& e, const std::string& setOrGoddess, const std::string& name) {
  for (const auto& d : e.defs)
    if ((d.set == setOrGoddess || d.goddess == setOrGoddess) && d.name == name) return d.id;
  return -1;
}

TEST_CASE("damage: X capped at 5 unless 超克; X/- moves all aura") {
  auto fresh = [&]() {
    Config cfg;
    cfg.seed = 1;
    auto e = std::make_unique<Engine>(cfg);
    e->load_content(content_path());
    FirstAgent a;
    e->set_agent(P0, &a);
    e->set_agent(P1, &a);
    return e;
  };
  // aura=7, no life -> capped to 5, moved to dust
  {
    auto e = fresh();
    e->st.p[P1].aura = 5;
    e->st.dust = 0;
    e->deal_damage(P1, 7, std::nullopt, 0);
    CHECK(e->st.p[P1].aura == 0);
    CHECK(e->st.dust == 5);
  }
  // 超克 lifts the cap
  {
    auto e = fresh();
    e->st.p[P1].aura = 5;
    e->st.dust = 0;
    e->deal_damage(P1, 7, std::nullopt, AF_Overwhelm);
    CHECK(e->st.p[P1].aura == 0);
    CHECK(e->st.dust == 5);  // cannot move more than 5
  }
  // X/- with insufficient aura moves all of it
  {
    auto e = fresh();
    e->st.p[P1].aura = 2;
    e->st.dust = 0;
    e->deal_damage(P1, 4, std::nullopt, 0);
    CHECK(e->st.p[P1].aura == 0);
    CHECK(e->st.dust == 2);
  }
  // X/Y with insufficient aura forces life damage
  {
    auto e = fresh();
    e->st.p[P1].aura = 2;
    e->st.p[P1].life = 10;
    e->st.p[P1].flare = 0;
    e->deal_damage(P1, 4, 1, 0);  // FirstAgent prefers aura option, but it is illegal
    CHECK(e->st.p[P1].aura == 2);   // untouched
    CHECK(e->st.p[P1].life == 9);   // forced life
    CHECK(e->st.p[P1].flare == 1);
  }
}

TEST_CASE("enhance with zero 献 still runs 展开时 and 弃置时") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  e.load_content(content_path());
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hajimari.ukiro", "阴之阱");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Hand);
  // Make P0's dust + aura exactly 0 while conserving crystals.
  e.move_crystals(AreaRef::aura(P0), AreaRef::flare(P1), e.st.p[P0].aura);
  // 阴之阱 has no 展开时, but its 弃置时 must fire when there is no 献.
  e.play_card(P0, inst, false);
  CHECK(e.ci(inst).zone == Zone::Discard);
  CHECK(crystals_total(e) == 36);
}

TEST_CASE("a 终端 played during the opponent's turn blocks further responses") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  e.load_content(content_path());
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hajimari.okika", "精灵联动");  // terminal enhance
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P1);
  e.move_card(inst, Zone::Hand);
  e.st.active = P0;  // it is the opponent's turn
  e.st.dust = 3;
  e.st.p[P1].aura = 0;
  e.play_card(P1, inst, true);
  CHECK(e.ps(P1).cannotRespond);
}

TEST_CASE("破绽 covers the enhancement and routes its 献 to dust") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  e.load_content(content_path());
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "hajimari.ukiro", "阴之阱");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 2;
  int dust0 = e.st.dust;
  e.lose_life(P0, 1, true);
  CHECK(e.ci(inst).zone == Zone::Cover);
  CHECK(e.ci(inst).crystals == 0);
  CHECK(e.st.dust == dust0 + 2);
  CHECK(e.ci(inst).faceUp == false);
}

TEST_CASE("standard mode: draft, build, and conservation") {
  for (uint64_t seed = 0; seed < 8; ++seed) {
    Config cfg;
    cfg.seed = seed;
    cfg.mode = "standard";
    Engine e(cfg);
    load_standard(e);
    RandomAgent a0(seed + 1), a1(seed + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.setup_match();
    for (int i = 0; i < 2; ++i) {
      CHECK(e.st.p[i].deck.size() + e.st.p[i].hand.size() == 7);
      CHECK(e.st.p[i].special.size() == 3);
      CHECK(e.st.p[i].hand.size() == 3);
      CHECK(e.observation(static_cast<Player>(i))["players"][i]["sets"].size() == 2);
    }
    CHECK(crystals_total(e) == 36);
  }
}

TEST_CASE("standard mode full random games") {
  for (uint64_t seed = 0; seed < 25; ++seed) {
    Config cfg;
    cfg.seed = seed;
    cfg.mode = "standard";
    Engine e(cfg);
    load_standard(e);
    RandomAgent a0(seed + 500), a1(seed + 900);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    CHECK(e.st.over);
    CHECK(crystals_total(e) == 36);
    for (int i = 0; i < 2; ++i) {
      CHECK(e.st.p[i].life >= 0);
      CHECK(e.st.p[i].life <= 10);
      CHECK(e.st.p[i].aura >= 0);
      CHECK(e.st.p[i].aura <= 5);
    }
  }
}

TEST_CASE("无音壁: crystals count as armor and are consumed first") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine", "无音壁");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 5;
  e.st.p[P0].aura = 0;
  e.st.dust = 0;
  CHECK(e.effective_armor(P0) == 5);
  e.deal_damage(P0, 3, std::nullopt, 0);
  CHECK(e.ci(inst).crystals == 2);
  CHECK(e.st.dust == 3);
  CHECK(e.st.p[P0].aura == 0);
}

TEST_CASE("两侧伤害 resolves aura and life together") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P0].aura = 2;
  e.st.p[P0].life = 10;
  e.st.p[P0].flare = 0;
  e.st.dust = 0;
  e.deal_damage(P0, 3, 2, AF_BothSides);
  CHECK(e.st.p[P0].aura == 0);    // only 2 available
  CHECK(e.st.dust == 2);
  CHECK(e.st.p[P0].life == 8);
  CHECK(e.st.p[P0].flare == 2);
}

TEST_CASE("圈域 crystals decay into distance") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine", "圈域");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 2;
  int d0 = e.st.distance;
  int dust0 = e.st.dust;
  e.consume_enhance_crystal(inst);
  CHECK(e.st.distance == d0 + 1);
  CHECK(e.st.dust == dust0);
  CHECK(e.ci(inst).crystals == 1);
}

TEST_CASE("迷烟 negates card-effect distance changes but not basic actions") {
  Config cfg;
  cfg.seed = 1;
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "himika", "迷烟");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Enhance);
  e.ci(inst).crystals = 3;
  e.st.distance = 10;
  CHECK(e.move_crystals(AreaRef::distance(), AreaRef::dust(), 1, true) == 0);
  CHECK(e.move_crystals(AreaRef::distance(), AreaRef::dust(), 1, false) == 1);
  CHECK(e.st.distance == 9);
}

TEST_CASE("variant forms, deck assembly, and card replacement") {
  Engine e;
  load_standard(e);
  CHECK(e.available_forms("yurina").size() == 3);
  CHECK(e.available_forms("saine").size() == 3);
  CHECK(e.available_forms("tokoyo").size() == 3);
  CHECK(e.available_forms("himika").size() == 2);  // A1 only
  int variants = 0;
  for (auto& d : e.defs)
    if (d.form != "O") variants++;
  CHECK(variants == 21);  // yurina 6 + saine 6 + himika 3 + tokoyo 6

  auto has = [&](const std::vector<int>& ids, const std::string& name) {
    for (int id : ids)
      if (e.def(id).name == name) return true;
    return false;
  };
  auto a1 = e.deck_def_ids("saine", "A1");
  CHECK(has(a1, "合奏"));          // A1-N1 replaces O-N1 八面斩
  CHECK_FALSE(has(a1, "八面斩"));
  CHECK(has(a1, "伴奏"));          // A1-N6 replaces O-N6 冲音晶
  CHECK_FALSE(has(a1, "冲音晶"));
  CHECK(has(a1, "薙刀斩"));        // unchanged O card kept
  auto o = e.deck_def_ids("saine", "O");
  CHECK(has(o, "八面斩"));
  CHECK_FALSE(has(o, "合奏"));
  // dual-goddess card (def-level check)
  for (int id : a1)
    if (e.def(id).name == "合奏") {
      bool dual = false;
      for (auto& g : e.def(id).goddesses)
        if (g == "tokoyo") dual = true;
      CHECK(dual);
    }
}

TEST_CASE("徒寄之八重樱 raises the aura cap to 8 while used") {
  Engine e;
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "tokoyo", "徒寄之八重樱");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  CHECK(e.max_aura(P0) == 8);
  e.ci(inst).faceUp = false;
  CHECK(e.max_aura(P0) == 5);
}

TEST_CASE("神座渡 locks X before paying its cost") {
  Engine e;
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "yurina", "神座渡");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ps(P0).flare = 4;
  CHECK(e.cut_cost(P0, def, inst) == 4);
  CHECK(e.load_int(inst, "X") == 4);
}

TEST_CASE("悠久之雪: choosing a 0-aura side still counts as taking aura damage") {
  Engine e;
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.st.p[P0].aura = 2;
  e.st.p[P0].life = 10;
  e.st.p[P0].flare = 0;
  e.st.dust = 0;
  e.deal_damage(P0, 0, 1, 0);  // FirstAgent picks option 0 (aura)
  CHECK(e.last_damage_side() == 1);  // aura side chosen even though amount is 0
  CHECK(e.last_damage_amount() == 0);
  CHECK(e.st.p[P0].aura == 2);
  CHECK(e.st.p[P0].life == 10);
}

TEST_CASE("绝唱绝华 ends the opponent's main phase when aura empties") {
  Engine e;
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "saine", "绝唱绝华");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P1);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = false;
  e.ps(P1).flare = 5;
  e.ps(P1).aura = 2;
  e.st.active = P0;
  e.st.distance = 5;
  Attack incoming;
  incoming.attacker = P0;
  incoming.sourceInst = -1;
  incoming.range.add(0, 10);
  incoming.aura = 2;  // aura-only: P1 must take aura and end at 0
  e.currentResponding = &incoming;
  e.play_card(P1, inst, true);  // responds; registers on_resolve on `incoming`
  e.resolve_attack(incoming);
  CHECK(e.main_aborted());
}

TEST_CASE("resetting 徒寄之八重樱 moves aura above 5 to dust") {
  Engine e;
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "tokoyo", "徒寄之八重樱");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.st.dust = 20;
  int moved = e.move_crystals(AreaRef::dust(), AreaRef::aura(P0), 8, false);
  CHECK(moved == 5);  // 3 -> 8 (cap 8)
  CHECK(e.st.p[P0].aura == 8);
  int dust = e.st.dust;
  e.reset_special(inst);
  CHECK(e.max_aura(P0) == 5);
  CHECK(e.st.p[P0].aura == 5);
  CHECK(e.st.dust == dust + 3);
}

TEST_CASE("standard mode replay reproduces the state hash") {
  Config cfg;
  cfg.seed = 77;
  cfg.mode = "standard";
  Engine rec(cfg);
  load_standard(rec);
  RandomAgent a0(3), a1(4);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  auto journal = rec.journal_json();

  Engine rep(cfg);
  load_standard(rep);
  rep.load_journal(journal);
  CHECK_NOTHROW(rep.run());
  CHECK(rep.state_hash() == rec.state_hash());
}

TEST_CASE("replay reproduces the recorded state hash") {
  Config cfg;
  cfg.seed = 314;
  Engine rec(cfg);
  rec.load_content(content_path());
  RandomAgent a0(11), a1(22);
  rec.set_agent(P0, &a0);
  rec.set_agent(P1, &a1);
  rec.start_recording();
  rec.run();
  auto journal = rec.journal_json();

  Engine rep(cfg);
  rep.load_content(content_path());
  rep.load_journal(journal);
  CHECK_NOTHROW(rep.run());
  CHECK(rep.state_hash() == rec.state_hash());
  CHECK(rep.st.winner == rec.st.winner);
}

TEST_CASE("determinism: same seed, same outcome") {
  auto play = [&](uint64_t seed) {
    Config cfg;
    cfg.seed = seed;
    Engine e(cfg);
    e.load_content(content_path());
    RandomAgent a0(seed * 2 + 1), a1(seed * 2 + 2);
    e.set_agent(P0, &a0);
    e.set_agent(P1, &a1);
    e.run();
    return std::make_tuple(e.st.turn, e.st.winner, e.st.p[0].life, e.st.p[1].life, e.st.dust);
  };
  CHECK(play(99) == play(99));
}
