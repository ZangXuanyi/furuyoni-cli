#include "engine/engine.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "engine/card_names.hpp"
#include "engine/effect_host.hpp"
#include "engine/engine_internal.hpp"

namespace fy {
using namespace detail;  // NOLINT

Engine::Engine(Config c) : cfg(std::move(c)) {
  effects_ = std::make_unique<EffectHost>();
  effects_->set_strict(cfg.strictLua);
}

int Engine::lua_error_count() const { return effects_->error_count(); }

Engine::~Engine() = default;

void Engine::load_content(const std::string& f) { effects_->load_file(f, defs); }

void Engine::set_agent(Player p, Agent* a) { agents_[p] = a; }

Decision Engine::decide(Player p, Request req) {
  req.player = p;
  if (req.state.is_null()) req.state = observation(p);

  int fi = -1;
  if (tracing_) {
    using nlohmann::json;
    json f;
    f["label"] = req.kind;
    f["player"] = static_cast<int>(p);
    f["prompt"] = req.prompt;
    f["options"] = json::array();
    for (const Option& o : req.options) {
      json jo;
      jo["label"] = o.label;
      jo["enabled"] = o.enabled;
      if (!o.data.is_null()) jo["data"] = o.data;
      f["options"].push_back(jo);
    }
    f["state"] = full_state_json();
    frames_.push_back(std::move(f));
    fi = static_cast<int>(frames_.size()) - 1;
  }

  Decision d;
  if (replaying_) {
    if (replay_pos_ >= journal_.size()) throw std::runtime_error("replay: journal exhausted");
    const JournalEntry& e = journal_[replay_pos_++];
    if (e.player != p || e.kind != req.kind)
      throw std::runtime_error("replay: decision mismatch (kind " + e.kind + " vs " + req.kind + ")");
    d = Decision{e.indices};
  } else {
    d = agents_[p] ? agents_[p]->decide(req) : FirstAgent{}.decide(req);
  }
  // Untrusted input (external agents, malformed journals): repair before use.
  d = sanitize_decision(p, req, std::move(d));
  if (replaying_ && illegalCount_[p] > 0)
    throw std::runtime_error("replay: journal contains an illegal decision");
  if (recording_) journal_.push_back({p, req.kind, d.indices});

  if (tracing_ && fi >= 0) {
    using nlohmann::json;
    json ch = json::array();
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(req.options.size())) ch.push_back(req.options[i].label);
    frames_[static_cast<size_t>(fi)]["choice"] = ch;
  }
  return d;
}

// Repair an untrusted decision so that the engine can never index out of range
// or act on a disabled option. Anything that had to be changed is counted as an
// illegal move for that player.

Decision Engine::sanitize_decision(Player p, const Request& req, Decision d) {
  const int n = static_cast<int>(req.options.size());
  std::vector<int> enabled;
  enabled.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
    if (req.options[static_cast<size_t>(i)].enabled) enabled.push_back(i);

  // With no selectable option there is nothing the agent could have done right,
  // so do not punish it (and never ask it again in this request).
  if (enabled.empty()) return Decision{};

  std::vector<int> kept;
  kept.reserve(d.indices.size());
  bool illegal = false;
  for (int i : d.indices) {
    if (i < 0 || i >= n || !req.options[static_cast<size_t>(i)].enabled) {
      illegal = true;
      continue;
    }
    if (std::find(kept.begin(), kept.end(), i) != kept.end()) {
      illegal = true;
      continue;
    }
    kept.push_back(i);
  }

  int mn = std::max(0, req.minSel);
  int mx = req.maxSel < 0 ? n : req.maxSel;
  if (mx > n) mx = n;
  if (mn > static_cast<int>(enabled.size())) mn = static_cast<int>(enabled.size());
  if (static_cast<int>(kept.size()) > mx) {
    illegal = true;
    kept.resize(static_cast<size_t>(mx));
  }
  if (static_cast<int>(kept.size()) < mn) {
    illegal = true;
    for (int i : enabled) {
      if (static_cast<int>(kept.size()) >= mn) break;
      if (std::find(kept.begin(), kept.end(), i) != kept.end()) continue;
      kept.push_back(i);
    }
  }
  if (illegal)
    count_illegal(p, std::string("kind=") + req.kind + " prompt=" + req.prompt);
  return Decision{kept};
}

void Engine::count_illegal(Player p, const std::string& why) {
  illegalCount_[p] += 1;
  if (st.over) return;
  if (cfg.illegalTolerance >= 0 && illegalCount_[p] > cfg.illegalTolerance) {
    st.over = true;
    st.winner = opp(p);
    forfeited_ = true;
    if (tracing_) {
      frames_.push_back({{"label", "forfeit"},
                         {"player", static_cast<int>(p)},
                         {"reason", why},
                         {"illegalCount", illegalCount_[p]}});
    }
  }
}

int Engine::ask_one(Player p, Request req) {
  if (req.state.is_null()) req.state = observation(p);
  if (req.minSel < 1) req.minSel = 1;
  Decision d = decide(p, std::move(req));
  if (d.indices.empty()) return 0;
  return d.indices[0];
}

bool Engine::ask_yes_no(Player p, const std::string& prompt) {
  Request r;
  r.kind = "option";
  r.prompt = prompt;
  r.options.push_back({"yes", true, {}});
  r.options.push_back({"no", true, {}});
  return ask_one(p, std::move(r)) == 0;
}

// ---------------------------------------------------------------------------
// state primitives
// ---------------------------------------------------------------------------

int Engine::add_instance(int defId, Player owner) {
  CardInstance c;
  c.inst = static_cast<int>(st.insts.size());
  c.def = defId;
  c.owner = owner;
  c.holder = owner;
  c.zone = Zone::Removed;
  st.insts.push_back(c);
  return c.inst;
}

void Engine::move_card(int inst, Zone z) {
  CardInstance& c = ci(inst);
  // Poison cards cannot be discarded or covered by any means (only played/removed).
  if (!poisonForce_ && c.def >= 0 && def_of(inst).isPoison &&
      (z == Zone::Discard || z == Zone::Cover))
    return;
  // An expanded enhancement that leaves play keeps no 献: route them to dust/decay.
  bool leavingEnhance =
      c.crystals > 0 && (c.zone == Zone::Enhance || (c.zone == Zone::Special && c.faceUp));
  if (leavingEnhance && z != Zone::Enhance && z != Zone::Special) {
    int cr = c.crystals;
    c.crystals = 0;
    if (defs[static_cast<size_t>(c.def)].decayTo == "distance")
      st.distance += cr;
    else
      st.dust += cr;
  }
  Zone old = c.zone;
  if (auto* v = zone_ptr(st, c.holder, old))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  // A borrowed card (诡辩/引用) is on the user's side while in use, but returns
  // to its true owner as soon as it leaves the field (Limbo keeps the holder).
  if (z != Zone::Enhance && z != Zone::Special && z != Zone::Limbo) c.holder = c.owner;
  c.zone = z;
  if (auto* v = zone_ptr(st, c.holder, z)) v->push_back(inst);
  if (z == Zone::Discard && old != Zone::Discard)
    fire("discarded", c.owner, nullptr, inst, false);  // 提婆
}

void Engine::move_card_top(int inst) {
  move_card(inst, Zone::Deck);  // deck top == back of the vector
}

void Engine::move_card_bottom(int inst) {
  CardInstance& c = ci(inst);
  if (auto* v = zone_ptr(st, c.holder, c.zone))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  c.holder = c.owner;  // leaving the field sends a borrowed card home
  c.zone = Zone::Deck;
  ps(c.holder).deck.insert(ps(c.holder).deck.begin(), inst);  // bottom == front
}

int Engine::amount(AreaRef a) const {
  switch (a.kind) {
    case AreaKind::Life:     return st.p[a.p].life;
    case AreaKind::Aura:     return st.p[a.p].aura;
    case AreaKind::Flare:    return st.p[a.p].flare;
    case AreaKind::Distance: return st.distance;
    case AreaKind::Dust:     return st.dust;
    case AreaKind::Card:     return ci(a.inst).crystals;
  }
  return 0;
}

void Engine::add_crystals(AreaRef a, int n) {
  switch (a.kind) {
    case AreaKind::Life:
      st.p[a.p].life = std::clamp(st.p[a.p].life + n, 0, st.maxLife);
      break;
    case AreaKind::Aura:
      st.p[a.p].aura = std::clamp(st.p[a.p].aura + n, 0, max_aura(a.p));
      break;
    case AreaKind::Flare:    st.p[a.p].flare = std::max(0, st.p[a.p].flare + n); break;
    case AreaKind::Distance: st.distance = std::max(0, st.distance + n); break;
    case AreaKind::Dust:     st.dust = std::max(0, st.dust + n); break;
    case AreaKind::Card:     ci(a.inst).crystals = std::max(0, ci(a.inst).crystals + n); break;
  }
}

int Engine::move_crystals(AreaRef from, AreaRef to, int n, bool cardEffect) {
  if (n <= 0) return 0;
  // 迷烟: card effects that would change distance are negated (basic actions are not).
  if (cardEffect && (from.kind == AreaKind::Distance || to.kind == AreaKind::Distance) &&
      any_lock_distance())
    return 0;
  int cap = std::numeric_limits<int>::max();
  switch (to.kind) {
    case AreaKind::Life: cap = st.maxLife - st.p[to.p].life; break;
    case AreaKind::Aura: cap = max_aura(to.p) - st.p[to.p].aura; break;
    default: break;
  }
  int moved = std::min({n, amount(from), cap});
  if (moved <= 0) return 0;
  int a0 = st.p[P0].aura, a1 = st.p[P1].aura;
  add_crystals(from, -moved);
  add_crystals(to, moved);
  if (st.p[P0].aura != a0) notify_aura_changed(P0);
  if (st.p[P1].aura != a1) notify_aura_changed(P1);
  return moved;
}

bool Engine::any_lock_distance() const {
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    for (int inst : ps(o).enhance)
      if (def_of(inst).lockDistance) return true;
    for (int inst : ps(o).special)
      if (enhance_active(inst) && def_of(inst).lockDistance) return true;
  }
  return false;
}

int Engine::cut_cost(Player p, int defId, int inst) {
  int base = effects_->has_hook(defId, "cost")
                 ? std::max(0, effects_->eval_cost(*this, defId, p, inst))
                 : std::max(0, def(defId).cost);
  int delta = ps(p).cutCostDelta + (ps(p).cutCostPermanent ? -1 : 0);
  int cost = std::max(0, base + delta);
  // "使用后" cost auras (壮绝旅程: your cuts stop consuming 气).
  for (int other : ps(p).special) {
    if (!ci(other).faceUp) continue;
    if (!effects_->has_continuous(def_of(other).id)) continue;
    cost = effects_->eval_continuous_cost(*this, p, other, cost);
  }
  for (int other : ps(p).enhance) {
    if (!effects_->has_continuous(def_of(other).id)) continue;
    cost = effects_->eval_continuous_cost(*this, p, other, cost);
  }
  return std::max(0, cost);
}

bool Engine::playable_card(Player p, int inst) {
  const CardDef& d = def_of(inst);
  if (effects_->has_hook(d.id, "playable")) return effects_->eval_pred(*this, d.id, "playable", p, inst);
  return true;
}

bool Engine::respondable_card(Player p, int inst) {
  const CardDef& d = def_of(inst);
  // A `respond` predicate can both enable a non-对应 card (识破) and further
  // restrict a 对应 card (终焉: only as a response to a 切札 attack), so it must
  // be consulted even when the card carries the 对应 keyword.
  if (effects_->has_hook(d.id, "respond"))
    return effects_->eval_pred(*this, d.id, "respond", p, inst);
  return (d.flags & CF_Response) != 0;
}

void Engine::reveal_hand(Player p) {
  (void)p;  // 仅当下公开一次；CLI 的观察是按需拉取的，无持续机械影响
}

void Engine::remove_card(int inst) { move_card(inst, Zone::Removed); }

void Engine::gain_external(AreaRef a, int n) {
  int before = amount(a);
  add_crystals(a, n);
  externalAdded_ += amount(a) - before;
}

int Engine::gain_extra(Player p, const std::string& name) {
  for (const CardDef& d : defs)
    if (d.isExtra && d.name == name) {
      int inst = add_instance(d.id, p);
      move_card(inst, Zone::Special);
      ci(inst).faceUp = false;
      return inst;
    }
  return -1;
}

void Engine::give_cower(Player p) { ps(p).cower = true; }

void Engine::gain_vigor(Player p, int n) {
  PlayerState& s = ps(p);
  if (s.cower) {
    s.cower = false;  // 畏缩: skip this gain
    return;
  }
  s.vigor = std::min(2, s.vigor + n);
}

void Engine::draw(Player p, int n) {
  for (int i = 0; i < n; ++i) {
    if (st.over) return;
    PlayerState& s = ps(p);
    if (s.deck.empty()) {
      // failed draw -> 焦躁 1/1, no source, unrespondable
      deal_damage(p, 1, 1, AF_Unrespondable);
    } else {
      int inst = s.deck.back();
      s.deck.pop_back();
      ci(inst).zone = Zone::Hand;
      ci(inst).faceUp = true;
      s.hand.push_back(inst);
    }
  }
}

void Engine::rebuild(Player p, bool costLife) {
  if (costLife) {
    lose_life(p, 1, false);  // rebuild life loss does not trigger 破绽
    if (st.over) return;
  }
  PlayerState& s = ps(p);
  std::vector<int> all;
  all.insert(all.end(), s.deck.begin(), s.deck.end());
  all.insert(all.end(), s.discard.begin(), s.discard.end());
  all.insert(all.end(), s.cover.begin(), s.cover.end());
  s.deck = std::move(all);
  s.discard.clear();
  s.cover.clear();
  for (int inst : s.deck) {
    ci(inst).zone = Zone::Deck;
    ci(inst).faceUp = true;
  }
  st.rng.shuffle(s.deck);
  float_poisons(p);  // poison cards rise to the top after a shuffle
  rebuiltThisTurn_[p] = true;
  fire("rebuilt", p, nullptr, -1, false);  // 紧那罗
}

void Engine::run_rebuild(Player p) {
  bool canElectronic = false;
  for (int inst : assembled_parts(p))
    if (def_of(inst).corePart) canElectronic = true;

  Request r;
  r.kind = "option";
  r.prompt = "重铸牌库？";
  r.options.push_back({"不重铸", true, {}});
  r.options.push_back({"正常重铸（1 命伤 + 洗牌）", true, {}});
  if (canElectronic) r.options.push_back({"电子设置（替换整次重铸）", true, {}});
  int c = ask_one(p, std::move(r));
  if (c == 0) return;
  if (canElectronic && c == 2) {
    do_electronic_setup(p);
    return;
  }

  // normal rebuild: 胧文书 etc. ("当你将要重铸牌库时"), then optional 设置 cards.
  fire("before_rebuild", p, nullptr, -1, false);
  if (st.over) return;
  auto has_named = [&](const std::string& nm) {
    for (int inst : ps(p).enhance)
      if (def_of(inst).name == nm) return true;
    for (int inst : ps(p).special)
      if (ci(inst).faceUp && def_of(inst).name == nm) return true;
    return false;
  };
  // Ask for a 设置 card from the cover pile that passes `filter`.
  auto ask_setup = [&](bool nonAttackOnly, const std::string& prompt) -> int {
    std::vector<int> setupCards;
    for (int inst : ps(p).cover) {
      const CardDef& d = def_of(inst);
      if (!d.setupCard) continue;
      if (nonAttackOnly && d.type == CardType::Attack) continue;
      setupCards.push_back(inst);
    }
    if (setupCards.empty()) return -1;
    Request sr;
    sr.kind = "option";
    sr.prompt = prompt;
    sr.options.push_back({"不使用", true, {}});
    for (int inst : setupCards) sr.options.push_back({card_label(def_of(inst)), true, {}});
    int sc = ask_one(p, std::move(sr));
    if (sc <= 0 || sc > static_cast<int>(setupCards.size())) return -1;
    return setupCards[static_cast<size_t>(sc - 1)];
  };

  // Base 设置 (one card, any setup card); 忍步 played this way grants one more.
  int allowance = 1;
  for (int used = 0; used < allowance && !st.over; ++used) {
    int chosen = ask_setup(false, "设置：从盖牌区使用一张设置牌？");
    if (chosen < 0) break;
    play_from_cover(p, chosen, false, true);  // setup cards go to the deck
    if (def_of(chosen).name == cards::kNinbu) allowance += 1;
    if (st.over) return;
  }
  // 虚鱼: an independent extra 设置 card, restricted to non-attack cards. It is
  // available even when the base 设置 was declined.
  if (has_named(cards::kXuYu) && !st.over) {
    int chosen = ask_setup(true, "虚鱼：额外使用一张非攻击设置牌？");
    if (chosen >= 0) play_from_cover(p, chosen, false, true);
    if (st.over) return;
  }
  rebuild(p, true);  // 1 life loss + shuffle
}

void Engine::do_electronic_setup(Player p) {
  std::vector<int> cores, adds;
  for (int inst : assembled_parts(p)) {
    if (def_of(inst).corePart)
      cores.push_back(inst);
    else
      adds.push_back(inst);
  }
  if (cores.empty()) return;

  Request r;
  r.kind = "option";
  r.prompt = "电子设置：选择核心零件";
  for (int inst : cores) r.options.push_back({def_of(inst).name, true, {}});
  int c0 = ask_one(p, std::move(r));
  c0 = std::min(c0, static_cast<int>(cores.size()) - 1);
  int core = cores[static_cast<size_t>(c0)];

  std::vector<int> chosenAdds;
  if (!adds.empty()) {
    Request ar;
    ar.kind = "cards";
    ar.prompt = "电子设置：选择任意数量的附加零件";
    for (int inst : adds) {
      Option o;
      o.label = def_of(inst).name;
      o.data = {{"inst", inst}};
      ar.options.push_back(o);
    }
    ar.minSel = 0;
    ar.maxSel = static_cast<int>(adds.size());
    Decision d = decide(p, std::move(ar));
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(adds.size())) chosenAdds.push_back(adds[static_cast<size_t>(i)]);
  }

  Attack a = make_attack(p, core, false, true);
  int n = static_cast<int>(chosenAdds.size());
  for (int cp : chosenAdds) effects_->apply_part(*this, ci(cp).def, p, a, n, "apply");
  resolve_attack(a);
  if (a.hit) {
    for (int cp : chosenAdds) effects_->apply_part(*this, ci(cp).def, p, a, n, "after");
    // The core part's own 攻击后 text (e.g. 核心零件Z) must resolve too.
    if (effects_->has(ci(core).def, "on_attack_after"))
      effects_->call(*this, ci(core).def, "on_attack_after", p, core);
  }
  disassemble_part(p, core);
  for (int cp : chosenAdds) disassemble_part(p, cp);
  check_win();
}

std::string Engine::card_zone(int inst) const {
  switch (ci(inst).zone) {
    case Zone::Deck: return "deck";
    case Zone::Hand: return "hand";
    case Zone::Discard: return "discard";
    case Zone::Cover: return "cover";
    case Zone::Enhance: return "enhance";
    case Zone::Special: return "special";
    case Zone::Parts: return "parts";
    case Zone::Sealed: return "sealed";
    case Zone::Bag: return "bag";
    case Zone::Removed: return "removed";
    case Zone::Limbo: return "limbo";
  }
  return "?";
}

void Engine::start_phase(Player p) {
  gain_vigor(p, 1);
  std::vector<int> list;
  for (int inst : ps(p).enhance) list.push_back(inst);
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).nagi >= 0) list.push_back(inst);
  for (int inst : list) {
    if (st.over) return;
    if (ci(inst).zone == Zone::Enhance || ci(inst).zone == Zone::Special)
      if (ci(inst).crystals > 0) consume_enhance_crystal(inst);
  }
  if (st.over) return;
  run_rebuild(p);
  if (st.over) return;
  draw(p, ps(p).nextDrawOne ? 1 : 2);  // 夜叉: 下个回合开始时只抽一张
  ps(p).nextDrawOne = false;
}

bool Engine::basic_legal(Player p, BasicAction a) const {
  if (ps(p).cannotBasic) return false;  // 二重奏·吹弹阳明: 本回合不能执行基本动作
  if (transform_is(p, cards::kYasha) && ps(p).steamEngine == 0) return false;  // 夜叉: 引擎空不能基本动作
  switch (a) {
    case BasicAction::Advance:
      return !ps(p).cannotAdvance && distance() > near_distance() && distance() >= 1 &&
             ps(p).aura < max_aura(p);
    case BasicAction::Retreat:
      return !has_named_active(opp(p), cards::kMud) && ps(p).aura >= 1;
    case BasicAction::Aura:    return st.dust >= 1 && ps(p).aura < max_aura(p);
    case BasicAction::Flare:   return ps(p).aura >= 1;
    case BasicAction::Escape:
      return !has_named_active(opp(p), cards::kMud) && distance() <= near_distance() && st.dust >= 1;
  }
  return false;
}

bool Engine::do_basic(Player p, BasicAction a) {
  if (!basic_legal(p, a)) return false;
  switch (a) {
    case BasicAction::Advance: move_crystals(AreaRef::distance(), AreaRef::aura(p), 1, false); break;
    case BasicAction::Retreat: move_crystals(AreaRef::aura(p), AreaRef::distance(), 1, false); break;
    case BasicAction::Aura:    move_crystals(AreaRef::dust(), AreaRef::aura(p), 1, false); break;
    case BasicAction::Flare:   move_crystals(AreaRef::aura(p), AreaRef::flare(p), 1, false); break;
    case BasicAction::Escape:  move_crystals(AreaRef::dust(), AreaRef::distance(), 1, false); break;
  }
  // Rule 19/94: a basic action performed by a card effect still counts as one.
  didBasicThisTurn_[p] = true;
  return true;
}

void Engine::free_basics(Player p, int maxTimes) {
  for (int i = 0; i < maxTimes; ++i) {
    if (st.over) return;
    std::vector<BasicAction> legal;
    for (int bi = 0; bi < 5; ++bi) {
      BasicAction ba = static_cast<BasicAction>(bi);
      if (basic_legal(p, ba)) legal.push_back(ba);
    }
    if (legal.empty()) return;
    Request r;
    r.kind = "option";
    r.prompt = "free basic action";
    for (BasicAction ba : legal)
      r.options.push_back({std::string("basic: ") + basic_name(ba), true, {}});
    r.options.push_back({"stop", true, {}});
    int idx = ask_one(p, std::move(r));
    if (idx >= static_cast<int>(legal.size())) return;
    do_basic(p, legal[static_cast<size_t>(idx)]);
  }
}

void Engine::free_basics_of(Player p, int maxTimes, const std::vector<std::string>& allowed) {
  auto allowed_basic = [&](BasicAction a) {
    const char* n = basic_name(a);
    for (const auto& s : allowed)
      if (s == n) return true;
    return false;
  };
  for (int i = 0; i < maxTimes; ++i) {
    if (st.over) return;
    std::vector<BasicAction> legal;
    for (int bi = 0; bi < 5; ++bi) {
      BasicAction ba = static_cast<BasicAction>(bi);
      if (allowed_basic(ba) && basic_legal(p, ba)) legal.push_back(ba);
    }
    if (legal.empty()) return;
    Request r;
    r.kind = "option";
    r.prompt = "free basic action";
    for (BasicAction ba : legal)
      r.options.push_back({std::string("basic: ") + basic_name(ba), true, {}});
    r.options.push_back({"stop", true, {}});
    int idx = ask_one(p, std::move(r));
    if (idx >= static_cast<int>(legal.size())) return;
    do_basic(p, legal[static_cast<size_t>(idx)]);
  }
}

int Engine::max_aura(Player p) const {
  int m = st.maxAura;
  for (int inst : ps(p).enhance) {
    const CardDef& d = def_of(inst);
    if (d.auraMax > m) m = d.auraMax;
  }
  for (int inst : ps(p).special)
    if (ci(inst).faceUp) {
      const CardDef& d = def_of(inst);
      if (d.auraMax > m) m = d.auraMax;
    }
  return m;
}

int Engine::effective_hand_limit(Player p) const { return ps(p).handLimit; }

std::vector<int> Engine::enhances(Player p) const {
  std::vector<int> out = ps(p).enhance;
  for (int inst : ps(p).special)
    if (enhance_active(inst)) out.push_back(inst);
  return out;
}

bool Engine::enhance_active(int inst) const {
  const CardInstance& c = ci(inst);
  const CardDef& d = def_of(inst);
  if (c.zone == Zone::Enhance) return c.crystals > 0;
  // A 切札付与 is "展开中" only while it still holds 献; once they run out it
  // stays in the special zone in its used state but its aura text stops.
  if (c.zone == Zone::Special && c.faceUp && d.nagi >= 0) return c.crystals > 0;
  return false;
}

bool Engine::card_has_goddess(int inst, const std::string& g) const {
  const CardDef& d = def_of(inst);
  return d.goddess == g || std::find(d.goddesses.begin(), d.goddesses.end(), g) != d.goddesses.end();
}

int Engine::used_special_count(Player p, const std::string& g) const {
  int c = 0;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && card_has_goddess(inst, g)) c++;
  return c;
}

void Engine::reset_special(int inst) {
  if (ci(inst).zone == Zone::Special && ci(inst).faceUp) {
    // A special that is currently an expanded enhancement keeps its 献; when it
    // is turned back to unused, those crystals must leave play (to dust/decay).
    if (ci(inst).crystals > 0) {
      int c = ci(inst).crystals;
      ci(inst).crystals = 0;
      if (def_of(inst).decayTo == "distance")
        st.distance += c;
      else
        st.dust += c;
    }
    ci(inst).faceUp = false;
    clamp_aura(ci(inst).owner);
    fire("special_reset", ci(inst).owner, nullptr, inst, false);  // 魔能吸收
  }
}

void Engine::clamp_aura(Player p) {
  int over = ps(p).aura - max_aura(p);
  if (over > 0) {
    ps(p).aura -= over;
    st.dust += over;  // 自装中多于上限的部分移到虚
  }
}

void Engine::die(Player p) {
  // Lose all remaining life as life damage so crystals are conserved (life -> flare).
  damage_life(p, ps(p).life, AreaKind::Flare, true);
  check_win();
}

void Engine::store_int(int inst, const std::string& key, int v) { vars_[{inst, key}] = v; }

int Engine::load_int(int inst, const std::string& key, int def) const {
  auto it = vars_.find({inst, key});
  return it == vars_.end() ? def : it->second;
}

void Engine::end_current_main() { abortMain_ = true; }

void Engine::fire(const char* event, Player subject, Attack* atk, int card, bool first) {
  if (effects_) effects_->fire(*this, event, subject, atk, card, first);
  // 即再起 declared as `reset = { on = "<event>" }` rides the same event bus.
  for (int i = 0; i < 2; ++i) {
    Player p = static_cast<Player>(i);
    std::vector<int> sp = ps(p).special;
    for (int inst : sp) {
      if (!ci(inst).faceUp) continue;
      ResetInfo ri = effects_->reset_info(def_of(inst).id);
      if (ri.kind != 2 || ri.trigger.empty()) continue;
      if (ri.trigger != event) continue;
      reset_special(inst);
    }
  }
}

void Engine::notify_aura_changed(Player p) {
  if (auraChangeFired_[p]) return;  // only the first change this turn
  auraChangeFired_[p] = 1;
  fire("aura_changed", p, nullptr, -1, true);
}

void Engine::main_phase(Player p) {
  fire("main_start", p, nullptr, -1, false);  // 阵风祭天式
  mainDirty_ = false;
  while (!st.over) {
    if (abortMain_) break;
    struct Move {
      enum K { Basic, Play, Pass, ExtraBasic } k = Pass;
      int extraDef = -1;
      BasicAction basic = BasicAction::Advance;
      bool payCover = false;
      int inst = -1;
      bool fullPower = false;
      bool terminal = false;
      bool zenkai = false;
      bool fromCover = false;
    };
    std::vector<Move> moves;
    Request r;
    r.kind = "main";
    r.prompt = "main phase action";
    bool canCover = false;
    for (int inst : ps(p).hand)
      if (!is_poison(inst)) {
        canCover = true;
        break;
      }
    bool canPay = ps(p).vigor >= 1 || canCover;

    // Paying for a basic action by covering a hand card. 毒牌 cannot be covered,
    // so they are not offered (and never silently "paid" for free).
    auto ask_cover_payment = [&]() -> int {
      std::vector<int> cands;
      for (int inst : ps(p).hand)
        if (!is_poison(inst)) cands.push_back(inst);
      if (cands.empty()) return -1;
      Request cr;
      cr.kind = "cards";
      cr.prompt = "cover which card to pay?";
      for (int inst : cands) {
        Option o;
        o.label = card_label(def_of(inst));
        o.data = card_json(def_of(inst));
        o.data["inst"] = inst;
        cr.options.push_back(o);
      }
      cr.minSel = 1;
      cr.maxSel = 1;
      int k = ask_one(p, std::move(cr));
      if (k < 0 || k >= static_cast<int>(cands.size())) return -1;
      return cands[static_cast<size_t>(k)];
    };

    if (canPay && !ps(p).cannotBasic) {
      for (int bi = 0; bi < 5; ++bi) {
        BasicAction ba = static_cast<BasicAction>(bi);
        if (!basic_legal(p, ba)) continue;
        for (int payTmp = 0; payTmp < 2; ++payTmp) {
          bool payCover = payTmp == 1;
          if (payCover && !canCover) continue;
          if (!payCover && ps(p).vigor < 1) continue;
          Move m;
          m.k = Move::Basic;
          m.basic = ba;
          m.payCover = payCover;
          moves.push_back(m);
          Option o;
          o.label = std::string("basic: ") + basic_name(ba) + (payCover ? " (cover)" : " (vigor)");
          o.data = {{"kind", "basic"}, {"basic", basic_name(ba)}, {"pay_cover", payCover}};
          r.options.push_back(o);
        }
      }
    }

    for (int inst : ps(p).hand) {
      const CardDef& d = def_of(inst);
      if (d.kind != CardKind::Normal) continue;
      bool fp = (d.flags & CF_FullPower) != 0;
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (!playable_card(p, inst)) continue;
      if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!tmp.range.contains(distance())) continue;
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = (d.flags & CF_Terminal) != 0;
      moves.push_back(m);
      Option o;
      o.label = "play: " + card_label(d);
      o.data = card_json(d);
      o.data["inst"] = inst;
      o.data["kind"] = "play";
      o.data["full_power"] = fp;
      o.data["terminal"] = m.terminal;
      r.options.push_back(o);
      if (d.zenkai && !mainDirty_) {  // 全开: optional full-power-like use
        Move mz = m;
        mz.zenkai = true;
        moves.push_back(mz);
        Option oz;
        oz.label = "全开: " + card_label(d);
        oz.data = card_json(d);
        oz.data["inst"] = inst;
        oz.data["kind"] = "play";
        oz.data["zenkai"] = true;
        r.options.push_back(oz);
      }
    }

    for (int inst : ps(p).special) {
      if (ci(inst).faceUp) continue;
      const CardDef& d = def_of(inst);
      if (ps(p).flare < cut_cost(p, d.id, inst)) continue;
      if (!playable_card(p, inst)) continue;
      bool fp = (d.flags & CF_FullPower) != 0;
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!tmp.range.contains(distance())) continue;
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = (d.flags & CF_Terminal) != 0;
      moves.push_back(m);
      Option o;
      o.label = "special: " + card_label(d);
      o.data = card_json(d);
      o.data["inst"] = inst;
      o.data["kind"] = "play";
      o.data["full_power"] = fp;
      o.data["terminal"] = m.terminal;
      r.options.push_back(o);
      if (d.zenkai && !mainDirty_) {
        Move mz = m;
        mz.zenkai = true;
        moves.push_back(mz);
        Option oz;
        oz.label = "全开: " + card_label(d);
        oz.data = card_json(d);
        oz.data["inst"] = inst;
        oz.data["kind"] = "play";
        oz.data["zenkai"] = true;
        r.options.push_back(oz);
      }
    }

    for (int inst : ps(p).cover) {  // 回收利用 等：从盖牌区如同手牌使用
      const CardDef& d = def_of(inst);
      if (!d.playableFromCover) continue;
      bool fp = (d.flags & CF_FullPower) != 0;
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (!playable_card(p, inst)) continue;
      if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!tmp.range.contains(distance())) continue;
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = (d.flags & CF_Terminal) != 0;
      m.fromCover = true;
      moves.push_back(m);
      Option o;
      o.label = "cover-play: " + card_label(d);
      o.data = card_json(d);
      o.data["inst"] = inst;
      o.data["kind"] = "play";
      o.data["from_cover"] = true;
      r.options.push_back(o);
    }

    {  // 变形/快速改装: 追加基本动作
      bool canPayExtra = ps(p).vigor >= 1 || canCover;
      for (int td : active_transform_defs(p)) {
        if (!effects_->has(td, "extra_basic")) continue;
        if (def(td).name == cards::kAshura && ashuraExtraUsed_[p]) continue;
        if (!canPayExtra) continue;
        for (int payTmp = 0; payTmp < 2; ++payTmp) {
          bool payCover = payTmp == 1;
          if (payCover && !canCover) continue;
          if (!payCover && ps(p).vigor < 1) continue;
          Move m;
          m.k = Move::ExtraBasic;
          m.extraDef = td;
          m.payCover = payCover;
          moves.push_back(m);
          Option o;
          o.label = std::string("extra: ") + def(td).name + (payCover ? " (cover)" : " (vigor)");
          o.data = {{"kind", "extra_basic"}, {"def", td}, {"pay_cover", payCover}};
          r.options.push_back(o);
        }
      }
    }

    Move pass;
    pass.k = Move::Pass;
    moves.push_back(pass);
    r.options.push_back({"pass", true, {{"kind", "pass"}}});

    Decision dec = decide(p, r);
    int idx = dec.indices.empty() ? static_cast<int>(moves.size()) - 1 : dec.indices[0];
    if (idx < 0 || idx >= static_cast<int>(moves.size())) idx = static_cast<int>(moves.size()) - 1;
    Move m = moves[static_cast<size_t>(idx)];

    if (m.k == Move::Pass) break;

    if (m.k == Move::ExtraBasic) {
      if (m.payCover) {
        int coverInst = ask_cover_payment();
        if (coverInst < 0) continue;
        move_card(coverInst, Zone::Cover);
        ci(coverInst).faceUp = false;
      } else {
        ps(p).vigor -= 1;
      }
      if (def(m.extraDef).name == cards::kAshura) ashuraExtraUsed_[p] = true;
      effects_->call(*this, m.extraDef, "extra_basic", p, -1);
      mainDirty_ = true;
      check_win();
      continue;
    }

    if (m.k == Move::Basic) {
      if (m.payCover) {
        int coverInst = ask_cover_payment();
        if (coverInst < 0) continue;
        move_card(coverInst, Zone::Cover);
        ci(coverInst).faceUp = false;
      } else {
        ps(p).vigor -= 1;
      }
      do_basic(p, m.basic);  // payment already handled above
      mainDirty_ = true;
    } else {
      if (m.fromCover)
        play_from_cover(p, m.inst, false, false);
      else
        play_card(p, m.inst, false, m.zenkai);
      mainDirty_ = true;
      if (m.fullPower || m.terminal || m.zenkai) break;
    }
    check_win();
  }
  abortMain_ = false;
}

void Engine::cover_phase(Player p) {
  while (!st.over && static_cast<int>(ps(p).hand.size()) > effective_hand_limit(p)) {
    std::vector<int> choices;
    for (int inst : ps(p).hand)
      if (!is_poison(inst)) choices.push_back(inst);  // 毒牌不能被盖伏
    if (choices.empty()) break;                        // hand has only poisons
    Request r;
    r.kind = "cards";
    r.prompt = "cover a card (hand must be <= 2)";
    for (int inst : choices) {
      Option o;
      o.label = card_label(def_of(inst));
      o.data = card_json(def_of(inst));
      o.data["inst"] = inst;
      r.options.push_back(o);
    }
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(choices.size()) - 1);
    int inst = choices[static_cast<size_t>(idx)];
    move_card(inst, Zone::Cover);
    ci(inst).faceUp = false;
  }
}

void Engine::end_phase(Player p) {
  fire("end_phase_start", p, nullptr, -1, false);  // 大岚
  if (ps(p).yukihi && !st.over) {
    Request r;
    r.kind = "option";
    r.prompt = "结束阶段：切换武器？";
    r.options.push_back({ps(p).umbrella ? "切换到簪" : "切换到伞", true, {}});
    r.options.push_back({"不切换", true, {}});
    if (ask_one(p, std::move(r)) == 0) switch_weapon(p, -1);
  }
  fire("turn_end", p, nullptr, -1, false);  // 手里剑 等
  std::vector<int> specials = ps(p).special;
  for (int inst : specials) {
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    ResetInfo ri = effects_->reset_info(d.id);
    if (ri.kind != 1) continue;
    bool ok = ri.hasCond ? effects_->eval_reset_cond(*this, d.id, p, inst) : false;
    if (ok) reset_special(inst);
  }
}

void Engine::play_turn(Player p) {
  st.active = p;
  // 终端's "cannot respond" lasts until the end of the opponent's turn.
  ps(P0).cannotRespond = false;
  ps(P1).cannotRespond = false;
  // 连射 counts cards played this turn; "本回合中" modifiers only last this turn.
  ps(P0).cardsPlayedThisTurn = 0;
  ps(P1).cardsPlayedThisTurn = 0;
  effects_->clear_pending_mods(true);
  for (int i = 0; i < 2; ++i) {
    Player pl = static_cast<Player>(i);
    ps(pl).handLimit = 2;
    ps(pl).cutCostDelta = 0;
    ps(pl).cannotAttack = false;
    ps(pl).cannotBasic = false;
    attacksThisTurn_[i] = 0;
    auraChangesThisTurn_[i] = 0;
    auraChangeFired_[i] = 0;
    attackFirstFired_[i] = 0;
    auraDamagedThisTurn_[i] = false;
    normalNonYukihi_[i] = 0;
    attackedThisTurn_[i] = false;
    playedCentrifugalThisTurn_[i] = false;
    playedLianchengThisTurn_[i] = false;
    ps(pl).cannotAdvance = false;
    didBasicThisTurn_[i] = false;
    rebuiltThisTurn_[i] = false;
    usedFullPowerThisTurn_[i] = false;
    revealOppSpecials_[i] = false;
    for (int inst : ps(pl).special) ci(inst).usedThisTurn = false;
  }
  reset_steam_at_turn_start(p);  // 气动 steam returns to the exhausted module
  pendingNagiAdjust_ = 0;        // 回收利用's 纳 ±1 must not leak into a later 付与
  distanceAtTurnStart_ = distance();
  forceUnrespondable_ = false;
  abortMain_ = false;
  fire("turn_start", p, nullptr, -1, false);
  phase_ = "start";
  if (!ps(p).firstTurnDone) {
    ps(p).firstTurnDone = true;  // first turn of each player skips the start phase
  } else {
    start_phase(p);
  }
  phase_ = "main";
  if (!st.over) main_phase(p);
  phase_ = "cover";
  if (!st.over) cover_phase(p);
  phase_ = "end";
  if (!st.over) end_phase(p);
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

int Engine::enhance_crystal_total(Player p) const {
  // "所有付与牌上的樱花结晶数目" includes expanded 切札付与 (e.g. 无常其心).
  int t = 0;
  for (int i : ps(p).enhance) t += ci(i).crystals;
  for (int i : ps(p).special)
    if (enhance_active(i)) t += ci(i).crystals;
  return t;
}

int Engine::dust_to_card(int inst, int n) {
  return move_crystals(AreaRef::dust(), AreaRef::card(inst), n, true);
}

void Engine::set_used(int inst) {
  if (ci(inst).zone == Zone::Special) ci(inst).faceUp = true;
}

void Engine::switch_weapon(Player p, int source) {
  if (!ps(p).yukihi) return;
  ps(p).umbrella = !ps(p).umbrella;
  fire("weapon_switched", p, nullptr, source, false);  // 即再起 via the event bus
}

void Engine::prepare_strategy(Player p) {
  Request r;
  r.kind = "option";
  r.prompt = "秘密准备下一个计策";
  r.options.push_back({"神算", true, {}});
  r.options.push_back({"鬼谋", true, {}});
  int c = ask_one(p, std::move(r));
  ps(p).strategy = (c == 1) ? 1 : 0;
  ps(p).strategyKnown = false;
}

void Engine::seal_card(int host, int card) {
  move_card(card, Zone::Sealed);
  ci(card).sealedBy = host;
  ci(host).sealed.push_back(card);
}

void Engine::return_sealed(int host) {
  std::vector<int> list = ci(host).sealed;
  ci(host).sealed.clear();
  for (int c : list) {
    ci(c).sealedBy = -1;
    move_card(c, Zone::Discard);  // to its owner's discard
  }
}

int Engine::distance_delta() const {
  int d = 0;
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    d += ps(o).steamOnDist - ps(o).steamOnCrystal;  // 气动
    for (int inst : ps(o).enhance)
      if (def_of(inst).distanceMod) d += def_of(inst).distanceMod;
    for (int inst : ps(o).special)
      if (enhance_active(inst) && def_of(inst).distanceMod) d += def_of(inst).distanceMod;
  }
  return d;
}

int Engine::near_distance() const {
  int d = st.nearDistance;
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    for (int inst : ps(o).enhance) d += def_of(inst).nearDistanceMod;
    for (int inst : ps(o).special)
      if (enhance_active(inst)) d += def_of(inst).nearDistanceMod;
  }
  return d < 0 ? 0 : d;
}

int Engine::distance() const {
  int d = st.distance + distance_delta();
  return d < 0 ? 0 : d;
}

void Engine::place_poison(int inst, Player holder, Zone z) {
  // Remove the card from the *previous* owner's zone list before changing
  // ownership: move_card erases from the list of the current owner.
  if (ci(inst).owner != holder) {
    move_card(inst, Zone::Limbo);
    ci(inst).owner = holder;
  }
  move_card(inst, z);
}

void Engine::return_poison(int inst) {
  if (ci(inst).bagOwner < 0) return;
  Player bag = static_cast<Player>(ci(inst).bagOwner);
  if (ci(inst).owner != bag) {
    move_card(inst, Zone::Limbo);
    ci(inst).owner = bag;
  }
  move_card(inst, Zone::Bag);
}

void Engine::force_move(int inst, Zone z) {
  bool saved = poisonForce_;
  poisonForce_ = true;
  move_card(inst, z);
  poisonForce_ = saved;
}

void Engine::float_poisons(Player p) {
  std::vector<int> pois, rest;
  for (int inst : ps(p).deck) {
    if (def_of(inst).isPoison)
      pois.push_back(inst);
    else
      rest.push_back(inst);
  }
  if (pois.empty()) return;
  std::vector<int> out = rest;
  out.insert(out.end(), pois.begin(), pois.end());  // poisons at the back == top
  ps(p).deck = std::move(out);
}

void Engine::random_discard(Player p) {
  std::vector<int> pool;
  for (int inst : ps(p).hand)
    if (!is_poison(inst)) pool.push_back(inst);
  if (pool.empty()) return;
  int idx = st.rng.below(static_cast<int>(pool.size()));
  move_card(pool[static_cast<size_t>(idx)], Zone::Discard);
}

void Engine::discard_deck(Player p) {
  std::vector<int> d = ps(p).deck;
  for (int inst : d) move_card(inst, Zone::Discard);
}

int Engine::find_named(Player p, const std::string& name) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == name) return inst;
  for (int inst : ps(p).special)
    if (def_of(inst).name == name) return inst;
  for (int inst : ps(p).hand)
    if (def_of(inst).name == name) return inst;
  return -1;
}

int Engine::count_named(Player p, const std::string& name) const {
  int c = 0;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[static_cast<size_t>(i)].holder == p && def_of(i).name == name) c++;
  return c;
}

int Engine::sealed_card(int host) const {
  if (host < 0) return -1;
  const auto& s = ci(host).sealed;
  return s.empty() ? -1 : s.front();
}

int Engine::drain_card_crystals(int inst, int n) {
  int take = std::min(n, ci(inst).crystals);
  ci(inst).crystals -= take;
  st.dust += take;
  return take;
}

void Engine::discard_top(Player p) {
  if (ps(p).deck.empty()) return;
  int inst = ps(p).deck.back();
  move_card(inst, Zone::Discard);
}

void Engine::cover_deck(Player p) {
  const std::vector<int> d = ps(p).deck;
  for (int inst : d) {
    if (is_poison(inst)) continue;  // 毒牌不能被盖伏
    move_card(inst, Zone::Cover);
    ci(inst).faceUp = false;
  }
}

void Engine::cover_top(Player p) {
  if (ps(p).deck.empty()) return;
  int inst = ps(p).deck.back();
  if (is_poison(inst)) return;  // 毒牌不能被盖伏: the effect simply fails
  move_card(inst, Zone::Cover);
  ci(inst).faceUp = false;
}

uint32_t Engine::card_colors(int inst) const {
  const CardDef& d = def_of(inst);
  uint32_t c = 0;
  if (d.type == CardType::Attack) c |= COL_RED;
  if (d.type == CardType::Action) c |= COL_BLUE;
  if (d.type == CardType::Enhance) c |= COL_GREEN;
  if (d.flags & CF_Response) c |= COL_PURPLE;
  if (d.flags & CF_FullPower) c |= COL_YELLOW;
  return c;
}

std::string Engine::card_colors_str(int inst) const {
  uint32_t c = card_colors(inst);
  std::string s;
  if (c & COL_RED) s += "R";
  if (c & COL_BLUE) s += "B";
  if (c & COL_GREEN) s += "G";
  if (c & COL_PURPLE) s += "P";
  if (c & COL_YELLOW) s += "Y";
  return s;
}

int Engine::keisou_amount(Player p, int base) {
  if (keisouDoubled_) return base * 2;  // one spend doubles every number of that slot
  int host = -1;
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == cards::kHackDevice && ci(inst).crystals > 0) host = inst;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).name == cards::kHackDevice && ci(inst).crystals > 0) host = inst;
  if (host < 0) return base;
  Request r;
  r.kind = "option";
  r.prompt = "骇客装置：花 1 结晶使本机巧栏所有数字翻倍？";
  r.options.push_back({"翻倍", true, {}});
  r.options.push_back({"不翻倍", true, {}});
  if (ask_one(p, std::move(r)) == 0) {
    drain_card_crystals(host, 1);
    keisouDoubled_ = true;
    return base * 2;
  }
  return base;
}

bool Engine::keisou(Player p, const std::string& combo, bool otherOnly) const {
  std::map<char, int> need;
  for (char ch : combo)
    if (ch == 'R' || ch == 'B' || ch == 'G' || ch == 'P' || ch == 'Y') need[ch]++;
  std::map<char, int> have;
  auto add = [&](int inst) {
    if (otherOnly && def_of(inst).goddess == "kururu") return;
    uint32_t c = card_colors(inst);
    if (c & COL_RED) have['R']++;
    if (c & COL_BLUE) have['B']++;
    if (c & COL_GREEN) have['G']++;
    if (c & COL_PURPLE) have['P']++;
    if (c & COL_YELLOW) have['Y']++;
  };
  for (int inst : ps(p).discard) add(inst);
  for (int inst : ps(p).special)
    if (ci(inst).faceUp) add(inst);
  for (int inst : ps(p).enhance) add(inst);
  for (auto& [k, v] : need)
    if (have[k] < v) return false;
  return true;
}

std::vector<std::string> Engine::goddess_normal_names(Player p) const {
  // Exactly the normal card names of the decks this player actually built
  // (变格 replaces same-numbered cards), not every form of the goddess.
  std::vector<std::string> out;
  for (const std::string& s : playerSets_[p]) {
    std::string g = s, f = "O";
    auto dot = s.find('.');
    if (dot != std::string::npos) {
      std::string suffix = s.substr(dot + 1);
      if (suffix == "A1" || suffix == "A2") {
        g = s.substr(0, dot);
        f = suffix;
      }
    }
    for (int id : deck_def_ids(g, f)) {
      const CardDef& d = defs[static_cast<size_t>(id)];
      if (d.kind != CardKind::Normal) continue;
      if (std::find(out.begin(), out.end(), d.name) == out.end()) out.push_back(d.name);
    }
  }
  return out;
}

bool Engine::guess_name(Player guesser, int cardInst) {
  std::vector<std::string> cands = goddess_normal_names(ci(cardInst).owner);
  Request r;
  r.kind = "guess";
  r.prompt = "猜测对手盖牌的牌名";
  for (const auto& n : cands) r.options.push_back({n, true, {}});
  r.minSel = 1;
  r.maxSel = 1;
  int idx = ask_one(guesser, std::move(r));
  bool correct = idx >= 0 && idx < static_cast<int>(cands.size()) &&
                 cands[static_cast<size_t>(idx)] == def_of(cardInst).name;
  move_card(cardInst, Zone::Discard);  // the flipped card goes to its owner's discard
  return correct;
}

void Engine::burn(Player p, int x) {
  if (x <= 0) return;
  if (has_named_active(p, cards::kSariaNoKessaku)) {
    recover(p, x);
    return;
  }
  int m = std::min(x, ps(p).steamEngine);
  ps(p).steamEngine -= m;
  ps(p).steamExhausted += m;
}

void Engine::recover(Player p, int x) {
  int m = std::min(x, ps(p).steamExhausted);
  ps(p).steamExhausted -= m;
  ps(p).steamEngine += m;
}

void Engine::pneumatic(Player p) {
  if (ps(p).steamEngine < 1) return;
  Request r;
  r.kind = "option";
  r.prompt = "气动：距离 +1 或 -1？";
  r.options.push_back({"距离 +1", true, {}});
  r.options.push_back({"距离 -1", true, {}});
  int c = ask_one(p, std::move(r));
  ps(p).steamEngine -= 1;
  if (c == 1)
    ps(p).steamOnCrystal += 1;
  else
    ps(p).steamOnDist += 1;
  fire("pneumatic", p, nullptr, -1, false);
}

bool Engine::raira_can(Player p, const std::string& kind, int tier) const {
  return (kind == "wind" ? ps(p).wind : ps(p).thunder) >= tier;
}

bool Engine::raira_spend(Player p, const std::string& kind, int tier) {
  if (!raira_can(p, kind, tier)) return false;
  if (kind == "wind")
    ps(p).wind -= tier;
  else
    ps(p).thunder -= tier;
  return true;
}

bool Engine::has_named_active(Player p, const std::string& name) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == name) return true;
  for (int inst : ps(p).special) {
    const CardInstance& c = ci(inst);
    if (!c.faceUp) continue;
    // A 付与 whose 献 are gone is no longer active; other used 切札 stay active.
    if (def_of(inst).nagi >= 0 && c.crystals <= 0) continue;
    if (def_of(inst).name == name) return true;
  }
  return false;
}

bool Engine::current_from_cover() const {
  return !callStack_.empty() && callStack_.back().fromCover;
}

}  // namespace fy
