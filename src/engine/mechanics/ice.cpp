// 15-Konuru 凝努: 冰晶（Ice token：占装容位、不承伤）
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

int Engine::freeze(Player p, int n, int cause) {
  MoveReq m;
  m.from = AreaRef::external();
  m.to = AreaRef::aura(p);
  m.fromKind = m.toKind = Token::Ice;
  m.n = n;
  m.cause = cause;
  return token_move(m);
}

namespace mechanics {

void ice_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["freeze"] = [](LuaCtx& c, int p, int n, sol::optional<int> cause) {
    return c.e->freeze(static_cast<Player>(p), n, cause ? *cause : -1);
  };
  ctx["thaw"] = [](LuaCtx& c, int p, int n) { c.e->thaw(static_cast<Player>(p), n); };
  ctx["ice"] = [](LuaCtx& c, int p) { return c.e->ice_count(static_cast<Player>(p)); };
  ctx["frozen"] = [](LuaCtx& c, int p) { return c.e->frozen(static_cast<Player>(p)); };

}

}  // namespace mechanics
}  // namespace fy
