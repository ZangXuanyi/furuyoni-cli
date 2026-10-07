// 19-Megumi 泷河希: 耕种 / 土壤 / 假想树
// 种子/植株是 Soil 区的 Seed/Plant token，牌上绿色结晶是 Green token，流转均经
// token_move（see engine/tokens.cpp 的异樱分支）。框架与数据全在 C++（what.md
// 第 2 条），ctx 方法块经 mechanics/blocks.cpp 注册。
// 通用「牌上结晶移除」策略（card_crystal_count/take_card_crystals）留在
// engine.cpp——它们是跨机制的结算切点。
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"
#include "engine/effect_ctx.hpp"

namespace fy {
using namespace detail;  // NOLINT

// ---------------------------------------------------------------------------
// 19-Megumi 泷河希: 耕种 / 土壤 / 假想树
// ---------------------------------------------------------------------------

void Engine::init_soil(Player p) {
  PlayerState& s = ps(p);
  s.hasSoil = true;
  s.soilSeeds = 5;  // 开局：5 个绿色结晶放在「种子」
  s.soilPlants = 0;
  s.tree.assign(6, 0);
  s.treeActive = false;
  s.nextGrowth = 0;
}


int Engine::green_total(Player p) const {
  int t = ps(p).soilSeeds + ps(p).soilPlants;
  for (int v : ps(p).tree) t += v;
  for (const CardInstance& c : st.insts)
    if (c.holder == p) t += c.green;
  return t;
}

int Engine::total_green_on_enhances(Player p) const {
  int t = 0;
  for (int inst : ps(p).enhance) t += ci(inst).green;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp) t += ci(inst).green;
  return t;
}

int Engine::green_zones(Player p) const {
  int z = 0;
  if (ps(p).soilSeeds + ps(p).soilPlants > 0) z += 1;
  bool onCards = false;
  for (int inst : ps(p).enhance)
    if (ci(inst).green > 0) onCards = true;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && ci(inst).green > 0) onCards = true;
  if (onCards) z += 1;
  int tree = 0;
  for (int v : ps(p).tree) tree += v;
  if (tree > 0) z += 1;
  return z;
}

int Engine::crystal_redirect_host(Player p, int except) const {
  auto active = [&](int inst) {
    if (ci(inst).crystals + ci(inst).green <= 0) return false;  // 展开中
    return def_of(inst).redirectCrystals;
  };
  for (int inst : ps(p).special)
    if (inst != except && ci(inst).faceUp && active(inst)) return inst;
  for (int inst : ps(p).enhance)
    if (inst != except && active(inst)) return inst;
  return -1;
}



void Engine::seed_to_plant(Player p, int n) {
  MoveReq m;
  m.from = m.to = AreaRef::soil(p);
  m.fromKind = Token::Seed;
  m.toKind = Token::Plant;
  m.n = n;
  token_move(m);
}

void Engine::cultivate(Player p) {
  if (!ps(p).hasSoil) return;
  seed_to_plant(p, 1);  // 「你将1个土壤中的绿色结晶移到植株上」
}

int Engine::growth_of(Player p, int inst) const {
  int g = def_of(inst).growth;
  if (g < 0) g = 0;
  // 脱粒: 本回合你的下一张非希的付与牌获得【生长2】。
  if (ps(p).nextGrowth > 0 && !card_has_goddess(inst, "megumi"))
    g = std::max(g, ps(p).nextGrowth);
  return g;
}

void Engine::grow_enhance(Player p, int inst) {
  if (!ps(p).hasSoil) return;
  const bool tempered = ps(p).nextGrowth > 0 && !card_has_goddess(inst, "megumi");
  int g = growth_of(p, inst);
  if (tempered) ps(p).nextGrowth = 0;  // 只影响“下一张”
  int avail = std::min(g, ps(p).soilPlants);
  if (avail <= 0) return;  // 每次都要询问，但无植株可移时不产生决策
  Request r;
  r.kind = "option";
  r.prompt = "生长" + std::to_string(g) + "：将至多 " + std::to_string(avail) + " 个植株移到「" +
             def_of(inst).name + "」上（可以不移动）";
  for (int k = 0; k <= avail; ++k)
    r.options.push_back({"移动 " + std::to_string(k) + " 个", true, {}});
  int pick = ask_one(p, std::move(r));
  if (pick < 0 || pick > avail) pick = 0;
  attach_green(inst, pick);
}

int Engine::attach_green(int inst, int n) {
  if (inst < 0 || n <= 0) return 0;
  MoveReq m;
  m.from = AreaRef::soil(ci(inst).holder);
  m.to = AreaRef::card(inst);
  m.fromKind = Token::Plant;
  m.toKind = Token::Green;
  m.n = n;
  return token_move(m);
}

int Engine::detach_green_to_seeds(int inst, int n) {
  if (inst < 0 || n <= 0) return 0;
  MoveReq m;
  m.from = AreaRef::card(inst);
  m.to = AreaRef::soil(ci(inst).holder);
  m.fromKind = Token::Green;
  m.toKind = Token::Seed;
  m.n = n;
  return token_move(m);
}

void Engine::green_home(Player p) {
  if (ps(p).treeActive && tree_place_one(p) >= 0) return;
  token_adjust(AreaRef::soil(p), Token::Seed, 1);
}

int Engine::tree_slot(Player p, int i) const {
  if (i < 0 || i >= static_cast<int>(ps(p).tree.size())) return 0;
  return ps(p).tree[static_cast<size_t>(i)];
}

int Engine::tree_occupied(Player p) const {
  int t = 0;
  for (int v : ps(p).tree)
    if (v > 0) t += 1;
  return t;
}

// 假想树: 1 格 + 2 格 + 3 格。放第 N+1 层的前置是第 N 层至少有 1 个（不必满）。
std::vector<int> Engine::tree_legal_slots(Player p) const {
  std::vector<int> out;
  if (ps(p).tree.size() < 6) return out;
  const std::vector<int>& t = ps(p).tree;
  const bool l1 = t[0] > 0;
  const bool l2 = t[1] > 0 || t[2] > 0;
  if (t[0] == 0) out.push_back(0);
  if (l1) {
    if (t[1] == 0) out.push_back(1);
    if (t[2] == 0) out.push_back(2);
  }
  if (l1 && l2)
    for (int i = 3; i < 6; ++i)
      if (t[i] == 0) out.push_back(i);
  return out;
}

int Engine::tree_place_one(Player p) {
  if (!ps(p).treeActive) return -1;
  std::vector<int> legal = tree_legal_slots(p);
  if (legal.empty()) return -1;
  static const char* kSlotName[6] = {"第1层",     "第2层·左", "第2层·右",
                                     "第3层·左", "第3层·中", "第3层·右"};
  int pick = legal[0];
  if (legal.size() > 1) {
    Request r;
    r.kind = "option";
    r.prompt = "假想树：选择放置种结晶的空格";
    for (int s : legal) r.options.push_back({kSlotName[s], true, {}});
    int idx = ask_one(p, std::move(r));
    if (idx >= 0 && idx < static_cast<int>(legal.size())) pick = legal[static_cast<size_t>(idx)];
  }
  ps(p).tree[static_cast<size_t>(pick)] = 1;
  return pick;
}

int Engine::tree_place_from_soil(Player p) {
  if (!ps(p).treeActive || ps(p).soilSeeds <= 0) return -1;
  int slot = tree_place_one(p);
  if (slot < 0) return -1;
  ps(p).soilSeeds -= 1;
  return slot;
}

void Engine::tree_fall(Player p, int n) {
  if (!ps(p).treeActive) return;
  for (int k = 0; k < n; ++k) {
    if (st.over) return;
    int slot = -1;
    for (int i = 0; i < static_cast<int>(ps(p).tree.size()); ++i)
      if (ps(p).tree[static_cast<size_t>(i)] > 0) {
        slot = i;  // 从下往上
        break;
      }
    if (slot < 0) return;
    ps(p).tree[static_cast<size_t>(slot)] = 0;
    ps(p).soilSeeds += 1;                             // 掉回土壤
    fire("tree_fell", p, nullptr, slot, false);       // 散华时（ev:card() == 格子序号）
  }
}

std::vector<int> Engine::unchosen_cuts(Player p) const {
  std::vector<int> out;
  for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
    const CardInstance& c = st.insts[static_cast<size_t>(i)];
    if (c.owner != p || c.zone != Zone::Removed) continue;
    const CardDef& d = def_of(i);
    if (d.kind != CardKind::Special) continue;
    if (d.isExtra || d.isPart || d.isPoison || d.isTransform || d.soldier) continue;
    if (d.type == CardType::Enhance) continue;       // 非付与
    if (d.flags & CF_FullPower) continue;            // 非全力
    out.push_back(i);
  }
  return out;
}

void Engine::tree_use_cut(Player p, int inst) {
  if (st.over || inst < 0 || ci(inst).zone != Zone::Removed) return;
  const CardDef d = def_of(inst);
  if (d.kind != CardKind::Special || d.type == CardType::Enhance) return;
  if (d.flags & CF_FullPower) return;
  move_card(inst, Zone::Special);
  ci(inst).faceUp = true;  // 公开使用
  ci(inst).usedThisTurn = true;
  ps(p).cardsPlayedTotal += 1;
  push_play_frame(d.id, p, false);
  PlayFrameGuard frameGuard{*this};
  resolve_card_effect(p, inst, false);
  remove_card(inst);  // 使用后将该切牌移出游戏
}
namespace mechanics {

void soil_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // ---- 19-Megumi 泷河希: 耕种 / 土壤 / 假想树 ------------------------------
  ctx["has_soil"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).hasSoil; };
  ctx["seeds"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).soilSeeds; };
  ctx["plants"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).soilPlants; };
  ctx["green"] = [](LuaCtx& c, int inst) { return c.e->green_of(inst); };
  ctx["card_crystal_count"] = [](LuaCtx& c, int inst) { return c.e->card_crystal_count(inst); };
  ctx["total_green_on_enhances"] = [](LuaCtx& c, int p) {
    return c.e->total_green_on_enhances(static_cast<Player>(p));
  };
  ctx["green_zones"] = [](LuaCtx& c, int p) { return c.e->green_zones(static_cast<Player>(p)); };
  ctx["green_total"] = [](LuaCtx& c, int p) { return c.e->green_total(static_cast<Player>(p)); };
  ctx["seed_to_plant"] = [](LuaCtx& c, int p, int n) {
    c.e->seed_to_plant(static_cast<Player>(p), n);
  };
  ctx["attach_green"] = [](LuaCtx& c, int inst, int n) { return c.e->attach_green(inst, n); };
  ctx["detach_green"] = [](LuaCtx& c, int inst, int n) {
    return c.e->detach_green_to_seeds(inst, n);
  };
  ctx["remove_card_crystals"] = [](LuaCtx& c, int inst, int n) {
    c.e->set_crystal_mover(c.who);
    int got = c.e->remove_card_crystals(inst, n);
    c.e->clear_crystal_mover();
    return got;
  };
  ctx["tree_active"] = [](LuaCtx& c, int p) { return c.e->tree_active(static_cast<Player>(p)); };
  ctx["tree_enter"] = [](LuaCtx& c, int p) {
    c.e->ps(static_cast<Player>(p)).treeActive = true;
  };
  ctx["enhance_active"] = [](LuaCtx& c, int inst) { return c.e->enhance_active(inst); };
  ctx["tree_slot"] = [](LuaCtx& c, int p, int i) {
    return c.e->tree_slot(static_cast<Player>(p), i);
  };
  ctx["tree_occupied"] = [](LuaCtx& c, int p) {
    return c.e->tree_occupied(static_cast<Player>(p));
  };
  ctx["tree_place"] = [](LuaCtx& c, int p) {
    return c.e->tree_place_from_soil(static_cast<Player>(p));
  };
  ctx["tree_fall"] = [](LuaCtx& c, int p, int n) {
    c.e->tree_fall(static_cast<Player>(p), n);
  };
  ctx["set_next_growth"] = [](LuaCtx& c, int p, int x) {
    c.e->set_next_growth(static_cast<Player>(p), x);
  };
  ctx["growth_of"] = [](LuaCtx& c, int inst) {
    return c.e->growth_of(static_cast<Player>(c.who), inst);
  };
  ctx["used_generated_attack"] = [](LuaCtx& c, int p) {
    return c.e->used_generated_attack(static_cast<Player>(p));
  };
  ctx["is_borrowed"] = [](LuaCtx& c, int inst) { return c.e->is_borrowed(inst); };
  ctx["card_owner"] = [](LuaCtx& c, int inst) { return c.e->card_owner(inst); };
  ctx["unchosen_cuts"] = [](LuaCtx& c, int p) {
    return c.e->unchosen_cuts(static_cast<Player>(p));
  };
  ctx["tree_use_cut"] = [](LuaCtx& c, int inst) {
    c.e->tree_use_cut(static_cast<Player>(c.who), inst);
  };

}

}  // namespace mechanics
}  // namespace fy
