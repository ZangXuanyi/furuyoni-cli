// 17-Hatsumi 初美: 航海 / 潜水 / 罗盘
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

void Engine::declare_dive(Player p, int kind) {
  if (kind != 1 && kind != 2) return;
  if (ps(p).dive != 0) return;  // 已处于潜水状态: 此次潜水替换为“什么都不做”
  ps(p).dive = kind;
}

void Engine::begin_tailwind(Player p) {
  ps(p).oppAttackedLastTurn = attacksThisTurn_[opp(p)] > 0;
  ps(p).tailwind = ps(p).forcedTailwind || !ps(p).oppAttackedLastTurn;
  ps(p).forcedTailwind = false;
}

bool Engine::reveal_dive(Player diver, bool byAttack) {
  const int k = ps(diver).dive;
  if (k == 0) return false;
  ps(diver).dive = 0;
  // 潜水前进: 本回合内距离/达人距离 -1；后退: +1（回合开始时重置）。
  const int delta = (k == 1) ? -1 : 1;
  add_temp_distance(diver, delta);
  add_temp_near_distance(diver, delta);
  return byAttack;
}

int Engine::compass_count(Player p) const {
  int n = 0;
  for (int inst : ps(p).enhance)
    if (def_of(inst).compass) n += 1;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).compass) n += 1;  // 切札付与同样有效
  return n;
}

void Engine::apply_compass(Attack& a) const {
  // 「你的攻击额外获得攻击距离5，对手的攻击失去攻击距离5」；多个罗盘相互抵消。
  const int net = compass_count(a.attacker) - compass_count(opp(a.attacker));
  if (net == 0) return;
  auto& spans = a.range.spans;
  auto has_five = [&]() {
    for (const auto& sp : spans)
      if (5 >= sp.first && 5 <= sp.second) return true;
    return false;
  };
  if (net > 0) {
    if (!has_five()) spans.push_back({5, 5});  // 追加离散值 5
    return;
  }
  // net < 0: 删除离散值 5（必要时把一个区间拆成两段）。
  std::vector<std::pair<int, int>> out;
  for (const auto& sp : spans) {
    if (5 < sp.first || 5 > sp.second) {
      out.push_back(sp);
    } else if (sp.first == 5 && sp.second == 5) {
      continue;
    } else if (sp.first == 5) {
      out.push_back({6, sp.second});
    } else if (sp.second == 5) {
      out.push_back({sp.first, 4});
    } else {
      out.push_back({sp.first, 4});
      out.push_back({6, sp.second});
    }
  }
  spans = std::move(out);
}

namespace mechanics {

void dive_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["tailwind"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->tailwind(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["dive"] = [](LuaCtx& c, int kind) { c.e->declare_dive(c.who, kind); };
  ctx["dive_state"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->dive_state(p ? static_cast<Player>(*p) : c.who);
  };

}

}  // namespace mechanics
}  // namespace fy
