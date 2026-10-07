// 11-Thallya 泰拉娅: 蒸汽引擎 / 气动 / 变形（蒸汽 = Steam token，移动经 token_move）
// 框架与数据全在 C++（what.md 第 2 条）；本文件包含引擎函数与该机制的 ctx
// 方法块（经 mechanics/blocks.cpp 注册）。
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

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
  // 变形 are numbered O-TR1..; a 变格 transform with the same slot replaces the O
  // one (A1-TR1 紧那罗 -> O-TR1 夜叉), while unreplaced O transforms stay
  // available (A1 keeps O-TR2 娜迦). Slot = num % 10 (801,802,803 / 811,813,814).
  std::map<int, int> chosen;
  for (const CardDef& d : defs)
    if (d.isTransform && d.goddess == "thallya" && d.form == "O") chosen[d.local % 10] = d.id;
  if (form != "O")
    for (const CardDef& d : defs)
      if (d.isTransform && d.goddess == "thallya" && d.form == form)
        chosen[d.local % 10] = d.id;
  for (const auto& [slot, id] : chosen) add_instance(id, p);  // Zone::Removed (追加区)
}

std::vector<int> Engine::transform_cards(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed && def_of(i).isTransform)
      out.push_back(i);
  return out;
}

int Engine::active_transform_inst(Player p) const {
  int d = ps(p).transformDef;
  if (d < 0) return -1;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i)
    if (st.insts[i].owner == p && st.insts[i].zone == Zone::Removed && st.insts[i].def == d)
      return i;
  return -1;
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

void Engine::reset_steam_at_turn_start(Player p) {
  // 气动 steam lasts until the owner's own next turn: only that player's
  // off-engine steam returns to the exhausted module.
  ps(p).steamExhausted += ps(p).steamOnDist + ps(p).steamOnCrystal;
  ps(p).steamOnDist = 0;
  ps(p).steamOnCrystal = 0;
  ashuraExtraUsed_[p] = false;
}

std::vector<int> Engine::active_transform_defs(Player p) const {
  std::vector<int> out;
  auto add = [&](int def) {
    if (def < 0) return;
    if (std::find(out.begin(), out.end(), def) == out.end()) out.push_back(def);
  };
  add(ps(p).transformDef);
  for (int inst : ps(p).enhance)
    if (def_of(inst).name == cards::kSokaiKaisou) add(sealed_card(inst) >= 0 ? ci(sealed_card(inst)).def : -1);
  return out;
}

bool Engine::can_burn(Player p, int x) const {
  // 燃烧X needs X counters that can actually move. Under 萨利亚的杰作 the source
  // is the exhausted module and a partial recovery is not allowed, so a burn card
  // can only be played when the full amount can be recovered.
  if (has_named_active(p, cards::kSariaNoKessaku)) return ps(p).steamExhausted >= x;
  return ps(p).steamEngine >= x;
}

bool Engine::transform_is(Player p, const std::string& name) const {
  return ps(p).transformDef >= 0 && def(ps(p).transformDef).name == name;
}

namespace mechanics {

void steam_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // Thallya
  ctx["can_burn"] = [](LuaCtx& c, int p, int x) { return c.e->can_burn(static_cast<Player>(p), x); };
  ctx["burn"] = [](LuaCtx& c, int p, sol::optional<int> x) {
    c.e->burn(static_cast<Player>(p), x ? *x : 1);
  };
  ctx["recover"] = [](LuaCtx& c, int p, int x) { c.e->recover(static_cast<Player>(p), x); };
  ctx["pneumatic"] = [](LuaCtx& c, int p) { c.e->pneumatic(static_cast<Player>(p)); };
  ctx["transform"] = [](LuaCtx& c, int p, std::string n) {
    c.e->transform(static_cast<Player>(p), n);
  };
  ctx["steam_engine"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).steamEngine;
  };
  ctx["steam_exhausted"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).steamExhausted;
  };
  ctx["transform_count"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).transformCount;
  };
  ctx["transform_choose"] = [](LuaCtx& c, int p) { c.e->transform_choose(static_cast<Player>(p)); };
  ctx["transform_cards"] = [](LuaCtx& c, int p) {
    return c.e->transform_cards(static_cast<Player>(p));
  };
  ctx["set_next_draw_one"] = [](LuaCtx& c, int p) {
    c.e->set_next_draw_one(static_cast<Player>(p));
  };
  ctx["current_transform"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    Player pl = static_cast<Player>(p);
    int td = c.e->ps(pl).transformDef;
    if (td < 0) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, c.e->def(td).name);
  };

}

}  // namespace mechanics
}  // namespace fy
