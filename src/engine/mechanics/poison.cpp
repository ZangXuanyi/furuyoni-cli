// 09-Chikage 千景: 毒袋 / 毒牌归属 / 浮毒
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

void Engine::place_poison(int inst, Player holder, Zone z) {
  // Remove the card from the *previous* owner's zone list before changing
  // ownership: move_card erases from the list of the current owner.
  if (ci(inst).owner != holder) {
    move_card(inst, Zone::Limbo);
    ci(inst).owner = holder;
  }
  move_card(inst, z);
}

void Engine::return_poison(int inst) {
  if (ci(inst).bagOwner < 0) return;
  Player bag = static_cast<Player>(ci(inst).bagOwner);
  if (ci(inst).owner != bag) {
    move_card(inst, Zone::Limbo);
    ci(inst).owner = bag;
  }
  move_card(inst, Zone::Bag);
}

void Engine::force_move(int inst, Zone z) {
  bool saved = poisonForce_;
  poisonForce_ = true;
  move_card(inst, z);
  poisonForce_ = saved;
}

void Engine::float_poisons(Player p) {
  std::vector<int> pois, rest;
  for (int inst : ps(p).deck) {
    if (def_of(inst).isPoison)
      pois.push_back(inst);
    else
      rest.push_back(inst);
  }
  if (pois.empty()) return;
  std::vector<int> out = rest;
  out.insert(out.end(), pois.begin(), pois.end());  // poisons at the back == top
  ps(p).deck = std::move(out);
}

namespace mechanics {

void poison_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  ctx["is_poison"] = [](LuaCtx& c, int inst) { return c.e->is_poison(inst); };
  ctx["poison_bag"] = [](LuaCtx& c, int p) { return c.e->poison_bag(static_cast<Player>(p)); };

}

}  // namespace mechanics
}  // namespace fy
