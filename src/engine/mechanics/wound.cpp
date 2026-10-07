// 24-Shisui 桑畑志水: 裂伤（Wound token：(区域, 施加者)，置入经 token_move）
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

int Engine::wound_count(Player target, int area, int source) const {
  if (area < 0 || area > 2) return 0;
  if (source < 0) return ps(target).wound[area][0] + ps(target).wound[area][1];
  return ps(target).wound[area][source];
}

void Engine::check_immediate_resets(Player p) {
  std::vector<int> specials = ps(p).special;
  for (int inst : specials) {
    if (st.over) return;
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    ResetInfo ri = effects_->reset_info(d.id);
    if (ri.kind != 2 || !ri.hasCond) continue;
    // 事件驱动的即再起（reset.on）由事件总线负责；单次命伤阈值由 on_life_loss 负责。
    if (!ri.trigger.empty() || ri.lifeThreshold >= 0) continue;
    if (effects_->eval_reset_cond(*this, d.id, p, inst)) reset_special(inst);
  }
}

void Engine::note_damage_taken(Player target, Player source, int area) {
  ps(target).damageTakenThisTurn += 1;
  check_immediate_resets(target);
  // 19-Megumi 假想树 3-1 / 3-3: 「在你的回合中，第一次对对手的命/装造成伤害」。
  if (st.over || source == target) return;
  if (st.active != source) return;
  if (area == kWoundLife) {
    const bool first = enemyLifeDamageFired_[source] == 0;
    enemyLifeDamageFired_[source] += 1;
    fire("enemy_life_damaged", source, nullptr, -1, first);
  } else if (area == kWoundAura) {
    const bool first = enemyAuraDamageFired_[source] == 0;
    enemyAuraDamageFired_[source] += 1;
    fire("enemy_aura_damaged", source, nullptr, -1, first);
  }
}

int Engine::resolve_wound_group(Player target, int area, Player source) {
  if (area < 0 || area > 2) return 0;
  const int si = static_cast<int>(source);
  const int n = ps(target).wound[area][si];
  if (n <= 0) return 0;
  token_adjust(AreaRef::wound(static_cast<AreaKind>(area == kWoundAura    ? AreaKind::Aura
                                                     : area == kWoundFlare ? AreaKind::Flare
                                                                           : AreaKind::Life),
                               target, source),
               Token::Wound, -n);
  int moved = 0;
  // 裂伤伤害化本身是一次伤害（不是来自攻击）：更新 last_damage_* 供即再起判定，
  // 避免读到上一次伤害留下的陈旧值。
  lastDmgFromAttack_ = false;
  lastDmgSide_ = area == kWoundAura ? 1 : (area == kWoundLife ? 2 : 0);
  switch (area) {
    case kWoundAura: {
      auraDamagedThisTurn_[target] = true;
      moved = move(AreaRef::aura(target), AreaRef::dust(), n, false);
      break;
    }
    case kWoundFlare:
      moved = move(AreaRef::flare(target), AreaRef::dust(), n, false);
      break;
    default: {  // kWoundLife: 命 → 气（按命伤处理：破绽 / 即再起 / 死亡检查）
      const int before = ps(target).life;
      damage_life(target, n, AreaKind::Flare, true);
      moved = before - ps(target).life;
      break;
    }
  }
  lastDmgAmount_ = moved;
  // 「只有裂伤伤害化的时候才算造成了伤害」——实际移动了结晶才算一次伤害。
  if (moved > 0) note_damage_taken(target, source, area);
  else check_immediate_resets(target);
  return n;
}

void Engine::resolve_wound_area(Player target, int area) {
  // 同一区域内的两个来源各自合并为 1 次伤害（来源顺序按 P0、P1 固定）。
  for (int s = 0; s < 2 && !st.over; ++s) resolve_wound_group(target, area, static_cast<Player>(s));
}

namespace {
// 「自装 / 敌装」等便于玩家选择的区域名。
std::string wound_area_label(Player viewer, Player target, int area) {
  const char* a = area == kWoundAura ? "装" : (area == kWoundFlare ? "气" : "命");
  return std::string(target == viewer ? "自" : "敌") + a;
}
}  // namespace

void Engine::resolve_all_wounds(Player active) {
  // 每个来源玩家自行决定「自己的裂伤」在各区域之间的结算顺序；来源之间按
  // 当前回合玩家优先（规则未规定，取确定顺序）。
  for (int si = 0; si < 2 && !st.over; ++si) {
    const Player src = (si == 0) ? active : opp(active);
    struct Group {
      Player target;
      int area;
    };
    std::vector<Group> groups;
    for (int ti = 0; ti < 2; ++ti) {
      const Player t = static_cast<Player>(ti);
      for (int a = 0; a < 3; ++a)
        if (ps(t).wound[a][static_cast<int>(src)] > 0) groups.push_back({t, a});
    }
    while (!groups.empty() && !st.over) {
      size_t pick = 0;
      if (groups.size() > 1) {
        Request r;
        r.kind = "option";
        r.prompt = "裂伤结算顺序：选择下一个结算的区域";
        for (const Group& g : groups)
          r.options.push_back({wound_area_label(src, g.target, g.area), true, {}});
        const int idx = ask_one(src, std::move(r));
        if (idx >= 0 && idx < static_cast<int>(groups.size()))
          pick = static_cast<size_t>(idx);
      }
      resolve_wound_group(groups[pick].target, groups[pick].area, src);
      groups.erase(groups.begin() + static_cast<long>(pick));
    }
  }
}

void Engine::add_wound(Player target, int area, int n, Player source) {
  if (n <= 0 || area < 0 || area > 2 || st.over) return;
  MoveReq m;
  m.from = AreaRef::external();
  m.to = AreaRef::wound(static_cast<AreaKind>(area == kWoundAura    ? AreaKind::Aura
                                              : area == kWoundFlare ? AreaKind::Flare
                                                                    : AreaKind::Life),
                        target, source);
  m.fromKind = m.toKind = Token::Wound;
  m.n = n;
  token_move(m);  // 命区超限立即伤害化的规则在 token_move 的裂伤分支内
}

void Engine::apply_wound_attack_damage(Player target, Player source, std::optional<int> aura,
                                       std::optional<int> life, int chooser) {
  // 承伤可用性比较**实际装结晶数**（裂伤不占位置，装中的裂伤不影响判定）。
  const bool canAura = aura.has_value() && ps(target).aura >= *aura;
  int side = 0;
  if (aura && life) {
    if (canAura) {
      Request r;
      r.kind = "damage";
      r.prompt = "choose how to take the wound";
      Option oa;
      oa.label = "take " + std::to_string(*aura) + " aura wound";
      oa.data = {{"side", "aura"}, {"amount", *aura}};
      Option ol;
      ol.label = "take " + std::to_string(*life) + " life wound";
      ol.data = {{"side", "life"}, {"amount", *life}};
      r.options = {oa, ol};
      const Player decider = chooser >= 0 ? static_cast<Player>(chooser) : target;
      side = ask_one(decider, std::move(r)) == 0 ? 1 : 2;
    } else {
      side = 2;  // 实际装 < X → 只能吃 Y 命裂伤
    }
  } else if (aura) {
    side = 1;
  } else if (life) {
    side = 2;
  }
  // 「受到裂伤时不移动装结晶」；承伤侧的选择对「攻击后：若对手选择用命承伤」可见
  // （last_attack_side），但裂伤不是伤害，因此不动 last_damage_*。
  lastDmgSide_ = 0;
  lastDmgAmount_ = 0;
  lastDmgFromAttack_ = true;
  lastAtkSide_ = side;
  lastAtkAmount_ = side == 1 ? (aura ? *aura : 0) : (life ? *life : 0);
  if (side == 1)
    add_wound(target, kWoundAura, *aura, source);
  else if (side == 2)
    add_wound(target, kWoundLife, *life, source);
}
namespace mechanics {

void wound_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // ---- 24-Shisui 桑畑志水: 裂伤 ---------------------------------------------
  // 向 target 的 "aura"/"flare"/"life" 放置 n 个裂伤指示物（source 默认 c.who）。
  ctx["wound"] = [](LuaCtx& c, int target, std::string area, int n, sol::optional<int> source) {
    const int a = wound_area_of(area);
    if (a < 0) return;
    const Player src = source ? static_cast<Player>(*source) : c.who;
    c.e->add_wound(static_cast<Player>(target), a, n, src);
  };
  // 该区域中由 source（省略 = 双方合计）造成的裂伤数。
  ctx["wound_count"] = [](LuaCtx& c, int target, std::string area, sol::optional<int> source) {
    const int a = wound_area_of(area);
    if (a < 0) return 0;
    return c.e->wound_count(static_cast<Player>(target), a, source ? *source : -1);
  };
  // 把双方场上所有裂伤指示物伤害化（owner 决定自己那些裂伤的结算顺序）。
  ctx["resolve_wounds"] = [](LuaCtx& c, sol::optional<int> owner) {
    c.e->resolve_all_wounds(owner ? static_cast<Player>(*owner) : c.who);
  };
  // 只把 target 的某个区域内的裂伤指示物伤害化（O-S1）。
  ctx["resolve_wound"] = [](LuaCtx& c, int target, std::string area) {
    const int a = wound_area_of(area);
    if (a < 0) return;
    c.e->resolve_wound_area(static_cast<Player>(target), a);
  };
  // 攻击裂伤化: 该攻击的 X/Y 伤害变为 {X/Y} 裂伤（O-S3）。
  ctx["wound_attack"] = [](LuaCtx& c, LuaAttack& atk) {
    (void)c;
    if (atk.a) atk.a->wound = true;
  };
  ctx["damage_taken_this_turn"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->damage_taken_this_turn(p ? static_cast<Player>(*p) : c.who);
  };
  ctx["no_death"] = [](LuaCtx& c, int p) { return c.e->no_death(static_cast<Player>(p)); };

}

}  // namespace mechanics
}  // namespace fy
