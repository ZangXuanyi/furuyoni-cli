// 12-Raira 蕾拉: 风雷槽（风神/雷神）
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

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

namespace mechanics {

void slots_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["raira_can"] = [](LuaCtx& c, std::string k, int t) { return c.e->raira_can(c.who, k, t); };
  ctx["raira_spend"] = [](LuaCtx& c, std::string k, int t) { return c.e->raira_spend(c.who, k, t); };
  ctx["raira_restrict"] = [](LuaCtx& c, int p) {
    c.e->raira_gain_restrict(static_cast<Player>(p));
  };
  ctx["raira_perm_cut"] = [](LuaCtx& c, int p) {
    c.e->raira_perm_cut(static_cast<Player>(p));
  };
  ctx["wind"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).wind; };
  ctx["thunder"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).thunder; };
  ctx["raira_gain"] = [](LuaCtx& c, int p, sol::object slot) {
    Player pl = static_cast<Player>(p);
    // Accept "wind"/"thunder" (preferred) or the legacy slot number (0=风, 1=雷).
    bool thunder = false;
    if (slot.is<std::string>()) thunder = slot.as<std::string>() == "thunder";
    else if (slot.is<int>()) thunder = slot.as<int>() == 1;
    if (thunder) {
      if (c.e->ps(pl).thunder < 20) c.e->ps(pl).thunder += 1;
    } else {
      if (c.e->ps(pl).wind < 20) c.e->ps(pl).wind += 1;
    }
  };
  ctx["set_slot"] = [](LuaCtx& c, int p, std::string k, int v) {
    Player pl = static_cast<Player>(p);
    if (v < 0) v = 0;
    if (v > 20) v = 20;
    if (k == "wind")
      c.e->ps(pl).wind = v;
    else
      c.e->ps(pl).thunder = v;
  };

}

}  // namespace mechanics
}  // namespace fy
