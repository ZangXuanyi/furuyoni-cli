// 21-Kamuwi 神威: 诅咒(禁忌) / 血飞沫 / 阡（denied-aura 与死亡保护查询）
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

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

namespace mechanics {

void curse_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["curse"] = [](LuaCtx& c, int p) { return c.e->curse(static_cast<Player>(p)); };
  ctx["add_curse"] = [](LuaCtx& c, int p, int n) {
    c.e->add_curse(static_cast<Player>(p), n);
  };
  ctx["set_extra_attack_cost"] = [](LuaCtx& c, int p, std::string goddess) {
    c.e->ps(static_cast<Player>(p)).extraAttackCostGoddess = goddess;
  };

}

}  // namespace mechanics
}  // namespace fy
