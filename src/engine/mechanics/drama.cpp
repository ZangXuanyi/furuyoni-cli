// 20-Kanawe 叶慧: 地图 / 戏剧
// 地图节点表（node_table）与戏剧条件/推进/封杀全在此；节点表是纯数据，
// 戏剧条件按槽位硬编码（数据化见审计文档剩余清单）。框架与数据全在 C++
// （what.md 第 2 条），六张戏剧牌是纯标记（drama/drama_slot），ctx 方法块经
// mechanics/blocks.cpp 注册。
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
// 20-Kanawe 叶慧: 地图 / 戏剧
// ---------------------------------------------------------------------------
namespace {

// 地图（rules/20-kanawe.md）。color: -1 无 / 0 红 / 1 紫 / 2 绿 / 3 黄。
// next = 普通可达；trial = 「可试炼到」（只有完成升级版戏剧才能到达）。
struct MapNodeDef {
  const char* id;
  int color;
  int value;
  std::vector<const char*> next;
  std::vector<const char*> trial;
  bool terminal;
};

const std::vector<MapNodeDef>& node_table() {
  static const std::vector<MapNodeDef> t = {
      {"O2", -1, 2, {"1A", "1B", "2C"}, {}, false},
      {"1A", 0, 0, {"2A", "2B"}, {}, false},
      {"1B", 2, 2, {"2A", "2B"}, {}, false},
      {"2A", 0, 2, {"3A"}, {}, false},
      {"2B", 1, 4, {"3A", "3B"}, {"3C"}, false},
      {"2C", 0, 3, {"3D"}, {}, false},
      {"3A", 0, 4, {"4A"}, {}, false},
      {"3B", 2, 6, {"4A"}, {}, false},
      {"3C", 3, 4, {"4B"}, {}, false},
      {"3D", 1, 5, {}, {"4B"}, false},
      {"4A", 1, 0, {"5A", "5B", "5C"}, {}, false},
      {"4B", 0, 3, {"5C"}, {}, false},
      {"5A", 0, 2, {}, {"END"}, false},
      {"5B", 1, 0, {}, {"END"}, false},
      {"5C", 2, 0, {}, {"END"}, false},
      {"END", -1, -1, {}, {}, true},
  };
  return t;
}

const MapNodeDef* find_node(const std::string& id) {
  for (const MapNodeDef& n : node_table())
    if (id == n.id) return &n;
  return nullptr;
}

std::string node_label(const MapNodeDef& n) {
  if (n.terminal) return "终点";
  static const char* kColor[4] = {"红", "紫", "绿", "黄"};
  std::string s = n.id;
  if (n.color >= 0 && n.color < 4) s += std::string("(") + kColor[n.color] + " " +
                                       std::to_string(n.value) + ")";
  return s;
}

// 《戏剧》达成所需次数：T1..T3/T5/T6 基础 2 / 升级 1；《战栗》基础 1 / 升级 2；
// 《明转》升级的条件明写「达成次数2」。
int drama_need(int slot, int tier) {
  if (slot == 5) return tier == 1 ? 2 : 1;
  if (slot == 4 && tier == 1) return 2;
  return tier == 1 ? 1 : 2;
}

}  // namespace

int Engine::node_value(Player p) const {
  const MapNodeDef* n = find_node(ps(p).node);
  return n ? n->value : 0;
}

int Engine::node_color(Player p) const {
  const MapNodeDef* n = find_node(ps(p).node);
  return n ? n->color : -1;
}

void Engine::init_dramas(Player p) {
  ps(p).dramas.clear();
  ps(p).dramaPrepared = -1;
  ps(p).dramaProgressedThisTurn = false;
  ps(p).dramaProgressedLastTurn = false;
  ps(p).noDramaThisTurn = false;
  ps(p).node = "O2";
  // O-T1..O-T6 按槽位顺序建立实例（zone == Removed，由 PlayerState.dramas 保管）。
  for (int slot = 1; slot <= 6; ++slot)
    for (const CardDef& d : defs)
      if (d.isDrama && d.goddess == "kanawe" && d.dramaSlot == slot) {
        int inst = add_instance(d.id, p);
        store_int(inst, "tag", 0);          // 0 未完成 / 1 戏剧栏 / 2 已完成
        store_int(inst, "progress", 0);
        store_int(inst, "counted_turn", -1);
        ps(p).dramas.push_back(inst);
        break;
      }
}

bool Engine::prepare_drama(Player p, bool allowCompleted) {
  std::vector<int> cands;
  for (int inst : ps(p).dramas) {
    const int tag = load_int(inst, "tag", 0);
    if (tag == 1) continue;                      // 已在戏剧栏
    if (tag == 2 && !allowCompleted) continue;   // 已完成堆（撰写不可选）
    cands.push_back(inst);
  }
  if (cands.empty()) return false;

  Request r;
  r.kind = "option";
  r.prompt = "准备一个戏剧";
  for (int inst : cands) r.options.push_back({def_of(inst).name, true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(cands.size())) idx = 0;
  const int chosen = cands[static_cast<size_t>(idx)];
  const bool fromCompleted = load_int(chosen, "tag", 0) == 2;

  // 条件版本：基础版（达成次数多但条件宽）或升级版（可试炼到分支）。
  Request vr;
  vr.kind = "option";
  vr.prompt = "选择戏剧" + def_of(chosen).name + "的条件版本";
  vr.options.push_back({"基础版条件", true, {}});
  vr.options.push_back({"升级版条件（可试炼）", true, {}});
  const int vt = ask_one(p, std::move(vr));
  const int tier = vt == 1 ? 1 : 0;

  const int old = ps(p).dramaPrepared;
  if (old >= 0 && old != chosen) {  // 被换下的戏剧回到未完成堆，进度不保留
    store_int(old, "tag", 0);
    store_int(old, "progress", 0);
    store_int(old, "counted_turn", -1);
  }
  store_int(chosen, "tag", 1);
  store_int(chosen, "progress", 0);
  store_int(chosen, "counted_turn", -1);
  store_int(chosen, "prep_turn", st.turn);
  store_int(chosen, "tier", tier);
  ps(p).dramaPrepared = chosen;
  check_dramas();
  return fromCompleted;
}

bool Engine::drama_condition_met(Player p, int inst, int slot, int tier) const {
  switch (slot) {
    case 1: {  // 《杀阵》: 本回合双方进行的攻击 + 对应
      const int n = attacksThisTurn_[P0] + attacksThisTurn_[P1] +
                    ps(P0).responsesPlayedThisTurn + ps(P1).responsesPlayedThisTurn;
      return tier == 1 ? n >= 5 : n >= 2;
    }
    case 2:  // 《樱花》
      return tier == 1 ? crystalBatchAny_ >= 5 : crystalBatchNonCard_ >= 3;
    case 3:  // 《鼓动》
      return tier == 1 ? lifeChangeMaxThisTurn_ >= 2 : lifeChangedThisTurn_;
    case 4:  // 《明转》
      return cardCrystalMovesThisTurn_ >= (tier == 1 ? 2 : 1);
    case 5:  // 《战栗》: 不是本回合准备的 + 本回合使用过全力牌
      if (load_int(inst, "prep_turn", -1) == st.turn) return false;
      return usedFullPowerThisTurn_[P0] || usedFullPowerThisTurn_[P1];
    case 6: {  // 《定位》: 回合结束时的距离变化
      int d = distance() - distanceAtTurnStart_;
      if (d < 0) d = -d;
      if (tier == 1) return d >= 5;
      return d >= 2 && distance() <= 8;
    }
    default:
      (void)p;
      return false;
  }
}

void Engine::check_dramas(bool atTurnEnd) {
  if (st.over) return;
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const int inst = ps(p).dramaPrepared;
    if (inst < 0) continue;
    if (load_int(inst, "tag", 0) != 1) continue;
    if (load_int(inst, "counted_turn", -1) == st.turn) continue;  // 同回合只计 1 次
    const int slot = def_of(inst).dramaSlot;
    const int tier = load_int(inst, "tier", 0);
    if (slot == 6 && !atTurnEnd) continue;  // 《定位》只在回合结束时判定
    if (!drama_condition_met(p, inst, slot, tier)) continue;
    const int need = drama_need(slot, tier);
    int prog = load_int(inst, "progress", 0);
    // 演出: 本回合不能完成戏剧（这一回合的达成不计入）。
    if (ps(p).noDramaThisTurn && prog + 1 >= need) continue;
    store_int(inst, "counted_turn", st.turn);
    prog += 1;
    store_int(inst, "progress", prog);
    ps(p).dramaProgressedThisTurn = true;
    if (prog >= need) {
      store_int(inst, "tag", 2);  // 完成的戏剧立即进入已完成堆
      ps(p).dramaPrepared = -1;
      // 推进地图会询问玩家并可能插入伤害结算，因此延迟到下一个安全点。
      pendingAdvance_[pi] = true;
      pendingAdvanceTier_[pi] = tier == 1;
    }
  }
}

// 在安全的决策点结算「完成戏剧 → 前进一格」。advance_node 会询问玩家并结算
// 落格奖励（红/紫/绿），期间可能嵌套伤害；这里保存并恢复引擎的瞬时全局，
// 使调用者（可能是 apply_damage_to / resolve_attack 的中间态）不受影响。
void Engine::flush_drama_advances() {
  for (int pi = 0; pi < 2; ++pi) {
    if (!pendingAdvance_[pi]) continue;
    pendingAdvance_[pi] = false;
    const bool tier = pendingAdvanceTier_[pi];
    // 推进地图会触发玩家决策/事件；「最近一次伤害/攻击」记录跨过该窗口保存，
    // 供之后的即再起/攻击后谓词读取。伤害路由已是参数（DamageRoute），无需保存。
    const int sDmgSide = lastDmgSide_, sDmgAmount = lastDmgAmount_;
    const bool sDmgFrom = lastDmgFromAttack_;
    const int sAtkSide = lastAtkSide_, sAtkAmount = lastAtkAmount_;
    const int sPendCard = pendingDamageToCard_;
    advance_node(static_cast<Player>(pi), tier);
    lastDmgSide_ = sDmgSide;
    lastDmgAmount_ = sDmgAmount;
    lastDmgFromAttack_ = sDmgFrom;
    lastAtkSide_ = sAtkSide;
    lastAtkAmount_ = sAtkAmount;
    pendingDamageToCard_ = sPendCard;
    if (st.over) return;
  }
}

bool Engine::advance_node(Player p, bool upgraded) {
  const MapNodeDef* cur = find_node(ps(p).node);
  if (!cur || cur->terminal) return false;
  std::vector<const MapNodeDef*> opts;
  for (const char* id : cur->next)
    if (const MapNodeDef* n = find_node(id)) opts.push_back(n);
  if (upgraded)
    for (const char* id : cur->trial)
      if (const MapNodeDef* n = find_node(id)) opts.push_back(n);
  if (opts.empty()) return false;  // 无可前进的分支（例如 5A 的基础版）

  Request r;
  r.kind = "option";
  r.prompt = "地图：从 " + std::string(cur->id) + " 选择前进的节点";
  for (const MapNodeDef* n : opts) r.options.push_back({node_label(*n), true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(opts.size())) idx = 0;
  const MapNodeDef* dest = opts[static_cast<size_t>(idx)];
  ps(p).node = dest->id;
  fire("node_advanced", p, nullptr, -1, false);
  if (st.over) return true;
  if (dest->terminal) {
    die(opp(p));  // 走到终点：对手死亡
    return true;
  }
  resolve_node_reward(p);  // 落格奖励（按颜色）
  return true;
}

void Engine::resolve_node_reward(Player p) {
  switch (node_color(p)) {
    case 0:  // 红: 对敌人造成 1 命伤
      damage_life(opp(p), 1, AreaKind::Flare, true);
      break;
    case 1:  // 紫: 执行一次基本动作
      forced_basic(p);
      break;
    case 2: {  // 绿: 从盖牌区选一张放到牌库底
      if (ps(p).cover.empty()) break;
      Request r;
      r.kind = "cards";
      r.prompt = "绿：从盖牌区选择一张放到你的牌库底";
      for (int inst : ps(p).cover) {
        Option o;
        o.label = card_label(def_of(inst));
        o.data = {{"inst", inst}};
        r.options.push_back(o);
      }
      int idx = ask_one(p, std::move(r));
      if (idx < 0 || idx >= static_cast<int>(ps(p).cover.size())) idx = 0;
      move_card_bottom(ps(p).cover[static_cast<size_t>(idx)]);
      break;
    }
    case 3:  // 黄: 持续光环（apply_node_attack_bonus）
    default:
      break;
  }
}

void Engine::forced_basic(Player p) {
  std::vector<BasicAction> legal;
  for (int bi = 0; bi < 5; ++bi) {
    BasicAction ba = static_cast<BasicAction>(bi);
    if (basic_legal(p, ba)) legal.push_back(ba);
  }
  if (legal.empty()) return;
  Request r;
  r.kind = "option";
  r.prompt = "执行一次基本动作";
  for (BasicAction ba : legal) r.options.push_back({basic_name(ba), true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(legal.size())) idx = 0;
  do_basic(p, legal[static_cast<size_t>(idx)]);
}

void Engine::apply_node_attack_bonus(Player p, Attack& a) const {
  if (a.generated) return;             // 只强化非衍生攻击
  if (node_color(p) != 3) return;      // 站在黄色地点
  if (a.life.has_value()) a.lifeDelta += 1;  // +0/+1
}

// 20-Kanawe 封杀: 生效中，对手不能使用与宣言牌名相同的名称的切牌；若当前剧目
// 颜色为红（封杀持有者的当前剧目），对手也不能使用同名通常牌。
bool Engine::cut_name_banned(Player p, int inst) const {
  const CardKind kind = def_of(inst).kind;
  if (kind != CardKind::Special && kind != CardKind::Normal) return false;
  const Player o = opp(p);
  const bool red = node_color(o) == 0;  // 0 = 红
  auto banned_by = [&](int e) {
    if (!def_of(e).cutBan) return false;
    const int d = load_int(e, "ban_def", -1);
    if (d < 0 || d >= static_cast<int>(defs.size())) return false;
    if (defs[static_cast<size_t>(d)].name != def_of(inst).name) return false;
    if (kind == CardKind::Special) return true;
    return red;  // 通常牌: 仅当封杀持有者当前剧目为红
  };
  for (int e : ps(o).enhance)
    if (banned_by(e)) return true;
  for (int e : ps(o).special)
    if (ci(e).faceUp && banned_by(e)) return true;
  return false;
}

int Engine::declare_cut_ban(Player p, int inst) {
  const Player o = opp(p);
  std::vector<int> cands;  // def ids
  for (const CardDef& d : defs) {
    // 宣言范围 = 对手构筑池中的常规牌与切牌（剧目/追加牌/零件/士兵等除外）。
    if (d.isDrama || d.isExtra || d.isPart || d.isPoison || d.isTransform || d.soldier)
      continue;
    bool inSets = false;
    for (const std::string& s : playerSets_[o])
      if (s == d.set) inSets = true;
    if (!inSets) continue;
    bool dup = false;
    for (int c : cands)
      if (defs[static_cast<size_t>(c)].name == d.name) dup = true;
    if (!dup) cands.push_back(d.id);
  }
  if (cands.empty()) return -1;
  Request r;
  r.kind = "option";
  r.prompt = "封杀：宣言一个牌名（切牌立刻生效；通常牌仅在你当前剧目为红时生效）";
  for (int c : cands) r.options.push_back({defs[static_cast<size_t>(c)].name, true, {}});
  int idx = ask_one(p, std::move(r));
  if (idx < 0 || idx >= static_cast<int>(cands.size())) idx = 0;
  const int defId = cands[static_cast<size_t>(idx)];
  store_int(inst, "ban_def", defId);
  return defId;
}
namespace mechanics {

void drama_ctx(CtxTypes& t) {
  auto& ctx = t.ctx;

  // ---- 20-Kanawe 叶慧: 地图 / 戏剧 ------------------------------------------
  // 当前所在地的剧目数值 / 颜色（"red"/"purple"/"green"/"yellow"/"none"）。
  ctx["node_value"] = [](LuaCtx& c) { return c.e->node_value(c.who); };
  ctx["node_color"] = [](LuaCtx& c) -> std::string {
    switch (c.e->node_color(c.who)) {
      case 0: return "red";
      case 1: return "purple";
      case 2: return "green";
      case 3: return "yellow";
      default: return "none";
    }
  };
  ctx["node_name"] = [](LuaCtx& c) { return c.e->node_name(c.who); };
  // 准备下一幕戏剧；返回是否选择了已完成过的戏剧（疾书弗尽据此移出游戏）。
  ctx["prepare_drama"] = [](LuaCtx& c, sol::optional<bool> allowCompleted) {
    return c.e->prepare_drama(c.who, allowCompleted ? *allowCompleted : false);
  };
  ctx["drama_progressed_last_turn"] = [](LuaCtx& c, int p) {
    return c.e->drama_progressed_last_turn(static_cast<Player>(p));
  };
  // 演出: 本回合不能完成戏剧。
  ctx["set_no_drama"] = [](LuaCtx& c) { c.e->set_no_drama_this_turn(c.who); };
  // 芳颜无常: 结算一次当前所在地的效果（红/紫/绿）。
  ctx["resolve_node_reward"] = [](LuaCtx& c) { c.e->resolve_node_reward(c.who); };
  // 即兴: 把一张手牌当作对应打出。
  ctx["play_hand_card_as_response"] = [](LuaCtx& c, int inst) {
    c.e->play_hand_card_response(c.who, inst);
  };
  // 知音难觅: 构筑时未获得的常规牌 / 切牌。
  ctx["unchosen_normals"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    std::vector<int> v = c.e->unchosen_normals(static_cast<Player>(p));
    sol::table t = sol::table::create(ts.L);
    for (size_t i = 0; i < v.size(); ++i) t[i + 1] = v[i];
    return sol::make_object(ts.L, t);
  };
  ctx["unchosen_specials"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    std::vector<int> v = c.e->unchosen_specials(static_cast<Player>(p));
    sol::table t = sol::table::create(ts.L);
    for (size_t i = 0; i < v.size(); ++i) t[i + 1] = v[i];
    return sol::make_object(ts.L, t);
  };
  ctx["gain_unchosen_normal"] = [](LuaCtx& c, int inst) {
    c.e->gain_unchosen_normal(c.who, inst);
  };
  ctx["gain_unchosen_cut"] = [](LuaCtx& c, int inst) { c.e->gain_unchosen_cut(inst); };
  // 移出游戏（不同于「追加区」：不会被重新获得）。
  ctx["remove_from_game"] = [](LuaCtx& c, int inst) { c.e->remove_from_game(inst); };
  // 封杀: 宣言一个牌名（记录在本牌上），对手不能使用同名切牌。
  ctx["declare_cut_ban"] = [](LuaCtx& c) { return c.e->declare_cut_ban(c.who, c.source); };
  ctx["attacks_and_responses"] = [](LuaCtx& c) { return c.e->attacks_and_responses_this_turn(); };

  // ---- 22-Renri 夜山恋离: 伪证 / 回归 / 铭镌之衣 / 洛阳铲 -------------------

}

}  // namespace mechanics
}  // namespace fy
