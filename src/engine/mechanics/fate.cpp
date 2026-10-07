// 26-Innealra 诺伦: 三把枪 / 命运槽 / 共鸣 / 纠葛 / 惑
// 惑（Waku）是每玩家的 Waku 区域（樱花结晶）；命运是隐藏 CardDef（fate/fate_slot），
// 槽位轮转与共鸣流程在此；命运牌的牌面效果仍以 Lua on_fate 表达。
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

bool Engine::has_innealra(Player p) const { return has_mech(p, MC_Fate); }

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
  // 形态相关配置（O→0 / A1→1 / A2→2），非运行期女神探测。
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

namespace mechanics {

void fate_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // ---- 26-Innealra 诺伦: 惑 / 命运槽 / 共鸣 / 纠葛 --------------------------
  ctx["waku"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->ps(p ? static_cast<Player>(*p) : c.who).waku;
  };
  ctx["fate_slot"] = [](LuaCtx& c, int i) { return c.e->fate_slot(c.who, i); };
  ctx["fate_pos"] = [](LuaCtx& c, std::string name) { return c.e->fate_pos(c.who, name); };
  ctx["resolve_fate_slot"] = [](LuaCtx& c, int i, sol::optional<bool> fts) {
    c.e->resolve_fate_slot(c.who, i, fts.value_or(false));
  };
  ctx["resonance"] = [](LuaCtx& c, sol::optional<bool> fts) {
    c.e->resonance(c.who, fts.value_or(false));
  };
  ctx["rotate_fates"] = [](LuaCtx& c) { c.e->rotate_fates(c.who); };
  ctx["entangle_fates"] = [](LuaCtx& c, bool v) { c.e->entangle_fates(c.who, v); };
  ctx["fates_entangled"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->fates_entangled(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["resonance_count"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->resonance_count(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["used_non_innealra"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->used_non_innealra(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["used_normal_this_turn"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->ps(p ? static_cast<Player>(*p) : c.who).usedNormalThisTurn;
  };
  ctx["fate_resolving_slot"] = [](LuaCtx& c) { return c.e->fate_resolving_slot(); };
  ctx["fate_from_turn_start"] = [](LuaCtx& c) { return c.e->fate_from_turn_start(); };
  ctx["cost_paid"] = [](LuaCtx& c) { return c.e->cost_paid(c.source); };
  ctx["cost_to_waku"] = [](LuaCtx& c) { return c.e->cost_to_waku(c.who, c.source); };
  ctx["last_attack_responded"] = [](LuaCtx& c) { return c.e->last_attack_responded(); };
  ctx["set_cannot_use_normals"] = [](LuaCtx& c, int p) {
    c.e->ps(static_cast<Player>(p)).cannotUseNormals = true;
  };
  ctx["cannot_use_normals"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).cannotUseNormals;
  };
  ctx["set_cannot_retreat"] = [](LuaCtx& c, int p) {
    c.e->ps(static_cast<Player>(p)).cannotRetreat = true;
  };
  ctx["set_rebuild_freeze"] = [](LuaCtx& c, int p) {
    c.e->set_rebuild_freeze(static_cast<Player>(p));
  };
  ctx["fragile_will_active"] = [](LuaCtx& c, int p) {
    return c.e->fragile_will_host(static_cast<Player>(p)) >= 0;
  };

}

}  // namespace mechanics
}  // namespace fy
