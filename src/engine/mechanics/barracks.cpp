// 18-Mizuki 美月: 动员 / 兵舍 / 阵地 / 词条改写
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

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

namespace mechanics {

void barracks_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["position"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->position(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["mobilize"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->mobilize(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["is_soldier"] = [](LuaCtx& c, int inst) { return c.e->is_soldier(inst); };
  ctx["soldier_mobilized"] = [](LuaCtx& c, int inst) {
    return c.e->soldier_mobilized(inst);
  };
  ctx["barracks"] = [](LuaCtx& c, sol::optional<int> p, sol::this_state ts) -> sol::object {
    std::vector<int> v =
        c.e->ps(p ? static_cast<Player>(*p) : c.who).barracks;
    sol::table t = sol::table::create(ts.L);
    for (size_t i = 0; i < v.size(); ++i) t[i + 1] = v[i];
    return sol::make_object(ts.L, t);
  };
  ctx["barracks_count"] = [](LuaCtx& c, sol::optional<int> p) {
    return static_cast<int>(c.e->ps(p ? static_cast<Player>(*p) : c.who).barracks.size());
  };
  ctx["mobilized_count"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->barracks_mobilized_count(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["gain_soldier"] = [](LuaCtx& c, std::string name) {
    return c.e->gain_soldier(c.who, name);
  };
  ctx["hand_to_barracks"] = [](LuaCtx& c, int inst) {
    c.e->hand_to_barracks(c.who, inst);
  };

}

}  // namespace mechanics
}  // namespace fy
