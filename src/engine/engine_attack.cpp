#include "engine/engine.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/card_names.hpp"
#include "engine/effect_host.hpp"
#include "engine/engine_internal.hpp"

namespace fy {
using namespace detail;  // NOLINT

int Engine::effective_armor(Player p) const {
  int armor = ps(p).aura;
  bool ice_armor = false;
  // 19-Megumi: 「视作装」按牌上的结晶数计，绿色结晶视作樱花结晶。
  for (int inst : ps(p).enhance) {
    if (def_of(inst).armorFromCrystals) armor += card_crystal_count(inst);
    if (def_of(inst).iceAsArmor) ice_armor = true;
  }
  for (int inst : ps(p).special) {
    if (!ci(inst).faceUp) continue;
    if (def_of(inst).armorFromCrystals) armor += card_crystal_count(inst);
    if (def_of(inst).iceAsArmor) ice_armor = true;
  }
  if (ice_armor) armor += ps(p).ice;  // 冰凌包覆: 冰晶视作装
  return armor;
}

void Engine::spend_aura(Player target, int n) {
  if (n <= 0) return;
  auraDamagedThisTurn_[target] = true;  // 斩击乱舞: 本回合敌装受到过伤害
  int remaining = n;
  // "视作装"的卡上结晶优先消耗（移入虚）。19-Megumi: 先樱花、后绿色。
  auto drain = [&](int inst) {
    if (remaining <= 0) return;
    if (!def_of(inst).armorFromCrystals) return;
    if (ci(inst).zone == Zone::Special && !ci(inst).faceUp) return;  // unused 切札 has no aura
    int room = card_crystal_count(inst);
    int take = std::min(remaining, room);
    if (take <= 0) return;
    int sak = 0;
    int got = take_card_crystals(inst, take, kTakeNormal, &sak);
    if (got <= 0) return;
    token_adjust(AreaRef::dust(), Token::Sakura, sak);
    remaining -= got;
    drop_enhance_if_empty(inst);  // 无音壁/遗响壁: 献尽即离场
  };
  // Copies: draining can move a card out of the zone we are iterating.
  const std::vector<int> enh = ps(target).enhance;
  const std::vector<int> sp = ps(target).special;
  for (int inst : enh) drain(inst);
  for (int inst : sp) drain(inst);
  if (remaining > 0) move_crystals(AreaRef::aura(target), AreaRef::dust(), remaining, false);
}

void Engine::drop_enhance_if_empty(int inst) {
  if (card_crystal_count(inst) > 0) return;
  const CardDef& d = def_of(inst);
  const Player who = ci(inst).holder;  // controller while in play
  if (ci(inst).zone == Zone::Enhance) {
    move_card(inst, Zone::Discard);
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", who, inst);
    fire("enhance_left", who, nullptr, inst, false);  // 森罗判证
  }
  // A used 切札付与 stays in the special zone; enhance_active() ends its aura.
}

void Engine::check_win() {
  if (st.over) return;
  auto is_dead = [&](Player p) { return ps(p).life <= 0 || ps(p).curse >= 16; };
  bool dead[2] = {is_dead(P0), is_dead(P1)};
  // 24-Shisui 埋骨地: 本牌展开中，持有者不会死亡。
  for (int i = 0; i < 2; ++i)
    if (dead[i] && no_death(static_cast<Player>(i))) dead[i] = false;
  // 阡: 本牌弃置前，对手不会死亡。
  for (int i = 0; i < 2; ++i)
    if (dead[i] && protects_enemy(static_cast<Player>(i))) dead[i] = false;
  // 23-Akina O-S3 仙霄鬼泉天元术「当你死亡时」（在最后的结晶之前结算）。
  for (int i = 0; i < 2; ++i) {
    const Player p = static_cast<Player>(i);
    if (!dead[i]) continue;
    if (run_death_saves(p) && !is_dead(p)) dead[i] = false;
  }
  if (!dead[0] && !dead[1]) return;
  for (int i = 0; i < 2; ++i)
    if (dead[i] && try_revive(static_cast<Player>(i))) dead[i] = false;  // 最后的结晶
  if (dead[0] && dead[1]) {  // simultaneous death -> draw
    st.over = true;
    st.winner = -1;
    return;
  }
  st.over = true;
  if (dead[0]) {
    ps(P0).life = 0;
    st.winner = P1;
  } else {
    ps(P1).life = 0;
    st.winner = P0;
  }
}

bool Engine::try_revive(Player p) {
  if (ps(p).usedLastCrystal) return false;
  int card = -1;
  for (int inst : ps(p).special)
    if (!ci(inst).faceUp && def_of(inst).name == cards::kSaigoNoKesshou) card = inst;
  if (card < 0) return false;
  int cost = cut_cost(p, ci(card).def, card);
  if (ps(p).flare < cost) return false;
  Request r;
  r.kind = "option";
  r.prompt = "命归零：使用『最后的结晶』复活？";
  r.options.push_back({"使用（支付 " + std::to_string(cost) + " 气）", true, {}});
  r.options.push_back({"不使用", true, {}});
  if (ask_one(p, std::move(r)) != 0) return false;
  move_crystals(AreaRef::flare(p), AreaRef::dust(), cost, false);
  move_crystals(AreaRef::life(p), AreaRef::dust(), ps(p).life, false);  // all remaining life -> dust
  if (!ps(p).hand.empty()) {
    Request cr;
    cr.kind = "cards";
    cr.prompt = "盖伏 1 张手牌";
    for (int inst : ps(p).hand) {
      Option o;
      o.label = def_of(inst).name;
      o.data = {{"inst", inst}};
      cr.options.push_back(o);
    }
    cr.minSel = 1;
    cr.maxSel = 1;
    int k = ask_one(p, std::move(cr));
    k = std::min(k, static_cast<int>(ps(p).hand.size()) - 1);
    int ci2 = ps(p).hand[static_cast<size_t>(k)];
    move_card(ci2, Zone::Cover);
    ci(ci2).faceUp = false;
  }
  move_crystals(AreaRef::dust(), AreaRef::life(p), 1, false);
  ci(card).faceUp = true;
  ps(p).usedLastCrystal = true;
  return true;
}

void Engine::damage_life(Player p, int n, AreaKind to, bool triggerBreak, int toCard, int toOwner) {
  // 倒车 routes life damage to 距; rebuild-style life loss goes to 气; a few
  // effects send it to 虚.
  AreaRef dest = AreaRef::flare(p);
  if (toCard >= 0)
    dest = AreaRef::card(toCard);  // 在此旗
  else if (to == AreaKind::Dust)
    dest = AreaRef::dust();
  else if (to == AreaKind::Distance)
    dest = AreaRef::distance();
  else if (to == AreaKind::Waku)
    dest = AreaRef::waku(toOwner >= 0 ? static_cast<Player>(toOwner) : p);  // 26-Innealra 惑
  int moved = move_crystals(AreaRef::life(p), dest, n, false);
  ps(p).lastLifeLost = moved;
  if (moved > 0) on_life_loss(p, moved, triggerBreak);
  check_win();
}

void Engine::lose_life(Player p, int n, bool triggerBreak) {
  damage_life(p, n, AreaKind::Flare, triggerBreak);
}

void Engine::on_life_loss(Player p, int amount, bool triggerBreak) {
  if (triggerBreak) break_enhances(p);
  // immediate resets (即再起)
  std::vector<int> specials = ps(p).special;
  for (int inst : specials) {
    if (!ci(inst).faceUp) continue;
    const CardDef& d = def_of(inst);
    ResetInfo ri = effects_->reset_info(d.id);
    if (ri.kind != 2) continue;
    bool ok = false;
    if (ri.lifeThreshold >= 0)
      ok = amount >= ri.lifeThreshold;
    else if (ri.hasCond)
      ok = effects_->eval_reset_cond(*this, d.id, p, inst);
    if (ok) reset_special(inst);
  }
}

void Engine::break_enhances(Player p) {
  std::vector<int> targets;
  for (int inst : ps(p).enhance)
    if (def_of(inst).flags & CF_Break) targets.push_back(inst);
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).nagi >= 0 && (def_of(inst).flags & CF_Break))
      targets.push_back(inst);
  for (int inst : targets) {
    // 破绽: 献 all go to 虚（19-Megumi: 绿色结晶照常回种子/假想树）。
    int sak = 0;
    take_card_crystals(inst, card_crystal_count(inst), kTakeLeaving, &sak);
    st.dust += sak;
    // Face down, skip on_discard. A 切札 cannot enter the cover pile (切牌
    // never go to deck/discard/hand), so it returns to the special zone unused.
    if (def_of(inst).kind == CardKind::Special) {
      move_card(inst, Zone::Special);
      ci(inst).faceUp = false;
    } else {
      move_card(inst, Zone::Cover);
      ci(inst).faceUp = false;
    }
    fire("enhance_left", p, nullptr, inst, false);
  }
  clamp_aura(p);  // a card providing bonus aura may have left play
}

void Engine::consume_enhance_crystal(int inst) {
  CardInstance& c = ci(inst);
  if (card_crystal_count(inst) <= 0) return;
  const CardDef& d = def_of(inst);
  Player owner = c.holder;  // the controller, for on_discard / 弃置时
  // 19-Megumi: 先移除樱花结晶、再移除绿色结晶（绿色回种子/假想树）。
  int sak = 0;
  take_card_crystals(inst, 1, kTakeSustain, &sak);
  // a dropped 献 falls to 虚 by default; 圈域 sends it to 距 instead.
  if (sak > 0) decay_crystals(inst, sak);
  if (card_crystal_count(inst) > 0) return;
  if (d.kind == CardKind::Normal) {
    move_card(inst, Zone::Discard);
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
    fire("enhance_left", owner, nullptr, inst, false);  // 森罗判证
  } else {
    // 切札付与 also runs its 弃置时 when it leaves play.
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
    // 26-Innealra 万劫缠迫: 弃置时把自己移出游戏 —— 不能又被送回切牌区。
    if (ci(inst).zone == Zone::Removed) return;
    if (ci(inst).holder != ci(inst).owner) {  // borrowed: return to its owner
      move_card(inst, Zone::Limbo);
      ci(inst).holder = ci(inst).owner;
    }
    move_card(inst, Zone::Special);  // special enhance stays used in the special zone
  }
}

// ---------------------------------------------------------------------------
// attacks
// ---------------------------------------------------------------------------

Attack Engine::make_attack(Player p, int inst, bool asResponse, bool consumePending,
                           bool declareNow) {
  const CardDef& d = def_of(inst);
  Attack a;
  a.attacker = p;
  a.sourceInst = inst;
  a.fromSpecial = d.kind == CardKind::Special;
  a.fromResponse = asResponse;
  EvaluatedAttack ea = effects_->eval_attack(*this, d.id, p, inst, asResponse);
  a.range = ea.range;
  a.aura = ea.damage.aura;
  a.life = ea.damage.life;
  a.keywords = ea.keywords;
  a.wound = (ea.keywords & AF_Wound) != 0;  // 24-Shisui 裂伤攻击
  a.attackerChoosesDamage = ea.attackerChooses;
  a.terminal = ea.terminal;
  if (consumePending && forceUnrespondable_) {
    a.keywords |= AF_Unrespondable;
    forceUnrespondable_ = false;
  }
  effects_->finalize_attack(*this, p, a, consumePending);
  // 20-Kanawe 黄格: 站在黄色地点的玩家的非衍生攻击 +0/+1。
  apply_node_attack_bonus(p, a);
  if (consumePending) {
    a.damageToCard = pendingDamageToCard_;  // 在此旗
    // 潜水闪避的判定需要在攻击"计数/声明"之前完成，故允许延迟声明。
    if (declareNow) declare_attack(a);
  }
  pendingDamageToCard_ = -1;
  return a;
}

void Engine::declare_attack(Attack& a) {
  if (a.counted) return;
  a.counted = true;
  // 18-Mizuki O-N7: 本回合你回合内的第一张非切牌攻击（在 finalize 之后计数，
  // 使该攻击自身在结算时仍看到 0）。
  if (a.sourceInst >= 0 && st.active == a.attacker) {
    const CardDef& d = def_of(a.sourceInst);
    if (d.type == CardType::Attack && d.kind == CardKind::Normal)
      ps(a.attacker).normalAttacksThisTurn += 1;
  }
  int n = note_attack(a.attacker);
  fire("attack_declared", a.attacker, &a, -1, n == 1);
  check_dramas();  // 20-Kanawe《杀阵》
}

// ---------------------------------------------------------------------------
// 17-Hastumi: 潜水 / 罗盘
// ---------------------------------------------------------------------------

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

void Engine::apply_damage_to(Player target, std::optional<int> aura, std::optional<int> life,
                             uint32_t keywords, int sourceInst, int chooser, bool fromAttack,
                             const DamageRoute& route) {
  (void)sourceInst;
  std::optional<int> effA = aura, effL = life;
  if (effA && !(keywords & AF_Overwhelm)) *effA = std::min(*effA, 5);
  if (effA && *effA < 0) *effA = 0;
  if (effL && *effL < 0) *effL = 0;
  bool canAura = effA.has_value() && effective_armor(target) >= *effA;
  lastDmgFromAttack_ = fromAttack;
  if (has_damage_immunity(target)) {  // 夙愿: 你不会受到任何伤害
    lastDmgSide_ = 0;
    lastDmgAmount_ = 0;
    return;
  }
  // 路由快照（在此旗 / 惑）作为伤害结算的第一个原子操作读取。
  const int toCard = route.toCard;
  const bool toWaku = route.toWaku;
  const Player wakuOwner = route.wakuOwner;
  auto doAura = [&](int n) {
    // Record the chosen side *before* resolving: on_life_loss / 即再起
    // predicates read last_damage_side during the damage.
    lastDmgSide_ = 1;
    lastDmgAmount_ = n;
    if (toCard >= 0) {
      auraDamagedThisTurn_[target] = true;
      int remaining = n;
      const std::vector<int> enh = ps(target).enhance;
      const std::vector<int> sp = ps(target).special;
      auto drain = [&](int inst) {
        if (remaining <= 0 || !def_of(inst).armorFromCrystals) return;
        if (ci(inst).zone == Zone::Special && !ci(inst).faceUp) return;
        int take = std::min(remaining, ci(inst).crystals);
        if (take <= 0) return;
        ci(inst).crystals -= take;
        add_crystals(AreaRef::card(toCard), take);
        remaining -= take;
        drop_enhance_if_empty(inst);
      };
      for (int inst : enh) drain(inst);
      for (int inst : sp) drain(inst);
      if (remaining > 0)
        move_crystals(AreaRef::aura(target), AreaRef::card(toCard), remaining, false);
    } else if (toWaku) {
      // 视作装的卡上结晶优先耗尽（与 spend_aura 一致），但一律进入攻击者的惑。
      auraDamagedThisTurn_[target] = true;
      int remaining = n;
      auto drain = [&](int inst) {
        if (remaining <= 0 || !def_of(inst).armorFromCrystals) return;
        if (ci(inst).zone == Zone::Special && !ci(inst).faceUp) return;
        int take = std::min(remaining, card_crystal_count(inst));
        if (take <= 0) return;
        int sak = 0;
        int got = take_card_crystals(inst, take, kTakeNormal, &sak);
        if (got <= 0) return;
        if (sak > 0) add_crystals(AreaRef::waku(wakuOwner), sak);
        remaining -= got;
        drop_enhance_if_empty(inst);
      };
      const std::vector<int> enh2 = ps(target).enhance;
      const std::vector<int> sp2 = ps(target).special;
      for (int inst : enh2) drain(inst);
      for (int inst : sp2) drain(inst);
      if (remaining > 0)
        move_crystals(AreaRef::aura(target), AreaRef::waku(wakuOwner), remaining, false);
    } else if (route.toDistance || route.auraToDistance) {
      move_crystals(AreaRef::aura(target), AreaRef::distance(), n, false);
    } else {
      spend_aura(target, n);
    }
  };
  auto doLife = [&](int n) {
    lastDmgSide_ = 2;  // visible to 即再起 predicates during on_life_loss
    int before = ps(target).life;
    // 惑(26) > 倒车(距) > 强酸(虚) > 通常(敌气)
    AreaKind to = toWaku ? AreaKind::Waku
                         : (route.toDistance ? AreaKind::Distance
                                             : (route.toDust ? AreaKind::Dust : AreaKind::Flare));
    damage_life(target, n, to, true, toCard, toWaku ? static_cast<int>(wakuOwner) : -1);
    lastDmgAmount_ = before - ps(target).life;
  };

  if (keywords & AF_BothSides) {
    // 两侧伤害: resolve the aura side (like X/-) and the life side (like -/Y).
    if (effA) doAura(std::min(*effA, effective_armor(target)));
    if (effL) doLife(*effL);
  } else if (effA && effL && canAura) {
    Request r;
    r.kind = "damage";
    r.prompt = "choose how to take damage";
    Option oa;
    oa.label = "take " + std::to_string(*effA) + " aura damage";
    oa.data = {{"side", "aura"}, {"amount", *effA}};
    Option ol;
    ol.label = "take " + std::to_string(*effL) + " life damage";
    ol.data = {{"side", "life"}, {"amount", *effL}};
    r.options = {oa, ol};
    Player decider = chooser >= 0 ? static_cast<Player>(chooser) : target;
    int idx = ask_one(decider, std::move(r));
    if (idx == 0)
      doAura(*effA);
    else
      doLife(*effL);
  } else if (effA && !effL) {
    doAura(std::min(*effA, effective_armor(target)));
  } else if (effL) {
    doLife(*effL);
  }
  const int dmgSide = lastDmgSide_;
  const int dmgAmount = lastDmgAmount_;
  check_win();
  // 24-Shisui: 「本回合内受到伤害的次数」——每一次实际伤害（含裂伤伤害化）计 1 次。
  if (dmgAmount > 0)
    note_damage_taken(target, opp(target),
                      dmgSide == 1 ? kWoundAura : (dmgSide == 2 ? kWoundLife : -1));
  // 23-Akina 股价: 命受到攻击伤害时（敌方 +2 / 我方 -1）。
  note_attack_life_damage(target, fromAttack, dmgSide == 2 ? dmgAmount : 0);
}

void Engine::deal_damage(Player target, std::optional<int> aura, std::optional<int> life,
                         uint32_t keywords) {
  apply_damage_to(target, aura, life, keywords, -1);
}

// ---------------------------------------------------------------------------
// 24-Shisui 桑畑志水: 裂伤指示物
// ---------------------------------------------------------------------------

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
      moved = move_crystals(AreaRef::aura(target), AreaRef::dust(), n, false);
      break;
    }
    case kWoundFlare:
      moved = move_crystals(AreaRef::flare(target), AreaRef::dust(), n, false);
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

void Engine::pay_special_cost(Player p, int inst) {
  const CardDef& d = def_of(inst);
  store_int(inst, "paid_cost", 0);  // 26-Innealra: 记录本次支付量
  if (d.woundCost >= 0) {
    // 费用 {X}: 向自气放入 X 个裂伤指示物（不消耗气/虚）。
    add_wound(p, kWoundFlare, d.woundCost, p);
    return;
  }
  const int cost = cut_cost(p, d.id, inst);
  if (cost > 0) {
    move_crystals(AreaRef::flare(p), AreaRef::dust(), cost, false);
    store_int(inst, "paid_cost", cost);
  }
}

void Engine::resolve_attack(Attack& a) {
  if (st.over) return;
  lastAtkSide_ = 0;
  lastAtkAmount_ = 0;
  lastAtkResponded_ = false;  // 26-Innealra 祖枪/鸣枪/怪枪: 是否被对应
  if (!a.counted) declare_attack(a);
  attackedThisTurn_[a.attacker] = true;
  fire("attack_counted", a.attacker, &a, -1, attacksThisTurn_[a.attacker] == 2);
  Player target = opp(a.attacker);

  // ---- response window -----------------------------------------------------
  // 全力 and 对应 are mutually exclusive, so a response is never full power.
  Attack* prev = currentResponding;
  currentResponding = &a;  // visible to respond predicates (识破/终焉) and effects
  if (!(a.keywords & AF_Unrespondable) && !a.fromResponse && !ps(target).cannotRespond) {
    std::vector<int> resp;
    auto consider = [&](int inst) {
      const CardDef& d = def_of(inst);
      if (has_full_power(inst)) return;
      if ((a.keywords & AF_NoSpecialResponse) && d.kind == CardKind::Special) return;
      if ((a.keywords & AF_NoNormalResponse) && d.kind == CardKind::Normal) return;
      if ((a.keywords & AF_NoEnhanceResponse) && d.type == CardType::Enhance) return;
      if ((a.keywords & AF_NoAttackResponse) && d.type == CardType::Attack) return;
      if ((a.keywords & AF_NoActionResponse) && d.type == CardType::Action) return;
      // 26-Innealra 修省（纠葛）: 本回合你不能使用任何通常牌。
      if (ps(target).cannotUseNormals && d.kind == CardKind::Normal) return;
      if (!respondable_card(target, inst)) return;
      if (d.type == CardType::Attack) {
        if (attack_card_forbidden(target) || !can_attack(target)) return;
        Attack tmp = make_attack(target, inst, true, false);
        if (!attack_range_ok(tmp)) return;  // 25-Misora 追踪: 参考瞄准点
      }
      resp.push_back(inst);
    };
    for (int inst : ps(target).hand) consider(inst);
    // 18-Mizuki: 已动员的士兵「视为你的手牌可以打出」，因此也可以作为对应打出。
    for (int inst : ps(target).barracks)
      if (ci(inst).faceUp) consider(inst);
    for (int inst : ps(target).special) {
      if (ci(inst).faceUp) continue;
      if (def_of(inst).kind != CardKind::Special) continue;  // only 切札 respond from here
      if (ps(target).flare < cut_cost(target, ci(inst).def, inst)) continue;
      consider(inst);
    }
    if (!resp.empty()) {
      Request r;
      r.kind = "response";
      r.prompt = "respond to the attack?";
      r.options.push_back({"pass", true, {{"kind", "pass"}}});
      for (int inst : resp) {
        Option o;
        o.label = card_label(def_of(inst));
        o.data = card_json(def_of(inst));
        o.data["inst"] = inst;
        o.data["kind"] = "play";
        r.options.push_back(o);
      }
      int idx = ask_one(target, std::move(r));
      if (idx > 0 && idx <= static_cast<int>(resp.size())) {
        int chosen = resp[static_cast<size_t>(idx - 1)];
        ps(target).respondedThisTurn = true;  // 18-Mizuki“上回合/本回合进行过对应”
        if (a.keywords & AF_PreventResponse) {
          // 晓: 防止此次对应。对应牌仍要付费用，但效果不发生。
          ps(target).responsesPlayedThisTurn += 1;  // 仍然“进行过对应”
          const CardDef& rd = def_of(chosen);
          a.auraDelta -= 1;
          if (rd.kind == CardKind::Normal) {
            move_card(chosen, Zone::Discard);  // 用于对应的牌进入弃牌堆
          } else {
            a.lifeDelta -= 1;  // 王牌: -1/-1
            pay_special_cost(target, chosen);
            move_card(chosen, Zone::Special);
            ci(chosen).faceUp = true;  // 使用后状态
          }
        } else {
          play_card(target, chosen, true);
          lastAtkResponded_ = true;
          // 26-Innealra 诺伦: 对手使用对应牌时，你可以轮转一次命运槽（不共鸣）。
          if (!st.over && has_innealra(a.attacker))
            offer_fate_rotation(a.attacker, "对手使用了对应牌");
        }
        fire("responded_with", target, nullptr, chosen, false);
        effects_->run_on_response(*this, &a);  // 旋回刃: 每张对应牌结算完毕后
        if (st.over) {
          currentResponding = prev;
          return;
        }
      }
    }
  }
  currentResponding = prev;

  // ---- range re-check ------------------------------------------------------
  if (!(a.keywords & AF_Lock) && !attack_range_ok(a)) a.missed = true;
  bool negated = a.negated && !(a.keywords & AF_NoNegate);
  if (negated || a.missed) {
    // 落空/被打消同样是一次"结算完毕"（圆环轮回轮回旋等）。
    fire("attack_resolved", a.attacker, &a, -1, false);
    effects_->clear_attack_callbacks(&a);
    return;
  }
  if (a.negateDamage) {  // 驳论: negate only the damage, keep附加效果
    a.hit = true;
    fire("attack_resolved", a.attacker, &a, -1, false);
    effects_->run_after_attack(*this, &a);
    effects_->clear_attack_callbacks(&a);
    return;
  }

  // ---- damage --------------------------------------------------------------
  // 26-Innealra O1-S2 阴郁·埋葬: 展开中，对手的攻击不受攻击修正（只有卡面数值；
  // 数值替换类的连续效果在 finalize 阶段已经生效）。
  if (suppress_attack_mods(a.attacker)) {
    a.auraDelta = 0;
    a.lifeDelta = 0;
  }
  std::optional<int> effA = a.aura;
  std::optional<int> effL = a.life;
  if (effA) *effA += a.auraDelta;
  if (effL) *effL += a.lifeDelta;
  // 本次攻击的伤害路由（取代引擎全局标志）。
  DamageRoute route;
  route.toDistance = (a.keywords & AF_ToDistance) != 0;
  route.toDust = a.lifeDamageToDust;  // 强酸
  route.toWaku = (a.keywords & AF_ToWaku) != 0;      // 26-Innealra 星空/宇宙·幽邃
  route.wakuOwner = a.attacker;
  route.auraToDistance = (a.keywords & AF_AuraToDistance) != 0;  // 残恣·嗜灭
  route.toCard = a.damageToCard;
  if (a.wound) {
    // 24-Shisui 裂伤攻击: 不进行常规伤害结算，改为向区域塞入裂伤指示物
    // （受到裂伤时不移动装结晶）。造成裂伤 ≠ 造成伤害，但承伤侧的选择照常
    // 对「攻击后」可见。
    apply_wound_attack_damage(target, a.attacker, effA, effL,
                              a.attackerChoosesDamage ? a.attacker : -1);
    a.hit = true;
    fire("attack_resolved", a.attacker, &a, -1, false);
    effects_->run_after_attack(*this, &a);
    effects_->clear_attack_callbacks(&a);
    return;
  }
  apply_damage_to(target, effA, effL, a.keywords, a.sourceInst,
                  a.attackerChoosesDamage ? a.attacker : -1, true, route);
  lastAtkSide_ = lastDmgSide_;
  lastAtkAmount_ = lastDmgAmount_;
  a.hit = true;
  fire("attack_resolved", a.attacker, &a, -1, false);  // 圆环轮回旋
  effects_->run_after_attack(*this, &a);
  effects_->clear_attack_callbacks(&a);
}

void Engine::cover_card(int inst) {
  move_card(inst, Zone::Cover);
  ci(inst).faceUp = false;
}

void Engine::reuse_special(int inst) {
  // 25-Misora 观空穹仪: 不能被其它牌的效果再次发动。
  if (inst < 0 || def_of(inst).noReuse) return;
  store_int(inst, "paid_cost", 0);  // 26-Innealra: 再次发动没有支付费用
  // 再次发动同样是一次「打出结算」：压栈结算语境（伪证/全开等不跨牌泄漏）。
  push_play_frame(def_of(inst).id, ci(inst).owner, false);
  PlayFrameGuard frameGuard{*this};
  resolve_card_effect(ci(inst).owner, inst, false);
}

}  // namespace fy
