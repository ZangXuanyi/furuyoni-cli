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
  for (int inst : ps(p).enhance)
    if (def_of(inst).armorFromCrystals) armor += ci(inst).crystals;
  for (int inst : ps(p).special)
    if (ci(inst).faceUp && def_of(inst).armorFromCrystals) armor += ci(inst).crystals;
  return armor;
}

void Engine::spend_aura(Player target, int n) {
  if (n <= 0) return;
  auraDamagedThisTurn_[target] = true;  // 斩击乱舞: 本回合敌装受到过伤害
  int remaining = n;
  // "视作装"的卡上结晶优先消耗（移入虚）。
  auto drain = [&](int inst) {
    if (remaining <= 0) return;
    if (!def_of(inst).armorFromCrystals) return;
    if (ci(inst).zone == Zone::Special && !ci(inst).faceUp) return;  // unused 切札 has no aura
    int take = std::min(remaining, ci(inst).crystals);
    if (take <= 0) return;
    ci(inst).crystals -= take;
    st.dust += take;
    remaining -= take;
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
  if (ci(inst).crystals > 0) return;
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
  bool dead[2] = {ps(P0).life <= 0, ps(P1).life <= 0};
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

void Engine::damage_life(Player p, int n, AreaKind to, bool triggerBreak) {
  // 倒车 routes life damage to 距; rebuild-style life loss goes to 气; a few
  // effects send it to 虚.
  AreaRef dest = AreaRef::flare(p);
  if (to == AreaKind::Dust)
    dest = AreaRef::dust();
  else if (to == AreaKind::Distance)
    dest = AreaRef::distance();
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
    int c = ci(inst).crystals;
    ci(inst).crystals = 0;
    st.dust += c;  // 破绽: 献 all go to 虚
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
  if (c.crystals <= 0) return;
  const CardDef& d = def_of(inst);
  Player owner = c.holder;  // the controller, for on_discard / 弃置时
  c.crystals -= 1;
  // a dropped 献 falls to 虚 by default; e.g. 圈域 sends it to 距 instead.
  if (d.decayTo == "distance")
    st.distance += 1;
  else
    st.dust += 1;
  if (c.crystals > 0) return;
  if (d.kind == CardKind::Normal) {
    move_card(inst, Zone::Discard);
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
    fire("enhance_left", owner, nullptr, inst, false);  // 森罗判证
  } else {
    // 切札付与 also runs its 弃置时 when it leaves play.
    if (effects_->has(d.id, "on_discard")) effects_->call(*this, d.id, "on_discard", owner, inst);
    move_card(inst, Zone::Special);  // special enhance stays used in the special zone
  }
}

// ---------------------------------------------------------------------------
// attacks
// ---------------------------------------------------------------------------

Attack Engine::make_attack(Player p, int inst, bool asResponse, bool consumePending) {
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
  a.attackerChoosesDamage = ea.attackerChooses;
  a.terminal = ea.terminal;
  if (consumePending && forceUnrespondable_) {
    a.keywords |= AF_Unrespondable;
    forceUnrespondable_ = false;
  }
  effects_->finalize_attack(*this, p, a, consumePending);
  if (consumePending) declare_attack(a);
  return a;
}

void Engine::declare_attack(Attack& a) {
  if (a.counted) return;
  a.counted = true;
  int n = note_attack(a.attacker);
  fire("attack_declared", a.attacker, &a, -1, n == 1);
}

void Engine::apply_damage_to(Player target, std::optional<int> aura, std::optional<int> life,
                             uint32_t keywords, int sourceInst, int chooser, bool fromAttack) {
  (void)sourceInst;
  std::optional<int> effA = aura, effL = life;
  if (effA && !(keywords & AF_Overwhelm)) *effA = std::min(*effA, 5);
  if (effA && *effA < 0) *effA = 0;
  if (effL && *effL < 0) *effL = 0;
  bool canAura = effA.has_value() && effective_armor(target) >= *effA;
  lastDmgFromAttack_ = fromAttack;
  auto doAura = [&](int n) {
    // Record the chosen side *before* resolving: on_life_loss / 即再起
    // predicates read last_damage_side during the damage.
    lastDmgSide_ = 1;
    lastDmgAmount_ = n;
    if (damageToDistance_)
      move_crystals(AreaRef::aura(target), AreaRef::distance(), n, false);
    else
      spend_aura(target, n);
  };
  auto doLife = [&](int n) {
    lastDmgSide_ = 2;  // visible to 即再起 predicates during on_life_loss
    int before = ps(target).life;
    damage_life(target, n, damageToDistance_ ? AreaKind::Distance : AreaKind::Flare, true);
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
  damageToDistance_ = false;
  check_win();
}

void Engine::deal_damage(Player target, std::optional<int> aura, std::optional<int> life,
                         uint32_t keywords) {
  apply_damage_to(target, aura, life, keywords, -1);
}

void Engine::resolve_attack(Attack& a) {
  if (st.over) return;
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
      if (d.flags & CF_FullPower) return;
      if ((a.keywords & AF_NoSpecialResponse) && d.kind == CardKind::Special) return;
      if ((a.keywords & AF_NoNormalResponse) && d.kind == CardKind::Normal) return;
      if ((a.keywords & AF_NoEnhanceResponse) && d.type == CardType::Enhance) return;
      if ((a.keywords & AF_NoAttackResponse) && d.type == CardType::Attack) return;
      if ((a.keywords & AF_NoActionResponse) && d.type == CardType::Action) return;
      if (!respondable_card(target, inst)) return;
      if (d.type == CardType::Attack) {
        if (attack_card_forbidden(target) || !can_attack(target)) return;
        Attack tmp = make_attack(target, inst, true, false);
        if (!tmp.range.contains(distance())) return;
      }
      resp.push_back(inst);
    };
    for (int inst : ps(target).hand) consider(inst);
    for (int inst : ps(target).special) {
      if (ci(inst).faceUp) continue;
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
        play_card(target, chosen, true);
        fire("responded_with", target, nullptr, chosen, false);
        if (st.over) {
          currentResponding = prev;
          return;
        }
      }
    }
  }
  currentResponding = prev;

  // ---- range re-check ------------------------------------------------------
  if (!(a.keywords & AF_Lock) && !a.range.contains(distance())) a.missed = true;
  bool negated = a.negated && !(a.keywords & AF_NoNegate);
  if (negated || a.missed) {
    // 落空/被打消同样是一次"结算完毕"（圆环轮回旋等）。
    fire("attack_resolved", a.attacker, &a, -1, false);
    return;
  }
  if (a.negateDamage) {  // 驳论: negate only the damage, keep附加效果
    a.hit = true;
    fire("attack_resolved", a.attacker, &a, -1, false);
    effects_->run_after_attack(*this, &a);
    return;
  }

  // ---- damage --------------------------------------------------------------
  std::optional<int> effA = a.aura;
  std::optional<int> effL = a.life;
  if (effA) *effA += a.auraDelta;
  if (effL) *effL += a.lifeDelta;
  damageToDistance_ = (a.keywords & AF_ToDistance) != 0;
  apply_damage_to(target, effA, effL, a.keywords, a.sourceInst,
                  a.attackerChoosesDamage ? a.attacker : -1, true);
  a.hit = true;
  fire("attack_resolved", a.attacker, &a, -1, false);  // 圆环轮回旋
  effects_->run_after_attack(*this, &a);
}

// ---------------------------------------------------------------------------
// playing cards
// ---------------------------------------------------------------------------

void Engine::play_card(Player p, int inst, bool asResponse, bool zenkai) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  callStack_.push_back({defId, p});
  StackGuard stackGuard{callStack_};
  ps(p).cardsPlayedThisTurn += 1;

  if (d.kind == CardKind::Special) {
    move_crystals(AreaRef::flare(p), AreaRef::dust(), cut_cost(p, defId, inst));
    ci(inst).faceUp = true;  // used / 展开
    ci(inst).usedThisTurn = true;
  } else {
    auto& h = ps(p).hand;
    h.erase(std::remove(h.begin(), h.end(), inst), h.end());
    ci(inst).zone = Zone::Limbo;
  }

  resolve_card_effect(p, inst, asResponse, zenkai);

  if (d.kind == CardKind::Normal && ci(inst).zone == Zone::Limbo) {
    if (d.isPoison) {
      if (d.name == cards::kMetsutouDoku)
        force_move(inst, Zone::Discard);
      else
        return_poison(inst);  // poisons return to their owner's bag
    } else {
      move_card(inst, Zone::Discard);
    }
  }
}

void Engine::resolve_card_effect(Player p, int inst, bool asResponse, bool zenkai) {
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  keisouDoubled_ = false;  // 骇客装置: one doubling per 机巧 resolution
  // Raira 风雷: using a non-Raira card raises one slot by 1.
  if (ps(p).raira && d.goddess != "raira" && !ps(p).rairaGainRestricted) {
    Request r;
    r.kind = "option";
    r.prompt = "风雷：选择一个槽 +1";
    r.options.push_back({"风神 +1", true, {}});
    r.options.push_back({"雷神 +1", true, {}});
    int c = ask_one(p, std::move(r));
    if (c == 1) {
      if (ps(p).thunder < 20) ps(p).thunder += 1;
    } else {
      if (ps(p).wind < 20) ps(p).wind += 1;
    }
  }
  if (d.centrifugal) playedCentrifugalThisTurn_[p] = true;
  if (d.name == cards::kRenseiKougeki) playedLianchengThisTurn_[p] = true;
  bool prevZenkai = zenkaiActive_;
  zenkaiActive_ = zenkai;
  // Yukihi A1-S2: first non-Yukihi normal card each turn.
  if (d.kind == CardKind::Normal && !card_has_goddess(inst, "yukihi")) {
    normalNonYukihi_[p] += 1;
    fire("normal_card_used", p, nullptr, inst, normalNonYukihi_[p] == 1);
  }
  if (d.type == CardType::Enhance) {
    // Re-expanding a card that still holds 献 (e.g. reuse): return the old ones first.
    if (ci(inst).crystals > 0) {
      int old = ci(inst).crystals;
      ci(inst).crystals = 0;
      if (d.decayTo == "distance")
        st.distance += old;
      else
        st.dust += old;
    }
    // Order per the rules: 展开时 first, then place 献, then discard if empty.
    if (effects_->has(defId, "on_enter")) effects_->call(*this, defId, "on_enter", p, inst);
    int take = 0;
    int total = st.dust + ps(p).aura;
    int nagiVal = d.nagi + pendingNagiAdjust_;
    pendingNagiAdjust_ = 0;
    if (nagiVal < 0) nagiVal = 0;
    if (total > 0 && nagiVal > 0) {
      take = std::min(nagiVal, total);
      int loDust = std::max(0, take - ps(p).aura);
      int hiDust = std::min(take, st.dust);
      int fromDust = loDust;
      if (loDust != hiDust) {
        Request r;
        r.kind = "option";
        r.prompt = "choose 纳 cost split";
        for (int df = loDust; df <= hiDust; ++df) {
          Option o;
          o.label = "dust " + std::to_string(df) + " + aura " + std::to_string(take - df);
          r.options.push_back(o);
        }
        fromDust = loDust + ask_one(p, std::move(r));
      }
      int auraBefore = ps(p).aura;
      add_crystals(AreaRef::dust(), -fromDust);
      add_crystals(AreaRef::aura(p), -(take - fromDust));
      if (ps(p).aura != auraBefore) notify_aura_changed(p);
    }
    if (d.kind == CardKind::Normal)
      move_card(inst, Zone::Enhance);
    else
      move_card(inst, Zone::Special);
    ci(inst).crystals += take;  // on_enter may have added crystals (e.g. 反射装置)
    if (ci(inst).crystals <= 0) {
      ci(inst).crystals = 0;
      if (d.kind == CardKind::Normal) {
        move_card(inst, Zone::Discard);
        if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
        fire("enhance_left", p, nullptr, inst, false);
      } else {
        if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
        move_card(inst, Zone::Special);
      }
    }
  } else if (d.type == CardType::Attack) {
    // 迟缓毒: "你不能使用攻击牌" — playing an attack card is refused (generated
    // attacks via ctx:attack are not "using" a card and stay allowed).
    if (attack_card_forbidden(p)) return;
    // on_play (if any) runs before the attack and may modify the responded attack.
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
    Attack a = make_attack(p, inst, asResponse, true);
    resolve_attack(a);
    if (a.hit && effects_->has(defId, "on_attack_after"))
      effects_->call(*this, defId, "on_attack_after", p, inst);
    if (a.terminal) {  // 电磁炮 黄: dynamic 终端
      if (st.active != p)
        ps(p).cannotRespond = true;
      else
        abortMain_ = true;
    }
  } else {  // Action
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
    fire("action_resolved", p, nullptr, inst, false);  // 模块化
  }
  if (d.flags & CF_FullPower) {
    usedFullPowerThisTurn_[p] = true;
    fire("fullpower_used", p, nullptr, inst, false);
  }
  if ((d.flags & CF_Terminal) && st.active != p) ps(p).cannotRespond = true;
  zenkaiActive_ = prevZenkai;
}

void Engine::cover_card(int inst) {
  move_card(inst, Zone::Cover);
  ci(inst).faceUp = false;
}

void Engine::use_from_cover(int inst, bool asResponse) {
  play_from_cover(ci(inst).owner, inst, asResponse, false);
}

void Engine::play_from_cover(Player p, int inst, bool asResponse, bool toDeckAfter) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  // Only a card actually taken from the cover pile counts as "from the cover
  // pile"; 经纱/诸式理解 use play_from_cover for discard-pile cards too.
  const bool fromCover = (ci(inst).zone == Zone::Cover);
  move_card(inst, Zone::Limbo);
  callStack_.push_back({d.id, p, fromCover});
  StackGuard stackGuard{callStack_};
  ps(p).cardsPlayedThisTurn += 1;
  resolve_card_effect(p, inst, asResponse);
  if (ci(inst).zone == Zone::Limbo) {
    if (toDeckAfter)
      move_card(inst, Zone::Deck);
    else
      move_card(inst, Zone::Discard);
  }
}

// ---------------------------------------------------------------------------
// turn structure
// ---------------------------------------------------------------------------

void Engine::use_card(int inst, bool asResponse) {
  play_from_cover(ci(inst).owner, inst, asResponse, false);
}

void Engine::use_foreign_card(Player user, int inst) {
  if (st.over) return;
  const int defId = ci(inst).def;
  // A borrowed 付与 expands on the *user's* side (ruling): transfer ownership
  // first so it joins the user's enhance zone and its aura benefits the user.
  if (def_of(inst).type == CardType::Enhance && ci(inst).holder != user) {
    move_card(inst, Zone::Limbo);
    ci(inst).holder = user;  // on the user's field; owner unchanged (returns home)
  }
  move_card(inst, Zone::Limbo);
  callStack_.push_back({defId, user, false});
  StackGuard stackGuard{callStack_};
  resolve_card_effect(user, inst, false);
  if (ci(inst).zone == Zone::Limbo) move_card(inst, Zone::Discard);  // owner's discard
}

void Engine::reuse_special(int inst) { resolve_card_effect(ci(inst).owner, inst, false); }

}  // namespace fy
