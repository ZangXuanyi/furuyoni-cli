// 结算管线（what.md 第 3 条）。
//
// 打出一张牌的完整流程，按命名阶段组织：
//   1. costStage      费用：尸的额外弃置（21）/ 切札耗能 气→虚 或 {X} 裂伤（24）；
//   2. declareStage   声明：伪证声明+质疑（22）；on_declare 伪装/复制改结算；
//                    声明期计数（禁忌自换的选择在卡效果内锁定于攻击求值时）；
//   3. interceptStage 拦截：子午灯塔（17，拦截非攻击牌效果）/ 潜水公开（17）/
//                    晓（21，防对应——在攻击的对应窗口生效）；
//   4. resolveStage   结算打出效果（压栈）：
//       攻击  对应窗口（对应牌压栈优先结算）→ 持续修正 → 距离重检（锁定除外）
//             → 伤害 → 攻击后；衍生攻击视作虚拟牌走同一流程；
//       付与  种植（19）→ 给献（纳支付）→ 展开时（单一钩子，可读最终献数）→
//             献=0 立即弃置；否则驻留提供光环；
//       行动  on_play → action_resolved；
//   5. destinationStage 去向：兵舍回营（18）/ 毒回袋（09）/ 弃牌堆 / 使用后态。
//
// 每张被打出的牌压入一个 PlayFrame（含结算语境快照），弹栈恢复——伪证/伪装/
// 全开等跨牌泄漏由此统一防护，取代散落的手工保存/恢复。
#include "engine/engine_internal.hpp"
#include "engine/effect_host.hpp"

namespace fy {
using namespace detail;  // NOLINT


// ---------------------------------------------------------------------------
// 结算栈帧
// ---------------------------------------------------------------------------

void Engine::push_play_frame(int defId, Player owner, bool fromCover) {
  PlayFrame f;
  f.def = defId;
  f.owner = owner;
  f.fromCover = fromCover;
  f.ctxBluffActive = bluffActive_;
  f.ctxBluffInst = bluffInst_;
  f.ctxBluffClaimDef = bluffClaimDef_;
  f.ctxBluffNotDoubted = bluffNotDoubted_;
  f.ctxBluffDoubtFailed = bluffDoubtFailed_;
  f.ctxPendingResolveAs = pendingResolveAs_;
  f.ctxZenkai = zenkaiActive_;
  callStack_.push_back(f);
}

void Engine::pop_play_frame() {
  if (callStack_.empty()) return;
  const PlayFrame f = callStack_.back();
  callStack_.pop_back();
  bluffActive_ = f.ctxBluffActive;
  bluffInst_ = f.ctxBluffInst;
  bluffClaimDef_ = f.ctxBluffClaimDef;
  bluffNotDoubted_ = f.ctxBluffNotDoubted;
  bluffDoubtFailed_ = f.ctxBluffDoubtFailed;
  pendingResolveAs_ = f.ctxPendingResolveAs;
  zenkaiActive_ = f.ctxZenkai;
}

// ---------------------------------------------------------------------------
// declareStage: 伪证声明 + 质疑（22-Renri 夜山恋离）
// ---------------------------------------------------------------------------

// 在自己回合内使用常规牌时，可以背面向上打出并声称它是某张伪证牌。对手
// 可以选择质疑。返回：0 = 正常打出；1 = 按声称的牌结算；2 = 质疑成功，
// 这张牌与声称牌的效果都不执行。
int Engine::declare_bluff(Player p, int inst) {
  std::vector<int> claims = bluff_claim_defs(p);
  if (claims.empty()) return 0;
  Request r;
  r.kind = "bluff";
  r.prompt = "伪证：背面向上打出并声称？";
  r.options.push_back({"正常打出", true, {{"bluff", false}}});
  for (int defId : claims) {
    const CardDef& cd = def(defId);
    Option o;
    o.label = "伪证：" + cd.name;
    o.data = {{"bluff", true}, {"def", defId}, {"name", cd.name}};
    r.options.push_back(o);
  }
  const int idx = ask_one(p, std::move(r));
  if (idx <= 0 || idx > static_cast<int>(claims.size())) return 0;
  const int claimDef = claims[static_cast<size_t>(idx - 1)];

  bluffActive_ = true;
  bluffInst_ = inst;
  bluffClaimDef_ = claimDef;
  bluffNotDoubted_ = false;
  bluffDoubtFailed_ = false;

  Request q;
  q.kind = "doubt";
  q.prompt = "对手声称这是「" + def(claimDef).name + "」。质疑？";
  q.options.push_back({"不质疑", true, {{"doubt", 0}}});
  q.options.push_back({"质疑", true, {{"doubt", 1}}});
  const int doubted = ask_one(opp(p), std::move(q)) == 1;
  if (doubted) {
    if (def_of(inst).name == def(claimDef).name) {
      // 质疑失败: 对手焦躁一次，然后正常使用这张牌。
      ps(opp(p)).doubtFailedThisTurn = true;
      bluffDoubtFailed_ = true;
      impatience(opp(p));
    } else {
      // 质疑成功: 两者效果都不执行。
      bluffActive_ = false;
      bluffInst_ = -1;
      bluffClaimDef_ = -1;
      return 2;
    }
  } else {
    bluffNotDoubted_ = true;
  }
  pendingResolveAs_ = claimDef;
  return 1;
}

// ---------------------------------------------------------------------------
// 主入口：打出一张牌
// ---------------------------------------------------------------------------

void Engine::play_card(Player p, int inst, bool asResponse, bool zenkai) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  // 18-Mizuki: 已动员的士兵从兵舍打出；结算完毕后翻回未动员并留在兵舍。
  const bool fromBarracks = vec_has(ps(p).barracks, inst);
  if (fromBarracks) leave_barracks(p, inst);
  push_play_frame(defId, p, false);
  PlayFrameGuard frameGuard{*this};
  ps(p).cardsPlayedThisTurn += 1;
  // 18-Mizuki: “本回合打出的第一张对应”序号（嵌套结算用栈）。
  if (asResponse) {
    ps(p).responsesPlayedThisTurn += 1;
    responseOrdStack_.push_back(ps(p).responsesPlayedThisTurn);
    check_dramas();  // 20-Kanawe《杀阵》
  } else {
    responseOrdStack_.push_back(0);
  }

  // ---- costStage -----------------------------------------------------------
  // 尸: 本回合对手的下一次攻击必须额外弃置一张该女神的牌作为费用。
  if (d.type == CardType::Attack && !ps(p).extraAttackCostGoddess.empty()) {
    const std::string g = ps(p).extraAttackCostGoddess;
    std::vector<int> cands;
    for (int h : ps(p).hand)
      if (h != inst && def_of(h).goddess == g) cands.push_back(h);
    for (int h : ps(p).special)
      if (!ci(h).faceUp && def_of(h).goddess == g) cands.push_back(h);
    if (!cands.empty()) {
      Request r;
      r.kind = "cards";
      r.prompt = "尸：额外弃置一张" + g + "的牌作为费用";
      r.minSel = 1;
      r.maxSel = 1;
      for (int h : cands) {
        Option o;
        o.label = card_label(def_of(h));
        o.data = card_json(def_of(h));
        o.data["inst"] = h;
        r.options.push_back(o);
      }
      int pick = ask_one(p, std::move(r));
      if (pick >= 0 && pick < static_cast<int>(cands.size())) {
        int h = cands[static_cast<size_t>(pick)];
        move_card(h, Zone::Discard);
      }
    }
    ps(p).extraAttackCostGoddess.clear();
  }

  if (d.kind == CardKind::Special) {
    pay_special_cost(p, inst);  // 含 24-Shisui 的 {X} 裂伤费用
    ci(inst).faceUp = true;  // used / 展开
    ci(inst).usedThisTurn = true;
  } else {
    // Remove it from whichever zone list currently holds it (normal EX cards can
    // sit outside the hand), then park it in Limbo while it resolves.
    if (!fromBarracks) {
      if (auto* v = zone_ptr(st, ci(inst).holder, ci(inst).zone))
        v->erase(std::remove(v->begin(), v->end(), inst), v->end());
    }
    ci(inst).zone = Zone::Limbo;
  }

  // ---- declareStage --------------------------------------------------------
  // 18-Mizuki O-S2 即再起: 你打出具有终端的牌（该牌结算之前再起）。
  if (has_terminal(inst)) fire("terminal_card_used", p, nullptr, inst, false);
  // 22-Renri 伪证: 只在自己回合、只对常规牌、对应打出时不能伪证。
  int bluffResult = 0;  // 0 无 / 1 按声称结算 / 2 质疑成功
  if (!asResponse && st.active == p && d.kind == CardKind::Normal && !bluffActive_)
    bluffResult = declare_bluff(p, inst);

  // ---- resolveStage（含 interceptStage，见 resolve_card_effect）------------
  const bool bluffed = bluffResult != 0;
  if (bluffResult != 2) {
    resolve_card_effect(p, inst, asResponse, zenkai);
  } else {
    ps(p).cardsPlayedTotal += 1;  // 这张牌被使用过（但没有任何效果）
  }
  if (!responseOrdStack_.empty()) responseOrdStack_.pop_back();
  if (st.over) return;

  if (bluffed) {
    // 道化的觉悟: 伪证没有被质疑 → 公开（+1 集中力由触发器结算）。
    if (bluffNotDoubted_) fire("bluff_undoubted", p, nullptr, inst, false);
    // 回归: 对手质疑失败时，可以把这张牌移出游戏并把「考古」置回弃牌堆。
    if (bluffDoubtFailed_ && def_of(inst).regression && ci(inst).zone != Zone::Removed) {
      if (ask_yes_no(p, "回归：将这张牌移出游戏，并把「考古」置回弃牌堆？")) {
        remove_from_game(inst);
        for (int i = 0; i < static_cast<int>(st.insts.size()); ++i) {
          if (ci(i).owner == p && ci(i).zone == Zone::Removed && def_of(i).kaoguReturn) {
            move_card(i, Zone::Discard);
            break;
          }
        }
      }
    }
  }

  // ---- destinationStage ----------------------------------------------------
  if (fromBarracks && (ci(inst).zone == Zone::Limbo || ci(inst).zone == Zone::Discard)) {
    // 攻击/行动士兵结算完毕 → 翻回未动员回兵舍；enhance 士兵（骑兵）在离场时才回。
    to_barracks(p, inst, false);
  } else if (d.kind == CardKind::Normal && ci(inst).zone == Zone::Limbo) {
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

// ---------------------------------------------------------------------------
// 结算打出效果
// ---------------------------------------------------------------------------

void Engine::resolve_card_effect(Player p, int inst, bool asResponse, bool zenkai) {
  // ---- declareStage（后半）：伪装/复制改结算 -------------------------------
  // 22-Renri: 伪证按声称的牌结算 / 铭镌之衣按复制结算 / 洛阳铲按声称结算。
  // on_declare 在类型分派之前运行，可以请求「改用另一张牌结算」。
  const int realDefId = ci(inst).def;
  if (resolveOverrideInst_ != inst) {
    if (pendingResolveAs_ < 0 && effects_->has(realDefId, "on_declare"))
      effects_->call(*this, realDefId, "on_declare", p, inst);
    const int overrideDef = pendingResolveAs_;
    pendingResolveAs_ = -1;
    if (overrideDef >= 0 && overrideDef != realDefId) {
      const int prevOverride = resolveOverrideInst_;
      resolveOverrideInst_ = inst;
      ci(inst).def = overrideDef;
      resolve_card_effect(p, inst, asResponse, zenkai);
      ci(inst).def = realDefId;
      resolveOverrideInst_ = prevOverride;
      return;
    }
  }
  const CardDef& d = def_of(inst);
  const int defId = d.id;
  // 26-Innealra 诺伦: 「本回合内你使用过非诺伦的牌」/「本回合内使用过通常牌」。
  note_card_used_by(p, d);
  keisouDoubled_ = false;  // 骇客装置: one doubling per 机巧 resolution

  // ---- interceptStage --------------------------------------------------------
  // 子午灯塔: 对手的回合内，对手从手牌使用了非攻击牌 → 改为弃置本牌（不结算效果，
  // 视作对手使用了这张牌），然后将其变为未使用状态。
  // 顺序在潜水判定之前：结算效果被替换/取消时不进入解除潜水流程（卡面裁定）。
  if (st.active == p && d.type != CardType::Attack) {
    int lh = -1;
    for (int s : ps(opp(p)).special)
      if (ci(s).faceUp && def_of(s).interceptNonAttack) {
        lh = s;
        break;
      }
    if (lh >= 0) {
      ps(p).cardsPlayedTotal += 1;  // 视作对手使用了这张牌
      reset_special(lh);
      return;
    }
  }
  // 潜水: 对手使用非攻击牌时，在该牌结算之前先公开潜水、执行效果并解除潜水状态。
  // 攻击牌要等"使用"被确认（迟缓毒等会拒绝使用）之后，见下方攻击分支。
  if (d.type != CardType::Attack && ps(opp(p)).dive != 0) reveal_dive(opp(p), false);
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
  ps(p).cardsPlayedTotal += 1;  // 万叶仍未识: 本局打出的牌数
  if (d.centrifugal) playedCentrifugalThisTurn_[p] = true;
  if (d.name == cards::kRenseiKougeki) playedLianchengThisTurn_[p] = true;
  zenkaiActive_ = zenkai;
  // Yukihi A1-S2: first non-Yukihi normal card each turn.
  if (d.kind == CardKind::Normal && !card_has_goddess(inst, "yukihi")) {
    normalNonYukihi_[p] += 1;
    fire("normal_card_used", p, nullptr, inst, normalNonYukihi_[p] == 1);
  }

  // ---- resolveStage ----------------------------------------------------------
  if (d.type == CardType::Enhance) {
    resolve_enhance(p, inst, d);
  } else if (d.type == CardType::Attack) {
    // 迟缓毒: "你不能使用攻击牌" — playing an attack card is refused (generated
    // attacks via ctx:attack are not "using" a card and stay allowed).
    if (attack_card_forbidden(p)) return;
    ps(p).attackCardsPlayedThisTurn += 1;  // 18-Mizuki O-S2
    // 潜水: 攻击牌的使用被确认后、结算之前公开潜水。
    const bool diveByAttack =
        ps(opp(p)).dive != 0 && reveal_dive(opp(p), true);
    // on_play (if any) runs before the attack and may modify the responded attack.
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
    // 潜水闪避: 由攻击牌触发的公开要立即检查距离（无视“锁定”词条）。落空则该攻击
    // 视为未发生过：不声明、不计数、不进入对应与结算步骤；牌照常进弃牌堆/使用后态。
    Attack a = make_attack(p, inst, asResponse, true, /*declareNow=*/false);
    const bool evaded =
        diveByAttack && !(a.keywords & AF_Lock) && !attack_range_ok(a);
    if (evaded) {
      ps(opp(p)).forcedTailwind = true;  // 潜水闪避成功: 你的下回合固定顺风
    } else {
      declare_attack(a);
      resolve_attack(a);
      if (a.hit && effects_->has(defId, "on_attack_after"))
        effects_->call(*this, defId, "on_attack_after", p, inst);
      if (a.terminal) {  // 电磁炮 黄: dynamic 终端
        if (st.active != p)
          ps(p).cannotRespond = true;
        else
          abortMain_ = true;
      }
    }
    // 23-Akina O-S1 差列递归征税法: 资本 > 对手时必须再使用一次（照常支付费用）。
    if (!st.over) maybe_force_reuse(p, inst);
  } else {  // Action
    if (effects_->has(defId, "on_play")) effects_->call(*this, defId, "on_play", p, inst);
    fire("action_resolved", p, nullptr, inst, false);  // 模块化
  }
  if (has_full_power(inst)) {
    usedFullPowerThisTurn_[p] = true;
    fire("fullpower_used", p, nullptr, inst, false);
    check_dramas();  // 20-Kanawe《战栗》
    // 26-Innealra 诺伦: 对手使用全力牌时，你可以轮转一次命运槽（不共鸣）。
    if (!st.over && has_innealra(opp(p))) offer_fate_rotation(opp(p), "对手使用了全力牌");
  }
  if (has_terminal(inst) && st.active != p) ps(p).cannotRespond = true;
}

// 付与的打出效果（what.md 字面顺序）：
//   种植 → 给献（纳支付）→ 展开时（单一钩子，读最终献数）→ 献=0 立即弃置。
void Engine::resolve_enhance(Player p, int inst, const CardDef& d) {
  const int defId = d.id;
  // 1. 种植（19-Megumi 耕种）：先把 1 个种子移到「植株」。
  cultivate(p);
  // Re-expanding a card that still holds 献 (e.g. reuse): return the old ones first.
  if (card_crystal_count(inst) > 0) {
    int sak = 0;
    int old = card_crystal_count(inst);
    take_card_crystals(inst, old, kTakeNormal, &sak);
    if (sak > 0) decay_crystals(inst, sak);
  }
  // 2. 给献（纳支付；虚+装不足尽量多；0 则不放）。pay_nagi 直接把献放到牌上。
  pay_nagi(p, inst);
  // 进付与区（通常）/ 切牌区（切札付与）。
  if (d.kind == CardKind::Normal)
    move_card(inst, Zone::Enhance);
  else
    move_card(inst, Zone::Special);
  // 3. 展开时：单一钩子，此时献已落位（寄花等可读最终献数；反射装置/冰凌包覆
  //    等纳0牌在展开时自加结晶可免于弃置）。
  if (effects_->has(defId, "on_expand")) effects_->call(*this, defId, "on_expand", p, inst);
  // 19-Megumi 生长X：询问玩家是否将至多 X 个「植株」移到该牌上（可以不移动）。
  grow_enhance(p, inst);
  // 4. 献=0：立即弃置（弃置时效果照常触发）。
  if (card_crystal_count(inst) <= 0) {
    ci(inst).crystals = 0;
    ci(inst).green = 0;
    if (d.kind == CardKind::Normal) {
      move_card(inst, Zone::Discard);
      if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
      fire("enhance_left", p, nullptr, inst, false);
    } else {
      if (effects_->has(defId, "on_discard")) effects_->call(*this, defId, "on_discard", p, inst);
      // 26-Innealra 万劫缠迫: 弃置时把自己移出游戏 —— 不能又被送回切牌区。
      if (ci(inst).zone == Zone::Removed) return;
      move_card(inst, Zone::Special);
    }
  }
}

// ---------------------------------------------------------------------------
// 其它打出入口（同管线：压栈 → 结算 → 去向）
// ---------------------------------------------------------------------------

void Engine::play_from_cover(Player p, int inst, bool asResponse, bool toDeckAfter) {
  if (st.over) return;
  const CardDef& d = def_of(inst);
  // Only a card actually taken from the cover pile counts as "from the cover
  // pile"; 经纱/诸式理解 use play_from_cover for discard-pile cards too.
  const bool fromCover = (ci(inst).zone == Zone::Cover);
  move_card(inst, Zone::Limbo);
  push_play_frame(d.id, p, fromCover);
  PlayFrameGuard frameGuard{*this};
  ps(p).cardsPlayedThisTurn += 1;
  if (asResponse) {
    ps(p).responsesPlayedThisTurn += 1;
    responseOrdStack_.push_back(ps(p).responsesPlayedThisTurn);
    ps(p).respondedThisTurn = true;
  } else {
    responseOrdStack_.push_back(0);
  }
  resolve_card_effect(p, inst, asResponse);
  if (!responseOrdStack_.empty()) responseOrdStack_.pop_back();
  if (ci(inst).zone == Zone::Limbo) {
    if (toDeckAfter)
      move_card(inst, Zone::Deck);
    else
      move_card(inst, Zone::Discard);
  }
}

void Engine::use_from_cover(int inst, bool asResponse) {
  play_from_cover(ci(inst).owner, inst, asResponse, false);
}

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
  push_play_frame(defId, user, false);
  PlayFrameGuard frameGuard{*this};
  resolve_card_effect(user, inst, false);
  if (ci(inst).zone == Zone::Limbo) {
    // 切牌永远不会进入弃牌堆: a borrowed 切札 returns to a special zone (used),
    // everything else goes to its owner's discard pile.
    move_card(inst, def_of(inst).kind == CardKind::Special ? Zone::Special : Zone::Discard);
  }
}

}  // namespace fy
