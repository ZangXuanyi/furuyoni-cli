// 23-Akina 源上安琪娜: 资本 / 股市 / 股价 / 投资 / 套现 / 算法
// 股市在统一 Token 模型中是每玩家的 Market 区域（樱花结晶）；框架与数据全在
// C++（what.md 第 2 条），ctx 方法块经 mechanics/blocks.cpp 注册。
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

static AreaRef invest_source(int stock, Player p) {
  switch (stock) {
    case 2: return AreaRef::aura(p);
    case 3: return AreaRef::flare(p);
    case 4: return AreaRef::life(p);
    default: return AreaRef::dust();
  }
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
  move(AreaRef::market(p), AreaRef::dust(), 1, false);
  // 2) 按股价把一片结晶移入自装（不足则尽量多，即 0 片）。
  switch (stock) {
    case 1: move(AreaRef::dust(), AreaRef::aura(p), 1, false); break;
    case 2: move(AreaRef::aura(o), AreaRef::aura(p), 1, false); break;
    case 3: move(AreaRef::flare(o), AreaRef::aura(p), 1, false); break;
    default: move(AreaRef::life(o), AreaRef::aura(p), 1, false); break;
  }
  // 3) 股价 -2。
  add_stock(p, -2);
  ps(p).cashOutThisTurn = true;
  check_win();  // 股价 4 时从敌命移入自装可能使对手命归零
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
  move(src, AreaRef::market(p), 1, false);
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
    move(AreaRef::aura(p), AreaRef::flare(p), 1, false);
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

namespace mechanics {

void market_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // ---- 23-Akina 源上安琪娜: 资本 / 股市 / 股价 / 投资 / 套现 / 算法 -------------
  ctx["market"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->ps(p ? static_cast<Player>(*p) : c.who).market;
  };
  ctx["stock"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->stock_price(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["capital"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->capital(p ? static_cast<Player>(*p) : c.who);
  };
  // 套现一次（股市不足 1 个结晶时什么也不做）。返回是否执行。
  ctx["cash_out"] = [](LuaCtx& c, sol::optional<int> p) -> bool {
    const Player who = p ? static_cast<Player>(*p) : c.who;
    if (!c.e->can_cash_out(who)) return false;
    c.e->cash_out(who);
    return true;
  };
  // 投资一次（没有可翻的投资券或对应区域不足时什么也不做）。返回是否执行。
  ctx["invest"] = [](LuaCtx& c, sol::optional<int> p) -> bool {
    return c.e->invest(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["invest_available"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->invest_available(p ? static_cast<Player>(*p) : c.who);
  };
  // O-N5 算法: 本回合内所有攻击获得距离扩大（近1）与距离缩小（远1）。
  ctx["set_algorithm"] = [](LuaCtx& c) { c.e->ps(c.who).algorithmThisTurn = true; };
  ctx["algorithm"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->ps(p ? static_cast<Player>(*p) : c.who).algorithmThisTurn;
  };

}

}  // namespace mechanics
}  // namespace fy
