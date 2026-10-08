#include "engine/engine.hpp"

#include <algorithm>
#include <future>
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

  int fi = push_trace_frame(p, req);
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
  return finalize_decision(p, req, std::move(d), fi);
}

// 追加一个 trace 帧（返回帧下标；未开 tracing 返回 -1）。智能体 IO 期间引擎
// 状态不变，帧在 IO 之后推送与请求时刻等价——decide_both 依赖这一点做固定
// 顺序的串行簿记。
int Engine::push_trace_frame(Player p, const Request& req) {
  if (!tracing_) return -1;
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
    if (!o.data.is_null() && o.data.contains("attack")) f["data"] = req.data;
    f["options"].push_back(jo);
  }
  f["state"] = full_state_json();
  frames_.push_back(std::move(f));
  return static_cast<int>(frames_.size()) - 1;
}

// 决策的净化与簿记（非法计数/回放校验/日志/trace 选择回填）。
Decision Engine::finalize_decision(Player p, const Request& req, Decision d, int fi) {
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

// 并行询问双方（三拾/一舍/眼前构筑/换牌）：线程只做智能体 IO；簿记按 P0→P1
// 固定顺序串行，回放/日志顺序与线程完成顺序无关。
std::pair<Decision, Decision> Engine::decide_both(Request ra, Request rb) {
  ra.player = P0;
  rb.player = P1;
  if (!st.over && (pendingAdvance_[P0] || pendingAdvance_[P1])) flush_drama_advances();
  if (ra.state.is_null()) ra.state = observation(P0);
  if (rb.state.is_null()) rb.state = observation(P1);
  Agent* a0 = agents_[P0];
  Agent* a1 = agents_[P1];
  if (replaying_ || !a0 || !a1 || a0 == a1) {
    // 回放 / 共享智能体 / 缺席：顺序询问（决策结果与并行等价）。
    return {decide(P0, std::move(ra)), decide(P1, std::move(rb))};
  }
  auto fut0 = std::async(std::launch::async, [a0, ra]() mutable { return a0->decide(ra); });
  auto fut1 = std::async(std::launch::async, [a1, rb]() mutable { return a1->decide(rb); });
  Decision d0 = fut0.get();
  Decision d1 = fut1.get();
  const int f0 = push_trace_frame(P0, ra);
  const int f1 = push_trace_frame(P1, rb);
  d0 = finalize_decision(P0, ra, std::move(d0), f0);
  d1 = finalize_decision(P1, rb, std::move(d1), f1);
  return {d0, d1};
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
  // 「谎言的武器」，如「设置」一样打出。该宣称**视作一次完整的伪证**（2026-10-07
  // 裁定）：对手可以质疑——未质疑按声称结算；质疑后展示该牌，若它真是
  // 「谎言的武器」则质疑失败（对手焦躁一次），正常使用并可「回归」（移出游戏，
  // 置回「考古」）；若不是，则宣称作罢（两效果都不执行），牌留在盖牌区。
  // 电子设置替换整次重铸（上方提前 return），不进入本流程。
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
        const Player o = opp(p);
        // 视作伪证：对手可以质疑。
        Request dr;
        dr.kind = "doubt";
        dr.prompt = "对手宣称盖牌中的一张牌是「" + def(claimDef).name + "」。质疑？";
        dr.options.push_back({"不质疑", true, {{"doubt", 0}}});
        dr.options.push_back({"质疑", true, {{"doubt", 1}}});
        const bool doubted = ask_one(o, std::move(dr)) == 1;
        const bool real = def_of(chosen).id == claimDef;
        const bool sa = bluffActive_, sn = bluffNotDoubted_, sf = bluffDoubtFailed_;
        const int si = bluffInst_, sd = bluffClaimDef_, spr = pendingResolveAs_;
        if (doubted && !real) {
          // 质疑成功：宣称作罢，牌留在盖牌区，两效果都不执行。
          return;
        }
        if (doubted && real) {
          // 质疑失败：对手焦躁一次，正常使用这张牌（它就是「谎言的武器」）。
          ps(o).doubtFailedThisTurn = true;
          impatience(o);
        }
        if (!doubted && !real) {
          // 未质疑但牌不是武器：按声称（谎言的武器）结算。
        }
        bluffActive_ = true;
        bluffInst_ = chosen;
        bluffClaimDef_ = claimDef;
        bluffNotDoubted_ = !doubted;
        bluffDoubtFailed_ = doubted && real;
        pendingResolveAs_ = claimDef;
        play_from_cover(p, chosen, false, true);  // 如设置一样打出，结算后进牌山
        if (!doubted && !st.over) fire("bluff_undoubted", p, nullptr, chosen, false);
        // 回归（质疑失败时可选）：移出游戏并把「考古」置回弃牌堆。
        if (doubted && real && !st.over && def_of(chosen).regression &&
            ci(chosen).zone != Zone::Removed) {
          if (ask_yes_no(p, "回归：将这张牌移出游戏，并把「考古」置回弃牌堆？")) {
            remove_from_game(chosen);
            for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
              if (ci(i).owner == p && ci(i).zone == Zone::Removed && def_of(i).kaoguReturn) {
                move_card(i, Zone::Discard);
                break;
              }
            }
          }
        }
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








void Engine::note_distance_changed() {
  ps(P0).distChanged = true;
  ps(P1).distChanged = true;
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
             ps(p).aura >= 1 && st.distance < max_distance_crystals();  // 距区需有余量
    case BasicAction::Aura: {
      if (st.dust < 1) return false;
      if (aura_free(p) > 0) return true;
      // 双掌生花: 自装满时仍可装附（结晶改为放到该牌上），但打出时的那次不替换。
      return absorb_aura_host(p) >= 0 && !ps(p).suppressAuraRedirect;
    }
    case BasicAction::Flare:
      return !has_enemy_no_flare(p) && (frozen(p) || ps(p).aura >= 1);
    case BasicAction::Escape:
      return !has_named_active(opp(p), cards::kMud) && distance() <= near_distance() &&
             st.dust >= 1 && st.distance < max_distance_crystals();  // 距区需有余量
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
    case BasicAction::Advance: move(AreaRef::distance(), AreaRef::aura(p), 1, false); break;
    case BasicAction::Retreat: move(AreaRef::aura(p), AreaRef::distance(), 1, false); break;
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
        move(AreaRef::dust(), AreaRef::card(host), 1, false);
      else
        move(AreaRef::dust(), AreaRef::aura(p), 1, false);
      ps(p).suppressAuraRedirect = false;
      fire("basic_aura", p, nullptr, -1, false);  // 双掌生花: 检查是否恰好 5
      break;
    }
    case BasicAction::Flare:
      if (frozen(p))
        ps(p).ice -= 1;  // 被冻结时，聚气改为移除 1 个冰晶
      else
        move(AreaRef::aura(p), AreaRef::flare(p), 1, false);
      break;
    case BasicAction::Escape:  move(AreaRef::dust(), AreaRef::distance(), 1, false); break;
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
    r.prompt = "执行一次基本动作（可不执行）";
    for (BasicAction ba : legal)
      r.options.push_back({std::string("基本动作：") + basic_cn(ba), true, {}});
    r.options.push_back({"停止", true, {}});
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
    r.prompt = "执行一次基本动作（可不执行）";
    for (BasicAction ba : legal)
      r.options.push_back({std::string("基本动作：") + basic_cn(ba), true, {}});
    r.options.push_back({"停止", true, {}});
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




int Engine::memory_size(Player p) const { return static_cast<int>(ps(p).memory.size()); }






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
    r.prompt = "主要阶段：选择动作（基本动作/打出牌/切札/结束）";
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
          o.label = std::string("基本动作：") + basic_cn(ba) +
                    (payCover ? "（盖伏一张手牌支付）" : "（支付集中力）");
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
    r.options.push_back({"结束主要阶段", true, {{"kind", "pass"}}});

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
  return move(AreaRef::dust(), AreaRef::card(inst), n, true);
}

void Engine::set_used(int inst) {
  if (ci(inst).zone == Zone::Special) ci(inst).faceUp = true;
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

// 牌上结晶 decay_to 目的地的剩余容量（裁定 2026-10-08：能移多少移多少）。
int Engine::decay_free_capacity(int inst) const {
  const CardDef& d = def_of(inst);
  if (d.decayTo == "distance") return max_distance_crystals() - st.distance;
  // 漫天的花道（decayToOwnerAura）：装满则转气（decay_crystals 自身处理），
  // 总能落位 → 无容量限制。
  return std::numeric_limits<int>::max();  // 虚/敌气/惑无上限
}

// 容量感知的「取牌上结晶并按 decay_to 归置」统一入口：放不下的留在牌上。
int Engine::drain_to_decay(int inst, int n, int mode) {
  if (inst < 0 || n <= 0) return 0;
  int take = std::min(n, decay_free_capacity(inst));
  if (take <= 0) return 0;
  int sak = 0;
  int got = take_card_crystals(inst, take, mode, &sak);
  if (sak > 0) decay_crystals(inst, sak);
  return got;
}

void Engine::empty_card(int inst) {
  if (inst < 0 || card_crystal_count(inst) <= 0) return;
  drain_to_decay(inst, card_crystal_count(inst), kTakeOwn);
  drop_enhance_if_empty(inst);  // 目的地满时结晶留在牌上，保持展开
}

void Engine::remove_all_normals(Player p) {
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    if (st.insts[static_cast<size_t>(i)].holder != p) continue;
    if (def_of(i).kind != CardKind::Normal) continue;
    if (st.insts[static_cast<size_t>(i)].zone == Zone::Removed) continue;
    if (vec_has(ps(p).barracks, i)) continue;  // 18-Mizuki: 兵舍士兵不受此影响
    if (card_crystal_count(i) > 0)  // 献 leave the card before it does
      drain_to_decay(i, card_crystal_count(i), kTakeNormal);
    move_card(i, Zone::Removed);
  }
}

int Engine::card_crystal_count(int inst) const {
  if (inst < 0) return 0;
  return ci(inst).crystals + ci(inst).green;
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

// ---- 19-Megumi 机制已迁至 engine/mechanics/soil.cpp ----

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

bool Engine::has_misora(Player p) const { return has_mech(p, MC_Aim); }

// ---- 24-Shisui 桑畑志水 -----------------------------------------------------

bool Engine::has_shisui(Player p) const { return has_mech(p, MC_Wound); }

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

bool Engine::has_akina(Player p) const { return has_mech(p, MC_Market); }





// 投资的资金来源区域：股价 1 -> 虚 / 2 -> 自装 / 3 -> 自气 / 4 -> 自命。










bool Engine::form_matches(const CardDef& d, const std::string& f) const {
  if (d.form == f) return true;
  return std::find(d.forms.begin(), d.forms.end(), f) != d.forms.end();
}

// ---- 26-Innealra 机制已迁至 engine/mechanics/fate.cpp ----





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
  return drain_to_decay(inst, n, kTakeNormal);  // 容量感知：放不下的留在牌上
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

// ---- 20-Kanawe 机制已迁至 engine/mechanics/drama.cpp ----

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

// 基本动作的中文名（选项标签用；data.basic 仍为英文键）。
const char* basic_cn(BasicAction a) {
  switch (a) {
    case BasicAction::Advance: return "前进";
    case BasicAction::Retreat: return "后退";
    case BasicAction::Aura:    return "装附";
    case BasicAction::Flare:   return "聚气";
    case BasicAction::Escape:  return "离脱";
  }
  return "?";
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
