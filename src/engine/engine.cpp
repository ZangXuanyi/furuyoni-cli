#include "engine/engine.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "engine/effect_host.hpp"

namespace fy {

namespace {

std::string dmg_str(const Damage& d) {
  auto s = [](const std::optional<int>& o) { return o ? std::to_string(*o) : std::string("-"); };
  return s(d.aura) + "/" + s(d.life);
}

std::string card_label(const CardDef& d) {
  std::string s = d.name;
  if (d.kind == CardKind::Special && d.cost >= 0) s += "(" + std::to_string(d.cost) + ")";
  if (d.hasAttack)
    s += " [" + d.attack.range.to_string() + " " + dmg_str(d.attack.damage) + "]";
  else if (d.nagi >= 0)
    s += " [纳" + std::to_string(d.nagi) + "]";
  return s;
}

nlohmann::json card_json(const CardDef& d) {
  nlohmann::json j;
  j["def"] = d.id;
  j["name"] = d.name;
  j["set"] = d.set;
  j["kind"] = d.kind == CardKind::Normal ? "normal" : "special";
  j["type"] = d.type == CardType::Attack   ? "attack"
              : d.type == CardType::Enhance ? "enhance"
                                            : "action";
  j["cost"] = d.cost;
  j["nagi"] = d.nagi;
  j["full_power"] = (d.flags & CF_FullPower) != 0;
  j["response"] = (d.flags & CF_Response) != 0;
  j["break"] = (d.flags & CF_Break) != 0;
  j["terminal"] = (d.flags & CF_Terminal) != 0;
  if (d.hasAttack) {
    j["range"] = d.attack.range.to_string();
    j["damage"] = dmg_str(d.attack.damage);
    j["unrespondable"] = (d.attack.keywords & AF_Unrespondable) != 0;
    j["lock"] = (d.attack.keywords & AF_Lock) != 0;
    j["overwhelm"] = (d.attack.keywords & AF_Overwhelm) != 0;
  }
  j["text"] = d.text;
  return j;
}

std::vector<int>* zone_ptr(GameState& st, Player owner, Zone z) {
  switch (z) {
    case Zone::Deck:    return &st.p[owner].deck;
    case Zone::Hand:    return &st.p[owner].hand;
    case Zone::Discard: return &st.p[owner].discard;
    case Zone::Cover:   return &st.p[owner].cover;
    case Zone::Enhance: return &st.p[owner].enhance;
    case Zone::Special: return &st.p[owner].special;
    case Zone::Removed: return nullptr;
    case Zone::Limbo:   return nullptr;
  }
  return nullptr;
}

bool vec_has(const std::vector<int>& v, int x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

}  // namespace

Engine::Engine(Config c) : cfg(std::move(c)) { effects_ = std::make_unique<EffectHost>(); }
Engine::~Engine() = default;

void Engine::load_content(const std::string& f) { effects_->load_file(f, defs); }
void Engine::set_agent(Player p, Agent* a) { agents_[p] = a; }

Decision Engine::decide(Player p, Request req) {
  req.player = p;
  if (req.state.is_null()) req.state = observation(p);
  if (replaying_) {
    if (replay_pos_ >= journal_.size()) throw std::runtime_error("replay: journal exhausted");
    const JournalEntry& e = journal_[replay_pos_++];
    if (e.player != p || e.kind != req.kind)
      throw std::runtime_error("replay: decision mismatch (kind " + e.kind + " vs " + req.kind + ")");
    return Decision{e.indices};
  }
  Decision d = agents_[p] ? agents_[p]->decide(req) : FirstAgent{}.decide(req);
  if (recording_) journal_.push_back({p, req.kind, d.indices});
  return d;
}

void Engine::start_recording() {
  recording_ = true;
  replaying_ = false;
  journal_.clear();
  replay_pos_ = 0;
}

nlohmann::json Engine::journal_json() const {
  using nlohmann::json;
  json j;
  j["seed"] = cfg.seed;
  j["hash"] = state_hash();
  j["turn"] = st.turn;
  j["winner"] = st.winner;
  j["entries"] = json::array();
  for (const JournalEntry& e : journal_) {
    json je;
    je["player"] = static_cast<int>(e.player);
    je["kind"] = e.kind;
    je["indices"] = e.indices;
    j["entries"].push_back(je);
  }
  return j;
}

void Engine::load_journal(const nlohmann::json& j) {
  journal_.clear();
  replay_pos_ = 0;
  recording_ = false;
  replaying_ = true;
  for (const auto& je : j.at("entries")) {
    JournalEntry e;
    e.player = static_cast<Player>(je.at("player").get<int>());
    e.kind = je.at("kind").get<std::string>();
    e.indices = je.at("indices").get<std::vector<int>>();
    journal_.push_back(std::move(e));
  }
}

uint64_t Engine::state_hash() const {
  std::string s;
  auto put = [&](auto v) { s += std::to_string(v); s += ','; };
  put(st.turn);
  put(static_cast<int>(st.active));
  put(st.distance);
  put(st.dust);
  put(static_cast<int>(st.over));
  put(st.winner);
  for (int i = 0; i < 2; ++i) {
    put(st.p[i].life);
    put(st.p[i].aura);
    put(st.p[i].flare);
    put(st.p[i].vigor);
    put(st.p[i].deck.size());
    put(st.p[i].hand.size());
    put(st.p[i].discard.size());
    put(st.p[i].cover.size());
    put(st.p[i].enhance.size());
    put(st.p[i].special.size());
  }
  for (const auto& c : st.insts) {
    put(c.def);
    put(static_cast<int>(c.zone));
    put(static_cast<int>(c.faceUp));
    put(c.crystals);
  }
  uint64_t h = 1469598103934665603ull;
  for (unsigned char ch : s) {
    h ^= ch;
    h *= 1099511628211ull;
  }
  return h;
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
  c.zone = Zone::Removed;
  st.insts.push_back(c);
  return c.inst;
}

void Engine::move_card(int inst, Zone z) {
  CardInstance& c = ci(inst);
  if (auto* v = zone_ptr(st, c.owner, c.zone))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  c.zone = z;
  if (auto* v = zone_ptr(st, c.owner, z)) v->push_back(inst);
}

void Engine::move_card_top(int inst) {
  move_card(inst, Zone::Deck);  // deck top == back of the vector
}

void Engine::move_card_bottom(int inst) {
  CardInstance& c = ci(inst);
  if (auto* v = zone_ptr(st, c.owner, c.zone))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  c.zone = Zone::Deck;
  ps(c.owner).deck.insert(ps(c.owner).deck.begin(), inst);  // bottom == front
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
      if (ci(inst).faceUp && def_of(inst).lockDistance) return true;
  }
  return false;
}

int Engine::effective_armor(Player p) const {
  int armor = ps(p).aura;
  for (int inst : ps(p).enhance)
    if (def_of(inst).armorFromCrystals) armor += ci(inst).crystals;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).armorFromCrystals) armor += ci(inst).crystals;
  return armor;
}

void Engine::spend_aura(Player target, int n) {
  if (n <= 0) return;
  int remaining = n;
  // "视作装"的卡上结晶优先消耗（移入虚）。
  for (int inst : ps(target).enhance) {
    if (remaining <= 0) break;
    if (!def_of(inst).armorFromCrystals) continue;
    int take = std::min(remaining, ci(inst).crystals);
    ci(inst).crystals -= take;
    st.dust += take;
    remaining -= take;
  }
  for (int inst : ps(target).special) {
    if (remaining <= 0) break;
    if (!ci(inst).faceUp || !def_of(inst).armorFromCrystals) continue;
    int take = std::min(remaining, ci(inst).crystals);
    ci(inst).crystals -= take;
    st.dust += take;
    remaining -= take;
  }
  if (remaining > 0) move_crystals(AreaRef::aura(target), AreaRef::dust(), remaining, false);
}

int Engine::cut_cost(Player p, int defId, int inst) {
  int base = effects_->has_hook(defId, "cost")
                 ? std::max(0, effects_->eval_cost(*this, defId, p, inst))
                 : std::max(0, def(defId).cost);
  return std::max(0, base + ps(p).cutCostDelta);
}

bool Engine::playable_card(Player p, int inst) {
  const CardDef& d = def_of(inst);
  if (effects_->has_hook(d.id, "playable")) return effects_->eval_pred(*this, d.id, "playable", p, inst);
  return true;
}

bool Engine::respondable_card(Player p, int inst) {
  const CardDef& d = def_of(inst);
  if (d.flags & CF_Response) return true;
  if (effects_->has_hook(d.id, "respond")) return effects_->eval_pred(*this, d.id, "respond", p, inst);
  return false;
}

void Engine::reveal_hand(Player p) {
  (void)p;  // 仅当下公开一次；CLI 的观察是按需拉取的，无持续机械影响
}

void Engine::check_win() {
  for (int i = 0; i < 2; ++i) {
    if (st.p[i].life <= 0) {
      st.over = true;
      st.winner = opp(static_cast<Player>(i));
      st.p[i].life = 0;
    }
  }
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
}

void Engine::damage_life(Player p, int n, AreaKind to, bool triggerBreak) {
  AreaRef dest = (to == AreaKind::Dust) ? AreaRef::dust() : AreaRef::flare(p);
  int moved = move_crystals(AreaRef::life(p), dest, n, false);
  ps(p).lastLifeLost = moved;
  if (moved > 0) on_life_loss(p, moved, triggerBreak);
  check_win();
}

void Engine::lose_life(Player p, int n, bool triggerBreak) {
  damage_life(p, n, AreaKind::Flare, triggerBreak);
}

void Engine::on_life_loss(Player p, int amount, bool triggerBreak) {
  if (triggerBreak) break_enhances(p);
  // immediate resets (即再起)
  std::vector<int> specials = ps(p).special;
  for (int inst : specials) {
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    ResetInfo ri = effects_->reset_info(d.id);
    if (ri.kind != 2) continue;
    bool ok = false;
    if (ri.lifeThreshold >= 0)
      ok = amount >= ri.lifeThreshold;
    else if (ri.hasCond)
      ok = effects_->eval_reset_cond(*this, d.id, p, inst);
    if (ok) reset_special(inst);
  }
}

void Engine::break_enhances(Player p) {
  std::vector<int> targets;
  for (int inst : ps(p).enhance)
    if (def_of(inst).flags & CF_Break) targets.push_back(inst);
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).nagi >= 0 && (def_of(inst).flags & CF_Break))
      targets.push_back(inst);
  for (int inst : targets) {
    int c = ci(inst).crystals;
    ci(inst).crystals = 0;
    st.dust += c;
    // cover it, face down, skip on_discard
    move_card(inst, Zone::Cover);
    ci(inst).faceUp = false;
  }
  clamp_aura(p);  // a card providing bonus aura may have left play
}

void Engine::consume_enhance_crystal(int inst) {
  CardInstance& c = ci(inst);
  if (c.crystals <= 0) return;
  const CardDef& d = def_of(inst);
  c.crystals -= 1;
  // a dropped 献 falls to 虚 by default; e.g. 圈域 sends it to 距 instead.
  if (d.decayTo == "distance")
    st.distance += 1;
  else
    st.dust += 1;
  if (c.crystals > 0) return;
  if (d.kind == CardKind::Normal) {
    move_card(inst, Zone::Discard);
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", c.owner, inst);
  } else {
    move_card(inst, Zone::Special);  // special enhance stays used in the special zone
  }
}

// ---------------------------------------------------------------------------
// attacks
// ---------------------------------------------------------------------------

Attack Engine::make_attack(Player p, int inst, bool asResponse, bool consumePending) {
  const CardDef& d = def_of(inst);
  Attack a;
  a.attacker = p;
  a.sourceInst = inst;
  a.fromSpecial = d.kind == CardKind::Special;
  a.fromResponse = asResponse;
  EvaluatedAttack ea = effects_->eval_attack(*this, d.id, p, inst, asResponse);
  a.range = ea.range;
  a.aura = ea.damage.aura;
  a.life = ea.damage.life;
  a.keywords = ea.keywords;
  a.evadeCover = ea.evade;
  a.attackerChoosesDamage = ea.attackerChooses;
  effects_->finalize_attack(*this, p, a, consumePending);
  // Fire "attack declared" only for real declarations during the attacker's own turn.
  if (consumePending && a.attacker == st.active) {
    attacksThisTurn_[a.attacker] += 1;
    fire("attack_declared", a.attacker, &a, -1, attacksThisTurn_[a.attacker] == 1);
  }
  return a;
}

void Engine::apply_damage_to(Player target, std::optional<int> aura, std::optional<int> life,
                             uint32_t keywords, int sourceInst, int chooser, bool fromAttack) {
  (void)sourceInst;
  std::optional<int> effA = aura, effL = life;
  if (effA && !(keywords & AF_Overwhelm)) *effA = std::min(*effA, 5);
  if (effA && *effA < 0) *effA = 0;
  if (effL && *effL < 0) *effL = 0;
  bool canAura = effA.has_value() && effective_armor(target) >= *effA;
  lastDmgFromAttack_ = fromAttack;

  if (keywords & AF_BothSides) {
    // 两侧伤害: resolve the aura side (like X/-) and the life side (like -/Y).
    if (effA) {
      int n = std::min(*effA, effective_armor(target));
      spend_aura(target, n);
      lastDmgSide_ = 1;
      lastDmgAmount_ = n;
    }
    if (effL) {
      int before = ps(target).life;
      damage_life(target, *effL, AreaKind::Flare, true);
      lastDmgSide_ = 2;
      lastDmgAmount_ = before - ps(target).life;
    }
  } else if (effA && effL && canAura) {
    Request r;
    r.kind = "damage";
    r.prompt = "choose how to take damage";
    Option oa;
    oa.label = "take " + std::to_string(*effA) + " aura damage";
    oa.data = {{"side", "aura"}, {"amount", *effA}};
    Option ol;
    ol.label = "take " + std::to_string(*effL) + " life damage";
    ol.data = {{"side", "life"}, {"amount", *effL}};
    r.options = {oa, ol};
    Player decider = chooser >= 0 ? static_cast<Player>(chooser) : target;
    int idx = ask_one(decider, std::move(r));
    if (idx == 0) {
      spend_aura(target, *effA);
      lastDmgSide_ = 1;
      lastDmgAmount_ = *effA;
    } else {
      int before = ps(target).life;
      damage_life(target, *effL, AreaKind::Flare, true);
      lastDmgSide_ = 2;
      lastDmgAmount_ = before - ps(target).life;
    }
  } else if (effA && !effL) {
    int n = std::min(*effA, effective_armor(target));
    spend_aura(target, n);
    lastDmgSide_ = 1;
    lastDmgAmount_ = n;
  } else if (effL) {
    int before = ps(target).life;
    damage_life(target, *effL, AreaKind::Flare, true);
    lastDmgSide_ = 2;
    lastDmgAmount_ = before - ps(target).life;
  }
  check_win();
}

void Engine::deal_damage(Player target, std::optional<int> aura, std::optional<int> life,
                         uint32_t keywords) {
  apply_damage_to(target, aura, life, keywords, -1);
}

void Engine::resolve_attack(Attack& a) {
  if (st.over) return;
  Player target = opp(a.attacker);

  // ---- response window -----------------------------------------------------
  // 全力 and 对应 are mutually exclusive, so a response is never full power.
  Attack* prev = currentResponding;
  currentResponding = &a;  // visible to respond predicates (识破/终焉) and effects
  if (!(a.keywords & AF_Unrespondable) && !a.fromResponse && !ps(target).cannotRespond) {
    std::vector<int> resp;
    auto consider = [&](int inst) {
      const CardDef& d = def_of(inst);
      if (d.flags & CF_FullPower) return;
      if ((a.keywords & AF_NoSpecialResponse) && d.kind == CardKind::Special) return;
      if (!respondable_card(target, inst)) return;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(target, inst, true, false);
        if (!tmp.range.contains(st.distance)) return;
      }
      resp.push_back(inst);
    };
    for (int inst : ps(target).hand) consider(inst);
    for (int inst : ps(target).special) {
      if (ci(inst).faceUp) continue;
      if (ps(target).flare < cut_cost(target, ci(inst).def, inst)) continue;
      consider(inst);
    }
    if (!resp.empty()) {
      Request r;
      r.kind = "response";
      r.prompt = "respond to the attack?";
      r.options.push_back({"pass", true, {{"kind", "pass"}}});
      for (int inst : resp) {
        Option o;
        o.label = card_label(def_of(inst));
        o.data = card_json(def_of(inst));
        o.data["inst"] = inst;
        o.data["kind"] = "play";
        r.options.push_back(o);
      }
      int idx = ask_one(target, std::move(r));
      if (idx > 0 && idx <= static_cast<int>(resp.size())) {
        int chosen = resp[static_cast<size_t>(idx - 1)];
        play_card(target, chosen, true);
        fire("responded_with", target, nullptr, chosen, false);
        if (st.over) {
          currentResponding = prev;
          return;
        }
      }
    }
  }
  currentResponding = prev;

  // ---- range re-check ------------------------------------------------------
  if (!(a.keywords & AF_Lock) && !a.range.contains(st.distance)) a.missed = true;
  if (a.negated || a.missed) return;

  // ---- 问答: defender may skip the damage by covering the top of their deck ----
  if (a.evadeCover > 0) {
    Request r;
    r.kind = "option";
    r.prompt = "take the damage, or cover cards and take none?";
    r.options.push_back({"take damage", true, {}});
    r.options.push_back({"cover " + std::to_string(a.evadeCover) + " and take no damage", true, {}});
    int idx = ask_one(target, std::move(r));
    if (idx == 1) {
      for (int k = 0; k < a.evadeCover && !ps(target).deck.empty(); ++k) {
        int inst = ps(target).deck.back();
        move_card(inst, Zone::Cover);
        ci(inst).faceUp = false;
      }
      a.hit = true;
      return;
    }
  }

  // ---- damage --------------------------------------------------------------
  std::optional<int> effA = a.aura;
  std::optional<int> effL = a.life;
  if (effA) *effA += a.auraDelta;
  if (effL) *effL += a.lifeDelta;
  apply_damage_to(target, effA, effL, a.keywords, a.sourceInst,
                  a.attackerChoosesDamage ? a.attacker : -1, true);
  a.hit = true;
  effects_->run_after_attack(*this, &a);
}

// ---------------------------------------------------------------------------
// playing cards
// ---------------------------------------------------------------------------

void Engine::play_card(Player p, int inst, bool asResponse) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  ps(p).cardsPlayedThisTurn += 1;

  if (d.kind == CardKind::Special) {
    move_crystals(AreaRef::flare(p), AreaRef::dust(), cut_cost(p, defId, inst));
    ci(inst).faceUp = true;  // used / 展开
  } else {
    auto& h = ps(p).hand;
    h.erase(std::remove(h.begin(), h.end(), inst), h.end());
    ci(inst).zone = Zone::Limbo;
  }

  if (d.type == CardType::Enhance) {
    // Order per the rules: 展开时 first, then place 献, then discard if empty.
    if (effects_->has(defId, "on_enter")) effects_->call(*this, defId, "on_enter", p, inst);
    int total = st.dust + ps(p).aura;
    if (total <= 0) {
      // No 献 can be placed: the card still ran 展开时 and now runs 弃置时.
      if (d.kind == CardKind::Normal) {
        move_card(inst, Zone::Discard);
        if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
      }
      // a 切札 enhance stays face-up (used) in the special zone
    } else {
      int take = std::min(d.nagi, total);
      int loDust = std::max(0, take - ps(p).aura);
      int hiDust = std::min(take, st.dust);
      int fromDust = loDust;
      if (loDust != hiDust) {
        Request r;
        r.kind = "option";
        r.prompt = "choose 纳 cost split";
        for (int df = loDust; df <= hiDust; ++df) {
          Option o;
          o.label = "dust " + std::to_string(df) + " + aura " + std::to_string(take - df);
          r.options.push_back(o);
        }
        fromDust = loDust + ask_one(p, std::move(r));
      }
      int auraBefore = ps(p).aura;
      add_crystals(AreaRef::dust(), -fromDust);
      add_crystals(AreaRef::aura(p), -(take - fromDust));
      if (ps(p).aura != auraBefore) notify_aura_changed(p);
      if (d.kind == CardKind::Normal)
        move_card(inst, Zone::Enhance);
      else
        move_card(inst, Zone::Special);
      ci(inst).crystals = take;
    }
  } else if (d.type == CardType::Attack) {
    // on_play (if any) runs before the attack and may modify the responded attack.
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
    Attack a = make_attack(p, inst, asResponse, true);
    resolve_attack(a);
    if (a.hit && effects_->has(defId, "on_attack_after"))
      effects_->call(*this, defId, "on_attack_after", p, inst);
  } else {  // Action
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
  }

  // 终端 played during the opponent's turn: cannot respond for the rest of it.
  if ((d.flags & CF_Terminal) && st.active != p) ps(p).cannotRespond = true;

  if (d.kind == CardKind::Normal && ci(inst).zone == Zone::Limbo) move_card(inst, Zone::Discard);
}

// ---------------------------------------------------------------------------
// turn structure
// ---------------------------------------------------------------------------

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
  if (ask_yes_no(p, "rebuild deck (1 life)?")) {
    rebuild(p, true);
    if (st.over) return;
  }
  draw(p, 2);
}

bool Engine::basic_legal(Player p, BasicAction a) const {
  switch (a) {
    case BasicAction::Advance:
      return st.distance > st.nearDistance && st.distance >= 1 && ps(p).aura < max_aura(p);
    case BasicAction::Retreat: return ps(p).aura >= 1;
    case BasicAction::Aura:    return st.dust >= 1 && ps(p).aura < max_aura(p);
    case BasicAction::Flare:   return ps(p).aura >= 1;
    case BasicAction::Escape:  return st.distance <= st.nearDistance && st.dust >= 1;
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
    ci(inst).faceUp = false;
    clamp_aura(ci(inst).owner);
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
}

void Engine::notify_aura_changed(Player p) {
  if (auraChangeFired_[p]) return;  // only the first change this turn
  auraChangeFired_[p] = 1;
  fire("aura_changed", p, nullptr, -1, true);
}

void Engine::main_phase(Player p) {
  mainDirty_ = false;
  while (!st.over) {
    if (abortMain_) break;
    struct Move {
      enum K { Basic, Play, Pass } k = Pass;
      BasicAction basic = BasicAction::Advance;
      bool payCover = false;
      int inst = -1;
      bool fullPower = false;
      bool terminal = false;
    };
    std::vector<Move> moves;
    Request r;
    r.kind = "main";
    r.prompt = "main phase action";
    bool canPay = ps(p).vigor >= 1 || !ps(p).hand.empty();

    if (canPay && !ps(p).cannotBasic) {
      for (int bi = 0; bi < 5; ++bi) {
        BasicAction ba = static_cast<BasicAction>(bi);
        if (!basic_legal(p, ba)) continue;
        for (int payTmp = 0; payTmp < 2; ++payTmp) {
          bool payCover = payTmp == 1;
          if (payCover && ps(p).hand.empty()) continue;
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
      if (!playable_card(p, inst)) continue;
      if (d.type == CardType::Attack && ps(p).cannotAttack) continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!tmp.range.contains(st.distance)) continue;
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
    }

    for (int inst : ps(p).special) {
      if (ci(inst).faceUp) continue;
      const CardDef& d = def_of(inst);
      if (ps(p).flare < cut_cost(p, d.id, inst)) continue;
      if (!playable_card(p, inst)) continue;
      bool fp = (d.flags & CF_FullPower) != 0;
      if (fp && mainDirty_) continue;
      if (d.type == CardType::Attack && ps(p).cannotAttack) continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!tmp.range.contains(st.distance)) continue;
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

    if (m.k == Move::Basic) {
      if (m.payCover) {
        Request cr;
        cr.kind = "cards";
        cr.prompt = "cover which card to pay?";
        for (int inst : ps(p).hand) {
          Option o;
          o.label = card_label(def_of(inst));
          o.data = card_json(def_of(inst));
          o.data["inst"] = inst;
          cr.options.push_back(o);
        }
        int ci2 = ask_one(p, std::move(cr));
        int coverInst = ps(p).hand[static_cast<size_t>(std::min(ci2, static_cast<int>(ps(p).hand.size()) - 1))];
        move_card(coverInst, Zone::Cover);
        ci(coverInst).faceUp = false;
      } else {
        ps(p).vigor -= 1;
      }
      do_basic(p, m.basic);  // payment already handled above
      mainDirty_ = true;
    } else {
      play_card(p, m.inst, false);
      mainDirty_ = true;
      if (m.fullPower || m.terminal) break;
    }
    check_win();
  }
  abortMain_ = false;
}

void Engine::cover_phase(Player p) {
  while (!st.over && static_cast<int>(ps(p).hand.size()) > effective_hand_limit(p)) {
    Request r;
    r.kind = "cards";
    r.prompt = "cover a card (hand must be <= 2)";
    for (int inst : ps(p).hand) {
      Option o;
      o.label = card_label(def_of(inst));
      o.data = card_json(def_of(inst));
      o.data["inst"] = inst;
      r.options.push_back(o);
    }
    int idx = ask_one(p, std::move(r));
    idx = std::min(idx, static_cast<int>(ps(p).hand.size()) - 1);
    int inst = ps(p).hand[static_cast<size_t>(idx)];
    move_card(inst, Zone::Cover);
    ci(inst).faceUp = false;
  }
}

void Engine::end_phase(Player p) {
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
  }
  abortMain_ = false;
  fire("turn_start", p, nullptr, -1, false);
  if (!ps(p).firstTurnDone) {
    ps(p).firstTurnDone = true;  // first turn of each player skips the start phase
  } else {
    start_phase(p);
  }
  if (!st.over) main_phase(p);
  if (!st.over) cover_phase(p);
  if (!st.over) end_phase(p);
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

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
    if (d.goddess == g && d.form == "O") chosen[{static_cast<int>(d.kind), d.local}] = d.id;
  if (f != "O")
    for (const CardDef& d : defs)
      if (d.goddess == g && d.form == f)
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

  build_from_pool(p, normals, specials);
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

nlohmann::json Engine::observation(Player v) const {
  using nlohmann::json;
  json j;
  j["you"] = static_cast<int>(v);
  j["turn"] = st.turn;
  j["active"] = static_cast<int>(st.active);
  j["distance"] = st.distance;
  j["dust"] = st.dust;
  j["nearDistance"] = st.nearDistance;
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const PlayerState& s = ps(p);
    json pj;
    pj["life"] = s.life;
    pj["aura"] = s.aura;
    pj["flare"] = s.flare;
    pj["vigor"] = s.vigor;
    pj["cower"] = s.cower;
    pj["cannotRespond"] = s.cannotRespond;
    pj["cardsPlayedThisTurn"] = s.cardsPlayedThisTurn;
    pj["sets"] = playerSets_[pi];
    pj["handCount"] = static_cast<int>(s.hand.size());
    pj["deckCount"] = static_cast<int>(s.deck.size());
    pj["coverCount"] = static_cast<int>(s.cover.size());
    json disc = json::array();
    for (int inst : s.discard) disc.push_back(def_of(inst).name);
    pj["discard"] = disc;
    json enh = json::array();
    for (int inst : s.enhance) {
      json e;
      e["owner"] = pi;
      e["inst"] = inst;
      e["name"] = def_of(inst).name;
      e["crystals"] = ci(inst).crystals;
      enh.push_back(e);
    }
    pj["enhance"] = enh;
    json sp = json::array();
    for (int inst : s.special) {
      const CardInstance& c = ci(inst);
      json e;
      e["owner"] = pi;
      if (pi == static_cast<int>(v) || c.faceUp) {
        e["inst"] = inst;
        e["name"] = def_of(inst).name;
        e["used"] = c.faceUp;
      } else {
        e["hidden"] = true;
      }
      sp.push_back(e);
    }
    pj["special"] = sp;
    if (pi == static_cast<int>(v)) {
      json h = json::array();
      for (int inst : s.hand) {
        json e = card_json(def_of(inst));
        e["inst"] = inst;
        h.push_back(e);
      }
      pj["hand"] = h;
    }
    j["players"][pi] = pj;
  }
  return j;
}

}  // namespace fy
