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


void Engine::set_agent(Player p, Agent* a) { agents_[p] = a; }

Decision Engine::decide(Player p, Request req) {
  req.player = p;
  // 20-Kanawe: 完成戏剧后的地图推进在所有决策点之前结算（此时不会破坏
  // 正在进行的攻击/伤害结算的中间态）。
  if (!st.over && (pendingAdvance_[P0] || pendingAdvance_[P1])) flush_drama_advances();
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
  // 18-Mizuki: a soldier that would be discarded/covered returns to its 兵舍
  // face down instead (打出后翻回未动员并留在兵舍).
  if (c.soldier && (z == Zone::Discard || z == Zone::Cover) &&
      !vec_has(ps(c.holder).barracks, inst)) {
    if (c.crystals + c.green > 0) {
      int sak = 0;
      int cr = c.crystals + c.green;
      take_card_crystals(inst, cr, kTakeLeaving, &sak);
      if (sak > 0) decay_crystals(inst, sak);
    }
    // The soldier may be leaving play from the enhance zone (骑兵); that vector
    // must be cleared before it moves to the barracks.
    if (auto* v = zone_ptr(st, c.holder, c.zone))
      v->erase(std::remove(v->begin(), v->end(), inst), v->end());
    to_barracks(c.holder, inst, false);
    return;
  }
  // Poison cards cannot be discarded or covered by any means (only played/removed).
  if (!poisonForce_ && c.def >= 0 && def_of(inst).isPoison &&
      (z == Zone::Discard || z == Zone::Cover))
    return;
  // An expanded enhancement that leaves play keeps no 献: route them to dust/decay.
  bool leavingEnhance = c.crystals + c.green > 0 &&
                        (c.zone == Zone::Enhance || (c.zone == Zone::Special && c.faceUp));
  if (leavingEnhance && z != Zone::Enhance && z != Zone::Special) {
    int sak = 0;
    int cr = c.crystals + c.green;
    take_card_crystals(inst, cr, kTakeLeaving, &sak);
    if (sak > 0) decay_crystals(inst, sak);
  }
  Zone old = c.zone;
  // 18-Mizuki 兵舍: barracks is not backed by a generic zone vector, so it must
  // be cleared explicitly before the card changes zone.
  leave_barracks(c.holder, inst);
  if (auto* v = zone_ptr(st, c.holder, old))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  // A borrowed card (诡辩/引用) is on the user's side while in use, but returns
  // to its true owner as soon as it leaves the field (Limbo keeps the holder).
  if (z != Zone::Enhance && z != Zone::Special && z != Zone::Limbo) c.holder = c.owner;
  c.zone = z;
  if (auto* v = zone_ptr(st, c.holder, z)) v->push_back(inst);
  if (z == Zone::Discard && old != Zone::Discard)
    fire("discarded", c.owner, nullptr, inst, false);  // 提婆
  // 22-Renri: 「此牌移出游戏时」的触发（刀刃的本质 / 最初的樱花）。
  if (z == Zone::Removed && old != Zone::Removed)
    fire("removed_from_game", c.owner, nullptr, inst, false);
}

void Engine::move_card_top(int inst) {
  move_card(inst, Zone::Deck);  // deck top == back of the vector
}

void Engine::move_card_bottom(int inst) {
  CardInstance& c = ci(inst);
  leave_barracks(c.holder, inst);  // 18-Mizuki 兵舍
  if (auto* v = zone_ptr(st, c.holder, c.zone))
    v->erase(std::remove(v->begin(), v->end(), inst), v->end());
  c.holder = c.owner;  // leaving the field sends a borrowed card home
  c.zone = Zone::Deck;
  ps(c.holder).deck.insert(ps(c.holder).deck.begin(), inst);  // bottom == front
}

int Engine::amount(AreaRef a) const {
  // 统一 Token 入口：樱花计数走 token_amount（土壤/蒸汽区无樱花，返回 0）。
  return token_amount(a, Token::Sakura);
}

void Engine::add_crystals(AreaRef a, int n) {
  // 统一 Token 入口（what.md 第 1 条）：无对端增减走 token_adjust。
  token_adjust(a, Token::Sakura, n);
}

int Engine::move_crystals(AreaRef from, AreaRef to, int n, bool cardEffect) {
  // 统一 Token 入口（what.md 第 1 条）：全部移动策略集中在 token_move。
  MoveReq m;
  m.from = from;
  m.to = to;
  m.n = n;
  m.cardEffect = cardEffect;
  return token_move(m);
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
  // 24-Shisui 费用 {X} = 向自气放置 X 个裂伤指示物（不消耗气/虚，不可被费用
  // 修正改变），因此对「气是否足够」而言费用恒为 0。
  if (def(defId).woundCost >= 0) return 0;
  // 23-Akina 费用 = 当前股价，不能被任何其他方式改变（短路掉全部费用修正）。
  if (def(defId).stockCost) return ps(p).stockPrice;
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
  if (cut_name_banned(p, inst)) return false;  // 20-Kanawe 封杀
  if (!limit_distance_ok(d)) return false;     // 「限制距离X-Y」= 打出时距离必须满足
  if (effects_->has_hook(d.id, "playable")) return effects_->eval_pred(*this, d.id, "playable", p, inst);
  return true;
}

// 「限制距离X-Y」= 打出这张牌时当前距必须 ∈ [X,Y]（类似攻击牌的攻击距离限制）。
bool Engine::limit_distance_ok(const CardDef& d) const {
  if (d.limitDistanceLo < 0) return true;
  const int dist = distance();
  return dist >= d.limitDistanceLo && dist <= d.limitDistanceHi;
}

bool Engine::respondable_card(Player p, int inst) {
  const CardDef& d = def_of(inst);
  if (cut_name_banned(p, inst)) return false;  // 20-Kanawe 封杀（使用 = 含对应）
  if (!limit_distance_ok(d)) return false;     // 对应也是「打出」，同样受限制距离约束
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
  MoveReq m;
  m.from = AreaRef::external();
  m.to = a;
  m.n = n;
  move_from_external(m);
}

int Engine::gain_extra(Player p, const std::string& name) {
  for (const CardDef& d : defs)
    if (d.isExtra && d.name == name) {
      int inst = add_instance(d.id, p);
      move_card(inst, Zone::Special);
      ci(inst).faceUp = false;
      // A 追加牌 leaves the 追加牌区 here: 四季轮回 etc. may react.
      fire("extra_gained", p, nullptr, inst, false);
      return inst;
    }
  return -1;
}

void Engine::give_cower(Player p) { ps(p).cower = true; }

// 22-Renri: 焦躁一次（抽牌失败 / 恐吓 等）。道化的觉悟 展开中时对手受到的
// 焦躁伤害变为 2/1。
void Engine::impatience(Player target) {
  int aura = 1;
  Player src = opp(target);
  auto boost = [&](int inst) {
    if (!def_of(inst).enemyImpatienceUp) return;
    if (def_of(inst).nagi >= 0 && card_crystal_count(inst) <= 0) return;  // 需要展开中
    aura = 2;
  };
  for (int inst : ps(src).enhance) boost(inst);
  for (int inst : ps(src).special)
    if (ci(inst).faceUp) boost(inst);
  deal_damage(target, aura, 1, AF_Unrespondable);
}

void Engine::gain_vigor(Player p, int n) {
  PlayerState& s = ps(p);
  if (s.cower) {
    s.cower = false;  // 畏缩: skip this gain
    return;
  }
  s.vigor = std::min(2, s.vigor + n);
}

void Engine::draw(Player p, int n) {
  // 此目所及之物与世: 可以不抽牌，改为从回忆区取等量的牌加入手牌。
  if (n > 0 && has_memory_draw(p) && memory_size(p) > 0) {
    Request r;
    r.kind = "option";
    r.prompt = "此目所及之物与世：从回忆区取牌代替抽牌？";
    r.options.push_back({"抽牌", true, {}});
    r.options.push_back({"从回忆区取牌", true, {}});
    if (ask_one(p, std::move(r)) == 1) n -= memory_draw(p, n);
  }
  for (int i = 0; i < n; ++i) {
    if (st.over) return;
    PlayerState& s = ps(p);
    if (s.deck.empty()) {
      // failed draw -> 焦躁 1/1, no source, unrespondable (道化的觉悟 -> 2/1)
      impatience(p);
    } else {
      int inst = s.deck.back();
      s.deck.pop_back();
      ci(inst).zone = Zone::Hand;
      ci(inst).faceUp = true;
      s.hand.push_back(inst);
    }
  }
}

// ---------------------------------------------------------------------------
// 22-Renri 夜山恋离: 伪证 / 铭镌之衣 / 洛阳铲 的支持接口
// ---------------------------------------------------------------------------

// 可以声称的伪证牌: 本格 5 张（O 形态）；A1 追加 3 张史前遗物。
std::vector<int> Engine::bluff_claim_defs(Player p) const {
  bool renri = false, a1 = false;
  for (const std::string& s : playerSets_[p]) {
    std::string g = s, f = "O";
    auto dot = s.find('.');
    if (dot != std::string::npos) {
      g = s.substr(0, dot);
      f = s.substr(dot + 1);
    }
    if (g != "renri") continue;
    renri = true;
    if (f == "A1") a1 = true;
  }
  std::vector<int> out;
  if (!renri) return out;
  for (const CardDef& d : defs) {
    if (!d.bluff || d.goddess != "renri") continue;
    if (d.form == "O" || (a1 && d.form == "A1")) out.push_back(d.id);
  }
  return out;
}

bool Engine::bluff_is_real() const {
  if (!bluffActive_ || bluffInst_ < 0 || bluffClaimDef_ < 0) return false;
  return def_of(bluffInst_).name == def(bluffClaimDef_).name;
}

std::string Engine::bluff_claim_name() const {
  if (bluffClaimDef_ < 0) return std::string();
  return def(bluffClaimDef_).name;
}

// 让正在结算的实例按另一张牌的名字结算（铭镌之衣的复制 / 洛阳铲的声称）。
bool Engine::resolve_as(int inst, const std::string& name) {
  if (inst < 0) return false;
  // 结算中的普通牌在 Limbo；切札仍在切牌区（使用后状态）。
  const Zone z = ci(inst).zone;
  if (z != Zone::Limbo && z != Zone::Special && z != Zone::Enhance) return false;
  for (const CardDef& d : defs)
    if (d.name == name) {
      pendingResolveAs_ = d.id;
      return true;
    }
  return false;
}

int Engine::def_cost_by_name(const std::string& name) const {
  for (const CardDef& d : defs)
    if (d.name == name) return d.cost;
  return -1;
}

// 对手「眼前构筑阶段能够选择的牌」= 对手实际选取的 (女神, 形态) 的构筑池中
// 的常规非付与牌（洛阳铲的声称范围）。
std::vector<std::string> Engine::opponent_build_normals(Player p) const {
  std::vector<std::string> out;
  for (const std::string& s : playerSets_[opp(p)]) {
    std::string g = s, f = "O";
    auto dot = s.find('.');
    if (dot != std::string::npos) {
      g = s.substr(0, dot);
      f = s.substr(dot + 1);
    }
    for (int id : deck_def_ids(g, f)) {
      const CardDef& d = defs[static_cast<size_t>(id)];
      if (d.kind != CardKind::Normal || d.type == CardType::Enhance) continue;
      if (std::find(out.begin(), out.end(), d.name) == out.end()) out.push_back(d.name);
    }
  }
  return out;
}

bool Engine::opponent_shows_name(Player p, const std::string& name) const {
  Player o = opp(p);
  auto holds = [&](const std::vector<int>& v) {
    for (int i : v)
      if (def_of(i).name == name) return true;
    return false;
  };
  return holds(ps(o).hand) || holds(ps(o).cover) || holds(ps(o).discard);
}

void Engine::init_start_used(Player p) {
  for (int inst : ps(p).special)
    if (def_of(inst).startUsed) ci(inst).faceUp = true;  // 使用后状态（0 献）
}

void Engine::rebuild(Player p, bool costLife) {
  if (costLife) {
    // 此目所及之物与世: 可以改为从回忆区永久移除一张牌来抵消这次洗牌伤害。
    if (has_memory_shield(p) && memory_size(p) > 0) {
      Request r;
      r.kind = "option";
      r.prompt = "此目所及之物与世：抵消这次重铸命伤？";
      r.options.push_back({"受到1命伤", true, {}});
      r.options.push_back({"从回忆区永久移除一张牌", true, {}});
      if (ask_one(p, std::move(r)) == 1) {
        int inst = ps(p).memory.back();
        move_card(inst, Zone::Removed);
        costLife = false;
      }
    }
    if (costLife) {
      lose_life(p, 1, false);  // rebuild life loss does not trigger 破绽
      if (st.over) return;
    }
  }
  PlayerState& s = ps(p);
  std::vector<int> all;
  all.insert(all.end(), s.deck.begin(), s.deck.end());
  // 26-Innealra O1-S3 栖身·垂暮: 对手下一次重铸牌库时，他弃牌堆中所有牌不因重铸
  // 而移动；重铸后把这张牌移出游戏。
  const bool freezeDiscard = s.nextRebuildFreeze;
  if (!freezeDiscard) all.insert(all.end(), s.discard.begin(), s.discard.end());
  all.insert(all.end(), s.cover.begin(), s.cover.end());
  s.deck = std::move(all);
  if (!freezeDiscard) s.discard.clear();
  s.cover.clear();
  for (int inst : s.deck) {
    ci(inst).zone = Zone::Deck;
    ci(inst).faceUp = true;
  }
  st.rng.shuffle(s.deck);
  float_poisons(p);  // poison cards rise to the top after a shuffle
  rebuiltThisTurn_[p] = true;
  if (freezeDiscard) {
    s.nextRebuildFreeze = false;
    // 使用后：对手重铸牌库后，将此牌移出游戏（找对手场上「栖身·垂暮」的实例）。
    for (int inst : ps(opp(p)).special) {
      if (ci(inst).faceUp && def_of(inst).rebuildFreeze) {
        remove_from_game(inst);
        break;
      }
    }
  }
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
  // 23-Akina: 投资在重铸流程开始时（洗牌之前）结算。
  if (has_akina(p) && !st.over && invest_available(p)) {
    if (ask_yes_no(p, "重铸开始时：投资？")) invest(p);
    if (st.over) return;
  }
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
  // 22-Renri A1-EX-N1 谎言的武器: 重铸牌库时，可以宣称盖牌区的一张背面牌是
  // 「谎言的武器」，如「设置」一样打出。该宣称视作一次伪证（对手无从质疑），
  // 结算完毕这张牌必然进入牌山（toDeckAfter），随后正常重组牌库。
  {
    int claimDef = -1;
    if (!ps(p).cover.empty()) {
      for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
        const CardInstance& c = st.insts[static_cast<size_t>(i)];
        if (c.owner != p || c.zone == Zone::Removed) continue;
        if (def_of(i).rebuildClaim) {
          claimDef = c.def;
          break;
        }
      }
    }
    if (claimDef >= 0) {
      const std::vector<int> faces = ps(p).cover;
      Request cr;
      cr.kind = "option";
      cr.prompt = "谎言的武器：宣称盖牌区一张背面牌是「谎言的武器」并如设置一样打出？";
      cr.options.push_back({"不宣称", true, {}});
      for (int inst : faces) cr.options.push_back({card_label(def_of(inst)), true, {{"inst", inst}}});
      const int sc = ask_one(p, std::move(cr));
      if (sc >= 1 && sc <= static_cast<int>(faces.size())) {
        const int chosen = faces[static_cast<size_t>(sc - 1)];
        const bool sa = bluffActive_, sn = bluffNotDoubted_, sf = bluffDoubtFailed_;
        const int si = bluffInst_, sd = bluffClaimDef_, spr = pendingResolveAs_;
        bluffActive_ = true;
        bluffInst_ = chosen;
        bluffClaimDef_ = claimDef;
        bluffNotDoubted_ = true;
        bluffDoubtFailed_ = false;
        pendingResolveAs_ = claimDef;
        play_from_cover(p, chosen, false, true);  // 结算后进牌山
        if (!st.over) fire("bluff_undoubted", p, nullptr, chosen, false);
        bluffActive_ = sa;
        bluffInst_ = si;
        bluffClaimDef_ = sd;
        bluffNotDoubted_ = sn;
        bluffDoubtFailed_ = sf;
        pendingResolveAs_ = spr;
        if (st.over) return;
      }
    }
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
  if (inst >= 0 && inst < static_cast<int>(st.insts.size())) {
    Player h = ci(inst).holder;
    if (std::find(ps(h).barracks.begin(), ps(h).barracks.end(), inst) != ps(h).barracks.end())
      return "barracks";  // 18-Mizuki 兵舍
  }
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
    case Zone::Memory: return "memory";
    case Zone::Removed: return "removed";
    case Zone::Limbo: return "limbo";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// 18-Mizuki: 动员 / 兵舍 / 阵地 / 词条改写
// ---------------------------------------------------------------------------

bool Engine::is_soldier(int inst) const {
  if (inst < 0 || inst >= static_cast<int>(st.insts.size())) return false;
  if (ci(inst).soldier) return true;
  const auto& bq = ps(ci(inst).holder).barracks;
  return std::find(bq.begin(), bq.end(), inst) != bq.end();
}

int Engine::barracks_mobilized_count(Player p) const {
  int c = 0;
  for (int inst : ps(p).barracks)
    if (ci(inst).faceUp) c += 1;
  return c;
}

void Engine::leave_barracks(Player p, int inst) {
  auto& bq = ps(p).barracks;
  bq.erase(std::remove(bq.begin(), bq.end(), inst), bq.end());
}

void Engine::to_barracks(Player p, int inst, bool mobilized) {
  CardInstance& c = ci(inst);
  leave_barracks(p, inst);
  c.holder = p;
  c.zone = Zone::Limbo;  // 兵舍不是通用区域：不列入任何 zone 向量（见 zone_ptr）
  c.faceUp = mobilized;
  ps(p).barracks.push_back(inst);
}

int Engine::mobilize(Player p) {
  std::vector<int> cand;
  for (int inst : ps(p).barracks)
    if (!ci(inst).faceUp) cand.push_back(inst);
  if (cand.empty()) return -1;  // 兵舍里没有未动员的士兵则无事发生
  int pick = cand[0];
  if (cand.size() > 1) {
    Request r;
    r.kind = "option";
    r.prompt = "动员：选择一张士兵翻到正面";
    for (int inst : cand) r.options.push_back({def_of(inst).name, true, {}});
    int idx = ask_one(p, std::move(r));
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(cand.size())) idx = static_cast<int>(cand.size()) - 1;
    pick = cand[static_cast<size_t>(idx)];
  }
  ci(pick).faceUp = true;
  return pick;
}

int Engine::gain_soldier(Player p, const std::string& name) {
  for (const CardDef& d : defs)
    if (d.isExtra && d.name == name) {
      int inst = add_instance(d.id, p);
      ci(inst).soldier = true;
      to_barracks(p, inst, true);  // 以已动员状态加入兵舍
      return inst;
    }
  return -1;
}

void Engine::hand_to_barracks(Player p, int inst) {
  auto& h = ps(p).hand;
  if (std::find(h.begin(), h.end(), inst) == h.end()) return;
  h.erase(std::remove(h.begin(), h.end(), inst), h.end());
  ci(inst).soldier = true;  // 这张手牌此后也视为你的士兵
  to_barracks(p, inst, true);
}

void Engine::note_distance_changed() {
  ps(P0).distChanged = true;
  ps(P1).distChanged = true;
}

bool Engine::position(Player p) {
  // 有效距离（含光环修正）变化即算；达人距离的变动不算。
  const int d = distance();
  if (d != distanceBaseline_) {
    distanceBaseline_ = d;
    note_distance_changed();
  }
  return !ps(p).distChanged;
}

bool Engine::terminal_rewrite_active(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).terminalRewrite) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && enhance_active(inst) && def_of(inst).terminalRewrite) return true;
  return false;
}

bool Engine::has_terminal(int inst) const {
  const CardDef& d = def_of(inst);
  if (terminal_rewrite_active(ci(inst).holder)) {
    // O-S4: 原本具有终端的牌失去终端；全力牌失去全力并获得终端。
    if (d.flags & CF_FullPower) return true;
    return false;
  }
  return (d.flags & CF_Terminal) != 0;
}

bool Engine::has_full_power(int inst) const {
  const CardDef& d = def_of(inst);
  if ((d.flags & CF_FullPower) && terminal_rewrite_active(ci(inst).holder)) return false;
  return (d.flags & CF_FullPower) != 0;
}

bool Engine::first_response_played() const {
  return !responseOrdStack_.empty() && responseOrdStack_.back() == 1;
}

void Engine::start_phase(Player p) {
  // 24-Shisui 桑畑志水: 志水的准备阶段开始时，把双方场上所有裂伤指示物以任意
  // 顺序伤害化（同一「目标 + 区域 + 来源」合并为 1 次伤害）。
  if (has_shisui(p)) {
    resolve_all_wounds(p);
    if (st.over) return;
  }
  // 神居: 你的回合开始时，命 5-9 → 被诅咒 1 次；命 < 5 → 被诅咒 2 次。
  if (ps(p).hasCurse && ps(p).life >= 5 && ps(p).life <= 9)
    add_curse(p, 1);
  else if (ps(p).hasCurse && ps(p).life < 5)
    add_curse(p, 2);
  if (st.over) return;
  gain_vigor(p, 1);
  std::vector<int> list;
  for (int inst : ps(p).enhance) list.push_back(inst);
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).nagi >= 0) list.push_back(inst);
  // 同一位付与牌的 -1 视为同一时刻触发：由当前玩家决定顺序。
  std::vector<int> skipped;
  while (!st.over) {
    std::vector<int> cand;
    for (int inst : list)
      if (std::find(skipped.begin(), skipped.end(), inst) == skipped.end() &&
          card_crystal_count(inst) > 0 &&
          (ci(inst).zone == Zone::Enhance ||
           (ci(inst).zone == Zone::Special && ci(inst).faceUp)))
        cand.push_back(inst);
    if (cand.empty()) break;
    int chosen = cand[0];
    if (cand.size() > 1) {
      Request r;
      r.kind = "option";
      r.prompt = "准备阶段：付与牌的樱花结晶 -1（选择结算顺序）";
      for (int inst : cand) r.options.push_back({def_of(inst).name, true, {}});
      int idx = ask_one(p, std::move(r));
      if (idx >= 0 && idx < static_cast<int>(cand.size())) chosen = cand[static_cast<size_t>(idx)];
    }
    // 寒冰荆棘: 对手被冻结时，可以选择不移除本牌上的樱花结晶。
    if (def_of(chosen).maySkipCrystalLoss && frozen(opp(p))) {
      Request r;
      r.kind = "option";
      r.prompt = "寒冰荆棘：对手被冻结，是否不移除本牌上的樱花结晶？";
      r.options.push_back({"不移除", true, {}});
      r.options.push_back({"移除", true, {}});
      if (ask_one(p, std::move(r)) == 0) {
        skipped.push_back(chosen);
        continue;
      }
    }
    // 弄潮: 当且仅当你的回合内且你处于顺风状态，才可以移除这张牌上的樱花结晶。
    if (def_of(chosen).keepCrystalsUnlessTailwind && !ps(p).tailwind) {
      skipped.push_back(chosen);
      continue;
    }
    consume_enhance_crystal(chosen);
    // 每张付与牌每回合只 -1 一次（循环只用于让当前玩家选择结算顺序）。
    skipped.push_back(chosen);
  }
  if (st.over) return;
  run_rebuild(p);
  if (st.over) return;
  draw(p, ps(p).nextDrawOne ? 1 : 2);  // 夜叉: 下个回合开始时只抽一张
  ps(p).nextDrawOne = false;
  // 26-Innealra 诺伦: 除游戏的第一个回合外，回合开始时抽牌后，若手牌张数等于 3，
  // 进行一次共鸣（执行当前寄宿的枪对应时间点的命运，然后轮转命运槽）。
  if (!st.over && has_innealra(p) && st.turn > 1 && ps(p).hand.size() == 3)
    resonance(p, /*fromTurnStart=*/true);
}

int Engine::absorb_aura_host(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).absorbAuraBasic) return inst;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).absorbAuraBasic) return inst;
  return -1;
}

bool Engine::basic_legal(Player p, BasicAction a) const {
  if (ps(p).cannotBasic) return false;  // 二重奏·吹弹阳明: 本回合不能执行基本动作
  if (transform_is(p, cards::kYasha) && ps(p).steamEngine == 0) return false;  // 夜叉: 引擎空不能基本动作
  // 25-Misora 蔽目重云: 展开中你不能前进或离脱。
  if (a == BasicAction::Advance || a == BasicAction::Escape) {
    for (int inst : ps(p).enhance)
      if (enhance_active(inst) && def_of(inst).noAdvanceEscape) return false;
    for (int inst : ps(p).special)
      if (enhance_active(inst) && def_of(inst).noAdvanceEscape) return false;
  }
  switch (a) {
    case BasicAction::Advance:
      return !ps(p).cannotAdvance && distance() > near_distance() && distance() >= 1 &&
             aura_free(p) > 0;  // 冰晶占位
    case BasicAction::Retreat:
      return !ps(p).cannotRetreat && !has_named_active(opp(p), cards::kMud) &&
             ps(p).aura >= 1;
    case BasicAction::Aura: {
      if (st.dust < 1) return false;
      if (aura_free(p) > 0) return true;
      // 双掌生花: 自装满时仍可装附（结晶改为放到该牌上），但打出时的那次不替换。
      return absorb_aura_host(p) >= 0 && !ps(p).suppressAuraRedirect;
    }
    case BasicAction::Flare:
      return !has_enemy_no_flare(p) && (frozen(p) || ps(p).aura >= 1);
    case BasicAction::Escape:
      return !has_named_active(opp(p), cards::kMud) && distance() <= near_distance() && st.dust >= 1;
  }
  return false;
}

bool Engine::do_basic(Player p, BasicAction a) {
  if (!basic_legal(p, a)) return false;
  // 26-Innealra 脆弱意志: 需要区分「基本动作装附」与其它手段的装获得。
  const bool prevBasic = inBasicAction_;
  const Player prevActor = basicActor_;
  inBasicAction_ = true;
  basicActor_ = p;
  switch (a) {
    case BasicAction::Advance: move_crystals(AreaRef::distance(), AreaRef::aura(p), 1, false); break;
    case BasicAction::Retreat: move_crystals(AreaRef::aura(p), AreaRef::distance(), 1, false); break;
    case BasicAction::Aura: {
      int host = absorb_aura_host(p);
      bool redirect = false;
      if (host >= 0 && !ps(p).suppressAuraRedirect && st.dust >= 1) {
        Request r;
        r.kind = "option";
        r.prompt = "装附：结晶移到自装，还是移到「" + def_of(host).name + "」上？";
        r.options.push_back({"装到自装", true, {}});
        r.options.push_back({"装到" + def_of(host).name, true, {}});
        redirect = ask_one(p, std::move(r)) == 1;
      }
      if (redirect)
        move_crystals(AreaRef::dust(), AreaRef::card(host), 1, false);
      else
        move_crystals(AreaRef::dust(), AreaRef::aura(p), 1, false);
      ps(p).suppressAuraRedirect = false;
      fire("basic_aura", p, nullptr, -1, false);  // 双掌生花: 检查是否恰好 5
      break;
    }
    case BasicAction::Flare:
      if (frozen(p))
        ps(p).ice -= 1;  // 被冻结时，聚气改为移除 1 个冰晶
      else
        move_crystals(AreaRef::aura(p), AreaRef::flare(p), 1, false);
      break;
    case BasicAction::Escape:  move_crystals(AreaRef::dust(), AreaRef::distance(), 1, false); break;
  }
  // Rule 19/94: a basic action performed by a card effect still counts as one.
  didBasicThisTurn_[p] = true;
  inBasicAction_ = prevBasic;
  basicActor_ = prevActor;
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
  // 19-Megumi: 绿色结晶视作樱花结晶，同样维持「展开中」。
  if (c.zone == Zone::Enhance) return c.crystals + c.green > 0;
  // A 切札付与 is "展开中" only while it still holds 献; once they run out it
  // stays in the special zone in its used state but its aura text stops.
  if (c.zone == Zone::Special && c.faceUp && d.nagi >= 0) return c.crystals + c.green > 0;
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
  // 25-Misora 观空穹仪: 不能被其它牌的效果再次发动（含神座渡/再起式的重置）。
  if (inst < 0 || def_of(inst).noReuse) return;
  if (ci(inst).zone == Zone::Special && ci(inst).faceUp) {
    // A special that is currently an expanded enhancement keeps its 献; when it
    // is turned back to unused, those crystals must leave play (to dust/decay).
    if (card_crystal_count(inst) > 0 && !def_of(inst).keepCrystalsOnReset) {
      int sak = 0;
      int c = card_crystal_count(inst);
      take_card_crystals(inst, c, kTakeNormal, &sak);
      if (sak > 0) decay_crystals(inst, sak);
    }
    ci(inst).faceUp = false;
    // A borrowed 切札付与 returns to its owner when it stops being expanded.
    if (ci(inst).holder != ci(inst).owner) {
      move_card(inst, Zone::Limbo);
      ci(inst).holder = ci(inst).owner;
      move_card(inst, Zone::Special);
    }
    clamp_aura(ci(inst).holder);
    fire("special_reset", ci(inst).holder, nullptr, inst, false);  // 魔能吸收
  }
}

int Engine::aura_free(Player p) const {
  return std::max(0, max_aura(p) - ps(p).aura - ps(p).ice);
}

void Engine::fire_armor_full_if_new(Player p, int cause) {
  // 装从"有空位"变成"满"的瞬间（吹雪式的即再起）；cause = 造成变化的牌实例或 -1。
  if (armor_full(p)) fire("armor_full", p, nullptr, cause, false);
}

int Engine::freeze(Player p, int n, int cause) {
  MoveReq m;
  m.from = AreaRef::external();
  m.to = AreaRef::aura(p);
  m.fromKind = m.toKind = Token::Ice;
  m.n = n;
  m.cause = cause;
  return token_move(m);
}

int Engine::mirror(Player p) const {
  const PlayerState& a = ps(p);
  const PlayerState& b = ps(opp(p));
  int n = 0;
  if (a.aura == b.aura) n += 1;
  if (a.flare == b.flare) n += 1;
  if (a.life == b.life) n += 1;
  return n;
}

void Engine::to_memory(int inst) {
  // 扣置入回忆区：不结算弃置时效果，其上的樱花结晶移到虚。
  if (card_crystal_count(inst) > 0) {
    int sak = 0;
    int n = card_crystal_count(inst);
    take_card_crystals(inst, n, kTakeNormal, &sak);
    if (sak > 0) decay_crystals(inst, sak);
  }
  move_card(inst, Zone::Memory);
  ci(inst).faceUp = false;
}

int Engine::memory_size(Player p) const { return static_cast<int>(ps(p).memory.size()); }

bool Engine::has_memory_draw(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).memoryDraw) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).memoryDraw) return true;
  return false;
}

bool Engine::has_memory_shield(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).memoryRebuildShield) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).memoryRebuildShield) return true;
  return false;
}

int Engine::memory_draw(Player p, int n) {
  int got = 0;
  for (int i = 0; i < n; ++i) {
    if (ps(p).memory.empty()) break;
    int inst = ps(p).memory.back();
    move_card(inst, Zone::Hand);
    ci(inst).faceUp = true;
    got += 1;
    // 使用后：八叶的牌从回忆区加入手牌时，可以选择将其升级。
    if (has_memory_draw(p) && can_upgrade(inst)) {
      Request r;
      r.kind = "option";
      r.prompt = "此目所及之物与世：将「" + def_of(inst).name + "」升级为完全态？";
      r.options.push_back({"升级", true, {}});
      r.options.push_back({"不升级", true, {}});
      if (ask_one(p, std::move(r)) == 0) upgrade_card(inst);
    }
  }
  return got;
}

void Engine::all_normals_to_memory(Player p, int except) {
  std::vector<int> all;
  auto add = [&](const std::vector<int>& v) { all.insert(all.end(), v.begin(), v.end()); };
  add(ps(p).deck);
  add(ps(p).hand);
  add(ps(p).discard);
  add(ps(p).cover);
  add(ps(p).enhance);
  for (int inst : all) {
    if (def_of(inst).kind != CardKind::Normal) continue;
    if (inst == except) continue;  // 旅途: 保留至多 1 张手牌
    if (ci(inst).zone == Zone::Removed || ci(inst).zone == Zone::Memory) continue;
    to_memory(inst);  // 不结算弃置时效果
  }
}

int Engine::count_complete(Player p) const {
  int n = 0;
  for (const CardInstance& c : st.insts)
    if (c.holder == p && def_of(c.inst).complete && c.zone != Zone::Removed) n += 1;
  return n;
}

int Engine::lose_external(AreaRef a, int n) {
  MoveReq m;
  m.from = a;
  m.to = AreaRef::external();
  m.n = n;
  return move_to_external(m);
}

bool Engine::reverse_moves_active(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).reverseMoves) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).reverseMoves) return true;
  return false;
}

bool Engine::can_upgrade(int inst) const {
  if (inst < 0) return false;
  const CardDef& d = def_of(inst);
  if (d.upgrade.empty()) return false;
  for (const CardDef& t : defs)
    if (t.name == d.upgrade && t.goddess == d.goddess) return true;
  return false;
}

bool Engine::upgrade_card(int inst) {
  if (!can_upgrade(inst)) return false;
  const CardDef& d = def_of(inst);
  for (const CardDef& t : defs)
    if (t.name == d.upgrade && t.goddess == d.goddess) {
      ci(inst).def = t.id;
      fire("upgraded", ci(inst).holder, nullptr, inst, false);
      return true;
    }
  return false;
}

bool Engine::has_enemy_no_flare(Player p) const {
  // p 的对手场上有 enemy_no_flare 的牌 → p 不能聚气（冻僵）。
  Player o = opp(p);
  for (int inst : ps(o).enhance)
    if (def_of(inst).enemyNoFlare) return true;
  for (int inst : ps(o).special)
    if (ci(inst).faceUp && def_of(inst).enemyNoFlare) return true;
  return false;
}

int Engine::nagi_value(int defId, Player p, int inst) {
  const CardDef& d = def(defId);
  if (d.dynamicNagi) return std::max(0, effects_->eval_nagi(*this, defId, p, inst));
  return d.nagi;
}

void Engine::add_curse(Player p, int n) {
  if (n <= 0 || st.over) return;
  int before = ps(p).curse;
  ps(p).curse += n;
  fire("cursed", p, nullptr, -1, false);  // 尸: 即再起（诅咒变为 6 / 12）
  if (ps(p).curse >= 16) {  // 诅咒 >= 16 即死亡（另一个死亡条件是命 == 0）
    if (ps(p).life > 0) damage_life(p, ps(p).life, AreaKind::Flare, true);  // 保持结晶守恒
    check_win();
  }
  (void)before;
}

bool Engine::protects_enemy(Player p) const {
  // p 的对手有 protects_enemy 的牌 → p 不会死亡（21 阡）。
  Player o = opp(p);
  for (int inst : ps(o).enhance)
    if (def_of(inst).protectsEnemy) return true;
  for (int inst : ps(o).special)
    if (enhance_active(inst) && def_of(inst).protectsEnemy) return true;
  return false;
}

int Engine::deny_aura_host(Player p) const {
  Player o = opp(p);
  for (int inst : ps(o).enhance)
    if (def_of(inst).denyEnemyAura) return inst;
  for (int inst : ps(o).special)
    if (enhance_active(inst) && def_of(inst).denyEnemyAura) return inst;
  return -1;
}

void Engine::clamp_aura(Player p) {
  int over = ps(p).aura + ps(p).ice - max_aura(p);
  if (over > 0) {
    // 自装中多于上限的部分移到虚（静默：与旧实现一致，不触发装变化通知）。
    token_adjust(AreaRef::aura(p), Token::Sakura, -over);
    token_adjust(AreaRef::dust(), Token::Sakura, over);
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
  // An optional `cond` gates the reset (22-Renri 铭镌之衣: only while it copies 夙愿).
  // The reset belongs to the event's *subject* ("你的主要阶段开始时" / "当你重铸牌库时"),
  // so the opponent's card must not reset (regression: 夙愿 曾在对面的主要阶段开始时重置).
  for (int i = 0; i < 2; ++i) {
    Player p = static_cast<Player>(i);
    if (p != subject) continue;
    std::vector<int> sp = ps(p).special;
    for (int inst : sp) {
      if (!ci(inst).faceUp) continue;
      ResetInfo ri = effects_->reset_info(def_of(inst).id);
      if (ri.kind != 2 || ri.trigger.empty()) continue;
      if (ri.trigger != event) continue;
      if (ri.hasCond && !effects_->eval_reset_cond(*this, def_of(inst).id, p, inst)) continue;
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
  // 19-Megumi 假想树: 主要阶段开始时脱落 1 个种结晶（从下往上掉回土壤）。
  if (ps(p).treeActive) tree_fall(p, 1);
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
      bool fromBarracks = false;
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
    bool canPay = effective_vigor(p) >= 1 || canCover;

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
          if (!payCover && effective_vigor(p) < 1) continue;
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

    // 尸: 本回合对手的下一次攻击要额外弃一张该女神的牌；没有可弃的牌就不能攻击。
    std::string extraGoddess = ps(p).extraAttackCostGoddess;
    bool extraPayable = false;
    if (!extraGoddess.empty()) {
      for (int h : ps(p).hand)
        if (def_of(h).goddess == extraGoddess) extraPayable = true;
      for (int h : ps(p).special)
        if (!ci(h).faceUp && def_of(h).goddess == extraGoddess) extraPayable = true;
    }
    for (int inst : ps(p).hand) {
      const CardDef& d = def_of(inst);
      if (d.kind != CardKind::Normal) continue;
      if (ps(p).cannotUseNormals) continue;  // 26-Innealra 修省（纠葛）
      if (d.responseOnly) continue;  // 格杀: 仅限对应打出
      if (d.type == CardType::Attack && !extraGoddess.empty() && !extraPayable) continue;
      bool fp = has_full_power(inst);
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (!playable_card(p, inst)) continue;
      if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!attack_range_ok(tmp)) continue;  // 25-Misora 追踪
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = has_terminal(inst);
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
      if (d.kind != CardKind::Special) continue;  // normal EX cards live elsewhere
      if (ps(p).flare < cut_cost(p, d.id, inst)) continue;
      if (!playable_card(p, inst)) continue;
      bool fp = has_full_power(inst);
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!attack_range_ok(tmp)) continue;  // 25-Misora 追踪
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = has_terminal(inst);
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
      bool fp = has_full_power(inst);
      if (fp && mainDirty_) continue;
      if (d.centrifugal && !centrifugal_ok(p)) continue;
      if (!playable_card(p, inst)) continue;
      if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
      if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
        continue;
      if (d.type == CardType::Attack) {
        Attack tmp = make_attack(p, inst, false, false);
        if (!attack_range_ok(tmp)) continue;  // 25-Misora 追踪
      }
      Move m;
      m.k = Move::Play;
      m.inst = inst;
      m.fullPower = fp;
      m.terminal = has_terminal(inst);
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

    {  // 18-Mizuki: 已动员的士兵「视为你的手牌可以打出」
      for (int inst : ps(p).barracks) {
        if (!ci(inst).faceUp) continue;  // 未动员
        const CardDef& d = def_of(inst);
        if (d.kind != CardKind::Normal) continue;
        if (ps(p).cannotUseNormals) continue;  // 26-Innealra 修省（纠葛）
        if (d.responseOnly) continue;
        if (d.type == CardType::Attack && !extraGoddess.empty() && !extraPayable) continue;
        bool fp = has_full_power(inst);
        if (fp && mainDirty_) continue;
        if (d.centrifugal && !centrifugal_ok(p)) continue;
        if (!playable_card(p, inst)) continue;
        if (d.burnRequire > 0 && !can_burn(p, d.burnRequire)) continue;
        if (d.type == CardType::Attack && (attack_card_forbidden(p) || !can_attack(p)))
          continue;
        if (d.type == CardType::Attack) {
          Attack tmp = make_attack(p, inst, false, false);
          if (!attack_range_ok(tmp)) continue;  // 25-Misora 追踪
        }
        Move m;
        m.k = Move::Play;
        m.inst = inst;
        m.fullPower = fp;
        m.terminal = has_terminal(inst);
        m.fromBarracks = true;
        moves.push_back(m);
        Option o;
        o.label = "barracks: " + card_label(d);
        o.data = card_json(d);
        o.data["inst"] = inst;
        o.data["kind"] = "play";
        o.data["from_barracks"] = true;
        o.data["full_power"] = fp;
        o.data["terminal"] = m.terminal;
        r.options.push_back(o);
      }
    }

    {  // 变形/快速改装: 追加基本动作
      bool canPayExtra = effective_vigor(p) >= 1 || canCover;
      for (int td : active_transform_defs(p)) {
        if (!effects_->has(td, "extra_basic")) continue;
        if (def(td).name == cards::kAshura && ashuraExtraUsed_[p]) continue;
        if (!canPayExtra) continue;
        for (int payTmp = 0; payTmp < 2; ++payTmp) {
          bool payCover = payTmp == 1;
          if (payCover && !canCover) continue;
          if (!payCover && effective_vigor(p) < 1) continue;
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
  // 25-Misora 观空: 自己的回合内若进行过攻击，则在主要阶段结束时移除瞄准点
  // （注意是主要阶段结束，而不是回合结束——回合结束仍可重新记录）。
  clear_aim_if_attacked(p);
  abortMain_ = false;
}

void Engine::clear_aim_if_attacked(Player p) {
  if (attackedThisTurn_[p]) ps(p).aim = -1;
}

void Engine::offer_aim_recording(Player p) {
  // 记录当前距为瞄准点（可选）。已等于当前距时视为无操作，不再询问。
  if (ps(p).aim == distance()) return;
  if (ask_yes_no(p, "观空：将当前距记录为瞄准点？")) set_aim(p, distance());
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
  // 20-Kanawe《定位》: 回合结束时的距离变化判定。
  check_dramas(/*atTurnEnd=*/true);
  std::vector<int> specials = ps(p).special;
  for (int inst : specials) {
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    ResetInfo ri = effects_->reset_info(d.id);
    if (ri.kind != 1) continue;
    bool ok = ri.hasCond ? effects_->eval_reset_cond(*this, d.id, p, inst) : false;
    if (ok) reset_special(inst);
  }
  flush_drama_advances();  // 20-Kanawe: 回合结束时把未结算的地图推进结算掉
  // 25-Misora 观空: 双方每个回合结束时都可以（可选）把当前距记录为瞄准点。
  // 只有寄宿观空的玩家拥有瞄准点。
  if (!st.over) {
    for (int oi = 0; oi < 2; ++oi) {
      Player who = (oi == 0) ? p : opp(p);
      if (!has_misora(who)) continue;
      offer_aim_recording(who);
    }
  }
  // 23-Akina: 回合结束且本回合内没有套现时可以投资。
  if (!st.over) akina_end_of_turn(p);
}

void Engine::play_turn(Player p) {
  st.active = p;
  // 航海: 顺风判定。读取“上一回合”对手的攻击计数必须在任何每回合计数重置之前。
  // 第一回合没有上一回合（计数为 0），因此固定顺风；潜水闪避则固定下一回合顺风。
  begin_tailwind(p);
  // 终端's "cannot respond" lasts until the end of the opponent's turn.
  ps(P0).cannotRespond = false;
  ps(P1).cannotRespond = false;
  // 18-Mizuki: 上一回合的对应记录快照（阵地/对应计数的“上回合”判定）。
  for (int i = 0; i < 2; ++i) {
    Player pl = static_cast<Player>(i);
    ps(pl).respondedLastTurn = ps(pl).respondedThisTurn;
    // 20-Kanawe 疾书弗尽: 上一回合是否推进过戏剧。
    ps(pl).dramaProgressedLastTurn = ps(pl).dramaProgressedThisTurn;
  }
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
    // 19-Megumi: 衍生攻击计数与「第一次对敌命/敌装伤害」标记。
    generatedAttacks_[i] = 0;
    enemyLifeDamageFired_[i] = 0;
    enemyAuraDamageFired_[i] = 0;
    auraDamagedThisTurn_[i] = false;
    ps(pl).damageTakenThisTurn = 0;  // 24-Shisui: 本回合受到伤害的次数
    normalNonYukihi_[i] = 0;
    attackedThisTurn_[i] = false;
    playedCentrifugalThisTurn_[i] = false;
    playedLianchengThisTurn_[i] = false;
    ps(pl).cannotAdvance = false;
    ps(pl).tempDistanceMod = 0;      // 影飞翅: only until end of turn
    ps(pl).tempNearDistanceMod = 0;
    didBasicThisTurn_[i] = false;
    rebuiltThisTurn_[i] = false;
    usedFullPowerThisTurn_[i] = false;
    revealOppSpecials_[i] = false;
    // 18-Mizuki: 兵舍/阵地/对应的每回合计数
    ps(pl).distChanged = false;
    ps(pl).respondedThisTurn = false;
    ps(pl).attackCardsPlayedThisTurn = 0;
    ps(pl).normalAttacksThisTurn = 0;
    ps(pl).responsesPlayedThisTurn = 0;
    // 20-Kanawe: 每回合的戏剧计数
    ps(pl).dramaProgressedThisTurn = false;
    ps(pl).noDramaThisTurn = false;
    ps(pl).doubtFailedThisTurn = false;  // 22-Renri: 本回合质疑失败标记
    // 23-Akina: 算法的距离修正与「本回合内是否套现」按回合重置。
    ps(pl).algorithmThisTurn = false;
    ps(pl).cashOutThisTurn = false;
    // 26-Innealra: 每回合的共鸣/使用记录与禁则。
    ps(pl).resonanceCountThisTurn = 0;
    ps(pl).usedNonInnealraThisTurn = false;
    ps(pl).usedNormalThisTurn = 0;
    ps(pl).cannotUseNormals = false;
    ps(pl).cannotRetreat = false;
    for (int inst : ps(pl).special) ci(inst).usedThisTurn = false;
  }
  lifeChangedThisTurn_ = false;
  lifeChangeMaxThisTurn_ = 0;
  crystalBatchAny_ = 0;
  crystalBatchNonCard_ = 0;
  cardCrystalMovesThisTurn_ = 0;
  crystalLeftDustThisTurn_ = false;  // 22-Renri 罗织
  crystalMover_ = -1;
  reset_steam_at_turn_start(p);  // 气动 steam returns to the exhausted module
  pendingNagiAdjust_ = 0;        // 回收利用's 纳 ±1 must not leak into a later 付与
  // 潜水: 你的回合开始时若仍处于潜水状态，公开、执行效果并解除。
  // （必须在 tempDistanceMod 重置之后，使 ±1 作用于本回合。）
  if (ps(p).dive != 0) reveal_dive(p, false);
  distanceAtTurnStart_ = distance();
  distanceBaseline_ = distanceAtTurnStart_;  // 18-Mizuki 阵地基线
  forceUnrespondable_ = false;
  abortMain_ = false;
  fire("turn_start", p, nullptr, -1, false);
  // 23-Akina 源上安琪娜: 回合开始时的套现窗口（可被正解的 1自装到自气 替代）。
  // 先于准备阶段，因此「重铸开始时」的投资在套现之后结算。
  akina_turn_start(p);
  phase_ = "start";
  if (!ps(p).firstTurnDone) {
    ps(p).firstTurnDone = true;  // first turn of each player skips the start phase
  } else {
    start_phase(p);
  }
  phase_ = "main";
  if (!st.over && !ps(p).skipMainPhase) main_phase(p);  // 踽踽虚路行
  ps(p).skipMainPhase = false;
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
  // 19-Megumi: 绿色结晶视作樱花结晶。
  int t = 0;
  for (int i : ps(p).enhance) t += card_crystal_count(i);
  for (int i : ps(p).special)
    if (enhance_active(i)) t += card_crystal_count(i);
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
  if (card < 0 || def_of(card).unsealable) return;  // 炼成攻击: 不可封印
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
  int d = ps(P0).tempDistanceMod + ps(P1).tempDistanceMod;  // 影飞翅 (this turn)
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    d += ps(o).steamOnDist - ps(o).steamOnCrystal;  // 气动
    for (int inst : ps(o).enhance) {
      if (def_of(inst).distanceMod) d += def_of(inst).distanceMod;
      // 19-Megumi 芦苇: 当前距离增加 X（X = 本牌上的绿色结晶数）。
      if (def_of(inst).greenDistance) d += ci(inst).green;
    }
    for (int inst : ps(o).special) {
      if (enhance_active(inst) && def_of(inst).distanceMod) d += def_of(inst).distanceMod;
      if (enhance_active(inst) && def_of(inst).greenDistance) d += ci(inst).green;
    }
  }
  return d;
}

int Engine::areas_with(int n) const {
  // 新幕来临: areas are both players' 装/气/命, 距, 虚, and every 付与牌 on the field.
  int c = 0;
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    if (ps(p).aura == n) c++;
    if (ps(p).flare == n) c++;
    if (ps(p).life == n) c++;
  }
  if (st.distance == n) c++;
  if (st.dust == n) c++;
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    for (int inst : ps(p).enhance)
      if (card_crystal_count(inst) == n) c++;
    for (int inst : ps(p).special)
      if (enhance_active(inst) && card_crystal_count(inst) == n) c++;
  }
  return c;
}

bool Engine::has_damage_immunity(Player p) {
  for (int inst : ps(p).enhance)
    if (def_of(inst).damageImmune) return true;
  for (int inst : ps(p).special) {
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    if (d.damageImmune) return true;
    // 22-Renri 铭镌之衣: 「使用后」光环随终幕上的樱花结晶数动态变化——视作
    // 「夙愿」时才具有「你不会受到任何伤害」。查询是 Lua 回调，防止自递归。
    if (!effects_ || !effects_->has_hook(d.id, "damage_immune_aura")) continue;
    if (inDamageImmuneQuery_) continue;
    inDamageImmuneQuery_ = true;
    const bool ok = effects_->eval_pred(*this, d.id, "damage_immune_aura", p, inst);
    inDamageImmuneQuery_ = false;
    if (ok) return true;
  }
  return false;
}

void Engine::decay_crystals(int inst, int n) {
  if (n <= 0) return;
  const CardDef& d = def_of(inst);
  if (d.decayTo == "distance") {
    token_adjust(AreaRef::distance(), Token::Sakura, n);
    return;
  }
  if (d.decayToOwnerAura) {  // 漫天的花道: to the controller's 装 (or 气 when full)
    Player p = ci(inst).holder;
    int room = max_aura(p) - ps(p).aura;
    int toAura = std::min(n, room);
    token_adjust(AreaRef::aura(p), Token::Sakura, toAura);
    if (toAura < n) token_adjust(AreaRef::flare(p), Token::Sakura, n - toAura);
    return;
  }
  // 23-Akina O-N6 乱拨: 本牌上的樱花结晶被移除时改为移到敌气。
  if (d.decayTo == "enemy_flare") {
    token_adjust(AreaRef::flare(opp(ci(inst).holder)), Token::Sakura, n);
    return;
  }
  // 26-Innealra O3-N6 虚幻意志: 此牌上的樱花结晶被移除时移到持有者的惑。
  if (d.decayTo == "waku") {
    token_adjust(AreaRef::waku(ci(inst).holder), Token::Sakura, n);
    return;
  }
  token_adjust(AreaRef::dust(), Token::Sakura, n);
}

void Engine::empty_card(int inst) {
  if (inst < 0 || card_crystal_count(inst) <= 0) return;
  int sak = 0;
  int n = card_crystal_count(inst);
  take_card_crystals(inst, n, kTakeOwn, &sak);
  if (sak > 0) decay_crystals(inst, sak);
  drop_enhance_if_empty(inst);
}

void Engine::remove_all_normals(Player p) {
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    if (st.insts[static_cast<size_t>(i)].holder != p) continue;
    if (def_of(i).kind != CardKind::Normal) continue;
    if (st.insts[static_cast<size_t>(i)].zone == Zone::Removed) continue;
    if (vec_has(ps(p).barracks, i)) continue;  // 18-Mizuki: 兵舍士兵不受此影响
    if (card_crystal_count(i) > 0) {  // 献 leave the card before it does
      int sak = 0;
      int n = card_crystal_count(i);
      take_card_crystals(i, n, kTakeNormal, &sak);
      if (sak > 0) decay_crystals(i, sak);
    }
    move_card(i, Zone::Removed);
  }
}

// ---------------------------------------------------------------------------
// 19-Megumi 泷河希: 耕种 / 土壤 / 假想树
// ---------------------------------------------------------------------------

void Engine::init_soil(Player p) {
  PlayerState& s = ps(p);
  s.hasSoil = true;
  s.soilSeeds = 5;  // 开局：5 个绿色结晶放在「种子」
  s.soilPlants = 0;
  s.tree.assign(6, 0);
  s.treeActive = false;
  s.nextGrowth = 0;
}

int Engine::card_crystal_count(int inst) const {
  if (inst < 0) return 0;
  return ci(inst).crystals + ci(inst).green;
}

int Engine::green_total(Player p) const {
  int t = ps(p).soilSeeds + ps(p).soilPlants;
  for (int v : ps(p).tree) t += v;
  for (const CardInstance& c : st.insts)
    if (c.holder == p) t += c.green;
  return t;
}

int Engine::total_green_on_enhances(Player p) const {
  int t = 0;
  for (int inst : ps(p).enhance) t += ci(inst).green;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp) t += ci(inst).green;
  return t;
}

int Engine::green_zones(Player p) const {
  int z = 0;
  if (ps(p).soilSeeds + ps(p).soilPlants > 0) z += 1;
  bool onCards = false;
  for (int inst : ps(p).enhance)
    if (ci(inst).green > 0) onCards = true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && ci(inst).green > 0) onCards = true;
  if (onCards) z += 1;
  int tree = 0;
  for (int v : ps(p).tree) tree += v;
  if (tree > 0) z += 1;
  return z;
}

int Engine::crystal_redirect_host(Player p, int except) const {
  auto active = [&](int inst) {
    if (ci(inst).crystals + ci(inst).green <= 0) return false;  // 展开中
    return def_of(inst).redirectCrystals;
  };
  for (int inst : ps(p).special)
    if (inst != except && ci(inst).faceUp && active(inst)) return inst;
  for (int inst : ps(p).enhance)
    if (inst != except && active(inst)) return inst;
  return -1;
}

int Engine::take_card_crystals(int inst, int n, int mode, int* sakuraOut) {
  if (sakuraOut) *sakuraOut = 0;
  if (inst < 0 || n <= 0) return 0;
  CardInstance& c = ci(inst);
  int total = c.crystals + c.green;
  if (total <= 0) return 0;
  const CardDef& d = def_of(inst);
  // 23-Akina O-S3 仙霄鬼泉天元术: 本牌结晶不能被本牌以外的任何方式移除
  // （含每回合开始的 -1 与离场；只有本牌自身效果 kTakeOwn 可以移除）。
  if (d.crystalShield && mode != kTakeOwn) return 0;
  if (mode == kTakeNormal) {
    // 终结之果实：本牌结晶只能被「每回合开始的固定 -1」或本牌自身效果移除。
    if (d.crystalImmune) return 0;
    // 蔷薇：对手的回合内不能移除本牌上的结晶。
    if (d.keepCrystalsOnOppTurn && c.holder != st.active) return 0;
    // 22-Renri 终幕: 对手不能移动这张牌上的樱花结晶。
    if (d.enemyCrystalImmune && crystalMover_ >= 0 &&
        static_cast<Player>(crystalMover_) == opp(c.holder))
      return 0;
  }
  int take = std::min(n, total);
  // 终结之果实：其它付与牌要移除的结晶改为移到这张牌上（樱花/绿色各自保留）。
  int host = crystal_redirect_host(c.holder, inst);
  if (host >= 0) {
    int sak = std::min(take, c.crystals);
    int grn = take - sak;
    c.crystals -= sak;
    c.green -= grn;
    token_adjust(AreaRef::card(host), Token::Sakura, sak);
    token_adjust(AreaRef::card(host), Token::Green, grn);
    return take;
  }
  // 先移除樱花结晶，再移除绿色结晶；绿色回到种子（或假想树）。
  int sak = std::min(take, c.crystals);
  c.crystals -= sak;
  int grn = take - sak;
  c.green -= grn;
  for (int i = 0; i < grn; ++i) green_home(c.holder);
  if (sakuraOut) *sakuraOut = sak;
  return sak + grn;
}

int Engine::remove_card_crystals(int inst, int n) {
  int sak = 0;
  int got = take_card_crystals(inst, n, kTakeNormal, &sak);
  if (sak > 0) decay_crystals(inst, sak);
  return got;
}

void Engine::seed_to_plant(Player p, int n) {
  MoveReq m;
  m.from = m.to = AreaRef::soil(p);
  m.fromKind = Token::Seed;
  m.toKind = Token::Plant;
  m.n = n;
  token_move(m);
}

void Engine::cultivate(Player p) {
  if (!ps(p).hasSoil) return;
  seed_to_plant(p, 1);  // 「你将1个土壤中的绿色结晶移到植株上」
}

int Engine::growth_of(Player p, int inst) const {
  int g = def_of(inst).growth;
  if (g < 0) g = 0;
  // 脱粒: 本回合你的下一张非希的付与牌获得【生长2】。
  if (ps(p).nextGrowth > 0 && !card_has_goddess(inst, "megumi"))
    g = std::max(g, ps(p).nextGrowth);
  return g;
}

void Engine::grow_enhance(Player p, int inst) {
  if (!ps(p).hasSoil) return;
  const bool tempered = ps(p).nextGrowth > 0 && !card_has_goddess(inst, "megumi");
  int g = growth_of(p, inst);
  if (tempered) ps(p).nextGrowth = 0;  // 只影响“下一张”
  int avail = std::min(g, ps(p).soilPlants);
  if (avail <= 0) return;  // 每次都要询问，但无植株可移时不产生决策
  Request r;
  r.kind = "option";
  r.prompt = "生长" + std::to_string(g) + "：将至多 " + std::to_string(avail) + " 个植株移到「" +
             def_of(inst).name + "」上（可以不移动）";
  for (int k = 0; k <= avail; ++k)
    r.options.push_back({"移动 " + std::to_string(k) + " 个", true, {}});
  int pick = ask_one(p, std::move(r));
  if (pick < 0 || pick > avail) pick = 0;
  attach_green(inst, pick);
}

int Engine::attach_green(int inst, int n) {
  if (inst < 0 || n <= 0) return 0;
  MoveReq m;
  m.from = AreaRef::soil(ci(inst).holder);
  m.to = AreaRef::card(inst);
  m.fromKind = Token::Plant;
  m.toKind = Token::Green;
  m.n = n;
  return token_move(m);
}

int Engine::detach_green_to_seeds(int inst, int n) {
  if (inst < 0 || n <= 0) return 0;
  MoveReq m;
  m.from = AreaRef::card(inst);
  m.to = AreaRef::soil(ci(inst).holder);
  m.fromKind = Token::Green;
  m.toKind = Token::Seed;
  m.n = n;
  return token_move(m);
}

void Engine::green_home(Player p) {
  if (ps(p).treeActive && tree_place_one(p) >= 0) return;
  token_adjust(AreaRef::soil(p), Token::Seed, 1);
}

int Engine::tree_slot(Player p, int i) const {
  if (i < 0 || i >= static_cast<int>(ps(p).tree.size())) return 0;
  return ps(p).tree[static_cast<size_t>(i)];
}

int Engine::tree_occupied(Player p) const {
  int t = 0;
  for (int v : ps(p).tree)
    if (v > 0) t += 1;
  return t;
}

// 假想树: 1 格 + 2 格 + 3 格。放第 N+1 层的前置是第 N 层至少有 1 个（不必满）。
std::vector<int> Engine::tree_legal_slots(Player p) const {
  std::vector<int> out;
  if (ps(p).tree.size() < 6) return out;
  const std::vector<int>& t = ps(p).tree;
  const bool l1 = t[0] > 0;
  const bool l2 = t[1] > 0 || t[2] > 0;
  if (t[0] == 0) out.push_back(0);
  if (l1) {
    if (t[1] == 0) out.push_back(1);
    if (t[2] == 0) out.push_back(2);
  }
  if (l1 && l2)
    for (int i = 3; i < 6; ++i)
      if (t[i] == 0) out.push_back(i);
  return out;
}

int Engine::tree_place_one(Player p) {
  if (!ps(p).treeActive) return -1;
  std::vector<int> legal = tree_legal_slots(p);
  if (legal.empty()) return -1;
  static const char* kSlotName[6] = {"第1层",     "第2层·左", "第2层·右",
                                     "第3层·左", "第3层·中", "第3层·右"};
  int pick = legal[0];
  if (legal.size() > 1) {
    Request r;
    r.kind = "option";
    r.prompt = "假想树：选择放置种结晶的空格";
    for (int s : legal) r.options.push_back({kSlotName[s], true, {}});
    int idx = ask_one(p, std::move(r));
    if (idx >= 0 && idx < static_cast<int>(legal.size())) pick = legal[static_cast<size_t>(idx)];
  }
  ps(p).tree[static_cast<size_t>(pick)] = 1;
  return pick;
}

int Engine::tree_place_from_soil(Player p) {
  if (!ps(p).treeActive || ps(p).soilSeeds <= 0) return -1;
  int slot = tree_place_one(p);
  if (slot < 0) return -1;
  ps(p).soilSeeds -= 1;
  return slot;
}

void Engine::tree_fall(Player p, int n) {
  if (!ps(p).treeActive) return;
  for (int k = 0; k < n; ++k) {
    if (st.over) return;
    int slot = -1;
    for (int i = 0; i < static_cast<int>(ps(p).tree.size()); ++i)
      if (ps(p).tree[static_cast<size_t>(i)] > 0) {
        slot = i;  // 从下往上
        break;
      }
    if (slot < 0) return;
    ps(p).tree[static_cast<size_t>(slot)] = 0;
    ps(p).soilSeeds += 1;                             // 掉回土壤
    fire("tree_fell", p, nullptr, slot, false);       // 散华时（ev:card() == 格子序号）
  }
}

std::vector<int> Engine::unchosen_cuts(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    const CardInstance& c = st.insts[static_cast<size_t>(i)];
    if (c.owner != p || c.zone != Zone::Removed) continue;
    const CardDef& d = def_of(i);
    if (d.kind != CardKind::Special) continue;
    if (d.isExtra || d.isPart || d.isPoison || d.isTransform || d.soldier) continue;
    if (d.type == CardType::Enhance) continue;       // 非付与
    if (d.flags & CF_FullPower) continue;            // 非全力
    out.push_back(i);
  }
  return out;
}

void Engine::tree_use_cut(Player p, int inst) {
  if (st.over || inst < 0 || ci(inst).zone != Zone::Removed) return;
  const CardDef d = def_of(inst);
  if (d.kind != CardKind::Special || d.type == CardType::Enhance) return;
  if (d.flags & CF_FullPower) return;
  move_card(inst, Zone::Special);
  ci(inst).faceUp = true;  // 公开使用
  ci(inst).usedThisTurn = true;
  ps(p).cardsPlayedTotal += 1;
  push_play_frame(d.id, p, false);
  PlayFrameGuard frameGuard{*this};
  resolve_card_effect(p, inst, false);
  remove_card(inst);  // 使用后将该切牌移出游戏
}

int Engine::near_distance() const {
  int d = st.nearDistance + ps(P0).tempNearDistanceMod + ps(P1).tempNearDistanceMod;
  for (int oi = 0; oi < 2; ++oi) {
    Player o = static_cast<Player>(oi);
    for (int inst : ps(o).enhance) d += def_of(inst).nearDistanceMod;
    // “使用后”光环在切札处于使用后状态时生效（子午灯塔），与是否还有献无关。
    for (int inst : ps(o).special)
      if (ci(inst).faceUp) d += def_of(inst).nearDistanceMod;
  }
  return d < 0 ? 0 : d;
}

int Engine::distance() const {
  int d = st.distance + distance_delta();
  // 25-Misora 蔽目重云: 展开中当前距离直接变为持有者的瞄准点数值（若其有瞄准点）。
  {
    bool any = false;
    int chosen = 0;
    for (int oi = 0; oi < 2; ++oi) {
      Player o = static_cast<Player>(oi);
      bool has = false;
      for (int inst : ps(o).enhance)
        if (enhance_active(inst) && def_of(inst).distanceIsAim) has = true;
      for (int inst : ps(o).special)
        if (enhance_active(inst) && def_of(inst).distanceIsAim) has = true;
      if (has && ps(o).aim >= 0) {
        // 双方同时适用时取较小的瞄准点（对称且确定）。
        if (!any || ps(o).aim < chosen) chosen = ps(o).aim;
        any = true;
      }
    }
    if (any) d = chosen;
  }
  return d < 0 ? 0 : d;
}

bool Engine::has_misora(Player p) const {
  for (const std::string& s : playerSets_[p])
    if (s.rfind("misora", 0) == 0) return true;
  return false;
}

// ---- 24-Shisui 桑畑志水 -----------------------------------------------------

bool Engine::has_shisui(Player p) const {
  for (const std::string& s : playerSets_[p])
    if (s.rfind("shisui", 0) == 0) return true;
  return false;
}

bool Engine::no_death(Player p) const {
  for (int inst : ps(p).enhance)
    if (def_of(inst).noDeath && enhance_active(inst)) return true;
  for (int inst : ps(p).special)
    if (def_of(inst).noDeath && enhance_active(inst)) return true;
  return false;
}

int Engine::effective_vigor(Player p) const {
  // 埋骨地: 持有者的命为 0 时，对手的集中力视为 0。
  const Player o = opp(p);
  if (no_death(o) && ps(o).life <= 0) return 0;
  return ps(p).vigor;
}

bool Engine::attack_range_ok(const Attack& a) const {
  // 追踪: 距离判定参考瞄准点而非当前实际距离（含被对应导致不符时的重新判定）。
  // 没有瞄准点则不能打出/命中追踪攻击。
  if (a.keywords & AF_Tracking) {
    const int aim = ps(a.attacker).aim;
    return aim >= 0 && a.range.contains(aim);
  }
  return a.range.contains(distance());
}

// ---------------------------------------------------------------------------
// 23-Akina 源上安琪娜: 资本 / 股价 / 投资 / 套现 / 算法 / 死亡窗口
// ---------------------------------------------------------------------------

bool Engine::has_akina(Player p) const {
  for (const std::string& s : playerSets_[p])
    if (s.rfind("akina", 0) == 0) return true;
  return false;
}

int Engine::capital(Player p) const {
  // 资本 = 该玩家的装 + 气 + 股市结晶数；不控制安琪娜的玩家其股市视作 0。
  const int m = has_akina(p) ? ps(p).market : 0;
  return ps(p).aura + ps(p).flare + m;
}

void Engine::add_stock(Player p, int n) {
  ps(p).stockPrice = std::clamp(ps(p).stockPrice + n, 1, 4);
}

void Engine::note_attack_life_damage(Player target, bool fromAttack, int amount) {
  if (!fromAttack || amount <= 0) return;
  for (int i = 0; i < 2; ++i) {
    const Player q = static_cast<Player>(i);
    if (!has_akina(q)) continue;
    // 敌人的命受攻击伤害 -> 股价 +2；自己的命受攻击伤害 -> 股价 -1。
    if (q == target)
      add_stock(q, -1);
    else
      add_stock(q, 2);
  }
}

void Engine::cash_out(Player p) {
  if (ps(p).market < 1) return;
  const int stock = ps(p).stockPrice;
  const Player o = opp(p);
  // 1) 股市中一片樱花结晶移到虚。
  move_crystals(AreaRef::market(p), AreaRef::dust(), 1, false);
  // 2) 按股价把一片结晶移入自装（不足则尽量多，即 0 片）。
  switch (stock) {
    case 1: move_crystals(AreaRef::dust(), AreaRef::aura(p), 1, false); break;
    case 2: move_crystals(AreaRef::aura(o), AreaRef::aura(p), 1, false); break;
    case 3: move_crystals(AreaRef::flare(o), AreaRef::aura(p), 1, false); break;
    default: move_crystals(AreaRef::life(o), AreaRef::aura(p), 1, false); break;
  }
  // 3) 股价 -2。
  add_stock(p, -2);
  ps(p).cashOutThisTurn = true;
  check_win();  // 股价 4 时从敌命移入自装可能使对手命归零
}

// 投资的资金来源区域：股价 1 -> 虚 / 2 -> 自装 / 3 -> 自气 / 4 -> 自命。
static AreaRef invest_source(int stock, Player p) {
  switch (stock) {
    case 2: return AreaRef::aura(p);
    case 3: return AreaRef::flare(p);
    case 4: return AreaRef::life(p);
    default: return AreaRef::dust();
  }
}

bool Engine::invest_available(Player p) const {
  if (!has_akina(p)) return false;
  if (amount(invest_source(ps(p).stockPrice, p)) < 1) return false;  // 对应区域不足
  for (int inst : ps(p).discard)
    if (def_of(inst).investmentTicket && def_of(inst).kind == CardKind::Normal) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).investmentTicket) return true;
  return false;
}

bool Engine::invest(Player p) {
  if (!invest_available(p)) return false;
  const AreaRef src = invest_source(ps(p).stockPrice, p);
  std::vector<int> cands;
  // 恫吓 / 直接金融（常规牌）：从弃牌堆翻至背面向上（移到盖牌堆）。
  for (int inst : ps(p).discard)
    if (def_of(inst).investmentTicket && def_of(inst).kind == CardKind::Normal)
      cands.push_back(inst);
  // 正解（切牌）：从已使用重置为未使用。
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).investmentTicket) cands.push_back(inst);
  if (cands.empty()) return false;

  int pick = cands[0];
  if (cands.size() > 1) {
    Request r;
    r.kind = "option";
    r.prompt = "投资：选择一张投资券翻至背面向上";
    for (int inst : cands) r.options.push_back({card_label(def_of(inst)), true, {{"inst", inst}}});
    const int idx = ask_one(p, std::move(r));
    if (idx >= 0 && idx < static_cast<int>(cands.size()))
      pick = cands[static_cast<size_t>(idx)];
  }
  if (def_of(pick).kind == CardKind::Normal)
    cover_card(pick);  // 弃牌堆 -> 盖牌堆（背面向上）
  else
    reset_special(pick);  // 已使用 -> 未使用

  // 支付投资资金，然后股价 +1。
  move_crystals(src, AreaRef::market(p), 1, false);
  add_stock(p, 1);
  check_win();  // 股价 4 时从自命支付可能使自命归零
  return true;
}

bool Engine::answer_aura_active(Player p) const {
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).cashSubstitute) return true;
  return false;
}

void Engine::akina_turn_start(Player p) {
  if (st.over || !has_akina(p)) return;
  const bool canCash = can_cash_out(p);
  // 正解「替代套现操作」：替代的前提是本来能套现（股市至少 1 个结晶，裁定）。
  const bool canSub = canCash && answer_aura_active(p) && ps(p).aura >= 1;
  if (!canCash && !canSub) return;
  Request r;
  r.kind = "option";
  r.prompt = "回合开始：套现？";
  std::vector<std::string> kinds;
  if (canCash) {
    r.options.push_back({"套现", true, {}});
    kinds.push_back("cash");
  }
  if (canSub) {
    r.options.push_back({"正解：1 自装到自气（替代套现）", true, {}});
    kinds.push_back("sub");
  }
  r.options.push_back({"不套现", true, {}});
  kinds.push_back("none");
  const int idx = ask_one(p, r);  // ask_one takes the request by value: keep r intact
  if (idx < 0 || idx >= static_cast<int>(kinds.size())) return;
  const std::string what = kinds[static_cast<size_t>(idx)];
  if (what == "cash")
    cash_out(p);
  else if (what == "sub")
    move_crystals(AreaRef::aura(p), AreaRef::flare(p), 1, false);
}

void Engine::akina_end_of_turn(Player p) {
  if (st.over || !has_akina(p)) return;
  if (ps(p).cashOutThisTurn) return;  // 本回合内套现过则不能投资
  if (!invest_available(p)) return;
  if (ask_yes_no(p, "回合结束：投资？（本回合内没有套现）")) invest(p);
}

bool Engine::algorithm_active() const {
  return ps(P0).algorithmThisTurn || ps(P1).algorithmThisTurn;
}

void Engine::apply_algorithm(Attack& a) const {
  if (!algorithm_active()) return;
  a.range.algorithm_shift();
}

void Engine::maybe_force_reuse(Player p, int inst) {
  if (st.over || inForceReuse_ || inst < 0) return;
  const CardDef& d = def_of(inst);
  if (!d.reuseWhileAhead) return;
  inForceReuse_ = true;
  int guard = 0;
  while (!st.over && guard++ < 20 && capital(p) > capital(opp(p))) {
    const int cost = cut_cost(p, d.id, inst);
    if (ps(p).flare < cost) break;  // 无法照常支付费用
    pay_special_cost(p, inst);
    ci(inst).faceUp = true;
    ci(inst).usedThisTurn = true;
    resolve_card_effect(p, inst, false);
  }
  inForceReuse_ = false;
}

bool Engine::run_death_saves(Player p) {
  if (inDeathWindow_) return ps(p).life > 0;
  std::vector<int> cards;
  for (int inst : ps(p).enhance)
    if (enhance_active(inst) && effects_->has(def_of(inst).id, "on_death")) cards.push_back(inst);
  for (int inst : ps(p).special)
    if (enhance_active(inst) && effects_->has(def_of(inst).id, "on_death")) cards.push_back(inst);
  if (cards.empty()) return false;
  inDeathWindow_ = true;
  for (int inst : cards) {
    if (st.over || ps(p).life > 0) break;
    effects_->call(*this, def_of(inst).id, "on_death", p, inst);
  }
  inDeathWindow_ = false;
  return ps(p).life > 0;
}

// ---------------------------------------------------------------------------
// 26-Innealra 诺伦: 三把枪 / 命运槽 / 共鸣 / 纠葛 / 惑
// ---------------------------------------------------------------------------

bool Engine::has_innealra(Player p) const {
  for (const std::string& s : playerSets_[p])
    if (s.rfind("innealra", 0) == 0) return true;
  return false;
}

bool Engine::form_matches(const CardDef& d, const std::string& f) const {
  if (d.form == f) return true;
  return std::find(d.forms.begin(), d.forms.end(), f) != d.forms.end();
}

void Engine::init_fates(Player p, const std::string& form) {
  for (int i = 0; i < 4; ++i) ps(p).fate[i] = -1;
  for (const CardDef& d : defs) {
    if (!d.isFate || d.goddess != "innealra") continue;
    if (!form_matches(d, form)) continue;
    const int slot = d.fateSlot;
    if (slot < 0 || slot > 3) continue;
    ps(p).fate[slot] = d.id;
  }
}

int Engine::fate_slot(Player p, int i) const {
  if (i < 0 || i > 3) return -1;
  return ps(p).fate[i];
}

int Engine::fate_pos(Player p, const std::string& name) const {
  for (int i = 0; i < 4; ++i) {
    const int defId = ps(p).fate[i];
    if (defId >= 0 && def(defId).name == name) return i;
  }
  return -1;
}

void Engine::rotate_fates(Player p) {
  // 过去→待启、现在→过去、未来→现在、待启→未来。
  int* f = ps(p).fate;
  const int past = f[0];
  f[0] = f[1];
  f[1] = f[2];
  f[2] = f[3];
  f[3] = past;
}

int Engine::resonance_time_slot(Player p) const {
  for (const std::string& s : playerSets_[p]) {
    if (s.rfind("innealra", 0) != 0) continue;
    if (s == "innealra.A1") return 1;
    if (s == "innealra.A2") return 2;
    return 0;
  }
  return 0;
}

void Engine::resolve_fate_slot(Player p, int i, bool fromTurnStart) {
  if (st.over) return;
  const int defId = fate_slot(p, i);
  if (defId < 0) return;
  const int prevSlot = fateResolvingSlot_;
  const bool prevFrom = fateFromTurnStart_;
  fateResolvingSlot_ = i;
  fateFromTurnStart_ = fromTurnStart;
  effects_->call(*this, defId, "on_fate", p, -1);
  fateResolvingSlot_ = prevSlot;
  fateFromTurnStart_ = prevFrom;
}

void Engine::resonance(Player p, bool fromTurnStart) {
  if (st.over) return;
  resolve_fate_slot(p, resonance_time_slot(p), fromTurnStart);
  ps(p).resonanceCountThisTurn += 1;
  rotate_fates(p);
  check_win();
}

bool Engine::fates_entangled(Player p) const {
  if (ps(p).fatesEntangled) return true;
  for (int inst : ps(p).enhance)
    if (def_of(inst).fateEntangler) return true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).fateEntangler) return true;
  return false;
}

void Engine::note_card_used_by(Player p, const CardDef& d) {
  if (d.kind == CardKind::Normal) ps(p).usedNormalThisTurn += 1;
  bool inn = false;
  for (const std::string& g : d.goddesses)
    if (g == "innealra") inn = true;
  if (!inn) ps(p).usedNonInnealraThisTurn = true;
}

void Engine::offer_fate_rotation(Player p, const std::string& why) {
  if (st.over || !has_innealra(p)) return;
  if (ask_yes_no(p, why + "：轮转命运槽（不共鸣）？")) rotate_fates(p);
}

int Engine::fragile_will_host(Player gainer) const {
  const Player o = opp(gainer);
  for (int inst : ps(o).enhance)
    if (def_of(inst).fragileWill) return inst;
  for (int inst : ps(o).special)
    if (ci(inst).faceUp && def_of(inst).fragileWill) return inst;
  return -1;
}

bool Engine::suppress_attack_mods(Player attacker) const {
  const Player o = opp(attacker);
  for (int inst : ps(o).enhance)
    if (enhance_active(inst) && def_of(inst).suppressEnemyAttackMods) return true;
  for (int inst : ps(o).special)
    if (enhance_active(inst) && def_of(inst).suppressEnemyAttackMods) return true;
  return false;
}

int Engine::cost_to_waku(Player p, int inst) {
  const int n = cost_paid(inst);
  if (n <= 0) return 0;
  const int moved = move_crystals(AreaRef::dust(), AreaRef::waku(p), n, false);
  store_int(inst, "paid_cost", 0);
  return moved;
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
  if (inst < 0 || n <= 0) return 0;
  int sak = 0;
  int got = take_card_crystals(inst, n, kTakeNormal, &sak);
  if (sak > 0) decay_crystals(inst, sak);
  return got;
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

void Engine::return_enhance(int inst) {
  if (ci(inst).zone == Zone::Discard || ci(inst).zone == Zone::Removed)
    move_card(inst, Zone::Enhance);
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
  if (has_full_power(inst)) c |= COL_YELLOW;
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
  MoveReq m;
  m.from = AreaRef::steam_engine(p);
  m.to = AreaRef::steam_exhausted(p);
  m.fromKind = m.toKind = Token::Steam;
  m.n = x;
  token_move(m);  // 萨利亚的杰作重定向在 token_move 的蒸汽分支内
}

void Engine::recover(Player p, int x) {
  MoveReq m;
  m.from = AreaRef::steam_exhausted(p);
  m.to = AreaRef::steam_engine(p);
  m.fromKind = m.toKind = Token::Steam;
  m.n = x;
  token_move(m);
}

void Engine::pneumatic(Player p) {
  if (ps(p).steamEngine < 1) return;
  Request r;
  r.kind = "option";
  r.prompt = "气动：距离 +1 或 -1？";
  r.options.push_back({"距离 +1", true, {}});
  r.options.push_back({"距离 -1", true, {}});
  int c = ask_one(p, std::move(r));
  MoveReq m;
  m.from = AreaRef::steam_engine(p);
  // 「距离 -1」→ 放在距的结晶上（每枚 -1）；「距离 +1」→ 放在距上（每枚 +1）。
  m.to = c == 1 ? AreaRef{AreaKind::SteamOnCrystal, p, -1} : AreaRef{AreaKind::SteamOnDist, p, -1};
  m.fromKind = m.toKind = Token::Steam;
  m.n = 1;
  token_move(m);  // 距上蒸汽的 distance_changed/pneumatic 事件在蒸汽分支内
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

// ---------------------------------------------------------------------------
// 20-Kanawe 叶慧: 地图 / 戏剧
// ---------------------------------------------------------------------------
namespace {

// 地图（rules/20-kanawe.md）。color: -1 无 / 0 红 / 1 紫 / 2 绿 / 3 黄。
// next = 普通可达；trial = 「可试炼到」（只有完成升级版戏剧才能到达）。
struct MapNodeDef {
  const char* id;
  int color;
  int value;
  std::vector<const char*> next;
  std::vector<const char*> trial;
  bool terminal;
};

const std::vector<MapNodeDef>& node_table() {
  static const std::vector<MapNodeDef> t = {
      {"O2", -1, 2, {"1A", "1B", "2C"}, {}, false},
      {"1A", 0, 0, {"2A", "2B"}, {}, false},
      {"1B", 2, 2, {"2A", "2B"}, {}, false},
      {"2A", 0, 2, {"3A"}, {}, false},
      {"2B", 1, 4, {"3A", "3B"}, {"3C"}, false},
      {"2C", 0, 3, {"3D"}, {}, false},
      {"3A", 0, 4, {"4A"}, {}, false},
      {"3B", 2, 6, {"4A"}, {}, false},
      {"3C", 3, 4, {"4B"}, {}, false},
      {"3D", 1, 5, {}, {"4B"}, false},
      {"4A", 1, 0, {"5A", "5B", "5C"}, {}, false},
      {"4B", 0, 3, {"5C"}, {}, false},
      {"5A", 0, 2, {}, {"END"}, false},
      {"5B", 1, 0, {}, {"END"}, false},
      {"5C", 2, 0, {}, {"END"}, false},
      {"END", -1, -1, {}, {}, true},
  };
  return t;
}

const MapNodeDef* find_node(const std::string& id) {
  for (const MapNodeDef& n : node_table())
    if (id == n.id) return &n;
  return nullptr;
}

std::string node_label(const MapNodeDef& n) {
  if (n.terminal) return "终点";
  static const char* kColor[4] = {"红", "紫", "绿", "黄"};
  std::string s = n.id;
  if (n.color >= 0 && n.color < 4) s += std::string("(") + kColor[n.color] + " " +
                                       std::to_string(n.value) + ")";
  return s;
}

// 《戏剧》达成所需次数：T1..T3/T5/T6 基础 2 / 升级 1；《战栗》基础 1 / 升级 2；
// 《明转》升级的条件明写「达成次数2」。
int drama_need(int slot, int tier) {
  if (slot == 5) return tier == 1 ? 2 : 1;
  if (slot == 4 && tier == 1) return 2;
  return tier == 1 ? 1 : 2;
}

}  // namespace

int Engine::node_value(Player p) const {
  const MapNodeDef* n = find_node(ps(p).node);
  return n ? n->value : 0;
}

int Engine::node_color(Player p) const {
  const MapNodeDef* n = find_node(ps(p).node);
  return n ? n->color : -1;
}

void Engine::init_dramas(Player p) {
  ps(p).dramas.clear();
  ps(p).dramaPrepared = -1;
  ps(p).dramaProgressedThisTurn = false;
  ps(p).dramaProgressedLastTurn = false;
  ps(p).noDramaThisTurn = false;
  ps(p).node = "O2";
  // O-T1..O-T6 按槽位顺序建立实例（zone == Removed，由 PlayerState.dramas 保管）。
  for (int slot = 1; slot <= 6; ++slot)
    for (const CardDef& d : defs)
      if (d.isDrama && d.goddess == "kanawe" && d.dramaSlot == slot) {
        int inst = add_instance(d.id, p);
        store_int(inst, "tag", 0);          // 0 未完成 / 1 戏剧栏 / 2 已完成
        store_int(inst, "progress", 0);
        store_int(inst, "counted_turn", -1);
        ps(p).dramas.push_back(inst);
        break;
      }
}

bool Engine::prepare_drama(Player p, bool allowCompleted) {
  std::vector<int> cands;
  for (int inst : ps(p).dramas) {
    const int tag = load_int(inst, "tag", 0);
    if (tag == 1) continue;                      // 已在戏剧栏
    if (tag == 2 && !allowCompleted) continue;   // 已完成堆（撰写不可选）
    cands.push_back(inst);
  }
  if (cands.empty()) return false;

  Request r;
  r.kind = "option";
  r.prompt = "准备一个戏剧";
  for (int inst : cands) r.options.push_back({def_of(inst).name, true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(cands.size())) idx = 0;
  const int chosen = cands[static_cast<size_t>(idx)];
  const bool fromCompleted = load_int(chosen, "tag", 0) == 2;

  // 条件版本：基础版（达成次数多但条件宽）或升级版（可试炼到分支）。
  Request vr;
  vr.kind = "option";
  vr.prompt = "选择戏剧" + def_of(chosen).name + "的条件版本";
  vr.options.push_back({"基础版条件", true, {}});
  vr.options.push_back({"升级版条件（可试炼）", true, {}});
  const int vt = ask_one(p, std::move(vr));
  const int tier = vt == 1 ? 1 : 0;

  const int old = ps(p).dramaPrepared;
  if (old >= 0 && old != chosen) {  // 被换下的戏剧回到未完成堆，进度不保留
    store_int(old, "tag", 0);
    store_int(old, "progress", 0);
    store_int(old, "counted_turn", -1);
  }
  store_int(chosen, "tag", 1);
  store_int(chosen, "progress", 0);
  store_int(chosen, "counted_turn", -1);
  store_int(chosen, "prep_turn", st.turn);
  store_int(chosen, "tier", tier);
  ps(p).dramaPrepared = chosen;
  check_dramas();
  return fromCompleted;
}

bool Engine::drama_condition_met(Player p, int inst, int slot, int tier) const {
  switch (slot) {
    case 1: {  // 《杀阵》: 本回合双方进行的攻击 + 对应
      const int n = attacksThisTurn_[P0] + attacksThisTurn_[P1] +
                    ps(P0).responsesPlayedThisTurn + ps(P1).responsesPlayedThisTurn;
      return tier == 1 ? n >= 5 : n >= 2;
    }
    case 2:  // 《樱花》
      return tier == 1 ? crystalBatchAny_ >= 5 : crystalBatchNonCard_ >= 3;
    case 3:  // 《鼓动》
      return tier == 1 ? lifeChangeMaxThisTurn_ >= 2 : lifeChangedThisTurn_;
    case 4:  // 《明转》
      return cardCrystalMovesThisTurn_ >= (tier == 1 ? 2 : 1);
    case 5:  // 《战栗》: 不是本回合准备的 + 本回合使用过全力牌
      if (load_int(inst, "prep_turn", -1) == st.turn) return false;
      return usedFullPowerThisTurn_[P0] || usedFullPowerThisTurn_[P1];
    case 6: {  // 《定位》: 回合结束时的距离变化
      int d = distance() - distanceAtTurnStart_;
      if (d < 0) d = -d;
      if (tier == 1) return d >= 5;
      return d >= 2 && distance() <= 8;
    }
    default:
      (void)p;
      return false;
  }
}

void Engine::check_dramas(bool atTurnEnd) {
  if (st.over) return;
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const int inst = ps(p).dramaPrepared;
    if (inst < 0) continue;
    if (load_int(inst, "tag", 0) != 1) continue;
    if (load_int(inst, "counted_turn", -1) == st.turn) continue;  // 同回合只计 1 次
    const int slot = def_of(inst).dramaSlot;
    const int tier = load_int(inst, "tier", 0);
    if (slot == 6 && !atTurnEnd) continue;  // 《定位》只在回合结束时判定
    if (!drama_condition_met(p, inst, slot, tier)) continue;
    const int need = drama_need(slot, tier);
    int prog = load_int(inst, "progress", 0);
    // 演出: 本回合不能完成戏剧（这一回合的达成不计入）。
    if (ps(p).noDramaThisTurn && prog + 1 >= need) continue;
    store_int(inst, "counted_turn", st.turn);
    prog += 1;
    store_int(inst, "progress", prog);
    ps(p).dramaProgressedThisTurn = true;
    if (prog >= need) {
      store_int(inst, "tag", 2);  // 完成的戏剧立即进入已完成堆
      ps(p).dramaPrepared = -1;
      // 推进地图会询问玩家并可能插入伤害结算，因此延迟到下一个安全点。
      pendingAdvance_[pi] = true;
      pendingAdvanceTier_[pi] = tier == 1;
    }
  }
}

// 在安全的决策点结算「完成戏剧 → 前进一格」。advance_node 会询问玩家并结算
// 落格奖励（红/紫/绿），期间可能嵌套伤害；这里保存并恢复引擎的瞬时全局，
// 使调用者（可能是 apply_damage_to / resolve_attack 的中间态）不受影响。
void Engine::flush_drama_advances() {
  for (int pi = 0; pi < 2; ++pi) {
    if (!pendingAdvance_[pi]) continue;
    pendingAdvance_[pi] = false;
    const bool tier = pendingAdvanceTier_[pi];
    // 推进地图会触发玩家决策/事件；「最近一次伤害/攻击」记录跨过该窗口保存，
    // 供之后的即再起/攻击后谓词读取。伤害路由已是参数（DamageRoute），无需保存。
    const int sDmgSide = lastDmgSide_, sDmgAmount = lastDmgAmount_;
    const bool sDmgFrom = lastDmgFromAttack_;
    const int sAtkSide = lastAtkSide_, sAtkAmount = lastAtkAmount_;
    const int sPendCard = pendingDamageToCard_;
    advance_node(static_cast<Player>(pi), tier);
    lastDmgSide_ = sDmgSide;
    lastDmgAmount_ = sDmgAmount;
    lastDmgFromAttack_ = sDmgFrom;
    lastAtkSide_ = sAtkSide;
    lastAtkAmount_ = sAtkAmount;
    pendingDamageToCard_ = sPendCard;
    if (st.over) return;
  }
}

bool Engine::advance_node(Player p, bool upgraded) {
  const MapNodeDef* cur = find_node(ps(p).node);
  if (!cur || cur->terminal) return false;
  std::vector<const MapNodeDef*> opts;
  for (const char* id : cur->next)
    if (const MapNodeDef* n = find_node(id)) opts.push_back(n);
  if (upgraded)
    for (const char* id : cur->trial)
      if (const MapNodeDef* n = find_node(id)) opts.push_back(n);
  if (opts.empty()) return false;  // 无可前进的分支（例如 5A 的基础版）

  Request r;
  r.kind = "option";
  r.prompt = "地图：从 " + std::string(cur->id) + " 选择前进的节点";
  for (const MapNodeDef* n : opts) r.options.push_back({node_label(*n), true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(opts.size())) idx = 0;
  const MapNodeDef* dest = opts[static_cast<size_t>(idx)];
  ps(p).node = dest->id;
  fire("node_advanced", p, nullptr, -1, false);
  if (st.over) return true;
  if (dest->terminal) {
    die(opp(p));  // 走到终点：对手死亡
    return true;
  }
  resolve_node_reward(p);  // 落格奖励（按颜色）
  return true;
}

void Engine::resolve_node_reward(Player p) {
  switch (node_color(p)) {
    case 0:  // 红: 对敌人造成 1 命伤
      damage_life(opp(p), 1, AreaKind::Flare, true);
      break;
    case 1:  // 紫: 执行一次基本动作
      forced_basic(p);
      break;
    case 2: {  // 绿: 从盖牌区选一张放到牌库底
      if (ps(p).cover.empty()) break;
      Request r;
      r.kind = "cards";
      r.prompt = "绿：从盖牌区选择一张放到你的牌库底";
      for (int inst : ps(p).cover) {
        Option o;
        o.label = card_label(def_of(inst));
        o.data = {{"inst", inst}};
        r.options.push_back(o);
      }
      int idx = ask_one(p, std::move(r));
      if (idx < 0 || idx >= static_cast<int>(ps(p).cover.size())) idx = 0;
      move_card_bottom(ps(p).cover[static_cast<size_t>(idx)]);
      break;
    }
    case 3:  // 黄: 持续光环（apply_node_attack_bonus）
    default:
      break;
  }
}

void Engine::forced_basic(Player p) {
  std::vector<BasicAction> legal;
  for (int bi = 0; bi < 5; ++bi) {
    BasicAction ba = static_cast<BasicAction>(bi);
    if (basic_legal(p, ba)) legal.push_back(ba);
  }
  if (legal.empty()) return;
  Request r;
  r.kind = "option";
  r.prompt = "执行一次基本动作";
  for (BasicAction ba : legal) r.options.push_back({basic_name(ba), true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(legal.size())) idx = 0;
  do_basic(p, legal[static_cast<size_t>(idx)]);
}

void Engine::apply_node_attack_bonus(Player p, Attack& a) const {
  if (a.generated) return;             // 只强化非衍生攻击
  if (node_color(p) != 3) return;      // 站在黄色地点
  if (a.life.has_value()) a.lifeDelta += 1;  // +0/+1
}

// 20-Kanawe 封杀: 生效中，对手不能使用与宣言牌名相同的名称的切牌；若当前剧目
// 颜色为红（封杀持有者的当前剧目），对手也不能使用同名通常牌。
bool Engine::cut_name_banned(Player p, int inst) const {
  const CardKind kind = def_of(inst).kind;
  if (kind != CardKind::Special && kind != CardKind::Normal) return false;
  const Player o = opp(p);
  const bool red = node_color(o) == 0;  // 0 = 红
  auto banned_by = [&](int e) {
    if (!def_of(e).cutBan) return false;
    const int d = load_int(e, "ban_def", -1);
    if (d < 0 || d >= static_cast<int>(defs.size())) return false;
    if (defs[static_cast<size_t>(d)].name != def_of(inst).name) return false;
    if (kind == CardKind::Special) return true;
    return red;  // 通常牌: 仅当封杀持有者当前剧目为红
  };
  for (int e : ps(o).enhance)
    if (banned_by(e)) return true;
  for (int e : ps(o).special)
    if (ci(e).faceUp && banned_by(e)) return true;
  return false;
}

int Engine::declare_cut_ban(Player p, int inst) {
  const Player o = opp(p);
  std::vector<int> cands;  // def ids
  for (const CardDef& d : defs) {
    // 宣言范围 = 对手构筑池中的常规牌与切牌（剧目/追加牌/零件/士兵等除外）。
    if (d.isDrama || d.isExtra || d.isPart || d.isPoison || d.isTransform || d.soldier)
      continue;
    bool inSets = false;
    for (const std::string& s : playerSets_[o])
      if (s == d.set) inSets = true;
    if (!inSets) continue;
    bool dup = false;
    for (int c : cands)
      if (defs[static_cast<size_t>(c)].name == d.name) dup = true;
    if (!dup) cands.push_back(d.id);
  }
  if (cands.empty()) return -1;
  Request r;
  r.kind = "option";
  r.prompt = "封杀：宣言一个牌名（切牌立刻生效；通常牌仅在你当前剧目为红时生效）";
  for (int c : cands) r.options.push_back({defs[static_cast<size_t>(c)].name, true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(cands.size())) idx = 0;
  const int defId = cands[static_cast<size_t>(idx)];
  store_int(inst, "ban_def", defId);
  return defId;
}

void Engine::remove_from_game(int inst) {
  move_card(inst, Zone::Removed);
  store_int(inst, "out", 1);
}

std::vector<int> Engine::unchosen_normals(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    const CardInstance& c = st.insts[static_cast<size_t>(i)];
    if (c.owner != p || c.zone != Zone::Removed) continue;
    const CardDef& d = def_of(i);
    if (d.kind != CardKind::Normal || d.isDrama || d.isExtra || d.isPart || d.isPoison ||
        d.isTransform || d.soldier)
      continue;
    if (load_int(i, "out", 0) == 1 || load_int(i, "gained", 0) == 1) continue;
    out.push_back(i);
  }
  return out;
}

std::vector<int> Engine::unchosen_specials(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    const CardInstance& c = st.insts[static_cast<size_t>(i)];
    if (c.owner != p || c.zone != Zone::Removed) continue;
    const CardDef& d = def_of(i);
    if (d.kind != CardKind::Special || d.isDrama || d.isExtra || d.isPart || d.isPoison ||
        d.isTransform || d.soldier)
      continue;
    if (load_int(i, "out", 0) == 1 || load_int(i, "gained", 0) == 1) continue;
    out.push_back(i);
  }
  return out;
}

void Engine::gain_unchosen_normal(Player p, int inst) {
  if (inst < 0 || ci(inst).zone != Zone::Removed || ci(inst).owner != p) return;
  move_card(inst, Zone::Hand);
  store_int(inst, "gained", 1);
}

void Engine::gain_unchosen_cut(int inst) {
  if (inst < 0 || ci(inst).zone != Zone::Removed) return;
  move_card(inst, Zone::Special);
  ci(inst).faceUp = false;  // 未使用状态
  store_int(inst, "gained", 1);
}

void Engine::play_hand_card_response(Player p, int inst) {
  if (inst < 0 || ci(inst).zone != Zone::Hand) return;
  play_card(p, inst, /*asResponse=*/true);
}

int Engine::attacks_and_responses_this_turn() const {
  return attacksThisTurn_[P0] + attacksThisTurn_[P1] + ps(P0).responsesPlayedThisTurn +
         ps(P1).responsesPlayedThisTurn;
}

void Engine::note_life_change(int delta) {
  if (delta == 0) return;
  lifeChangedThisTurn_ = true;
  const int a = delta < 0 ? -delta : delta;
  if (a > lifeChangeMaxThisTurn_) lifeChangeMaxThisTurn_ = a;
}

void Engine::note_crystal_move(const AreaRef& from, const AreaRef& to, int moved,
                               bool cardEffect) {
  if (moved <= 0) return;
  if (moved > crystalBatchAny_) crystalBatchAny_ = moved;
  if (to.kind != AreaKind::Card && moved > crystalBatchNonCard_) crystalBatchNonCard_ = moved;
  if (cardEffect) cardCrystalMovesThisTurn_ += 1;
  // 22-Renri 罗织: 「本回合内有樱花结晶移出虚」。
  if (from.kind == AreaKind::Dust) crystalLeftDustThisTurn_ = true;
}

}  // namespace fy
