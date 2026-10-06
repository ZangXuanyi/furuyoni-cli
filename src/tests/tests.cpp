#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"
#include "tools/web_replay.hpp"

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
                        "content/tokoyo.lua", "content/oboro.lua", "content/yukihi.lua",
                        "content/shinra.lua", "content/hagane.lua", "content/chikage.lua",
                        "content/kururu.lua", "content/thallya.lua", "content/raira.lua"})
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
    INFO("seed = ", seed);
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
    // 神代枝 adds crystals from outside the game, so account for those.
    CHECK(crystals_total(e) - e.external_added() == 36);
    for (int i = 0; i < 2; ++i) {
      CHECK(e.st.p[i].life >= 0);
      CHECK(e.st.p[i].life <= 10);
      CHECK(e.st.p[i].aura >= 0);
      CHECK(e.st.p[i].aura <= e.max_aura(static_cast<Player>(i)));
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
  Config cfg;
  cfg.preset = "gachi-tatsujin";  // 完全战达人: 异相 enabled
  Engine e(cfg);
  load_standard(e);
  CHECK(e.available_forms("yurina").size() == 3);
  CHECK(e.available_forms("saine").size() == 3);
  CHECK(e.available_forms("tokoyo").size() == 3);
  CHECK(e.available_forms("himika").size() == 2);  // A1 only
  CHECK(e.available_forms("oboro").size() == 3);
  int variants = 0;
  for (auto& d : e.defs)
    if (d.form != "O") variants++;
  CHECK(variants == 64);  // ... + thallya 6 + raira A1 3

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

TEST_CASE("Oboro: parts init, assemble cap, disassemble") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  // force oboro A2 for P0 by drafting directly through setup is complex; instead
  // initialize parts explicitly and exercise assemble semantics.
  e.init_parts(P0);
  CHECK(e.ps(P0).parts.size() == 7);
  CHECK(e.assembled_count(P0) == 0);
  e.assemble_part(P0, e.ps(P0).parts[0]);
  CHECK(e.assembled_count(P0) == 1);
  // cap 5: assembling a 6th must immediately disassemble (FirstAgent picks one)
  for (int i = 1; i < 6; ++i) e.assemble_part(P0, e.ps(P0).parts[i]);
  CHECK(e.assembled_count(P0) == 5);
  e.disassemble_to(P0, 2);
  CHECK(e.assembled_count(P0) == 2);
}

// Picks the maximum for card-selection requests, first enabled otherwise.
struct AllAgent : Agent {
  Decision decide(const Request& r) override {
    Decision d;
    if (r.kind == "cards") {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i)
        if (r.options[static_cast<size_t>(i)].enabled &&
            static_cast<int>(d.indices.size()) < r.maxSel)
          d.indices.push_back(i);
    } else {
      for (int i = 0; i < static_cast<int>(r.options.size()); ++i)
        if (r.options[static_cast<size_t>(i)].enabled) {
          d.indices.push_back(i);
          break;
        }
    }
    return d;
  }
};

TEST_CASE("Oboro: electronic setup forms an attack and unassembles the parts") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  AllAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_parts(P0);
  int mp1 = find_def(e, "oboro", "核心零件X");
  int cp2 = find_def(e, "oboro", "附加零件B");
  int cp3 = find_def(e, "oboro", "附加零件C");
  REQUIRE(mp1 >= 0);
  REQUIRE(cp2 >= 0);
  REQUIRE(cp3 >= 0);
  e.assemble_part(P0, e.part_by_def(P0, mp1));
  e.assemble_part(P0, e.part_by_def(P0, cp2));
  e.assemble_part(P0, e.part_by_def(P0, cp3));
  CHECK(e.assembled_count(P0) == 3);
  e.st.active = P0;
  e.st.distance = 4;
  e.st.p[P1].aura = 3;
  e.st.p[P1].life = 10;
  int a0 = e.st.p[P1].aura, l0 = e.st.p[P1].life;
  e.do_electronic_setup(P0);
  CHECK(e.assembled_count(P0) == 0);  // selected parts become unassembled
  bool damaged = e.st.p[P1].aura != a0 || e.st.p[P1].life != l0;
  CHECK(damaged);
}

TEST_CASE("Oboro: 神代枝 adds external crystals, grants EX, removes itself") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int def = find_def(e, "oboro", "神代枝");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ps(P0).parts.clear();
  int aura0 = e.st.p[P0].aura, flare0 = e.st.p[P0].flare;
  e.play_card(P0, inst, false);
  CHECK(e.st.p[P0].aura == aura0 + 1);
  CHECK(e.st.p[P0].flare == flare0 + 1);
  CHECK(e.external_added() == 2);
  CHECK(e.ci(inst).zone == Zone::Removed);
  bool hasExtra = false;
  for (int s : e.ps(P0).special)
    if (e.def_of(s).name == "最后的结晶") hasExtra = true;
  CHECK(hasExtra);
}

TEST_CASE("Yukihi: weapon switch, 即再起, and shared range") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).yukihi = true;
  CHECK(e.umbrella(P0));  // starts in 伞
  int def = find_def(e, "yukihi", "纷扬如雪");
  REQUIRE(def >= 0);
  int inst = e.add_instance(def, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.switch_weapon(P0);
  CHECK_FALSE(e.umbrella(P0));
  CHECK_FALSE(e.ci(inst).faceUp);  // 即再起: reset on weapon switch
  // 无常其心 grants shared range while expanded
  int wu = find_def(e, "yukihi", "无常其心");
  REQUIRE(wu >= 0);
  int wi = e.add_instance(wu, P0);
  e.move_card(wi, Zone::Special);
  e.ci(wi).faceUp = true;
  e.ci(wi).crystals = 7;  // 无常其心 is 【纳7】; an expanded 付与 needs 献
  CHECK(e.shared_range(P0));
}

TEST_CASE("Shinra: strategy, seal/return, negate-damage") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  CHECK(e.strategy(P0) == 0);  // starts at 神算
  e.prepare_strategy(P0);
  CHECK(e.ps(P0).strategyKnown == false);

  int host = e.add_instance(find_def(e, "shinra", "论破"), P0);
  int victim = e.add_instance(find_def(e, "yurina", "斩"), P1);
  e.move_card(victim, Zone::Discard);
  e.seal_card(host, victim);
  CHECK(e.ci(victim).zone == Zone::Sealed);
  e.return_sealed(host);
  CHECK(e.ci(victim).zone == Zone::Discard);

  // negate damage only: attack still "hits" but the defender takes nothing
  Attack atk;
  atk.attacker = P0;
  atk.sourceInst = -1;
  atk.range.add(0, 10);
  atk.aura = 2;
  atk.negateDamage = true;
  e.st.distance = 5;
  e.st.p[P1].aura = 3;
  int before = e.st.p[P1].aura;
  e.resolve_attack(atk);
  CHECK(e.st.p[P1].aura == before);
  CHECK(atk.hit);
}

TEST_CASE("Hagane: keyword flags and the 离心 condition") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int lx = find_def(e, "hagane", "离心击");
  REQUIRE(lx >= 0);
  CHECK(e.def(lx).centrifugal);
  CHECK((e.def(lx).flags & CF_Terminal) != 0);
  int yl = find_def(e, "hagane", "引力场");
  REQUIRE(yl >= 0);
  CHECK(e.def(yl).zenkai);
  int lc = find_def(e, "hagane", "炼成攻击");
  REQUIRE(lc >= 0);
  CHECK(e.def(lc).isExtra);
  // 离心: distance >= turn-start + 2, and not attacked this turn.
  e.st.distance = 5;
  CHECK(e.centrifugal_ok(P0));
  e.st.distance = 1;
  CHECK_FALSE(e.centrifugal_ok(P0));
}

TEST_CASE("Chikage: poison bag, protection, float, and distance aura") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.init_bag(P0);
  CHECK(e.ps(P0).bag.size() == 5);
  // poison cannot be discarded
  int pz = e.ps(P0).bag[0];
  e.move_card(pz, Zone::Discard);
  CHECK(e.ci(pz).zone == Zone::Bag);
  // poison floats to the top of the deck after a shuffle
  int p2 = e.ps(P0).bag[1];
  e.move_card(p2, Zone::Deck);
  int normal = e.add_instance(find_def(e, "yurina", "斩"), P0);
  e.move_card(normal, Zone::Deck);
  e.float_poisons(P0);
  CHECK(e.def_of(e.ps(P0).deck.back()).isPoison);
  // 蹑足 continuously reduces the effective distance by 2
  int shen = find_def(e, "chikage", "蹑足");
  REQUIRE(shen >= 0);
  int si = e.add_instance(shen, P0);
  e.move_card(si, Zone::Enhance);
  e.ci(si).crystals = 4;
  e.st.distance = 5;
  CHECK(e.distance() == 3);
}

TEST_CASE("Kururu: 机巧 color pool and counts") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int atk = e.add_instance(find_def(e, "yurina", "斩"), P0);      // 攻击 = 红
  int act = e.add_instance(find_def(e, "yurina", "身法"), P0);    // 行动 = 蓝
  e.move_card(atk, Zone::Discard);
  e.move_card(act, Zone::Discard);
  CHECK(e.keisou(P0, "R"));
  CHECK(e.keisou(P0, "B"));
  CHECK_FALSE(e.keisou(P0, "RR"));
  // a face-down (covered) card does not contribute
  e.move_card(act, Zone::Cover);
  CHECK_FALSE(e.keisou(P0, "B"));
}

TEST_CASE("Thallya: steam burn / recover / pneumatic") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).thallya = true;
  e.ps(P0).steamEngine = 5;
  e.burn(P0, 2);
  CHECK(e.ps(P0).steamEngine == 3);
  CHECK(e.ps(P0).steamExhausted == 2);
  e.recover(P0, 1);
  CHECK(e.ps(P0).steamEngine == 4);
  CHECK(e.ps(P0).steamExhausted == 1);
  e.ps(P0).steamEngine = 5;
  int d0 = e.st.distance;
  e.pneumatic(P0);  // FirstAgent picks 距离 +1
  CHECK(e.ps(P0).steamEngine == 4);
  CHECK(e.distance() == d0 + 1);
  e.reset_steam_at_turn_start(P0);
  CHECK(e.ps(P0).steamOnDist == 0);
  CHECK(e.ps(P0).steamExhausted == 2);  // the 气动 steam went to the exhausted module
}

TEST_CASE("Kururu: 骇客装置 doubles a 机巧 slot's numbers") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;  // picks the first option: "翻倍"
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  int hk = find_def(e, "kururu", "枢的骇客装置");
  REQUIRE(hk >= 0);
  int inst = e.add_instance(hk, P0);
  e.move_card(inst, Zone::Special);
  e.ci(inst).faceUp = true;
  e.ci(inst).crystals = 1;
  CHECK(e.keisou_amount(P0, 5) == 10);   // doubled
  CHECK(e.ci(inst).crystals == 0);       // spent one crystal
  CHECK(e.keisou_amount(P0, 5) == 10);   // same resolution: no further spend
}

TEST_CASE("Kururu: 神涉装置 borrows a cut; self win/loss applies to the user") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  // P1's used 炎天 (attack-after: if the opponent is alive, the user dies).
  int enTen = find_def(e, "himika", "炎天·红绯弥香");
  REQUIRE(enTen >= 0);
  int borrow = e.add_instance(enTen, P1);
  e.move_card(borrow, Zone::Special);
  e.ci(borrow).faceUp = true;
  e.st.active = P0;
  e.st.distance = 5;  // 炎天 range 0-7 -> 3/3 both sides
  e.st.p[P1].aura = 3;
  e.st.p[P1].life = 10;
  // P0 "uses" it via 神涉装置: the user (P0) must suffer the special loss.
  e.use_foreign_card(P0, borrow);
  CHECK(e.st.over);
  CHECK(e.st.winner == P1);  // P0 died
}

TEST_CASE("Raira: 风雷 slots and 岚之力 spend") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.ps(P0).raira = true;
  e.ps(P0).wind = 5;
  e.ps(P0).thunder = 3;
  CHECK(e.raira_can(P0, "wind", 5));
  CHECK(e.raira_can(P0, "thunder", 3));
  CHECK_FALSE(e.raira_can(P0, "wind", 6));
  CHECK(e.raira_spend(P0, "wind", 2));
  CHECK(e.ps(P0).wind == 3);
  CHECK_FALSE(e.raira_spend(P0, "thunder", 5));
}

TEST_CASE("draft never keeps two forms of the same goddess") {
  Config cfg;
  cfg.seed = 1;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  FirstAgent a;  // picks the first 3 options: yurina O / yurina A1 / yurina A2
  e.set_agent(P0, &a);
  e.set_agent(P1, &a);
  e.setup_match();
  for (int i = 0; i < 2; ++i) {
    auto sets = e.observation(static_cast<Player>(i))["players"][i]["sets"];
    REQUIRE(sets.size() == 2);
    auto goddess = [](const std::string& s) { return s.substr(0, s.find('.')); };
    CHECK(goddess(sets[0].get<std::string>()) != goddess(sets[1].get<std::string>()));
  }
}

TEST_CASE("trace captures frames, stack, draft, and result") {
  Config cfg;
  cfg.seed = 7;
  cfg.mode = "standard";
  Engine e(cfg);
  load_standard(e);
  RandomAgent a0(1), a1(2);
  e.set_agent(P0, &a0);
  e.set_agent(P1, &a1);
  e.start_trace();
  e.run();
  auto t = e.trace_json();
  CHECK(t["frames"].size() > 10);
  CHECK(t["frames"][0]["state"].contains("stack"));
  CHECK(t.contains("draft"));
  std::string rtext = t["result"]["text"].get<std::string>();
  bool hasVerdict = rtext.find("胜") != std::string::npos || rtext.find("平局") != std::string::npos;
  CHECK(hasVerdict);
  const auto& mid = t["frames"][t["frames"].size() / 2]["state"];
  CHECK(mid.contains("players"));
  CHECK(mid["players"].size() == 2);
  std::string html = render_replay_html(t);
  CHECK(html.find("const DATA =") != std::string::npos);
  CHECK(html.find("</script>") != std::string::npos);
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
