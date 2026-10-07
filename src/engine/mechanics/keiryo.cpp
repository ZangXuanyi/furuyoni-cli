// 07-Shinra 心良: 策略(神算/鬼谋) / 封印（计略效果求值在 EffectHost）
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

void Engine::prepare_strategy(Player p) {
  Request r;
  r.kind = "option";
  r.prompt = "秘密准备下一个计策";
  r.options.push_back({"神算", true, {}});
  r.options.push_back({"鬼谋", true, {}});
  int c = ask_one(p, std::move(r));
  ps(p).strategy = (c == 1) ? 1 : 0;
  ps(p).strategyKnown = false;
}

void Engine::seal_card(int host, int card) {
  if (card < 0 || def_of(card).unsealable) return;  // 炼成攻击: 不可封印
  move_card(card, Zone::Sealed);
  ci(card).sealedBy = host;
  ci(host).sealed.push_back(card);
}

void Engine::return_sealed(int host) {
  std::vector<int> list = ci(host).sealed;
  ci(host).sealed.clear();
  for (int c : list) {
    ci(c).sealedBy = -1;
    move_card(c, Zone::Discard);  // to its owner's discard
  }
}

namespace mechanics {

void keiryo_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["strategy"] = [](LuaCtx& c, int p) { return c.e->strategy(static_cast<Player>(p)); };
  ctx["prepare_strategy"] = [](LuaCtx& c, int p) {
    c.e->prepare_strategy(static_cast<Player>(p));
  };
  ctx["sealed_card"] = [](LuaCtx& c, int h) { return c.e->sealed_card(h); };

}

}  // namespace mechanics
}  // namespace fy
