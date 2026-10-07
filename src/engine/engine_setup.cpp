#include "engine/engine.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/card_names.hpp"
#include "engine/effect_host.hpp"
#include "engine/engine_internal.hpp"

namespace fy {
using namespace detail;  // NOLINT




std::vector<int> Engine::deck_def_ids(const std::string& g, const std::string& f) const {
  std::map<std::pair<int, int>, int> chosen;
  // 八叶 A1/AA1: 该形态的切札线整体取代 O 的切札线（A1/AA1 只有 1 张切牌）。
  bool solo = false;
  if (f != "O")
    for (const CardDef& d : defs)
      if (d.goddess == g && form_matches(d, f) && d.soloSpecials) solo = true;
  // 26-Innealra: 共有牌用 forms 列出适用形态；命运是不进构筑的隐藏 def。
  auto eligible = [&](const CardDef& d) {
    return !d.isPart && !d.isExtra && !d.isPoison && !d.isTransform && !d.soldier &&
           !d.isDrama && !d.isFate;
  };
  for (const CardDef& d : defs)
    if (d.goddess == g && form_matches(d, "O") && eligible(d) &&
        !(solo && d.kind == CardKind::Special))
      chosen[{static_cast<int>(d.kind), d.local}] = d.id;
  if (f != "O")
    for (const CardDef& d : defs)
      if (d.goddess == g && form_matches(d, f) && eligible(d))
        chosen[{static_cast<int>(d.kind), d.local}] = d.id;
  std::vector<int> out;
  for (const auto& [key, id] : chosen) out.push_back(id);
  return out;
}

void Engine::setup_player(Player p, const std::vector<std::pair<std::string, std::string>>& picks) {
  std::vector<int> normals, specials;
  std::vector<std::string> setIds;
  // Cross-goddess bans (铳镰禁真红凶弹 / village rules).
  std::vector<int> bannedDefs;
  for (const ComboBan& ban : cfg.comboBans) {
    bool has_a = false, has_b = false;
    for (const auto& [g, f] : picks) {
      if (g == ban.goddess_a) has_a = true;
      if (g == ban.goddess_b) has_b = true;
    }
    if (!has_a || !has_b) continue;
    for (const CardDef& d : defs)
      if (d.name == ban.card) bannedDefs.push_back(d.id);
  }
  for (const auto& [g, f] : picks) {
    setIds.push_back(f == "O" ? g : g + "." + f);
    for (int defId : deck_def_ids(g, f)) {
      if (std::find(bannedDefs.begin(), bannedDefs.end(), defId) != bannedDefs.end()) continue;
      int inst = add_instance(defId, p);
      if (def(defId).kind == CardKind::Normal)
        normals.push_back(inst);
      else
        specials.push_back(inst);
    }
  }
  playerSets_[p] = setIds;
  // 能力注册表：按所选女神置位机制位（取代运行期的 id 前缀匹配）。
  for (const std::string& s : setIds) {
    const size_t dot = s.find('.');
    ps(p).mech |= mechanic_bits(dot == std::string::npos ? s : s.substr(0, dot));
  }
  for (const std::string& s : setIds)
    if (s.rfind("kamuwi", 0) == 0) ps(p).hasCurse = true;  // 神居: 启用诅咒机制
  for (const auto& [g, f] : picks) {
    if (g == "oboro" && f == "A2") init_parts(p);
    if (g == "yukihi") ps(p).yukihi = true;
    if (g == "chikage") init_bag(p);
    if (g == "thallya") {
      ps(p).thallya = true;
      ps(p).steamEngine = 5;
      init_transforms(p, f);
    }
    if (g == "raira") ps(p).raira = true;
    if (g == "megumi") init_soil(p);  // 19-Megumi: 土壤 + 5 个绿色结晶
    if (g == "kanawe") init_dramas(p);  // 20-Kanawe: 6 张戏剧 + 起始节点
    if (g == "innealra") init_fates(p, f);  // 26-Innealra: 四个命运入命运槽
  }
  init_barracks(p, picks);

  build_from_pool(p, normals, specials);
  init_start_used(p);  // 22-Renri 道化的觉悟: 开局即使用后状态
}

// 18-Mizuki: 兵舍初始构成 = 该形态的全部士兵牌，背面向上（未动员）置于兵舍。
void Engine::init_barracks(Player p, const std::vector<std::pair<std::string, std::string>>& picks) {
  ps(p).barracks.clear();
  std::map<std::pair<int, int>, int> chosen;  // {kind, local} -> def
  for (const auto& [g, f] : picks) {
    for (const CardDef& d : defs)
      if (d.soldier && d.goddess == g && d.form == "O") chosen[{0, d.local}] = d.id;
    if (f != "O")
      for (const CardDef& d : defs)
        if (d.soldier && d.goddess == g && d.form == f) chosen[{0, d.local}] = d.id;
  }
  for (const auto& [key, defId] : chosen) {
    int copies = std::max(1, def(defId).copies);
    for (int k = 0; k < copies; ++k) {
      int inst = add_instance(defId, p);
      ci(inst).soldier = true;
      to_barracks(p, inst, false);  // 背面向上（未动员）
    }
  }
}















void Engine::add_unused_cuts(Player p) {
  std::vector<int> list;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed &&
        def_of(i).kind == CardKind::Special && !def_of(i).isExtra && !def_of(i).isPoison)
      list.push_back(i);
  for (int inst : list) {
    move_card(inst, Zone::Special);
    ci(inst).faceUp = false;
  }
}




// 女神 id → 机制位（what.md 第 2 条：能力注册表，取代运行期字符串前缀匹配）。
uint32_t mechanic_bits(const std::string& goddess) {
  if (goddess == "kamuwi") return MC_Curse;
  if (goddess == "oboro") return MC_Parts;
  if (goddess == "yukihi") return MC_Weapon;
  if (goddess == "chikage") return MC_Bag;
  if (goddess == "thallya") return MC_Steam;
  if (goddess == "raira") return MC_Slots;
  if (goddess == "megumi") return MC_Soil;
  if (goddess == "kanawe") return MC_Drama;
  if (goddess == "hatsumi") return MC_Dive;
  if (goddess == "mizuki") return MC_Barracks;
  if (goddess == "konuru") return MC_Ice;
  if (goddess == "shisui") return MC_Wound;
  if (goddess == "akina") return MC_Market;
  if (goddess == "misora") return MC_Aim;
  if (goddess == "innealra") return MC_Fate;
  return 0;
}

void Engine::setup_player_sets(Player p, const std::vector<std::string>& sets) {
  playerSets_[p] = sets;
  // 能力注册表：固定牌组路径同样按女神置位机制位。
  for (const std::string& s : sets) {
    const size_t dot = s.find('.');
    ps(p).mech |= mechanic_bits(dot == std::string::npos ? s : s.substr(0, dot));
  }
  for (const std::string& s : sets) {  // 19-Megumi: 土壤机制（固定牌组模式）
    if (s == "megumi" || s.rfind("megumi.", 0) == 0) init_soil(p);
    if (s == "kanawe" || s.rfind("kanawe.", 0) == 0) init_dramas(p);  // 20-Kanawe
    // 26-Innealra: 固定牌组模式同样要按形态放入命运。
    if (s == "innealra") init_fates(p, "O");
    else if (s.rfind("innealra.", 0) == 0) init_fates(p, s.substr(9));
  }
  std::vector<int> normals, specials;
  std::vector<int> picked;
  auto add_def = [&](int defId) {
    if (vec_has(picked, defId)) return;
    picked.push_back(defId);
    int inst = add_instance(defId, p);
    if (def(defId).kind == CardKind::Normal)
      normals.push_back(inst);
    else
      specials.push_back(inst);
  };
  for (const CardDef& d : defs) {
    if (d.isFate) continue;  // 26-Innealra: 命运不是构筑牌，不进牌组
    // 26-Innealra: 固定牌组模式也要按形态组装（含 forms 列出的共有牌）。
    bool innearla = false;
    for (const std::string& s : sets) {
      if (s != "innealra" && s.rfind("innealra.", 0) != 0) continue;
      std::string f = "O";
      auto dot = s.find('.');
      if (dot != std::string::npos) f = s.substr(dot + 1);
      if (d.goddess == "innealra" && form_matches(d, f)) innearla = true;
    }
    if (!innearla && std::find(sets.begin(), sets.end(), d.set) == sets.end()) continue;
    add_def(d.id);
  }
  build_from_pool(p, normals, specials);
  init_start_used(p);  // 22-Renri 道化的觉悟: 开局即使用后状态
}

void Engine::build_from_pool(Player p, std::vector<int>& normals, std::vector<int>& specials) {
  auto choose = [&](std::vector<int>& pool, int want, const char* what) {
    if (pool.empty()) return;
    Request r;
    r.kind = "build";
    r.prompt = std::string("choose ") + what;
    for (int inst : pool) {
      Option o;
      o.label = card_label(def_of(inst));
      o.data = card_json(def_of(inst));
      o.data["inst"] = inst;
      r.options.push_back(o);
    }
    int need = std::min(want, static_cast<int>(pool.size()));
    r.minSel = need;
    r.maxSel = need;
    r.state = observation(p);
    r.state["build_pool"] = nlohmann::json::array();
    for (auto& o : r.options) r.state["build_pool"].push_back(o.data);
    Decision d = decide(p, std::move(r));
    std::vector<int> chosen;
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(pool.size())) chosen.push_back(pool[static_cast<size_t>(i)]);
    for (int inst : pool) {
      if (static_cast<int>(chosen.size()) >= need) break;
      if (!vec_has(chosen, inst)) chosen.push_back(inst);
    }
    for (int inst : chosen) {
      if (def_of(inst).kind == CardKind::Special) {
        move_card(inst, Zone::Special);
        ci(inst).faceUp = false;  // unused 切札 start face down
      } else {
        move_card(inst, Zone::Deck);
      }
    }
  };
  choose(normals, 7, "normal cards");
  choose(specials, 3, "special cards");
  st.rng.shuffle(ps(p).deck);
  float_poisons(p);
}

void Engine::init_bag(Player p) {
  ps(p).bag.clear();
  for (const CardDef& d : defs)
    if (d.isPoison && d.goddess == "chikage") {
      for (int k = 0; k < d.copies; ++k) {
        int inst = add_instance(d.id, p);
        ci(inst).bagOwner = p;
        move_card(inst, Zone::Bag);
      }
    }
}

std::vector<std::pair<std::string, std::string>> Engine::draft_pick(Player p) {
  std::vector<std::pair<std::string, std::string>> opts;
  const std::vector<std::string> pool =
      cfg.draftPool.empty() ? goddess_pool() : cfg.draftPool;
  for (const auto& g : pool)
    for (const auto& f : available_forms(g)) opts.push_back({g, f});
  // 空抽选池（例如 --goddesses 写了不存在/未启用的女神）会让 draft_ban 越界。
  // 这里给出可读的错误，而不是未定义行为。
  if (opts.empty())
    throw std::runtime_error(
        "draft pool is empty: no enabled goddess matched the requested pool "
        "(check --goddesses / --packs / --content-dir / --allow-custom)");
  Request r;
  r.kind = "draft_pick";
  r.prompt = "choose three goddesses (with form)";
  for (const auto& [g, f] : opts) {
    Option o;
    o.label = g + " (" + f + ")";
    o.data = {{"goddess", g}, {"form", f}};
    r.options.push_back(o);
  }
  r.minSel = std::min<int>(3, static_cast<int>(opts.size()));
  r.maxSel = r.minSel;
  r.state = observation(p);
  Decision d = decide(p, std::move(r));
  std::vector<std::pair<std::string, std::string>> sel;
  auto has_goddess = [&](const std::string& g) {
    for (auto& s : sel)
      if (s.first == g) return true;
    return false;
  };
  for (int i : d.indices)
    if (i >= 0 && i < static_cast<int>(opts.size())) {
      auto c = opts[static_cast<size_t>(i)];
      if (!has_goddess(c.first)) sel.push_back(c);
    }
  for (const auto& g : pool) {
    if (static_cast<int>(sel.size()) >= 3) break;
    if (has_goddess(g)) continue;
    auto forms = available_forms(g);
    sel.push_back({g, forms.empty() ? "O" : forms[0]});
  }
  return sel;
}

std::pair<std::string, std::string> Engine::draft_ban(
    Player p, const std::vector<std::pair<std::string, std::string>>& opp) {
  if (opp.empty()) return {"", ""};  // 防御：没有可禁的柱（不应发生，见 draft_pick）
  Request r;
  r.kind = "draft_ban";
  r.prompt = "ban one of the opponent's goddesses";
  for (const auto& [g, f] : opp) {
    Option o;
    o.label = g + " (" + f + ")";
    o.data = {{"goddess", g}, {"form", f}};
    r.options.push_back(o);
  }
  r.minSel = 1;
  r.maxSel = 1;
  r.state = observation(p);
  Decision d = decide(p, std::move(r));
  int i = d.indices.empty() ? 0 : d.indices[0];
  if (i < 0 || i >= static_cast<int>(opp.size())) i = 0;
  return opp[static_cast<size_t>(i)];
}

void Engine::setup_match() {
  st = GameState{};
  st.rng = Rng(cfg.seed);
  phase_ = "setup";
  callStack_.clear();
  illegalCount_[0] = illegalCount_[1] = 0;
  forfeited_ = false;
  if (cfg.mode == "standard") {
    // 三拾一舍: pick 3 (goddess+form, secret, sync), then ban 1 of the opponent's 3.
    auto p0sel = draft_pick(P0);
    auto p1sel = draft_pick(P1);
    auto p0bans = draft_ban(P0, p1sel);  // P0 removes one of P1's
    auto p1bans = draft_ban(P1, p0sel);  // P1 removes one of P0's
    auto drop = [](std::vector<std::pair<std::string, std::string>>& v,
                   const std::pair<std::string, std::string>& s) {
      v.erase(std::remove(v.begin(), v.end(), s), v.end());
    };
    draftPicks_[0] = p0sel;
    draftPicks_[1] = p1sel;
    draftBans_[0] = p0bans;
    draftBans_[1] = p1bans;
    hasDraft_[0] = hasDraft_[1] = true;
    drop(p1sel, p0bans);
    drop(p0sel, p1bans);
    setup_player(P0, p0sel);
    setup_player(P1, p1sel);
    Player first = st.rng.below(2) == 0 ? P0 : P1;
    st.active = first;
    ps(first).vigor = 0;
    ps(opp(first)).vigor = 1;
  } else {
    setup_player_sets(P0, {cfg.p0Set});
    setup_player_sets(P1, {cfg.p1Set});
    st.active = P0;  // 最初的决斗: 虚路的碎片 is fixed first
    ps(P0).vigor = 0;
    ps(P1).vigor = 1;
  }
  draw(P0, 3);
  draw(P1, 3);

  // simultaneous, secret mulligan
  Request r0, r1;
  auto build_mul = [&](Player p, Request& r) {
    r.kind = "mulligan";
    r.prompt = "choose any cards to put on the bottom of your deck";
    for (int inst : ps(p).hand) {
      Option o;
      o.label = card_label(def_of(inst));
      o.data = card_json(def_of(inst));
      o.data["inst"] = inst;
      r.options.push_back(o);
    }
    r.minSel = 0;
    r.maxSel = static_cast<int>(ps(p).hand.size());
    r.state = observation(p);
  };
  build_mul(P0, r0);
  build_mul(P1, r1);
  Decision d0 = decide(P0, r0);
  Decision d1 = decide(P1, r1);

  auto apply_mul = [&](Player p, const Decision& d) {
    std::vector<int> chosen;
    for (int i : d.indices) chosen.push_back(ps(p).hand[static_cast<size_t>(i)]);
    for (int inst : chosen) {
      auto& h = ps(p).hand;
      h.erase(std::remove(h.begin(), h.end(), inst), h.end());
      ci(inst).zone = Zone::Deck;
      ps(p).deck.insert(ps(p).deck.begin(), inst);  // bottom
    }
    draw(p, static_cast<int>(chosen.size()));
  };
  apply_mul(P0, d0);
  apply_mul(P1, d1);
}

void Engine::run() {
  setup_match();
  while (!st.over && st.turn <= cfg.turnLimit) {
    play_turn(st.active);
    if (st.over) break;
    st.turn += 1;
    st.active = opp(st.active);
  }
  if (!st.over) {
    st.over = true;
    st.winner = -1;  // draw by turn limit
  }
}

// ---------------------------------------------------------------------------
// observation
// ---------------------------------------------------------------------------

}  // namespace fy
