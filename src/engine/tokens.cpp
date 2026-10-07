// 统一 Token 系统（what.md 第 1 条）。
//
// 本文件是全部结晶/异樱移动的唯一策略所在：容量（装含冰晶占位、命上限）、
// 重定向（迷烟 / 血飞沫 / 脆弱意志 / 萨利亚的杰作 / 终结之果实）、守恒审计
// （External 记账）、区域事件（aura_changed / distance_changed / armor_full /
// pneumatic）与戏剧计数（《樱花》《明转》）。
//
// 迁移原则（Phase 1）：存储仍是 GameState 既有字段；本层只统一地址与策略。
// 旧入口 add_crystals/move_crystals/gain_external/lose_external/burn/recover/
// freeze/thaw/seed_to_plant/attach_green/detach_green_to_seeds/add_wound 均已
// 变为 token_adjust/token_move 的薄包装。
#include <limits>

#include "engine/engine_internal.hpp"

namespace fy {

namespace area {

const char* name(AreaKind k) {
  switch (k) {
    case AreaKind::Life:           return "life";
    case AreaKind::Aura:           return "aura";
    case AreaKind::Flare:          return "flare";
    case AreaKind::Distance:       return "distance";
    case AreaKind::Dust:           return "dust";
    case AreaKind::Card:           return "card";
    case AreaKind::Market:         return "market";
    case AreaKind::Waku:           return "waku";
    case AreaKind::Soil:           return "soil";
    case AreaKind::SteamEngine:    return "steam_engine";
    case AreaKind::SteamExhausted: return "steam_exhausted";
    case AreaKind::SteamOnDist:    return "steam_dist";
    case AreaKind::SteamOnCrystal: return "steam_crystal";
    case AreaKind::External:       return "external";
  }
  return "?";
}

}  // namespace area

// ---------------------------------------------------------------------------
// 计数
// ---------------------------------------------------------------------------

int Engine::token_amount(const AreaRef& a, Token kind) const {
  switch (kind) {
    case Token::Sakura:
      switch (a.kind) {
        case AreaKind::Life:           return ps(a.p).life;
        case AreaKind::Aura:           return ps(a.p).aura;  // 不含冰晶（冰晶不承伤）
        case AreaKind::Flare:          return ps(a.p).flare;
        case AreaKind::Distance:       return st.distance;
        case AreaKind::Dust:           return st.dust;
        case AreaKind::Card:           return ci(a.inst).crystals;
        case AreaKind::Market:         return ps(a.p).market;
        case AreaKind::Waku:           return ps(a.p).waku;
        case AreaKind::External:       return externalAdded_;
        case AreaKind::Soil:           return 0;  // 土壤只放种子/植株，不放樱花
        case AreaKind::SteamEngine:
        case AreaKind::SteamExhausted:
        case AreaKind::SteamOnDist:
        case AreaKind::SteamOnCrystal: return 0;
      }
      return 0;
    case Token::Green:
      return a.kind == AreaKind::Card ? ci(a.inst).green : 0;
    case Token::Seed:
      return a.kind == AreaKind::Soil ? ps(a.p).soilSeeds : 0;
    case Token::Plant:
      return a.kind == AreaKind::Soil ? ps(a.p).soilPlants : 0;
    case Token::Steam:
      switch (a.kind) {
        case AreaKind::SteamEngine:    return ps(a.p).steamEngine;
        case AreaKind::SteamExhausted: return ps(a.p).steamExhausted;
        case AreaKind::SteamOnDist:    return ps(a.p).steamOnDist;
        case AreaKind::SteamOnCrystal: return ps(a.p).steamOnCrystal;
        default:                       return 0;
      }
    case Token::Ice:
      return a.kind == AreaKind::Aura ? ps(a.p).ice : 0;
    case Token::Wound:
      if (!area::wound_area(a.kind)) return 0;
      return ps(a.p).wound[area::wound_index(a.kind)][static_cast<int>(a.by)];
  }
  return 0;
}

// ---------------------------------------------------------------------------
// 增减（无对端）
// ---------------------------------------------------------------------------

void Engine::token_adjust(const AreaRef& a, Token kind, int n) {
  switch (kind) {
    case Token::Sakura: {
      switch (a.kind) {
        case AreaKind::Life: {
          const int before = ps(a.p).life;
          ps(a.p).life = std::clamp(ps(a.p).life + n, 0, st.maxLife);
          note_life_change(ps(a.p).life - before);  // 20-Kanawe 《鼓动》
          break;
        }
        case AreaKind::Aura: {
          bool was_full = armor_full(a.p);
          ps(a.p).aura =
              std::clamp(ps(a.p).aura + n, 0, std::max(0, max_aura(a.p) - ps(a.p).ice));
          // 装变满的瞬间（吹雪式的即再起）——不是由本牌的冻结造成的。
          if (!was_full && n > 0 && armor_full(a.p)) fire("armor_full", a.p, nullptr, -1, false);
          break;
        }
        case AreaKind::Flare:
          ps(a.p).flare = std::max(0, ps(a.p).flare + n);
          break;
        case AreaKind::Distance:
          if (n != 0 && st.distance != std::max(0, st.distance + n)) note_distance_changed();
          st.distance = std::max(0, st.distance + n);
          break;
        case AreaKind::Dust:
          // 22-Renri 罗织: 结晶离开虚也计入「移出虚」。
          if (n < 0 && st.dust > 0) crystalLeftDustThisTurn_ = true;
          st.dust = std::max(0, st.dust + n);
          break;
        case AreaKind::Card:
          ci(a.inst).crystals = std::max(0, ci(a.inst).crystals + n);
          break;
        case AreaKind::Market:
          ps(a.p).market = std::max(0, ps(a.p).market + n);
          break;
        case AreaKind::Waku:
          ps(a.p).waku = std::max(0, ps(a.p).waku + n);
          break;
        case AreaKind::External:
          externalAdded_ += n;  // 游戏外池 = 净引入量（守恒审计）
          break;
        case AreaKind::Soil:
        case AreaKind::SteamEngine:
        case AreaKind::SteamExhausted:
        case AreaKind::SteamOnDist:
        case AreaKind::SteamOnCrystal:
          break;  // 樱花不进这些区域
      }
      break;
    }
    case Token::Green:
      if (a.kind == AreaKind::Card) ci(a.inst).green = std::max(0, ci(a.inst).green + n);
      break;
    case Token::Seed:
      if (a.kind == AreaKind::Soil) ps(a.p).soilSeeds = std::max(0, ps(a.p).soilSeeds + n);
      break;
    case Token::Plant:
      if (a.kind == AreaKind::Soil) ps(a.p).soilPlants = std::max(0, ps(a.p).soilPlants + n);
      break;
    case Token::Steam:
      switch (a.kind) {
        case AreaKind::SteamEngine:    ps(a.p).steamEngine = std::max(0, ps(a.p).steamEngine + n); break;
        case AreaKind::SteamExhausted: ps(a.p).steamExhausted = std::max(0, ps(a.p).steamExhausted + n); break;
        case AreaKind::SteamOnDist:    ps(a.p).steamOnDist = std::max(0, ps(a.p).steamOnDist + n); break;
        case AreaKind::SteamOnCrystal: ps(a.p).steamOnCrystal = std::max(0, ps(a.p).steamOnCrystal + n); break;
        default: break;
      }
      break;
    case Token::Ice:
      if (a.kind == AreaKind::Aura) ps(a.p).ice = std::max(0, ps(a.p).ice + n);
      break;
    case Token::Wound:
      if (area::wound_area(a.kind))
        ps(a.p).wound[area::wound_index(a.kind)][static_cast<int>(a.by)] =
            std::max(0, ps(a.p).wound[area::wound_index(a.kind)][static_cast<int>(a.by)] + n);
      break;
  }
}

// ---------------------------------------------------------------------------
// 唯一移动入口
// ---------------------------------------------------------------------------

int Engine::token_move(const MoveReq& m) {
  if (m.n <= 0) return 0;
  if (m.fromKind != Token::Sakura || m.toKind != Token::Sakura)
    return move_special_token(m);
  if (m.from.kind == AreaKind::External) return move_from_external(m);
  if (m.to.kind == AreaKind::External) return move_to_external(m);
  if (m.from.kind == AreaKind::Card) return move_from_card(m);
  return move_sakura(m);
}

// 樱花的一般移动：迷烟 / 容量 / 血飞沫 / 脆弱意志 / 事件 / 计数。
// （原 move_crystals 的全部策略，逐句保持等价。）
int Engine::move_sakura(const MoveReq& m) {
  const AreaRef& from = m.from;
  const AreaRef& to = m.to;
  const int n = m.n;
  // 土壤/蒸汽区只收异樱 token，不参与樱花移动。
  if (from.kind == AreaKind::Soil || area::steam(from.kind) ||
      to.kind == AreaKind::Soil || area::steam(to.kind))
    return 0;
  // 迷烟: card effects that would change distance are negated (basic actions are not).
  if (m.cardEffect && (from.kind == AreaKind::Distance || to.kind == AreaKind::Distance) &&
      any_lock_distance())
    return 0;
  int cap = std::numeric_limits<int>::max();
  switch (to.kind) {
    case AreaKind::Life: cap = st.maxLife - ps(to.p).life; break;
    case AreaKind::Aura:
      cap = max_aura(to.p) - ps(to.p).aura - ps(to.p).ice;  // 冰晶也占位
      break;
    default: break;
  }
  int moved = std::min({n, token_amount(from, Token::Sakura), cap});
  if (moved <= 0) return 0;
  const bool touchesDistance =
      (from.kind == AreaKind::Distance || to.kind == AreaKind::Distance);
  const int distBefore = touchesDistance ? distance() : 0;
  // 鱼雷炮击的即再起需要“对手的回合内距减小 2 或以上”的通知。必须在整个移动完成之后
  // 才广播：事件会触发玩家决策（不变量检查），半途中的结晶总数会被判为不守恒。
  auto notify_distance = [&]() {
    if (!touchesDistance) return;
    const int d = distance();
    if (d != distBefore) {
      note_distance_changed();  // 18-Mizuki 阵地
      fire("distance_changed", st.active, nullptr, -1, d < distBefore);
    }
  };
  // 血飞沫: 若任意数量的樱花结晶将被移动到敌装，则改为移动到虚，并此牌上的 1 个献移动到虚。
  if (to.kind == AreaKind::Aura && from.kind != AreaKind::Card) {
    int host = deny_aura_host(to.p);
    if (host >= 0) {
      int a0 = st.p[P0].aura, a1 = st.p[P1].aura;
      token_adjust(from, Token::Sakura, -moved);
      token_adjust(AreaRef::dust(), Token::Sakura, moved);
      if (ci(host).crystals + ci(host).green > 0) {
        int sak = 0;
        take_card_crystals(host, 1, kTakeNormal, &sak);
        if (sak > 0) token_adjust(AreaRef::dust(), Token::Sakura, sak);
        drop_enhance_if_empty(host);
      }
      if (st.p[P0].aura != a0) notify_aura_changed(P0);
      if (st.p[P1].aura != a1) notify_aura_changed(P1);
      notify_distance();
      if (m.notes) {
        note_crystal_move(from, AreaRef::dust(), moved, m.cardEffect);
        check_dramas();
      }
      return moved;
    }
  }
  // 26-Innealra O2-N6 脆弱意志: 对手（此牌的持有者）因「装附外的手段」把樱花结晶
  // 移到自装时改为移到这张牌上；因「基本动作」装附时照常移动，然后从这张牌上把
  // 1 个樱花结晶移到虚。
  if (to.kind == AreaKind::Aura && from.kind != AreaKind::Card) {
    int host = fragile_will_host(to.p);
    if (host >= 0) {
      const bool byBasic = inBasicAction_ && basicActor_ == to.p;
      int a0b = st.p[P0].aura, a1b = st.p[P1].aura;
      token_adjust(from, Token::Sakura, -moved);
      token_adjust(byBasic ? to : AreaRef::card(host), Token::Sakura, moved);
      if (st.p[P0].aura != a0b) notify_aura_changed(P0);
      if (st.p[P1].aura != a1b) notify_aura_changed(P1);
      notify_distance();
      if (m.notes) note_crystal_move(from, byBasic ? to : AreaRef::card(host), moved, m.cardEffect);
      if (byBasic) {
        int sak = 0;
        take_card_crystals(host, 1, kTakeNormal, &sak);
        if (sak > 0) token_adjust(AreaRef::dust(), Token::Sakura, sak);
        drop_enhance_if_empty(host);
      }
      if (m.notes) check_dramas();
      return moved;
    }
  }
  int a0 = st.p[P0].aura, a1 = st.p[P1].aura;
  token_adjust(from, Token::Sakura, -moved);
  token_adjust(to, Token::Sakura, moved);
  if (st.p[P0].aura != a0) notify_aura_changed(P0);
  if (st.p[P1].aura != a1) notify_aura_changed(P1);
  notify_distance();
  if (m.notes) {
    note_crystal_move(from, to, moved, m.cardEffect);
    check_dramas();
  }
  return moved;
}

// 从游戏外引入结晶（原 gain_external；守恒记账 +，不触发移动计数）。
int Engine::move_from_external(const MoveReq& m) {
  if (m.to.kind == AreaKind::External) return 0;
  int cap = std::numeric_limits<int>::max();
  if (m.to.kind == AreaKind::Life) cap = st.maxLife - ps(m.to.p).life;
  if (m.to.kind == AreaKind::Aura) cap = max_aura(m.to.p) - ps(m.to.p).aura - ps(m.to.p).ice;
  const int moved = std::min(m.n, cap);
  if (moved <= 0) return 0;
  const int before = token_amount(m.to, Token::Sakura);
  token_adjust(m.to, Token::Sakura, moved);
  externalAdded_ += token_amount(m.to, Token::Sakura) - before;
  return moved;
}

// 把结晶移到游戏外（原 lose_external；守恒记账 -）。
int Engine::move_to_external(const MoveReq& m) {
  const int before = token_amount(m.from, Token::Sakura);
  token_adjust(m.from, Token::Sakura, -m.n);
  const int moved = before - token_amount(m.from, Token::Sakura);
  externalAdded_ -= moved;
  return moved;
}

// 从牌上移走结晶：先过牌上移除策略（免疫/重定向/先樱后绿——绿色部分按既有
// 规则回流，不经目的地），樱花部分再入位到目的地。
int Engine::move_from_card(const MoveReq& m) {
  if (m.to.kind == AreaKind::Card) return 0;
  int sak = 0;
  take_card_crystals(m.from.inst, m.n, m.takeMode, &sak);
  if (sak <= 0) return 0;
  if (m.to.kind == AreaKind::External) {
    externalAdded_ -= sak;
    if (m.notes) note_crystal_move(m.from, AreaRef::external(), sak, m.cardEffect);
    return sak;
  }
  // 入位：容量先行（溢出部分进虚，保持守恒），事件/计数与一般移动一致。
  int cap = std::numeric_limits<int>::max();
  if (m.to.kind == AreaKind::Life) cap = st.maxLife - ps(m.to.p).life;
  if (m.to.kind == AreaKind::Aura) cap = max_aura(m.to.p) - ps(m.to.p).aura - ps(m.to.p).ice;
  const int placed = std::min(sak, cap);
  const bool touchesDistance = m.to.kind == AreaKind::Distance;
  const int distBefore = touchesDistance ? distance() : 0;
  int a0 = st.p[P0].aura, a1 = st.p[P1].aura;
  token_adjust(m.to, Token::Sakura, placed);
  if (placed < sak) token_adjust(AreaRef::dust(), Token::Sakura, sak - placed);
  if (st.p[P0].aura != a0) notify_aura_changed(P0);
  if (st.p[P1].aura != a1) notify_aura_changed(P1);
  if (touchesDistance && distance() != distBefore) {
    note_distance_changed();
    fire("distance_changed", st.active, nullptr, -1, distance() < distBefore);
  }
  if (m.notes) {
    note_crystal_move(m.from, m.to, placed, m.cardEffect);
    check_dramas();
  }
  return placed;
}

// ---------------------------------------------------------------------------
// 异樱分支
// ---------------------------------------------------------------------------

int Engine::move_special_token(const MoveReq& m) {
  const Player p = m.from.p;
  // ---- 蒸汽（11-Thallya）-------------------------------------------------
  if (m.fromKind == Token::Steam && m.toKind == Token::Steam) {
    if (!area::steam(m.from.kind) || !area::steam(m.to.kind)) return 0;
    // 萨利亚的杰作: 燃烧（引擎→用尽）反转为恢复。
    AreaRef from = m.from, to = m.to;
    if (from.kind == AreaKind::SteamEngine && to.kind == AreaKind::SteamExhausted &&
        has_named_active(p, cards::kSariaNoKessaku))
      std::swap(from, to);
    const int moved = std::min(m.n, token_amount(from, Token::Steam));
    if (moved <= 0) return 0;
    token_adjust(from, Token::Steam, -moved);
    token_adjust(to, Token::Steam, moved);
    // 气动落位：距上蒸汽改变有效距离。
    if (to.kind == AreaKind::SteamOnDist || to.kind == AreaKind::SteamOnCrystal) {
      note_distance_changed();  // 18-Mizuki 阵地（气动改变有效距离）
      fire("pneumatic", p, nullptr, -1, false);
    }
    return moved;
  }
  // ---- 冰晶（15-Konuru）：自游戏外置入装位 / 移除 --------------------------
  if (m.fromKind == Token::Ice && m.toKind == Token::Ice) {
    if (m.to.kind == AreaKind::Aura && m.from.kind == AreaKind::External) {
      const int add = std::min(m.n, aura_free(m.to.p));  // 冻结目标玩家的装余量
      if (add <= 0) return 0;
      const bool was_full = armor_full(m.to.p);
      token_adjust(m.to, Token::Ice, add);
      if (!was_full && armor_full(m.to.p))
        fire("armor_full", m.to.p, nullptr, m.cause, false);
      return add;
    }
    if (m.from.kind == AreaKind::Aura && m.to.kind == AreaKind::External) {
      const int moved = std::min(m.n, ps(m.from.p).ice);
      token_adjust(m.from, Token::Ice, -moved);
      return moved;
    }
    return 0;
  }
  // ---- 种子/植株/绿晶（19-Megumi）----------------------------------------
  if (m.fromKind == Token::Seed && m.toKind == Token::Plant &&
      m.from.kind == AreaKind::Soil && m.to.kind == AreaKind::Soil) {
    const int moved = std::min(m.n, ps(p).soilSeeds);
    if (moved <= 0) return 0;
    token_adjust(m.from, Token::Seed, -moved);
    token_adjust(m.to, Token::Plant, moved);
    return moved;
  }
  if (m.fromKind == Token::Plant && m.toKind == Token::Green &&
      m.from.kind == AreaKind::Soil && m.to.kind == AreaKind::Card) {
    const int moved = std::min(m.n, ps(p).soilPlants);
    if (moved <= 0) return 0;
    token_adjust(m.from, Token::Plant, -moved);
    token_adjust(m.to, Token::Green, moved);
    return moved;
  }
  if (m.fromKind == Token::Green && m.toKind == Token::Seed &&
      m.from.kind == AreaKind::Card && m.to.kind == AreaKind::Soil) {
    const int moved = std::min(m.n, ci(m.from.inst).green);
    if (moved <= 0) return 0;
    token_adjust(m.from, Token::Green, -moved);
    token_adjust(m.to, Token::Seed, moved);
    return moved;
  }
  // ---- 裂伤（24-Shisui）：自游戏外置入 (区域, 施加者) ----------------------
  if (m.fromKind == Token::Wound && m.toKind == Token::Wound) {
    if (m.from.kind != AreaKind::External || !area::wound_area(m.to.kind)) return 0;
    if (st.over) return 0;
    token_adjust(m.to, Token::Wound, m.n);
    // 「命区域中来自同一玩家的裂伤数 > 其命时：立即伤害化该玩家命区域里同一来源
    // 的那些裂伤」（塞入的那一刻）。
    if (m.to.kind == AreaKind::Life) {
      const Player src = m.to.by;
      while (!st.over &&
             ps(m.to.p).wound[kWoundLife][static_cast<int>(src)] > ps(m.to.p).life)
        resolve_wound_group(m.to.p, kWoundLife, src);
    }
    return m.n;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// 纳支付（从 resolve_card_effect 抽出的命名操作）
// ---------------------------------------------------------------------------

int Engine::pay_nagi(Player p, int inst) {
  const CardDef& d = def_of(inst);
  int take = 0;
  // 22-Renri 道化的觉悟: 纳 中至少要有 nagiFromLife 个结晶来自持有者的命。
  const int lifePool = d.nagiFromLife > 0 ? ps(p).life : 0;
  // 26-Innealra 舍弃·希冀: 这张牌的献可以从「距」中选择。
  const int distPool = d.nagiFromDistance ? st.distance : 0;
  int total = st.dust + ps(p).aura + lifePool + distPool;
  // 虚伪: the opponent's newly expanded 付与 has 纳 -1 while it is expanded.
  int enemyNagiMod = 0;
  if (d.nagi >= 0) {
    for (int oi : ps(opp(p)).enhance) enemyNagiMod += def_of(oi).enemyNagiMod;
    for (int oi : ps(opp(p)).special)
      if (ci(oi).faceUp) enemyNagiMod += def_of(oi).enemyNagiMod;
  }
  int nagiVal = nagi_value(d.id, p, inst) + pendingNagiAdjust_ + enemyNagiMod;
  pendingNagiAdjust_ = 0;
  if (nagiVal < 0) nagiVal = 0;
  if (total > 0 && nagiVal > 0) {
    take = std::min(nagiVal, total);
    int fromDistance = 0;
    if (d.nagiFromDistance && distPool > 0) {
      const int maxD = std::min(take, distPool);
      if (maxD > 0) {
        Request r;
        r.kind = "option";
        r.prompt = "舍弃·希冀：从「距」取几个结晶作为献？";
        for (int k = 0; k <= maxD; ++k)
          r.options.push_back({"距 " + std::to_string(k), true, {}});
        int pick = ask_one(p, std::move(r));
        fromDistance = (pick >= 0 && pick <= maxD) ? pick : 0;
      }
    }
    const int afterDist = take - fromDistance;
    int fromLife = 0;
    if (d.nagiFromLife > 0) {
      // 至少有 nagiFromLife 个来自命；虚+装不足的部分也从命补足，
      // 否则会凭空产生结晶（结晶守恒）。
      const int pool = st.dust + ps(p).aura;
      const int need = std::max(d.nagiFromLife, afterDist - pool);
      fromLife = std::min({need, afterDist, ps(p).life});
    }
    const int rest = afterDist - fromLife;  // 其余从虚/装支付
    int loDust = std::max(0, rest - ps(p).aura);
    int hiDust = std::min(rest, st.dust);
    if (loDust > hiDust) loDust = hiDust;  // 保守：不凭空产生结晶
    int fromDust = loDust;
    if (loDust != hiDust) {
      Request r;
      r.kind = "option";
      r.prompt = "choose 纳 cost split";
      for (int df = loDust; df <= hiDust; ++df) {
        Option o;
        o.label = "dust " + std::to_string(df) + " + aura " + std::to_string(rest - df);
        r.options.push_back(o);
      }
      fromDust = loDust + ask_one(p, std::move(r));
    }
    int auraBefore = ps(p).aura;
    // notes=false：纳支付沿用旧例，不计入《樱花》《明转》/罗织（Phase 2 复审）。
    auto payFrom = [&](AreaRef from, int n) {
      if (n <= 0) return;
      MoveReq r;
      r.from = from;
      r.to = AreaRef::card(inst);
      r.n = n;
      r.notes = false;
      token_move(r);
    };
    payFrom(AreaRef::dust(), fromDust);
    payFrom(AreaRef::aura(p), rest - fromDust);
    payFrom(AreaRef::life(p), fromLife);
    payFrom(AreaRef::distance(), fromDistance);
    if (ps(p).aura != auraBefore) notify_aura_changed(p);
  }
  return take;
}

}  // namespace fy
