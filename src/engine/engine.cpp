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
    case Zone::Parts:   return &st.p[owner].parts;
    case Zone::Sealed:  return nullptr;
    case Zone::Bag:     return &st.p[owner].bag;
    case Zone::Removed: return nullptr;
    case Zone::Limbo:   return nullptr;
  }
  return nullptr;
}

bool vec_has(const std::vector<int>& v, int x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

// 女神 + 形态 -> 显示名（刀/古/心 ...），用于结果串。
std::string goddess_display(const std::string& g, const std::string& form) {
  if (g == "yurina") return form == "A1" ? "古" : form == "A2" ? "心" : "刀";
  if (g == "saine") return form == "A1" ? "琵" : form == "A2" ? "拒" : "薙";
  if (g == "himika") return form == "A1" ? "炎" : "铳";
  if (g == "tokoyo") return form == "A1" ? "笛" : form == "A2" ? "恐" : "扇";
  if (g == "oboro") return form == "A1" ? "战" : form == "A2" ? "电" : "忍";
  if (g == "yukihi") return form == "A1" ? "社" : "雪";
  if (g == "shinra") return form == "A1" ? "经" : "书";
  if (g == "hagane") return form == "A1" ? "金" : "锤";
  if (g == "chikage") return form == "A1" ? "绊" : "毒";
  if (g == "kururu") return form == "A1" ? "机" : form == "A2" ? "友" : "络";
  if (g == "raira") return form == "A1" ? "岚" : "爪";
  return g;
}

struct StackGuard {
  std::vector<Engine::StackEntry>& v;
  ~StackGuard() {
    if (!v.empty()) v.pop_back();
  }
};

}  // namespace

Engine::Engine(Config c) : cfg(std::move(c)) { effects_ = std::make_unique<EffectHost>(); }
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
    if (recording_) journal_.push_back({p, req.kind, d.indices});
  }

  if (tracing_ && fi >= 0) {
    using nlohmann::json;
    json ch = json::array();
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(req.options.size())) ch.push_back(req.options[i].label);
    frames_[static_cast<size_t>(fi)]["choice"] = ch;
  }
  return d;
}

nlohmann::json Engine::full_state_json() const {
  using nlohmann::json;
  json j;
  j["turn"] = st.turn;
  j["active"] = static_cast<int>(st.active);
  j["phase"] = phase_;
  j["distance"] = distance();
  j["dust"] = st.dust;
  j["nearDistance"] = st.nearDistance;
  j["over"] = st.over;
  j["winner"] = st.winner;
  json stack = json::array();
  for (const StackEntry& s : callStack_) {
    const CardDef& d = defs[static_cast<size_t>(s.def)];
    stack.push_back({{"name", d.name}, {"owner", static_cast<int>(s.owner)}, {"set", d.set}});
  }
  j["stack"] = stack;
  auto card = [&](int inst) -> json {
    const CardDef& d = def_of(inst);
    return {{"name", d.name},
            {"kind", d.kind == CardKind::Normal ? "normal" : "special"},
            {"type", d.type == CardType::Attack   ? "attack"
                     : d.type == CardType::Enhance ? "enhance"
                                                   : "action"},
            {"cost", d.cost},
            {"set", d.set}};
  };
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
    pj["umbrella"] = s.umbrella;
    pj["yukihi"] = s.yukihi;
    pj["strategy"] = s.strategy;
    pj["strategyKnown"] = s.strategyKnown;
    pj["sets"] = playerSets_[pi];
    pj["hand"] = json::array();
    for (int i : s.hand) pj["hand"].push_back(card(i));
    pj["special"] = json::array();
    for (int i : s.special) {
      json c = card(i);
      c["used"] = ci(i).faceUp;
      pj["special"].push_back(c);
    }
    pj["discard"] = json::array();
    for (int i : s.discard) pj["discard"].push_back(card(i));
    pj["cover"] = json::array();
    for (int i : s.cover) pj["cover"].push_back(card(i));
    pj["deck"] = json::array();
    for (int i : s.deck) pj["deck"].push_back(card(i));
    pj["enhance"] = json::array();
    for (int i : s.enhance) {
      json c = card(i);
      c["crystals"] = ci(i).crystals;
      pj["enhance"].push_back(c);
    }
    pj["parts"] = json::array();
    for (int i : s.parts) {
      json c = card(i);
      c["assembled"] = ci(i).assembled;
      c["core"] = def_of(i).corePart;
      pj["parts"].push_back(c);
    }
    j["players"].push_back(pj);
  }
  return j;
}

nlohmann::json Engine::trace_json() const {
  using nlohmann::json;
  json j;
  j["seed"] = cfg.seed;
  j["mode"] = cfg.mode;
  j["frames"] = frames_;
  json draft;
  if (hasDraft_[0] || hasDraft_[1]) {
    for (int i = 0; i < 2; ++i) {
      json picks = json::array();
      for (const auto& [g, f] : draftPicks_[i]) {
        json e;
        e["goddess"] = g;
        e["form"] = f;
        e["display"] = goddess_display(g, f);
        e["banned"] = (draftBans_[i] == std::make_pair(g, f));
        picks.push_back(e);
      }
      draft[std::to_string(i)] = picks;
    }
    j["draft"] = draft;
  }
  j["result"] = {{"winner", st.winner}, {"text", result_text()}};
  return j;
}

std::string Engine::result_text() const {
  if (hasDraft_[0] && hasDraft_[1]) {
    auto fmt = [&](int i) {
      std::string s;
      for (const auto& [g, f] : draftPicks_[i]) {
        std::string n = goddess_display(g, f);
        if (draftBans_[i] == std::make_pair(g, f)) n = "（" + n + "）";
        s += n;
      }
      return s;
    };
    std::string a = "Player0 " + fmt(0);
    std::string b = "Player1 " + fmt(1);
    if (st.winner == 0) return a + " 胜 " + b;
    if (st.winner == 1) return b + " 胜 " + a;
    return a + " 平局 " + b;
  }
  std::string a = "Player0";
  std::string b = "Player1";
  if (st.winner == 0) return a + " 胜 " + b;
  if (st.winner == 1) return b + " 胜 " + a;
  return a + " 平局 " + b;
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
  j["mode"] = cfg.mode;
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
  if (auto* v = zone_ptr(st, c.owner, c.zone))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  c.zone = z;
  if (auto* v = zone_ptr(st, c.owner, z)) v->push_back(inst);
  if (z == Zone::Discard && old != Zone::Discard)
    fire("discarded", c.owner, nullptr, inst, false);  // 提婆
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
  auraDamagedThisTurn_[target] = true;  // 斩击乱舞: 本回合敌装受到过伤害
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
  int delta = ps(p).cutCostDelta + (ps(p).cutCostPermanent ? -1 : 0);
  return std::max(0, base + delta);
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
    Player pl = static_cast<Player>(i);
    if (ps(pl).life > 0) continue;
    if (try_revive(pl)) continue;  // 最后的结晶救回
    st.over = true;
    st.winner = opp(pl);
    ps(pl).life = 0;
  }
}

bool Engine::try_revive(Player p) {
  if (ps(p).usedLastCrystal) return false;
  int card = -1;
  for (int inst : ps(p).special)
    if (!ci(inst).faceUp && def_of(inst).name == "最后的结晶") card = inst;
  if (card < 0) return false;
  int cost = cut_cost(p, ci(card).def, card);
  if (ps(p).flare < cost) return false;
  Request r;
  r.kind = "option";
  r.prompt = "命归零：使用『最后的结晶』复活？";
  r.options.push_back({"使用（支付 " + std::to_string(cost) + " 气）", true, {}});
  r.options.push_back({"不使用", true, {}});
  if (ask_one(p, std::move(r)) != 0) return false;
  move_crystals(AreaRef::flare(p), AreaRef::dust(), cost, false);
  move_crystals(AreaRef::life(p), AreaRef::dust(), ps(p).life, false);  // all remaining life -> dust
  if (!ps(p).hand.empty()) {
    Request cr;
    cr.kind = "cards";
    cr.prompt = "盖伏 1 张手牌";
    for (int inst : ps(p).hand) {
      Option o;
      o.label = def_of(inst).name;
      o.data = {{"inst", inst}};
      cr.options.push_back(o);
    }
    cr.minSel = 1;
    cr.maxSel = 1;
    int k = ask_one(p, std::move(cr));
    k = std::min(k, static_cast<int>(ps(p).hand.size()) - 1);
    int ci2 = ps(p).hand[static_cast<size_t>(k)];
    move_card(ci2, Zone::Cover);
    ci(ci2).faceUp = false;
  }
  move_crystals(AreaRef::dust(), AreaRef::life(p), 1, false);
  ci(card).faceUp = true;
  ps(p).usedLastCrystal = true;
  return true;
}

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
  int allowance = 1;
  if (has_named("虚鱼")) allowance += 1;
  int used = 0;
  while (used < allowance && !st.over) {
    std::vector<int> setupCards;
    for (int inst : ps(p).cover)
      if (def_of(inst).setupCard) setupCards.push_back(inst);
    if (setupCards.empty()) break;
    Request sr;
    sr.kind = "option";
    sr.prompt = "设置：从盖牌区使用一张设置牌？";
    sr.options.push_back({"不使用", true, {}});
    for (int inst : setupCards) sr.options.push_back({card_label(def_of(inst)), true, {}});
    int sc = ask_one(p, std::move(sr));
    if (sc <= 0) break;
    int chosen = setupCards[static_cast<size_t>(sc - 1)];
    play_from_cover(p, chosen, false, true);  // setup cards go to the deck
    used += 1;
    if (def_of(chosen).name == "忍步") allowance += 1;
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
  if (a.hit)
    for (int cp : chosenAdds) effects_->apply_part(*this, ci(cp).def, p, a, n, "after");
  disassemble_part(p, core);
  for (int cp : chosenAdds) disassemble_part(p, cp);
  check_win();
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
    fire("enhance_left", p, nullptr, inst, false);
  }
  clamp_aura(p);  // a card providing bonus aura may have left play
}

void Engine::consume_enhance_crystal(int inst) {
  CardInstance& c = ci(inst);
  if (c.crystals <= 0) return;
  const CardDef& d = def_of(inst);
  Player owner = c.owner;
  c.crystals -= 1;
  // a dropped 献 falls to 虚 by default; e.g. 圈域 sends it to 距 instead.
  if (d.decayTo == "distance")
    st.distance += 1;
  else
    st.dust += 1;
  if (c.crystals > 0) return;
  if (d.kind == CardKind::Normal) {
    move_card(inst, Zone::Discard);
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
    fire("enhance_left", owner, nullptr, inst, false);  // 森罗判证
  } else {
    // 切札付与 also runs its 弃置时 when it leaves play.
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
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
  if (consumePending && forceUnrespondable_) {
    a.keywords |= AF_Unrespondable;
    forceUnrespondable_ = false;
  }
  effects_->finalize_attack(*this, p, a, consumePending);
  if (consumePending) {
    int n = note_attack(p);
    fire("attack_declared", p, &a, -1, n == 1);
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
  auto doAura = [&](int n) {
    if (damageToDistance_)
      move_crystals(AreaRef::aura(target), AreaRef::distance(), n, false);
    else
      spend_aura(target, n);
  };
  auto doLife = [&](int n) {
    damage_life(target, n, damageToDistance_ ? AreaKind::Distance : AreaKind::Flare, true);
  };

  if (keywords & AF_BothSides) {
    // 两侧伤害: resolve the aura side (like X/-) and the life side (like -/Y).
    if (effA) {
      int n = std::min(*effA, effective_armor(target));
      doAura(n);
      lastDmgSide_ = 1;
      lastDmgAmount_ = n;
    }
    if (effL) {
      int before = ps(target).life;
      doLife(*effL);
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
      doAura(*effA);
      lastDmgSide_ = 1;
      lastDmgAmount_ = *effA;
    } else {
      int before = ps(target).life;
      doLife(*effL);
      lastDmgSide_ = 2;
      lastDmgAmount_ = before - ps(target).life;
    }
  } else if (effA && !effL) {
    int n = std::min(*effA, effective_armor(target));
    doAura(n);
    lastDmgSide_ = 1;
    lastDmgAmount_ = n;
  } else if (effL) {
    int before = ps(target).life;
    doLife(*effL);
    lastDmgSide_ = 2;
    lastDmgAmount_ = before - ps(target).life;
  }
  damageToDistance_ = false;
  check_win();
}

void Engine::deal_damage(Player target, std::optional<int> aura, std::optional<int> life,
                         uint32_t keywords) {
  apply_damage_to(target, aura, life, keywords, -1);
}

void Engine::resolve_attack(Attack& a) {
  if (st.over) return;
  if (!a.counted) {
    a.counted = true;
    note_attack(a.attacker);
  }
  attackedThisTurn_[a.attacker] = true;
  fire("attack_counted", a.attacker, &a, -1, attacksThisTurn_[a.attacker] == 2);
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
      if ((a.keywords & AF_NoNormalResponse) && d.kind == CardKind::Normal) return;
      if ((a.keywords & AF_NoEnhanceResponse) && d.type == CardType::Enhance) return;
      if ((a.keywords & AF_NoAttackResponse) && d.type == CardType::Attack) return;
      if ((a.keywords & AF_NoActionResponse) && d.type == CardType::Action) return;
      if (!respondable_card(target, inst)) return;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(target, inst, true, false);
        if (!tmp.range.contains(distance())) return;
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
  if (!(a.keywords & AF_Lock) && !a.range.contains(distance())) a.missed = true;
  bool negated = a.negated && !(a.keywords & AF_NoNegate);
  if (negated || a.missed) return;
  if (a.negateDamage) {  // 驳论: negate only the damage, keep附加效果
    a.hit = true;
    effects_->run_after_attack(*this, &a);
    return;
  }

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
  damageToDistance_ = (a.keywords & AF_ToDistance) != 0;
  apply_damage_to(target, effA, effL, a.keywords, a.sourceInst,
                  a.attackerChoosesDamage ? a.attacker : -1, true);
  a.hit = true;
  fire("attack_resolved", a.attacker, &a, -1, false);  // 圆环轮回旋
  effects_->run_after_attack(*this, &a);
}

// ---------------------------------------------------------------------------
// playing cards
// ---------------------------------------------------------------------------

void Engine::play_card(Player p, int inst, bool asResponse, bool zenkai) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  callStack_.push_back({defId, p});
  StackGuard stackGuard{callStack_};
  ps(p).cardsPlayedThisTurn += 1;

  if (d.kind == CardKind::Special) {
    move_crystals(AreaRef::flare(p), AreaRef::dust(), cut_cost(p, defId, inst));
    ci(inst).faceUp = true;  // used / 展开
    ci(inst).usedThisTurn = true;
  } else {
    auto& h = ps(p).hand;
    h.erase(std::remove(h.begin(), h.end(), inst), h.end());
    ci(inst).zone = Zone::Limbo;
  }

  resolve_card_effect(p, inst, asResponse, zenkai);

  if (d.kind == CardKind::Normal && ci(inst).zone == Zone::Limbo) {
    if (d.isPoison) {
      if (d.name == "灭灯毒")
        force_move(inst, Zone::Discard);
      else
        return_poison(inst);  // poisons return to their owner's bag
    } else {
      move_card(inst, Zone::Discard);
    }
  }
}

void Engine::resolve_card_effect(Player p, int inst, bool asResponse, bool zenkai) {
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  keisouDoubled_ = false;  // 骇客装置: one doubling per 机巧 resolution
  // Raira 风雷: using a non-Raira card raises one slot by 1.
  if (ps(p).raira && d.goddess != "raira" && !ps(p).rairaGainRestricted) {
    Request r;
    r.kind = "option";
    r.prompt = "风雷：选择一个槽 +1";
    r.options.push_back({"风神 +1", true, {}});
    r.options.push_back({"雷神 +1", true, {}});
    int c = ask_one(p, std::move(r));
    if (c == 1) {
      if (ps(p).thunder < 20) ps(p).thunder += 1;
    } else {
      if (ps(p).wind < 20) ps(p).wind += 1;
    }
  }
  if (d.centrifugal) playedCentrifugalThisTurn_[p] = true;
  if (d.name == "炼成攻击") playedLianchengThisTurn_[p] = true;
  bool prevZenkai = zenkaiActive_;
  zenkaiActive_ = zenkai;
  // Yukihi A1-S2: first non-Yukihi normal card each turn.
  if (d.kind == CardKind::Normal && !card_has_goddess(inst, "yukihi")) {
    normalNonYukihi_[p] += 1;
    fire("normal_card_used", p, nullptr, inst, normalNonYukihi_[p] == 1);
  }
  if (d.type == CardType::Enhance) {
    // Re-expanding a card that still holds 献 (e.g. reuse): return the old ones first.
    if (ci(inst).crystals > 0) {
      int old = ci(inst).crystals;
      ci(inst).crystals = 0;
      if (d.decayTo == "distance")
        st.distance += old;
      else
        st.dust += old;
    }
    // Order per the rules: 展开时 first, then place 献, then discard if empty.
    if (effects_->has(defId, "on_enter")) effects_->call(*this, defId, "on_enter", p, inst);
    int take = 0;
    int total = st.dust + ps(p).aura;
    int nagiVal = d.nagi + pendingNagiAdjust_;
    pendingNagiAdjust_ = 0;
    if (nagiVal < 0) nagiVal = 0;
    if (total > 0 && nagiVal > 0) {
      take = std::min(nagiVal, total);
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
    }
    if (d.kind == CardKind::Normal)
      move_card(inst, Zone::Enhance);
    else
      move_card(inst, Zone::Special);
    ci(inst).crystals += take;  // on_enter may have added crystals (e.g. 反射装置)
    if (ci(inst).crystals <= 0) {
      ci(inst).crystals = 0;
      if (d.kind == CardKind::Normal) {
        move_card(inst, Zone::Discard);
        if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
        fire("enhance_left", p, nullptr, inst, false);
      } else {
        if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
        move_card(inst, Zone::Special);
      }
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
    fire("action_resolved", p, nullptr, inst, false);  // 模块化
  }
  if (d.flags & CF_FullPower) {
    usedFullPowerThisTurn_[p] = true;
    fire("fullpower_used", p, nullptr, inst, false);
  }
  if ((d.flags & CF_Terminal) && st.active != p) ps(p).cannotRespond = true;
  zenkaiActive_ = prevZenkai;
}

void Engine::cover_card(int inst) {
  move_card(inst, Zone::Cover);
  ci(inst).faceUp = false;
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

void Engine::use_from_cover(int inst, bool asResponse) {
  play_from_cover(ci(inst).owner, inst, asResponse, false);
}

void Engine::play_from_cover(Player p, int inst, bool asResponse, bool toDeckAfter) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  move_card(inst, Zone::Limbo);
  callStack_.push_back({d.id, p, true});
  StackGuard stackGuard{callStack_};
  ps(p).cardsPlayedThisTurn += 1;
  resolve_card_effect(p, inst, asResponse);
  if (ci(inst).zone == Zone::Limbo) {
    if (toDeckAfter)
      move_card(inst, Zone::Deck);
    else
      move_card(inst, Zone::Discard);
  }
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
  run_rebuild(p);
  if (st.over) return;
  draw(p, 2);
}

bool Engine::basic_legal(Player p, BasicAction a) const {
  if (transform_is(p, "夜叉") && ps(p).steamEngine == 0) return false;  // 夜叉: 引擎空不能基本动作
  switch (a) {
    case BasicAction::Advance:
      return !ps(p).cannotAdvance && distance() > st.nearDistance && distance() >= 1 &&
             ps(p).aura < max_aura(p);
    case BasicAction::Retreat:
      return !has_named_active(opp(p), "泥泞") && ps(p).aura >= 1;
    case BasicAction::Aura:    return st.dust >= 1 && ps(p).aura < max_aura(p);
    case BasicAction::Flare:   return ps(p).aura >= 1;
    case BasicAction::Escape:
      return !has_named_active(opp(p), "泥泞") && distance() <= st.nearDistance && st.dust >= 1;
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
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (!playable_card(p, inst)) continue;
      if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
      if (d.type == CardType::Attack && (ps(p).cannotAttack || has_named_active(p, "迟缓毒")))
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
      if (d.type == CardType::Attack && (ps(p).cannotAttack || has_named_active(p, "迟缓毒")))
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
      if (d.type == CardType::Attack && (ps(p).cannotAttack || has_named_active(p, "迟缓毒")))
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
      bool canPay = ps(p).vigor >= 1 || !ps(p).hand.empty();
      for (int td : active_transform_defs(p)) {
        if (!effects_->has(td, "extra_basic")) continue;
        if (def(td).name == "阿修罗" && ashuraExtraUsed_[p]) continue;
        if (!canPay) continue;
        for (int payTmp = 0; payTmp < 2; ++payTmp) {
          bool payCover = payTmp == 1;
          if (payCover && ps(p).hand.empty()) continue;
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
        int k = ask_one(p, std::move(cr));
        k = std::min(k, static_cast<int>(ps(p).hand.size()) - 1);
        int coverInst = ps(p).hand[static_cast<size_t>(k)];
        move_card(coverInst, Zone::Cover);
        ci(coverInst).faceUp = false;
      } else {
        ps(p).vigor -= 1;
      }
      if (def(m.extraDef).name == "阿修罗") ashuraExtraUsed_[p] = true;
      effects_->call(*this, m.extraDef, "extra_basic", p, -1);
      mainDirty_ = true;
      check_win();
      continue;
    }

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
      didBasicThisTurn_[p] = true;
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
  reset_steam_at_turn_start();  // 气动 steam returns to the exhausted module
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

int Engine::enhance_crystal_total(Player p) const {
  int t = 0;
  for (int i : ps(p).enhance) t += ci(i).crystals;
  return t;
}

int Engine::dust_to_card(int inst, int n) {
  return move_crystals(AreaRef::dust(), AreaRef::card(inst), n, true);
}

void Engine::use_card(int inst, bool asResponse) {
  play_from_cover(ci(inst).owner, inst, asResponse, false);
}

void Engine::switch_weapon(Player p, int source) {
  if (!ps(p).yukihi) return;
  ps(p).umbrella = !ps(p).umbrella;
  fire("weapon_switched", p, nullptr, source, false);
  std::vector<int> sp = ps(p).special;
  for (int inst : sp) {
    if (!ci(inst).faceUp) continue;
    ResetInfo ri = effects_->reset_info(def_of(inst).id);
    if (ri.kind == 2 && ri.trigger == "weapon_switched") reset_special(inst);
  }
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

void Engine::use_foreign_card(Player user, int inst) {
  if (st.over) return;
  const int defId = ci(inst).def;
  move_card(inst, Zone::Limbo);
  callStack_.push_back({defId, user, false});
  StackGuard stackGuard{callStack_};
  resolve_card_effect(user, inst, false);
  if (ci(inst).zone == Zone::Limbo) move_card(inst, Zone::Discard);  // owner's discard
}

int Engine::distance_delta() const {
  int d = 0;
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    d += ps(o).steamOnDist - ps(o).steamOnCrystal;  // 气动
    for (int inst : ps(o).enhance)
      if (def_of(inst).distanceMod) d += def_of(inst).distanceMod;
    for (int inst : ps(o).special)
      if (ci(inst).faceUp && def_of(inst).distanceMod) d += def_of(inst).distanceMod;
  }
  return d;
}

int Engine::distance() const {
  int d = st.distance + distance_delta();
  return d < 0 ? 0 : d;
}

void Engine::place_poison(int inst, Player holder, Zone z) {
  ci(inst).owner = holder;
  move_card(inst, z);
}

void Engine::return_poison(int inst) {
  if (ci(inst).bagOwner >= 0) {
    ci(inst).owner = static_cast<Player>(ci(inst).bagOwner);
    move_card(inst, Zone::Bag);
  }
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

int Engine::sealed_card(int host) const {
  if (host < 0) return -1;
  const auto& s = ci(host).sealed;
  return s.empty() ? -1 : s.front();
}

void Engine::reuse_special(int inst) { resolve_card_effect(ci(inst).owner, inst, false); }

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

void Engine::cover_top(Player p) {
  if (ps(p).deck.empty()) return;
  int inst = ps(p).deck.back();
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
    if (def_of(inst).name == "枢的骇客装置" && ci(inst).crystals > 0) host = inst;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).name == "枢的骇客装置" && ci(inst).crystals > 0) host = inst;
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
  std::vector<std::string> gods;
  for (const auto& s : playerSets_[p]) {
    auto dot = s.find('.');
    gods.push_back(dot == std::string::npos ? s : s.substr(0, dot));
  }
  std::vector<std::string> out;
  for (const CardDef& d : defs) {
    if (d.kind != CardKind::Normal || d.isPart || d.isExtra || d.isPoison) continue;
    if (std::find(gods.begin(), gods.end(), d.goddess) == gods.end()) continue;
    if (std::find(out.begin(), out.end(), d.name) == out.end()) out.push_back(d.name);
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
  if (has_named_active(p, "萨利亚的杰作")) {
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
  for (const CardDef& d : defs)
    if (d.isTransform && d.goddess == "thallya" && d.form == form)
      add_instance(d.id, p);  // kept in Zone::Removed (追加区)
}

std::vector<int> Engine::transform_cards(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed && def_of(i).isTransform)
      out.push_back(i);
  return out;
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

void Engine::reset_steam_at_turn_start() {
  for (int i = 0; i < 2; ++i) {
    Player pl = static_cast<Player>(i);
    ps(pl).steamExhausted += ps(pl).steamOnDist + ps(pl).steamOnCrystal;
    ps(pl).steamOnDist = 0;
    ps(pl).steamOnCrystal = 0;
    ashuraExtraUsed_[i] = false;
  }
}

std::vector<int> Engine::active_transform_defs(Player p) const {
  std::vector<int> out;
  if (ps(p).transformDef >= 0) out.push_back(ps(p).transformDef);
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == "快速改装") {
      int s = sealed_card(inst);
      if (s >= 0) out.push_back(ci(s).def);
    }
  return out;
}

bool Engine::can_burn(Player p, int x) const {
  if (has_named_active(p, "萨利亚的杰作")) return ps(p).steamExhausted > 0;
  return ps(p).steamEngine >= x;
}

bool Engine::transform_is(Player p, const std::string& name) const {
  return ps(p).transformDef >= 0 && def(ps(p).transformDef).name == name;
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

bool Engine::has_named_active(Player p, const std::string& name) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == name) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).name == name) return true;
  return false;
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

bool Engine::current_from_cover() const {
  return !callStack_.empty() && callStack_.back().fromCover;
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

nlohmann::json Engine::observation(Player v) const {
  using nlohmann::json;
  json j;
  j["you"] = static_cast<int>(v);
  j["turn"] = st.turn;
  j["active"] = static_cast<int>(st.active);
  j["distance"] = distance();
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
    pj["umbrella"] = s.umbrella;
    pj["yukihi"] = s.yukihi;
    if (pi == static_cast<int>(v) || s.strategyKnown) pj["strategy"] = s.strategy;
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
      if (pi == static_cast<int>(v) || c.faceUp || revealOppSpecials_[v]) {
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
      json parts = json::array();
      for (int inst : s.parts) {
        json e;
        e["inst"] = inst;
        e["name"] = def_of(inst).name;
        e["assembled"] = ci(inst).assembled;
        parts.push_back(e);
      }
      pj["parts"] = parts;
    } else {
      pj["assembledCount"] = assembled_count(p);
      pj["partsTotal"] = static_cast<int>(s.parts.size());
    }
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
