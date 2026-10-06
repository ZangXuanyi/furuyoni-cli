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

int Engine::assemble_one(Player p) {
  auto u = unassembled_parts(p);
  if (u.empty()) return -1;
  Request r;
  r.kind = "option";
  r.prompt = "组装一个零件";
  for (int inst : u) r.options.push_back({def_of(inst).name, true, {}});
  int idx = ask_one(p, std::move(r));
  idx = std::min(idx, static_cast<int>(u.size()) - 1);
  int inst = u[static_cast<size_t>(idx)];
  assemble_part(p, inst);
  return inst;
}

void Engine::disassemble_to(Player p, int maxCount) {
  while (assembled_count(p) > maxCount && !st.over) {
    auto a = assembled_parts(p);
    if (a.empty()) break;
    Request r;
    r.kind = "option";
    r.prompt = "拆除一个零件";
    for (int inst : a) r.options.push_back({def_of(inst).name, true, {}});
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(a.size()) - 1);
    disassemble_part(p, a[static_cast<size_t>(idx)]);
  }
}

void Engine::assemble_many(Player p, int x) {
  for (int i = 0; i < x; ++i) {
    if (unassembled_parts(p).empty()) break;
    assemble_one(p);
  }
}

std::vector<std::string> Engine::available_forms(const std::string& g) const {
  std::vector<std::string> forms{"O"};
  for (const CardDef& d : defs)
    if (d.goddess == g && d.form != "O" &&
        std::find(forms.begin(), forms.end(), d.form) == forms.end())
      forms.push_back(d.form);
  return forms;
}

std::vector<int> Engine::deck_def_ids(const std::string& g, const std::string& f) const {
  std::map<std::pair<int, int>, int> chosen;
  for (const CardDef& d : defs)
    if (d.goddess == g && d.form == "O" && !d.isPart && !d.isExtra && !d.isPoison && !d.isTransform)
      chosen[{static_cast<int>(d.kind), d.local}] = d.id;
  if (f != "O")
    for (const CardDef& d : defs)
      if (d.goddess == g && d.form == f && !d.isPart && !d.isExtra && !d.isPoison && !d.isTransform)
        chosen[{static_cast<int>(d.kind), d.local}] = d.id;
  std::vector<int> out;
  for (const auto& [key, id] : chosen) out.push_back(id);
  return out;
}

void Engine::setup_player(Player p, const std::vector<std::pair<std::string, std::string>>& picks) {
  std::vector<int> normals, specials;
  std::vector<std::string> setIds;
  for (const auto& [g, f] : picks) {
    setIds.push_back(f == "O" ? g : g + "." + f);
    for (int defId : deck_def_ids(g, f)) {
      int inst = add_instance(defId, p);
      if (def(defId).kind == CardKind::Normal)
        normals.push_back(inst);
      else
        specials.push_back(inst);
    }
  }
  playerSets_[p] = setIds;
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
  }

  build_from_pool(p, normals, specials);
}

void Engine::init_parts(Player p) {
  ps(p).parts.clear();
  for (const CardDef& d : defs)
    if (d.isPart && d.goddess == "oboro") {
      int inst = add_instance(d.id, p);
      move_card(inst, Zone::Parts);
      ci(inst).assembled = false;
    }
}

int Engine::assembled_count(Player p) const {
  int c = 0;
  for (int inst : ps(p).parts)
    if (ci(inst).assembled) c++;
  return c;
}

std::vector<int> Engine::unassembled_parts(Player p) const {
  std::vector<int> out;
  for (int inst : ps(p).parts)
    if (!ci(inst).assembled) out.push_back(inst);
  return out;
}

std::vector<int> Engine::assembled_parts(Player p) const {
  std::vector<int> out;
  for (int inst : ps(p).parts)
    if (ci(inst).assembled) out.push_back(inst);
  return out;
}

void Engine::set_assembled(int inst, bool v) {
  if (ci(inst).zone == Zone::Parts) ci(inst).assembled = v;
}

void Engine::transform_choose(Player p) {
  std::vector<int> cards = transform_cards(p);
  if (cards.empty()) return;
  Request r;
  r.kind = "option";
  r.prompt = "选择变形光环";
  for (int inst : cards) r.options.push_back({def_of(inst).name, true, {}});
  int c = ask_one(p, std::move(r));
  c = std::min(c, static_cast<int>(cards.size()) - 1);
  transform(p, def_of(cards[static_cast<size_t>(c)]).name);
}

void Engine::init_transforms(Player p, const std::string& form) {
  // 变形 are numbered O-TR1..; a 变格 transform with the same slot replaces the O
  // one (A1-TR1 紧那罗 -> O-TR1 夜叉), while unreplaced O transforms stay
  // available (A1 keeps O-TR2 娜迦). Slot = num % 10 (801,802,803 / 811,813,814).
  std::map<int, int> chosen;
  for (const CardDef& d : defs)
    if (d.isTransform && d.goddess == "thallya" && d.form == "O") chosen[d.local % 10] = d.id;
  if (form != "O")
    for (const CardDef& d : defs)
      if (d.isTransform && d.goddess == "thallya" && d.form == form)
        chosen[d.local % 10] = d.id;
  for (const auto& [slot, id] : chosen) add_instance(id, p);  // Zone::Removed (追加区)
}

std::vector<int> Engine::transform_cards(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed && def_of(i).isTransform)
      out.push_back(i);
  return out;
}

int Engine::active_transform_inst(Player p) const {
  int d = ps(p).transformDef;
  if (d < 0) return -1;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed && st.insts[i].def == d)
      return i;
  return -1;
}

void Engine::transform(Player p, const std::string& name) {
  for (const CardDef& d : defs)
    if (d.isTransform && d.name == name) {
      ps(p).transformDef = d.id;
      ps(p).transformCount += 1;
      if (effects_->has(d.id, "on_transform")) effects_->call(*this, d.id, "on_transform", p, -1);
      fire("transformed", p, nullptr, -1, false);
      return;
    }
}

void Engine::reset_steam_at_turn_start(Player p) {
  // 气动 steam lasts until the owner's own next turn: only that player's
  // off-engine steam returns to the exhausted module.
  ps(p).steamExhausted += ps(p).steamOnDist + ps(p).steamOnCrystal;
  ps(p).steamOnDist = 0;
  ps(p).steamOnCrystal = 0;
  ashuraExtraUsed_[p] = false;
}

std::vector<int> Engine::active_transform_defs(Player p) const {
  std::vector<int> out;
  auto add = [&](int def) {
    if (def < 0) return;
    if (std::find(out.begin(), out.end(), def) == out.end()) out.push_back(def);
  };
  add(ps(p).transformDef);
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == cards::kSokaiKaisou) add(sealed_card(inst) >= 0 ? ci(sealed_card(inst)).def : -1);
  return out;
}

bool Engine::can_burn(Player p, int x) const {
  // 燃烧X needs X counters that can actually move. Under 萨利亚的杰作 the source
  // is the exhausted module and a partial recovery is not allowed, so a burn card
  // can only be played when the full amount can be recovered.
  if (has_named_active(p, cards::kSariaNoKessaku)) return ps(p).steamExhausted >= x;
  return ps(p).steamEngine >= x;
}

bool Engine::transform_is(Player p, const std::string& name) const {
  return ps(p).transformDef >= 0 && def(ps(p).transformDef).name == name;
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

int Engine::part_by_def(Player p, int def) const {
  for (int inst : ps(p).parts)
    if (ci(inst).def == def) return inst;
  return -1;
}

void Engine::disassemble_part(Player p, int inst) {
  (void)p;
  set_assembled(inst, false);
}

void Engine::assemble_part(Player p, int inst) {
  set_assembled(inst, true);
  // Hard cap 5: must immediately disassemble until <= 5.
  while (assembled_count(p) > 5 && !st.over) {
    auto as = assembled_parts(p);
    if (as.empty()) break;
    Request r;
    r.kind = "option";
    r.prompt = "组装超过 5 个，必须拆除一个零件";
    for (int i : as) r.options.push_back({def_of(i).name, true, {}});
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(as.size()) - 1);
    disassemble_part(p, as[static_cast<size_t>(idx)]);
  }
}

void Engine::setup_player_sets(Player p, const std::vector<std::string>& sets) {
  playerSets_[p] = sets;
  std::vector<int> normals, specials;
  for (const CardDef& d : defs) {
    if (std::find(sets.begin(), sets.end(), d.set) == sets.end()) continue;
    int inst = add_instance(d.id, p);
    if (d.kind == CardKind::Normal)
      normals.push_back(inst);
    else
      specials.push_back(inst);
  }
  build_from_pool(p, normals, specials);
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
  for (const auto& g : cfg.draftPool)
    for (const auto& f : available_forms(g)) opts.push_back({g, f});
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
  for (const auto& g : cfg.draftPool) {
    if (static_cast<int>(sel.size()) >= 3) break;
    if (has_goddess(g)) continue;
    auto forms = available_forms(g);
    sel.push_back({g, forms.empty() ? "O" : forms[0]});
  }
  return sel;
}

std::pair<std::string, std::string> Engine::draft_ban(
    Player p, const std::vector<std::pair<std::string, std::string>>& opp) {
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
