// 16-Yatsuha 八叶: 镜映 / 完全态 / 回忆区 / 变貌(伞/簪)
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

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

void Engine::switch_weapon(Player p, int source) {
  if (!ps(p).yukihi) return;
  ps(p).umbrella = !ps(p).umbrella;
  fire("weapon_switched", p, nullptr, source, false);  // 即再起 via the event bus
}

namespace mechanics {

void memory_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["mirror"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->mirror(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["can_upgrade"] = [](LuaCtx& c, int inst) { return c.e->can_upgrade(inst); };
  ctx["upgrade_card"] = [](LuaCtx& c, int inst) { return c.e->upgrade_card(inst); };
  ctx["memory_size"] = [](LuaCtx& c, int p) {
    return c.e->memory_size(static_cast<Player>(p));
  };
  ctx["memory_of"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    std::vector<int> v = c.e->ps(static_cast<Player>(p)).memory;
    sol::table t = sol::table::create(ts.L);
    for (size_t i = 0; i < v.size(); ++i) t[i + 1] = v[i];
    return sol::make_object(ts.L, t);
  };
  ctx["to_memory"] = [](LuaCtx& c, int inst) { c.e->to_memory(inst); };
  ctx["all_normals_to_memory"] = [](LuaCtx& c, int p, sol::optional<int> except) {
    c.e->all_normals_to_memory(static_cast<Player>(p), except ? *except : -1);
  };
  ctx["memory_draw"] = [](LuaCtx& c, int p, int n) {
    return c.e->memory_draw(static_cast<Player>(p), n);
  };
  ctx["count_complete"] = [](LuaCtx& c, int p) {
    return c.e->count_complete(static_cast<Player>(p));
  };
  ctx["umbrella"] = [](LuaCtx& c, int p) { return c.e->umbrella(static_cast<Player>(p)); };
  ctx["switch_weapon"] = [](LuaCtx& c) { c.e->switch_weapon(c.who, c.source); };
  ctx["shared_range"] = [](LuaCtx& c, int p) { return c.e->shared_range(static_cast<Player>(p)); };

}

}  // namespace mechanics
}  // namespace fy
