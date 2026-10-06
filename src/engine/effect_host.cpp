#include "engine/effect_host.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>

#include "engine/card_names.hpp"
#include "engine/engine.hpp"
#include "protocol/agent.hpp"

namespace fy {

namespace {

uint32_t kwflag(const std::string& s) {
  if (s == "unrespondable" || s == "不可对") return AF_Unrespondable;
  if (s == "lock" || s == "锁定") return AF_Lock;
  if (s == "overwhelm" || s == "超克") return AF_Overwhelm;
  if (s == "both_sides" || s == "两侧") return AF_BothSides;
  if (s == "no_special_response" || s == "切牌不可对") return AF_NoSpecialResponse;
  if (s == "no_normal_response" || s == "通常牌不可对") return AF_NoNormalResponse;
  if (s == "no_enhance_response" || s == "付与牌不可对") return AF_NoEnhanceResponse;
  if (s == "no_attack_response" || s == "攻击牌不可对") return AF_NoAttackResponse;
  if (s == "no_action_response" || s == "行动牌不可对") return AF_NoActionResponse;
  if (s == "no_negate" || s == "不可打消") return AF_NoNegate;
  if (s == "to_distance" || s == "到距") return AF_ToDistance;
  if (s == "prevent_response" || s == "防止对应") return AF_PreventResponse;
  return 0;
}

uint32_t flags_from(sol::table t) {
  uint32_t f = 0;
  if (t.get_or("full_power", false)) f |= CF_FullPower;
  if (t.get_or("response", false)) f |= CF_Response;
  if (t.get_or("breakable", false)) f |= CF_Break;
  if (t.get_or("terminal", false)) f |= CF_Terminal;
  return f;
}

void static_attack(CardDef& d, sol::table at) {
  sol::object rg = at["range"];
  if (rg.is<sol::table>()) {
    sol::table rt = rg;
    sol::object first = rt[1];
    if (first.is<sol::table>()) {
      for (auto& kv : rt)
        if (kv.second.is<sol::table>()) {
          sol::table sp = kv.second;
          d.attack.range.add(sp[1], sp[2]);
        }
    } else if (first.is<int>() || first.is<double>()) {
      int lo = first.as<int>();
      sol::object second = rt[2];
      int hi = (second.is<int>() || second.is<double>()) ? second.as<int>() : lo;
      d.attack.range.add(lo, hi);
    }
  }
  sol::object dmg = at["damage"];
  if (dmg.is<sol::table>()) {
    sol::table dt = dmg;
    sol::object a = dt["aura"], l = dt["life"];
    if (a.is<int>() || a.is<double>()) d.attack.damage.aura = a.as<int>();
    if (l.is<int>() || l.is<double>()) d.attack.damage.life = l.as<int>();
  }
  sol::object kw = at["keywords"];
  if (kw.is<sol::table>())
    for (auto& kv : static_cast<sol::table>(kw))
      if (kv.second.is<std::string>()) d.attack.keywords |= kwflag(kv.second.as<std::string>());
}

}  // namespace

struct LuaCtx {
  Engine* e = nullptr;
  Player who = P0;
  int source = -1;
};

struct LuaAttack {
  Attack* a = nullptr;
  Engine* e = nullptr;
};

// A mutable cost value handed to `continuous` query="cost" auras.
struct LuaCost {
  int value = 0;
};

struct LuaEvent {
  Engine* e = nullptr;
  std::string type;
  Player subject = P0;
  Attack* atk = nullptr;
  int card = -1;
  bool first = false;
};

namespace {

Range read_range(sol::object o, Engine& e, LuaCtx& c);
std::optional<int> read_int(sol::object o, Engine& e, LuaCtx& c);

uint32_t read_keywords(sol::object o, Engine& e, LuaCtx& c) {
  uint32_t k = 0;
  if (o.is<sol::function>()) {
    auto res = o.as<sol::function>()(c);
    if (res.valid()) return read_keywords(res.get<sol::object>(), e, c);
    return 0;
  }
  if (o.is<sol::table>())
    for (auto& kv : static_cast<sol::table>(o))
      if (kv.second.is<std::string>()) k |= kwflag(kv.second.as<std::string>());
  return k;
}

// Read a {range=..., damage=..., keywords=...} spec (table or function).
EvaluatedAttack read_spec(sol::object spec, Engine& e, LuaCtx& c) {
  EvaluatedAttack ea;
  if (spec.is<sol::function>()) {
    auto res = spec.as<sol::function>()(c);
    if (res.valid()) spec = res.get<sol::object>();
    else return ea;
  }
  if (!spec.is<sol::table>()) return ea;
  sol::table t = spec;
  ea.range = read_range(t["range"], e, c);
  sol::object dmg = t["damage"];
  if (dmg.is<sol::function>()) {
    auto res = dmg.as<sol::function>()(c);
    if (res.valid()) dmg = res.get<sol::object>();
  }
  if (dmg.is<sol::table>()) {
    sol::table dt = dmg;
    ea.damage.aura = read_int(dt["aura"], e, c);
    ea.damage.life = read_int(dt["life"], e, c);  // 0 is a real value; missing == "-"
  }
  ea.keywords = read_keywords(t["keywords"], e, c);
  {
    sol::object ac = t["attacker_chooses_damage"];
    if (ac.is<bool>()) ea.attackerChooses = ac.as<bool>();
    sol::object tm = t["terminal"];
    if (tm.is<bool>()) ea.terminal = tm.as<bool>();
  }
  return ea;
}

Range read_range(sol::object o, Engine& e, LuaCtx& c) {
  Range r;
  if (o.is<sol::function>()) {
    auto res = o.as<sol::function>()(c);
    if (res.valid()) return read_range(res.get<sol::object>(), e, c);
    return r;
  }
  if (o.is<sol::table>()) {
    sol::table t = o;
    sol::object first = t[1];
    if (first.is<sol::table>()) {
      for (auto& kv : t)
        if (kv.second.is<sol::table>()) {
          sol::table sp = kv.second;
          r.add(sp[1], sp[2]);
        }
    } else if (first.is<int>() || first.is<double>()) {
      int lo = first.as<int>();
      sol::object second = t[2];
      int hi = (second.is<int>() || second.is<double>()) ? second.as<int>() : lo;
      r.add(lo, hi);
    }
  }
  return r;
}

std::optional<int> read_int(sol::object o, Engine& e, LuaCtx& c) {
  if (o.is<sol::function>()) {
    auto res = o.as<sol::function>()(c);
    if (res.valid()) return read_int(res.get<sol::object>(), e, c);
    return std::nullopt;
  }
  if (o.is<int>()) return o.as<int>();
  if (o.is<double>()) return static_cast<int>(o.as<double>());
  return std::nullopt;
}

AreaRef area_of(const std::string& s, Player p) {
  if (s == "life") return AreaRef::life(p);
  if (s == "aura") return AreaRef::aura(p);
  if (s == "flare") return AreaRef::flare(p);
  if (s == "distance") return AreaRef::distance();
  if (s == "dust") return AreaRef::dust();
  return AreaRef::dust();
}

BasicAction parse_basic(const std::string& s) {
  if (s == "retreat") return BasicAction::Retreat;
  if (s == "aura") return BasicAction::Aura;
  if (s == "flare") return BasicAction::Flare;
  if (s == "escape") return BasicAction::Escape;
  return BasicAction::Advance;
}

}  // namespace

// ---------------------------------------------------------------------------

struct EffectHost::Impl {
  sol::state lua;
  int errors = 0;
  bool strict = false;
  void fail(const std::string& msg) {
    errors += 1;
    if (strict) throw std::runtime_error("lua error: " + msg);
    std::fprintf(stderr, "[lua error] %s\n", msg.c_str());
  }
  struct Hook {
    sol::object on_play, on_enter, on_discard, on_attack_after, on_use_after;
    sol::table spec;
    sol::object keiryo;  // Shinra 计略 effect (神算/鬼谋 branch)
    std::vector<sol::table> continuous;
    std::vector<sol::table> triggers;
    ResetInfo reset;
    sol::function reset_cond;
  };
  std::vector<Hook> hooks;

  struct PendingMod {
    Player owner = P0;
    sol::function match;
    sol::function apply;
    bool expires = true;  // "本回合中" modifiers expire at end of turn
  };
  std::vector<PendingMod> pending;
  std::map<Attack*, std::vector<std::pair<Player, sol::function>>> afterAttack;
  // ctx:on_response callbacks, keyed by the attack being responded to.
  std::map<Attack*, std::vector<std::pair<Player, sol::function>>> onResponse;
  std::vector<std::pair<Player, sol::function>> pendingOnResponse;
  // ctx:attack{after=...} 的攻击后闭包，按攻击对象索引（祟神复制要用）。
  std::map<Attack*, sol::function> attackAfterSpec;
};

EffectHost::EffectHost() : impl_(std::make_unique<Impl>()) {
  auto& L = impl_->lua;
  L.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
  L["os"] = sol::nil;
  L["io"] = sol::nil;
  if (L["math"].valid()) L["math"]["random"] = sol::nil;

  auto atkutil = L.new_usertype<LuaAttack>("Attack");
  atkutil["add"] = [](LuaAttack& h, sol::table t) {
    h.a->auraDelta += t.get_or("aura", 0);
    h.a->lifeDelta += t.get_or("life", 0);
  };
  atkutil["negate"] = [](LuaAttack& h) { h.a->negated = true; };
  atkutil["attacker"] = [](LuaAttack& h) { return static_cast<int>(h.a->attacker); };
  atkutil["keyword"] = [](LuaAttack& h, std::string k) { h.a->keywords |= kwflag(k); };
  atkutil["both_sides"] = [](LuaAttack& h) { h.a->keywords |= AF_BothSides; };
  atkutil["extend_far"] = [](LuaAttack& h, int n) { h.a->range.extend_far(n); };
  atkutil["extend_near"] = [](LuaAttack& h, int n) { h.a->range.extend_near(n); };
  atkutil["shrink_far"] = [](LuaAttack& h, int n) { h.a->range.shrink_far(n); };
  atkutil["shrink_near"] = [](LuaAttack& h, int n) { h.a->range.shrink_near(n); };
  atkutil["near"] = [](LuaAttack& h) {
    int n = 999;
    for (auto& sp : h.a->range.spans) n = std::min(n, sp.first);
    return n == 999 ? -1 : n;
  };
  atkutil["far"] = [](LuaAttack& h) {
    int n = -1;
    for (auto& sp : h.a->range.spans) n = std::max(n, sp.second);
    return n;
  };
  atkutil["source_full_power"] = [](LuaAttack& h) -> bool {
    if (h.a->sourceDefOverride >= 0 && h.e)
      return (h.e->def(h.a->sourceDefOverride).flags & CF_FullPower) != 0;
    if (h.a->sourceInst < 0 || !h.e) return false;
    return (h.e->def_of(h.a->sourceInst).flags & CF_FullPower) != 0;
  };
  atkutil["has_keyword"] = [](LuaAttack& h, std::string k) {
    uint32_t f = kwflag(k);
    return f != 0 && (h.a->keywords & f) != 0;
  };
  atkutil["aura_damage"] = [](LuaAttack& h, sol::this_state ts) -> sol::object {
    if (!h.a->aura) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, *h.a->aura + h.a->auraDelta);
  };
  atkutil["life_damage"] = [](LuaAttack& h, sol::this_state ts) -> sol::object {
    if (!h.a->life) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, *h.a->life + h.a->lifeDelta);
  };
  atkutil["source_inst"] = [](LuaAttack& h) { return h.a->sourceInst; };
  // 祟神复制品: sourceDefOverride 优先（复制品不持有实例，但"女神/通常/切札"要照抄来源）。
  auto src_def = [](LuaAttack& h) -> int {
    if (h.a->sourceDefOverride >= 0) return h.a->sourceDefOverride;
    return h.a->sourceInst;
  };
  atkutil["from_normal"] = [src_def](LuaAttack& h) -> bool {
    if (!h.e) return false;
    int d = src_def(h);
    if (d < 0) return false;
    return (h.a->sourceDefOverride >= 0 ? h.e->def(d) : h.e->def_of(d)).kind == CardKind::Normal;
  };
  atkutil["from_special"] = [src_def](LuaAttack& h) -> bool {
    int d = src_def(h);
    if (d < 0) return h.a->fromSpecial;
    if (!h.e) return false;
    return (h.a->sourceDefOverride >= 0 ? h.e->def(d) : h.e->def_of(d)).kind == CardKind::Special;
  };
  atkutil["source_goddess"] = [src_def](LuaAttack& h) -> std::string {
    if (!h.e) return std::string();
    int d = src_def(h);
    if (d < 0) return std::string();
    return (h.a->sourceDefOverride >= 0 ? h.e->def(d) : h.e->def_of(d)).goddess;
  };
  atkutil["source_set"] = [src_def](LuaAttack& h) -> std::string {
    if (!h.e) return std::string();
    int d = src_def(h);
    if (d < 0) return std::string();
    return (h.a->sourceDefOverride >= 0 ? h.e->def(d) : h.e->def_of(d)).set;
  };
  atkutil["source_is_goddess"] = [](LuaAttack& h, std::string g) -> bool {
    if (!h.e) return false;
    if (h.a->sourceDefOverride >= 0) return h.e->def(h.a->sourceDefOverride).goddess == g;
    if (h.a->sourceInst < 0) return false;
    return h.e->card_has_goddess(h.a->sourceInst, g);
  };
  atkutil["negate_damage"] = [](LuaAttack& h) { h.a->negateDamage = true; };
  atkutil["swap_damage"] = [](LuaAttack& h) {
    std::swap(h.a->aura, h.a->life);
    std::swap(h.a->auraDelta, h.a->lifeDelta);
  };
  atkutil["no_special_response"] = [](LuaAttack& h) { h.a->keywords |= AF_NoSpecialResponse; };
  atkutil["remove_unrespondable"] = [](LuaAttack& h) { h.a->keywords &= ~AF_Unrespondable; };
  atkutil["attacker_chooses_damage"] = [](LuaAttack& h) { h.a->attackerChoosesDamage = true; };
  atkutil["terminal"] = [](LuaAttack& h) { h.a->terminal = true; };

  auto ctx = L.new_usertype<LuaCtx>("Ctx");
  ctx["player"] = [](LuaCtx& c) { return static_cast<int>(c.who); };
  ctx["opp"] = [](LuaCtx& c) { return static_cast<int>(opp(c.who)); };
  ctx["source_inst"] = [](LuaCtx& c) { return c.source; };
  ctx["life"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).life; };
  ctx["aura"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).aura; };
  ctx["flare"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).flare; };
  ctx["vigor"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).vigor; };
  ctx["hand_size"] = [](LuaCtx& c, int p) { return static_cast<int>(c.e->ps(static_cast<Player>(p)).hand.size()); };
  ctx["deck_size"] = [](LuaCtx& c, int p) { return static_cast<int>(c.e->ps(static_cast<Player>(p)).deck.size()); };
  ctx["discard_size"] = [](LuaCtx& c, int p) { return static_cast<int>(c.e->ps(static_cast<Player>(p)).discard.size()); };
  ctx["cover_size"] = [](LuaCtx& c, int p) { return static_cast<int>(c.e->ps(static_cast<Player>(p)).cover.size()); };
  ctx["distance"] = [](LuaCtx& c) { return c.e->distance(); };
  ctx["dust"] = [](LuaCtx& c) { return c.e->st.dust; };
  ctx["crystals"] = [](LuaCtx& c, int inst) { return c.e->ci(inst).crystals; };
  ctx["desperation"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).life <= 3; };
  ctx["hasso"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).aura <= 1; };
  ctx["shinkyou"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).vigor == 2; };
  ctx["rensha"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).cardsPlayedThisTurn >= 3; };
  ctx["last_life_lost"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).lastLifeLost; };
  ctx["is_attack"] = [](LuaCtx& c, int inst) { return c.e->def_of(inst).type == CardType::Attack; };

  ctx["hand"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).hand; };
  ctx["discard_pile"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).discard; };
  ctx["cover_pile"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).cover; };
  ctx["to_deck_top"] = [](LuaCtx& c, int inst) { c.e->move_card_top(inst); };
  ctx["to_deck_bottom"] = [](LuaCtx& c, int inst) { c.e->move_card_bottom(inst); };
  ctx["discard_card"] = [](LuaCtx& c, int inst) { c.e->move_card(inst, Zone::Discard); };
  ctx["reveal_hand"] = [](LuaCtx& c, int p) { c.e->reveal_hand(static_cast<Player>(p)); };

  ctx["move"] = [](LuaCtx& c, std::string from, std::string to, int n, sol::optional<int> pf,
                   sol::optional<int> pt) -> int {
    Player pFrom = pf ? static_cast<Player>(*pf) : c.who;
    Player pTo = pt ? static_cast<Player>(*pt) : pFrom;
    // 虚鱼 (cards played from the cover) / 映界 (all of your crystal moves).
    if ((c.e->current_from_cover() && c.e->has_named_active(c.who, cards::kXuYu)) ||
        c.e->reverse_moves_active(c.who)) {
      Request r;
      r.kind = "option";
      r.prompt = c.e->reverse_moves_active(c.who) ? "映界：反向移动？" : "虚鱼：反向移动？";
      r.options.push_back({from + " -> " + to, true, {}});
      r.options.push_back({to + " -> " + from, true, {}});
      r.minSel = 1;
      r.maxSel = 1;
      Decision d = c.e->decide(c.who, std::move(r));
      int idx = d.indices.empty() ? 0 : d.indices[0];
      if (idx == 1) {
        std::swap(from, to);
        std::swap(pFrom, pTo);
      }
    }
    return c.e->move_crystals(area_of(from, pFrom), area_of(to, pTo), n, true);
  };
  ctx["choose"] = [](LuaCtx& c, std::string prompt, sol::table opts) -> int {
    Request r;
    r.kind = "option";
    r.prompt = prompt;
    for (size_t i = 1;; ++i) {
      sol::object o = opts[i];
      if (!o.valid() || o == sol::nil) break;
      Option op;
      op.label = o.is<std::string>() ? o.as<std::string>() : std::string("?");
      r.options.push_back(op);
    }
    r.minSel = 1;
    r.maxSel = 1;
    int n = static_cast<int>(r.options.size());
    if (n == 0) return 1;
    Decision d = c.e->decide(c.who, std::move(r));
    int idx = d.indices.empty() ? 0 : d.indices[0];
    if (idx < 0 || idx >= n) idx = 0;
    return idx + 1;
  };
  auto choose_cards_impl = [](Engine& e, Player chooser, const std::string& prompt, sol::table insts,
                              int mn, int mx) {
    Request r;
    r.kind = "cards";
    r.prompt = prompt;
    std::vector<int> ids;
    for (size_t i = 1;; ++i) {
      sol::object o = insts[i];
      if (!o.valid() || o == sol::nil) break;
      int id = o.as<int>();
      ids.push_back(id);
      Option op;
      op.label = e.def_of(id).name;
      op.data = {{"inst", id}};
      r.options.push_back(op);
    }
    r.minSel = mn;
    r.maxSel = mx;
    Decision d = e.decide(chooser, std::move(r));
    std::vector<int> out;
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(ids.size())) out.push_back(ids[static_cast<size_t>(i)]);
    return out;
  };
  ctx["choose_cards"] = [choose_cards_impl](LuaCtx& c, std::string prompt, sol::table insts, int mn,
                                            int mx) {
    return choose_cards_impl(*c.e, c.who, prompt, insts, mn, mx);
  };
  ctx["choose_cards_for"] = [choose_cards_impl](LuaCtx& c, int p, std::string prompt, sol::table insts,
                                                int mn, int mx) {
    return choose_cards_impl(*c.e, static_cast<Player>(p), prompt, insts, mn, mx);
  };
  // Ask *another* player to pick one / several options (收割, 重压, 魔食, 万象乖离残灭之影 …).
  auto choose_opts_impl = [](Engine& e, Player who, const std::string& prompt, sol::table opts,
                             int mn, int mx) {
    Request r;
    r.kind = "option";
    r.prompt = prompt;
    for (size_t i = 1;; ++i) {
      sol::object o = opts[i];
      if (!o.valid() || o == sol::nil) break;
      Option op;
      op.label = o.is<std::string>() ? o.as<std::string>() : std::string("?");
      r.options.push_back(op);
    }
    r.minSel = mn;
    r.maxSel = mx;
    if (r.options.empty()) return std::vector<int>{};
    Decision d = e.decide(who, std::move(r));
    std::vector<int> out;
    for (int i : d.indices)
      if (i >= 0 && i < static_cast<int>(r.options.size())) out.push_back(i);
    return out;
  };
  ctx["choose_for"] = [choose_opts_impl](LuaCtx& c, int p, std::string prompt, sol::table opts) {
    auto v = choose_opts_impl(*c.e, static_cast<Player>(p), prompt, opts, 1, 1);
    return v.empty() ? -1 : v[0];
  };
  ctx["choose_options_for"] = [choose_opts_impl](LuaCtx& c, int p, std::string prompt,
                                                 sol::table opts, int mn, int mx) {
    return choose_opts_impl(*c.e, static_cast<Player>(p), prompt, opts, mn, mx);
  };
  ctx["areas_with"] = [](LuaCtx& c, int n) { return c.e->areas_with(n); };
  ctx["redirect_damage_to_card"] = [](LuaCtx& c, int inst) {
    c.e->redirect_damage_to_card(inst);
  };
  ctx["freeze"] = [](LuaCtx& c, int p, int n, sol::optional<int> cause) {
    return c.e->freeze(static_cast<Player>(p), n, cause ? *cause : -1);
  };
  ctx["thaw"] = [](LuaCtx& c, int p, int n) { c.e->thaw(static_cast<Player>(p), n); };
  ctx["ice"] = [](LuaCtx& c, int p) { return c.e->ice_count(static_cast<Player>(p)); };
  ctx["frozen"] = [](LuaCtx& c, int p) { return c.e->frozen(static_cast<Player>(p)); };
  ctx["armor_full"] = [](LuaCtx& c, int p) { return c.e->armor_full(static_cast<Player>(p)); };
  ctx["aura_free"] = [](LuaCtx& c, int p) { return c.e->aura_free(static_cast<Player>(p)); };
  // 八叶: 镜映 = 你的装/气/命中与对手对应区域结晶数相同的区域数
  ctx["mirror"] = [](LuaCtx& c, sol::optional<int> p) {
    return c.e->mirror(p ? static_cast<Player>(*p) : c.who);
  };
  // 八叶: 完全态（把一张牌就地升级为它的升级版）
  ctx["can_upgrade"] = [](LuaCtx& c, int inst) { return c.e->can_upgrade(inst); };
  ctx["upgrade_card"] = [](LuaCtx& c, int inst) { return c.e->upgrade_card(inst); };
  ctx["turn_number"] = [](LuaCtx& c) { return c.e->st.turn; };
  ctx["curse"] = [](LuaCtx& c, int p) { return c.e->curse(static_cast<Player>(p)); };
  ctx["add_curse"] = [](LuaCtx& c, int p, int n) {
    c.e->add_curse(static_cast<Player>(p), n);
  };
  ctx["set_extra_attack_cost"] = [](LuaCtx& c, int p, std::string goddess) {
    c.e->ps(static_cast<Player>(p)).extraAttackCostGoddess = goddess;
  };
  ctx["memory_size"] = [](LuaCtx& c, int p) {
    return c.e->memory_size(static_cast<Player>(p));
  };
  ctx["memory_of"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    std::vector<int> v = c.e->ps(static_cast<Player>(p)).memory;
    sol::table t = sol::table::create(ts.L);
    for (size_t i = 0; i < v.size(); ++i) t[i + 1] = v[i];
    return sol::make_object(ts.L, t);
  };
  ctx["to_memory"] = [](LuaCtx& c, int inst) { c.e->to_memory(inst); };
  ctx["all_normals_to_memory"] = [](LuaCtx& c, int p, sol::optional<int> except) {
    c.e->all_normals_to_memory(static_cast<Player>(p), except ? *except : -1);
  };
  ctx["memory_draw"] = [](LuaCtx& c, int p, int n) {
    return c.e->memory_draw(static_cast<Player>(p), n);
  };
  ctx["cards_played_total"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).cardsPlayedTotal;
  };
  ctx["external_to_card"] = [](LuaCtx& c, int inst, int n) {
    return c.e->gain_external(AreaRef::card(inst), n);
  };
  ctx["count_complete"] = [](LuaCtx& c, int p) {
    return c.e->count_complete(static_cast<Player>(p));
  };
  // 把结晶移到游戏外（生命/装/气/距/虚）。
  ctx["to_external"] = [](LuaCtx& c, int p, std::string area, int n) {
    return c.e->lose_external(area_of(area, static_cast<Player>(p)), n);
  };
  ctx["damage_immune"] = [](LuaCtx& c, int p) {
    return c.e->has_damage_immunity(static_cast<Player>(p));
  };
  ctx["empty_card"] = [](LuaCtx& c, int inst) { c.e->empty_card(inst); };
  ctx["remove_all_normals"] = [](LuaCtx& c, int p) {
    c.e->remove_all_normals(static_cast<Player>(p));
  };
  ctx["skip_next_main"] = [](LuaCtx& c, int p) {
    c.e->skip_next_main(static_cast<Player>(p));
  };
  ctx["set_aura_redirect_suppressed"] = [](LuaCtx& c, bool v) {
    c.e->ps(c.who).suppressAuraRedirect = v;
  };
  ctx["add_temp_distance"] = [](LuaCtx& c, int p, int n) {
    c.e->add_temp_distance(static_cast<Player>(p), n);
  };
  ctx["add_temp_near_distance"] = [](LuaCtx& c, int p, int n) {
    c.e->add_temp_near_distance(static_cast<Player>(p), n);
  };
  // 祟神: 生成一张被对应攻击的**精确复制**（修正后的距离/伤害/超克/攻击后效果/女神）。
  ctx["copy_attack"] = [this](LuaCtx& c, LuaAttack& src) -> bool {
    Attack* s = src.a;
    if (!s || !c.e) return false;
    if (!c.e->can_attack(c.who)) return false;
    const int srcInst = s->sourceInst;
    const int srcDef = s->sourceDefOverride >= 0
                           ? s->sourceDefOverride
                           : (srcInst >= 0 ? c.e->ci(srcInst).def : -1);
    sol::function afterSpec;
    bool hasAfterSpec = false;
    auto it = impl_->attackAfterSpec.find(s);
    if (it != impl_->attackAfterSpec.end()) {
      afterSpec = it->second;
      hasAfterSpec = true;
    }
    Attack a;
    a.attacker = c.who;
    a.sourceInst = srcInst;  // 复制品照抄来源牌（女神/通常/切札 判定）
    a.sourceDefOverride = srcDef;
    a.range = s->range;
    a.aura = s->aura;
    a.life = s->life;
    a.auraDelta = s->auraDelta;
    a.lifeDelta = s->lifeDelta;
    a.keywords = s->keywords;
    a.attackerChoosesDamage = s->attackerChoosesDamage;
    a.terminal = s->terminal;
    finalize_attack(*c.e, c.who, a, true);
    c.e->declare_attack(a);
    c.e->resolve_attack(a);
    if (a.hit) {  // 攻击后效果同样复制
      if (hasAfterSpec) {
        LuaCtx c2{c.e, c.who, srcInst};
        LuaAttack ha{&a, c.e};
        auto res = afterSpec(c2, ha);
        if (!res.valid())
          impl_->fail(std::string("copy_attack after: ") + sol::error(res).what());
      } else if (srcDef >= 0 && has(srcDef, "on_attack_after")) {
        call(*c.e, srcDef, "on_attack_after", c.who, srcInst);
      }
    }
    return a.hit;
  };
  ctx["attack"] = [this](LuaCtx& c, sol::table spec) {
    // 二重奏·弹奏冰瞑 blocks every attack; 迟缓毒 only forbids *using* attack cards,
    // so a card-generated attack still resolves.
    if (!c.e->can_attack(c.who)) return;
    Attack a;
    a.attacker = c.who;
    a.sourceInst = c.source;  // generated attacks belong to the generating card's owner
    if (c.source >= 0) a.fromSpecial = c.e->def_of(c.source).kind == CardKind::Special;
    a.fromResponse = false;
    EvaluatedAttack ea = read_spec(spec, *c.e, c);
    a.range = ea.range;
    a.aura = ea.damage.aura;
    a.life = ea.damage.life;
    a.keywords = ea.keywords;
    a.attackerChoosesDamage = ea.attackerChooses;
    a.terminal = ea.terminal;
    finalize_attack(*c.e, c.who, a, true);
    sol::object after = spec["after"];
    if (after.is<sol::function>()) impl_->attackAfterSpec[&a] = after.as<sol::function>();
    c.e->declare_attack(a);  // virtual attacks are attacks too
    c.e->resolve_attack(a);
    if (after.is<sol::function>() && a.hit) {
      LuaCtx c2{c.e, c.who, c.source};
      LuaAttack ha{&a, c.e};
      auto res = after.as<sol::function>()(c2, ha);
      if (!res.valid())
        impl_->fail(std::string("attack after: ") + sol::error(res).what());
    }
    impl_->attackAfterSpec.erase(&a);
  };
  ctx["deal_damage"] = [](LuaCtx& c, int target, sol::object aura, sol::object life) {
    c.e->deal_damage(static_cast<Player>(target), read_int(aura, *c.e, c), read_int(life, *c.e, c), 0);
  };
  ctx["draw"] = [](LuaCtx& c, int p, int n) { c.e->draw(static_cast<Player>(p), n); };
  ctx["gain_vigor"] = [](LuaCtx& c, int p, int n) { c.e->gain_vigor(static_cast<Player>(p), n); };
  ctx["set_vigor"] = [](LuaCtx& c, int p, int n) { c.e->ps(static_cast<Player>(p)).vigor = n; };
  ctx["cower"] = [](LuaCtx& c, int p) { c.e->give_cower(static_cast<Player>(p)); };
  ctx["lose_life"] = [](LuaCtx& c, int p, int n, sol::optional<std::string> to) {
    AreaKind k = AreaKind::Flare;
    if (to && *to == "dust") k = AreaKind::Dust;
    c.e->damage_life(static_cast<Player>(p), n, k, true);
  };
  ctx["rebuild"] = [](LuaCtx& c, int p, sol::optional<bool> cost) {
    c.e->rebuild(static_cast<Player>(p), cost ? *cost : false);
  };
  ctx["discard_all_hand"] = [](LuaCtx& c, int p) {
    std::vector<int> copy = c.e->ps(static_cast<Player>(p)).hand;
    for (int inst : copy) c.e->move_card(inst, Zone::Discard);
  };
  ctx["free_basics"] = [](LuaCtx& c, int p, int maxTimes) {
    c.e->free_basics(static_cast<Player>(p), maxTimes);
  };
  ctx["do_basic"] = [](LuaCtx& c, int p, std::string a) {
    return c.e->do_basic(static_cast<Player>(p), parse_basic(a));
  };
  ctx["next_attack_mod"] = [this](LuaCtx& c, sol::table t) {
    Impl::PendingMod m;
    m.owner = c.who;
    sol::object mf = t["match"];
    if (mf.is<sol::function>()) m.match = mf.as<sol::function>();
    sol::object af = t["apply"];
    if (af.is<sol::function>()) m.apply = af.as<sol::function>();
    m.expires = t.get_or("this_turn", true);
    impl_->pending.push_back(std::move(m));
  };
  ctx["responding_attack"] = [](LuaCtx& c, sol::this_state ts) -> sol::object {
    if (!c.e->currentResponding) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, LuaAttack{c.e->currentResponding, c.e});
  };
  // Phase 3 helpers
  ctx["used_specials"] = [](LuaCtx& c, int p) {
    std::vector<int> out;
    for (int inst : c.e->ps(static_cast<Player>(p)).special)
      if (c.e->ci(inst).faceUp) out.push_back(inst);
    return out;
  };
  ctx["card_is_goddess"] = [](LuaCtx& c, int inst, std::string g) {
    return c.e->card_has_goddess(inst, g);
  };
  ctx["reset_special"] = [](LuaCtx& c, int inst) { c.e->reset_special(inst); };
  ctx["die"] = [](LuaCtx& c, int p) { c.e->die(static_cast<Player>(p)); };
  ctx["end_current_main"] = [](LuaCtx& c) { c.e->end_current_main(); };
  ctx["max_aura"] = [](LuaCtx& c, int p) { return c.e->max_aura(static_cast<Player>(p)); };
  ctx["add_hand_limit"] = [](LuaCtx& c, int p, int n) {
    c.e->ps(static_cast<Player>(p)).handLimit += n;
  };
  ctx["add_cut_cost_delta"] = [](LuaCtx& c, int p, int n) {
    c.e->ps(static_cast<Player>(p)).cutCostDelta += n;
  };
  ctx["set_cannot_attack"] = [](LuaCtx& c, int p) {
    c.e->ps(static_cast<Player>(p)).cannotAttack = true;
  };
  ctx["set_cannot_basic"] = [](LuaCtx& c, int p) {
    c.e->ps(static_cast<Player>(p)).cannotBasic = true;
  };
  ctx["last_damage_side"] = [](LuaCtx& c) { return c.e->last_damage_side(); };
  ctx["last_damage_amount"] = [](LuaCtx& c) { return c.e->last_damage_amount(); };
  ctx["last_damage_from_attack"] = [](LuaCtx& c) { return c.e->last_damage_from_attack(); };
  ctx["last_attack_side"] = [](LuaCtx& c) { return c.e->last_attack_side(); };
  ctx["last_attack_amount"] = [](LuaCtx& c) { return c.e->last_attack_amount(); };
  ctx["used_special_count"] = [](LuaCtx& c, int p, std::string g) {
    return c.e->used_special_count(static_cast<Player>(p), g);
  };
  ctx["legal_basics"] = [](LuaCtx& c, int p) {
    std::vector<std::string> out;
    for (int bi = 0; bi < 5; ++bi) {
      BasicAction a = static_cast<BasicAction>(bi);
      if (c.e->basic_legal(static_cast<Player>(p), a)) out.push_back(basic_name(a));
    }
    return out;
  };
  // Oboro
  ctx["from_cover"] = [](LuaCtx& c) { return c.e->current_from_cover(); };
  ctx["assemble_one"] = [](LuaCtx& c, int p) { return c.e->assemble_one(static_cast<Player>(p)); };
  ctx["disassemble_to"] = [](LuaCtx& c, int p, int m) {
    c.e->disassemble_to(static_cast<Player>(p), m);
  };
  ctx["assemble_many"] = [](LuaCtx& c, int p, int x) {
    c.e->assemble_many(static_cast<Player>(p), x);
  };
  ctx["assembled_count"] = [](LuaCtx& c, int p) {
    return c.e->assembled_count(static_cast<Player>(p));
  };
  ctx["cover_count"] = [](LuaCtx& c, int p) { return c.e->cover_count(static_cast<Player>(p)); };
  ctx["cover_cards"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).cover; };
  ctx["remove_card"] = [](LuaCtx& c, int inst) { c.e->remove_card(inst); };
  ctx["gain_extra"] = [](LuaCtx& c, std::string name) { return c.e->gain_extra(c.who, name); };
  ctx["cover_card"] = [](LuaCtx& c, int inst) { c.e->cover_card(inst); };
  ctx["cover_deck"] = [](LuaCtx& c, int p) { c.e->cover_deck(static_cast<Player>(p)); };
  ctx["return_enhance"] = [](LuaCtx& c, int inst) { c.e->return_enhance(inst); };
  ctx["can_attack"] = [](LuaCtx& c, int p) { return c.e->can_attack(static_cast<Player>(p)); };
  ctx["attack_card_forbidden"] = [](LuaCtx& c, int p) {
    return c.e->attack_card_forbidden(static_cast<Player>(p));
  };
  ctx["card_zone"] = [](LuaCtx& c, int inst) { return c.e->card_zone(inst); };
  ctx["use_from_cover"] = [](LuaCtx& c, int inst, sol::optional<bool> resp) {
    c.e->use_from_cover(inst, resp ? *resp : false);
  };
  ctx["force_unrespondable"] = [](LuaCtx& c) { c.e->force_unrespondable(); };
  ctx["aura_damaged_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->aura_damaged_this_turn(static_cast<Player>(p));
  };
  ctx["is_full_power"] = [](LuaCtx& c, int inst) {
    return (c.e->def_of(inst).flags & CF_FullPower) != 0;
  };
  // Yukihi
  ctx["umbrella"] = [](LuaCtx& c, int p) { return c.e->umbrella(static_cast<Player>(p)); };
  ctx["switch_weapon"] = [](LuaCtx& c) { c.e->switch_weapon(c.who, c.source); };
  ctx["shared_range"] = [](LuaCtx& c, int p) { return c.e->shared_range(static_cast<Player>(p)); };
  ctx["enhance_crystal_total"] = [](LuaCtx& c, int p) {
    return c.e->enhance_crystal_total(static_cast<Player>(p));
  };
  ctx["cards_played_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->cards_played_this_turn(static_cast<Player>(p));
  };
  ctx["dust_to_card"] = [](LuaCtx& c, int inst, int n) { return c.e->dust_to_card(inst, n); };
  ctx["move_to_card"] = [](LuaCtx& c, std::string from, int inst, int n, sol::optional<int> pf) {
    Player p = pf ? static_cast<Player>(*pf) : c.who;
    return c.e->move_crystals(area_of(from, p), AreaRef::card(inst), n, true);
  };
  ctx["move_from_card"] = [](LuaCtx& c, int inst, std::string to, int n, sol::optional<int> pt) {
    Player p = pt ? static_cast<Player>(*pt) : c.who;
    return c.e->move_crystals(AreaRef::card(inst), area_of(to, p), n, true);
  };
  ctx["set_used"] = [](LuaCtx& c, int inst) { c.e->set_used(inst); };
  ctx["opponent_pickable"] = [](LuaCtx& c, int inst) {
    return !c.e->def_of(inst).noOpponentPick;
  };
  ctx["use_card"] = [](LuaCtx& c, int inst, sol::optional<bool> r) {
    c.e->use_card(inst, r ? *r : false);
  };
  // Shinra
  ctx["strategy"] = [](LuaCtx& c, int p) { return c.e->strategy(static_cast<Player>(p)); };
  ctx["prepare_strategy"] = [](LuaCtx& c, int p) {
    c.e->prepare_strategy(static_cast<Player>(p));
  };
  ctx["seal_card"] = [](LuaCtx& c, int host, int card) { c.e->seal_card(host, card); };
  ctx["return_sealed"] = [](LuaCtx& c, int host) { c.e->return_sealed(host); };
  ctx["use_foreign_card"] = [](LuaCtx& c, int inst) { c.e->use_foreign_card(c.who, inst); };
  ctx["discard_top"] = [](LuaCtx& c, int p) { c.e->discard_top(static_cast<Player>(p)); };
  ctx["cover_top"] = [](LuaCtx& c, int p) { c.e->cover_top(static_cast<Player>(p)); };
  ctx["has_keiryo"] = [this](LuaCtx& c, int inst) { return has_keiryo(c.e->ci(inst).def); };
  ctx["execute_keiryo"] = [this](LuaCtx& c, int inst, int s) {
    call_keiryo(*c.e, c.e->ci(inst).def, c.e->ci(inst).owner, inst, s);
  };
  ctx["is_normal_card"] = [](LuaCtx& c, int i) { return c.e->is_normal_card(i); };
  ctx["is_enhance"] = [](LuaCtx& c, int i) { return c.e->is_enhance(i); };
  ctx["enhances"] = [](LuaCtx& c, int p) { return c.e->enhances(static_cast<Player>(p)); };
  ctx["reuse_special"] = [](LuaCtx& c, int i) { c.e->reuse_special(i); };
  ctx["drain_card_crystals"] = [](LuaCtx& c, int i, int n) {
    return c.e->drain_card_crystals(i, n);
  };
  // Hagane
  ctx["distance_at_turn_start"] = [](LuaCtx& c) { return c.e->distance_at_turn_start(); };
  ctx["attacked_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->attacked_this_turn(static_cast<Player>(p));
  };
  ctx["centrifugal_ok"] = [](LuaCtx& c, int p) {
    return c.e->centrifugal_ok(static_cast<Player>(p));
  };
  ctx["played_centrifugal_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->played_centrifugal_this_turn(static_cast<Player>(p));
  };
  ctx["played_liancheng_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->played_liancheng_this_turn(static_cast<Player>(p));
  };
  ctx["used_this_turn"] = [](LuaCtx& c, int i) { return c.e->used_this_turn(i); };
  ctx["zenkai"] = [](LuaCtx& c) { return c.e->zenkai_active(); };
  ctx["random_discard"] = [](LuaCtx& c, int p) { c.e->random_discard(static_cast<Player>(p)); };
  ctx["discard_deck"] = [](LuaCtx& c, int p) { c.e->discard_deck(static_cast<Player>(p)); };
  ctx["find_named"] = [](LuaCtx& c, int p, std::string n) {
    return c.e->find_named(static_cast<Player>(p), n);
  };
  ctx["count_named"] = [](LuaCtx& c, int p, std::string n) {
    return c.e->count_named(static_cast<Player>(p), n);
  };
  ctx["sealed_card"] = [](LuaCtx& c, int h) { return c.e->sealed_card(h); };
  ctx["special_cards"] = [](LuaCtx& c, int p) {
    return c.e->special_cards(static_cast<Player>(p));
  };
  ctx["is_used"] = [](LuaCtx& c, int i) { return c.e->is_used(i); };
  ctx["is_my_turn"] = [](LuaCtx& c) { return c.e->is_my_turn(c.who); };
  // Chikage
  ctx["poison_bag"] = [](LuaCtx& c, int p) { return c.e->poison_bag(static_cast<Player>(p)); };
  ctx["place_poison"] = [](LuaCtx& c, int inst, int holder, std::string where) {
    Zone z = (where == "hand") ? Zone::Hand : Zone::Deck;  // "deck_top" -> Deck (back == top)
    c.e->place_poison(inst, static_cast<Player>(holder), z);
  };
  ctx["return_poison"] = [](LuaCtx& c, int inst) { c.e->return_poison(inst); };
  ctx["force_discard"] = [](LuaCtx& c, int inst) { c.e->force_move(inst, Zone::Discard); };
  ctx["is_poison"] = [](LuaCtx& c, int inst) { return c.e->is_poison(inst); };
  ctx["effective_distance"] = [](LuaCtx& c) { return c.e->distance(); };
  ctx["card_name"] = [](LuaCtx& c, int i) { return c.e->def_of(i).name; };
  ctx["set_cannot_advance"] = [](LuaCtx& c, int p) {
    c.e->set_cannot_advance(static_cast<Player>(p));
  };
  ctx["did_basic_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->did_basic_this_turn(static_cast<Player>(p));
  };
  // Kururu
  ctx["keisou"] = [](LuaCtx& c, std::string s) { return c.e->keisou(c.who, s, false); };
  ctx["keisou_other"] = [](LuaCtx& c, std::string s) { return c.e->keisou(c.who, s, true); };
  ctx["card_colors_str"] = [](LuaCtx& c, int i) { return c.e->card_colors_str(i); };
  ctx["keisou_amount"] = [](LuaCtx& c, int base) { return c.e->keisou_amount(c.who, base); };
  ctx["reveal_opponent_cuts"] = [](LuaCtx& c, int p) {
    c.e->reveal_opponent_specials(static_cast<Player>(p));
  };
  ctx["is_zenkai"] = [](LuaCtx& c, int i) { return c.e->def_of(i).zenkai; };
  ctx["play_hand_card"] = [](LuaCtx& c, int i) { c.e->play_hand_card(c.who, i); };
  ctx["guess"] = [](LuaCtx& c, int inst) { return c.e->guess_name(c.who, inst); };
  ctx["rebuilt_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->rebuilt_this_turn(static_cast<Player>(p));
  };
  ctx["used_fullpower_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->used_fullpower_this_turn(static_cast<Player>(p));
  };
  ctx["set_nagi_adjust"] = [](LuaCtx& c, int n) { c.e->set_pending_nagi_adjust(n); };
  ctx["attacks_this_turn"] = [](LuaCtx& c, int p) {
    return c.e->attacks_this_turn(static_cast<Player>(p));
  };
  ctx["add_unused_cuts"] = [](LuaCtx& c, int p) {
    c.e->add_unused_cuts(static_cast<Player>(p));
  };
  // Thallya
  ctx["can_burn"] = [](LuaCtx& c, int p, int x) { return c.e->can_burn(static_cast<Player>(p), x); };
  ctx["burn"] = [](LuaCtx& c, int p, sol::optional<int> x) {
    c.e->burn(static_cast<Player>(p), x ? *x : 1);
  };
  ctx["recover"] = [](LuaCtx& c, int p, int x) { c.e->recover(static_cast<Player>(p), x); };
  ctx["pneumatic"] = [](LuaCtx& c, int p) { c.e->pneumatic(static_cast<Player>(p)); };
  ctx["transform"] = [](LuaCtx& c, int p, std::string n) {
    c.e->transform(static_cast<Player>(p), n);
  };
  ctx["steam_engine"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).steamEngine;
  };
  ctx["steam_exhausted"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).steamExhausted;
  };
  ctx["transform_count"] = [](LuaCtx& c, int p) {
    return c.e->ps(static_cast<Player>(p)).transformCount;
  };
  ctx["raira_can"] = [](LuaCtx& c, std::string k, int t) { return c.e->raira_can(c.who, k, t); };
  ctx["raira_spend"] = [](LuaCtx& c, std::string k, int t) { return c.e->raira_spend(c.who, k, t); };
  ctx["raira_restrict"] = [](LuaCtx& c, int p) {
    c.e->raira_gain_restrict(static_cast<Player>(p));
  };
  ctx["raira_perm_cut"] = [](LuaCtx& c, int p) {
    c.e->raira_perm_cut(static_cast<Player>(p));
  };
  ctx["wind"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).wind; };
  ctx["thunder"] = [](LuaCtx& c, int p) { return c.e->ps(static_cast<Player>(p)).thunder; };
  ctx["raira_gain"] = [](LuaCtx& c, int p, sol::object slot) {
    Player pl = static_cast<Player>(p);
    // Accept "wind"/"thunder" (preferred) or the legacy slot number (0=风, 1=雷).
    bool thunder = false;
    if (slot.is<std::string>()) thunder = slot.as<std::string>() == "thunder";
    else if (slot.is<int>()) thunder = slot.as<int>() == 1;
    if (thunder) {
      if (c.e->ps(pl).thunder < 20) c.e->ps(pl).thunder += 1;
    } else {
      if (c.e->ps(pl).wind < 20) c.e->ps(pl).wind += 1;
    }
  };
  ctx["set_slot"] = [](LuaCtx& c, int p, std::string k, int v) {
    Player pl = static_cast<Player>(p);
    if (v < 0) v = 0;
    if (v > 20) v = 20;
    if (k == "wind")
      c.e->ps(pl).wind = v;
    else
      c.e->ps(pl).thunder = v;
  };
  ctx["set_flare"] = [](LuaCtx& c, int p, int n) {
    c.e->ps(static_cast<Player>(p)).flare = n < 0 ? 0 : n;
  };
  ctx["transform_choose"] = [](LuaCtx& c, int p) { c.e->transform_choose(static_cast<Player>(p)); };
  ctx["transform_cards"] = [](LuaCtx& c, int p) {
    return c.e->transform_cards(static_cast<Player>(p));
  };
  ctx["set_next_draw_one"] = [](LuaCtx& c, int p) {
    c.e->set_next_draw_one(static_cast<Player>(p));
  };
  ctx["current_transform"] = [](LuaCtx& c, int p, sol::this_state ts) -> sol::object {
    Player pl = static_cast<Player>(p);
    int td = c.e->ps(pl).transformDef;
    if (td < 0) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, c.e->def(td).name);
  };
  ctx["random_index"] = [](LuaCtx& c, int n) { return c.e->rng_below(n); };
  ctx["choose_options"] = [](LuaCtx& c, std::string prompt, sol::table opts, int mn, int mx) {
    Request r;
    r.kind = "option";
    r.prompt = prompt;
    for (size_t i = 1;; ++i) {
      sol::object o = opts[i];
      if (!o.valid() || o == sol::nil) break;
      Option op;
      op.label = o.is<std::string>() ? o.as<std::string>() : std::string("?");
      r.options.push_back(op);
    }
    r.minSel = mn;
    r.maxSel = mx;
    Decision d = c.e->decide(c.who, std::move(r));
    return d.indices;  // 0-based
  };
  ctx["card_attack_spec"] = [this](LuaCtx& c, int inst, sol::this_state ts) -> sol::object {
    const CardDef& d = c.e->def_of(inst);
    EvaluatedAttack ea = eval_attack(*c.e, d.id, c.who, inst, false);
    sol::state_view L(ts.L);
    sol::table t = L.create_table();
    sol::table range = L.create_table();
    int ri = 1;
    for (auto& sp : ea.range.spans) {
      sol::table s = L.create_table();
      s[1] = sp.first;
      s[2] = sp.second;
      range[ri++] = s;
    }
    t["range"] = range;
    sol::table dmg = L.create_table();
    if (ea.damage.aura) dmg["aura"] = *ea.damage.aura;
    if (ea.damage.life) dmg["life"] = *ea.damage.life;
    t["damage"] = dmg;
    sol::table kw = L.create_table();
    int ki = 1;
    if (ea.keywords & AF_Unrespondable) kw[ki++] = "unrespondable";
    if (ea.keywords & AF_Lock) kw[ki++] = "lock";
    if (ea.keywords & AF_Overwhelm) kw[ki++] = "overwhelm";
    if (ea.keywords & AF_BothSides) kw[ki++] = "both_sides";
    if (ea.keywords & AF_NoNegate) kw[ki++] = "no_negate";
    t["keywords"] = kw;
    return t;
  };
  ctx["to_hand"] = [](LuaCtx& c, int inst) { c.e->move_card(inst, Zone::Hand); };
  ctx["add_crystal"] = [](LuaCtx& c, std::string area, int n, sol::optional<int> p) {
    Player pl = p ? static_cast<Player>(*p) : c.who;
    c.e->add_crystals(area_of(area, pl), n);
  };
  ctx["gain_external"] = [](LuaCtx& c, std::string area, int n, sol::optional<int> p) {
    Player pl = p ? static_cast<Player>(*p) : c.who;
    c.e->gain_external(area_of(area, pl), n);
  };
  ctx["store_int"] = [](LuaCtx& c, std::string k, int v) { c.e->store_int(c.source, k, v); };
  ctx["load_int"] = [](LuaCtx& c, std::string k, sol::optional<int> d) {
    return c.e->load_int(c.source, k, d ? *d : 0);
  };
  ctx["on_response"] = [this](LuaCtx& c, sol::function f) {
    // Registered from on_play (before the Attack exists) or from a response.
    if (c.e->currentResponding)
      impl_->onResponse[c.e->currentResponding].push_back({c.who, f});
    else
      impl_->pendingOnResponse.push_back({c.who, f});
  };
  ctx["on_resolve"] = [this](LuaCtx& c, sol::function f) {
    if (c.e->currentResponding) impl_->afterAttack[c.e->currentResponding].push_back({c.who, f});
  };
  ctx["free_basics_of"] = [](LuaCtx& c, int p, int maxTimes, sol::table allowed) {
    std::vector<std::string> names;
    for (size_t i = 1;; ++i) {
      sol::object o = allowed[i];
      if (!o.valid() || o == sol::nil) break;
      if (o.is<std::string>()) names.push_back(o.as<std::string>());
    }
    c.e->free_basics_of(static_cast<Player>(p), maxTimes, names);
  };

  auto costutil = L.new_usertype<LuaCost>("Cost");
  costutil["set"] = [](LuaCost& c, int v) { c.value = v; };
  costutil["add"] = [](LuaCost& c, int v) { c.value += v; };
  costutil["value"] = [](LuaCost& c) { return c.value; };

  auto ev = L.new_usertype<LuaEvent>("Event");
  ev["type"] = [](LuaEvent& e) { return e.type; };
  ev["subject"] = [](LuaEvent& e) { return static_cast<int>(e.subject); };
  ev["first"] = [](LuaEvent& e) { return e.first; };
  ev["card"] = [](LuaEvent& e) { return e.card; };
  ev["attacker"] = [](LuaEvent& e) { return e.atk ? static_cast<int>(e.atk->attacker) : -1; };
  ev["attack"] = [](LuaEvent& e, sol::this_state ts) -> sol::object {
    if (!e.atk) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, LuaAttack{e.atk, e.e});
  };
}

EffectHost::~EffectHost() = default;

void EffectHost::load_file(const std::string& path, std::vector<CardDef>& defs) {
  auto& L = impl_->lua;
  sol::protected_function_result res = L.safe_script_file(path);
  if (!res.valid()) throw std::runtime_error(std::string("lua load error: ") + sol::error(res).what());
  sol::table list = res.get<sol::table>();

  for (auto& kv : list) {
    if (!kv.second.is<sol::table>()) continue;
    sol::table t = kv.second;
    CardDef d;
    d.set = t.get_or("set", std::string(""));
    d.goddess = t.get_or("goddess", std::string(""));
    if (d.goddess.empty()) {
      auto dot = d.set.find('.');
      d.goddess = dot == std::string::npos ? d.set : d.set.substr(0, dot);
    }
    d.goddesses.push_back(d.goddess);
    d.form = t.get_or("form", std::string("O"));
    // dual-goddess cards (合奏) can list additional goddesses
    sol::object gg = t["goddesses"];
    if (gg.is<sol::table>())
      for (auto& kv : static_cast<sol::table>(gg))
        if (kv.second.is<std::string>()) {
          std::string g = kv.second.as<std::string>();
          if (std::find(d.goddesses.begin(), d.goddesses.end(), g) == d.goddesses.end())
            d.goddesses.push_back(g);
        }
    sol::object g2 = t["goddess2"];
    if (g2.is<std::string>()) {
      std::string g = g2.as<std::string>();
      if (std::find(d.goddesses.begin(), d.goddesses.end(), g) == d.goddesses.end())
        d.goddesses.push_back(g);
    }
    d.auraMax = t.get_or("aura_max", -1);
    d.local = t.get_or("num", 0);
    d.name = t.get_or("name", std::string(""));
    std::string kind = t.get_or("kind", std::string("normal"));
    d.kind = kind == "special" ? CardKind::Special : CardKind::Normal;
    std::string type = t.get_or("type", std::string("action"));
    d.type = type == "attack" ? CardType::Attack : (type == "enhance" ? CardType::Enhance : CardType::Action);
    d.cost = t.get_or("cost", -1);
    d.nagi = t.get_or("nagi", -1);
    d.text = t.get_or("text", std::string(""));
    d.armorFromCrystals = t.get_or("armor_as_crystals", false);
    d.lockDistance = t.get_or("lock_distance", false);
    d.isPart = t.get_or("part", false);
    d.corePart = t.get_or("core_part", false);
    d.electronic = t.get_or("electronic", false);
    d.setupCard = t.get_or("setup", false);
    d.isExtra = t.get_or("extra", false);
    d.centrifugal = t.get_or("centrifugal", false);
    d.zenkai = t.get_or("zenkai", false);
    d.isPoison = t.get_or("poison", false);
    d.playableFromCover = t.get_or("from_cover", false);
    d.burnRequire = t.get_or("burn_require", 0);
    d.isTransform = t.get_or("transform", false);
    d.unsealable = t.get_or("unsealable", false);
    d.noOpponentPick = t.get_or("no_opponent_pick", false);
    d.enemyNagiMod = t.get_or("enemy_nagi_mod", 0);
    d.damageImmune = t.get_or("damage_immune", false);
    d.decayToOwnerAura = t.get_or("decay_to_owner_aura", false);
    d.absorbAuraBasic = t.get_or("absorb_aura_basic", false);
    d.keepCrystalsOnReset = t.get_or("keep_crystals_on_reset", false);
    d.iceAsArmor = t.get_or("ice_as_armor", false);
    d.maySkipCrystalLoss = t.get_or("may_skip_crystal_loss", false);
    d.enemyNoFlare = t.get_or("enemy_no_flare", false);
    d.soloSpecials = t.get_or("solo_specials", false);
    d.upgrade = t.get_or("upgrade", std::string());
    d.reverseMoves = t.get_or("reverse_moves", false);
    d.complete = t.get_or("complete", false);
    d.limitDistance = t.get_or("limit_distance", false);
    d.responseOnly = t.get_or("response_only", false);
    d.denyEnemyAura = t.get_or("deny_enemy_aura", false);
    d.protectsEnemy = t.get_or("protects_enemy", false);
    d.memoryDraw = t.get_or("memory_draw", false);
    d.memoryRebuildShield = t.get_or("memory_rebuild_shield", false);
    {
      sol::object ng = t["nagi"];
      if (ng.is<sol::function>()) {
        d.nagi = 0;
        d.dynamicNagi = true;
      }
    }
    d.distanceMod = t.get_or("distance_mod", 0);
    d.nearDistanceMod = t.get_or("near_distance_mod", 0);
    d.copies = t.get_or("copies", 1);
    d.decayTo = t.get_or("decay_to", std::string("dust"));
    d.flags = flags_from(t);
    sol::object atk = t["attack"];
    if (atk.is<sol::table>()) {
      d.hasAttack = true;
      static_attack(d, atk);
    } else if (atk.is<sol::function>()) {
      d.hasAttack = true;  // dynamic range/damage resolved at play time
    }
    d.id = static_cast<int>(defs.size());
    defs.push_back(d);

    Impl::Hook h;
    h.spec = t;
    h.on_play = t["on_play"];
    h.on_enter = t["on_enter"];
    h.on_discard = t["on_discard"];
    h.on_attack_after = t["on_attack_after"];
    h.on_use_after = t["on_use_after"];
    h.keiryo = t["keiryo"];
    sol::object cont = t["continuous"];
    if (cont.is<sol::table>())
      for (auto& c2 : static_cast<sol::table>(cont))
        if (c2.second.is<sol::table>()) h.continuous.push_back(c2.second);
    sol::object trig = t["triggers"];
    if (trig.is<sol::table>())
      for (auto& tr : static_cast<sol::table>(trig))
        if (tr.second.is<sol::table>()) h.triggers.push_back(tr.second);
    sol::object rst = t["reset"];
    if (rst.is<sol::table>()) {
      sol::table rt = rst;
      std::string rk = rt.get_or("kind", std::string(""));
      if (rk == "end_turn") h.reset.kind = 1;
      else if (rk == "immediate") h.reset.kind = 2;
      sol::object lt = rt["at_least"];
      if (lt.is<int>()) h.reset.lifeThreshold = lt.as<int>();
      sol::object cond = rt["cond"];
      if (cond.is<sol::function>()) {
        h.reset.hasCond = true;
        h.reset_cond = cond.as<sol::function>();
      }
      sol::object trg = rt["on"];
      if (trg.is<std::string>()) h.reset.trigger = trg.as<std::string>();
    }
    impl_->hooks.push_back(std::move(h));
  }
}

bool EffectHost::has(int defId, const char* hook) const {
  // Any Lua hook the content defines (on_play / on_enter / on_discard /
  // on_attack_after / on_use_after / on_transform / extra_basic / ...).
  return has_hook(defId, hook);
}

bool EffectHost::has_hook(int defId, const char* hook) const {
  if (defId < 0 || defId >= static_cast<int>(impl_->hooks.size())) return false;
  sol::object o = impl_->hooks[static_cast<size_t>(defId)].spec[hook];
  return o.is<sol::function>();
}

void EffectHost::call(Engine& e, int defId, const char* hook, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  const sol::object* obj = nullptr;
  std::string hh(hook);
  if (hh == "on_play") obj = &h.on_play;
  else if (hh == "on_enter") obj = &h.on_enter;
  else if (hh == "on_discard") obj = &h.on_discard;
  else if (hh == "on_attack_after") obj = &h.on_attack_after;
  else if (hh == "on_use_after") obj = &h.on_use_after;
  LuaCtx c{&e, who, inst};
  if (!obj) {
    sol::object fn = h.spec[hook];
    if (!fn.is<sol::function>()) return;
    auto res = fn.as<sol::protected_function>()(c);
    if (!res.valid())
      impl_->fail(std::string(hook) + " in " + std::to_string(defId) + ": " + sol::error(res).what());
    return;
  }
  if (!obj->valid() || obj->get_type() == sol::type::nil) return;
  sol::protected_function f = obj->as<sol::function>();
  auto res = f(c);
  if (!res.valid())
    impl_->fail(std::string(sol::error(res).what()) + " in card " + std::to_string(defId) + " (" +
                e.def(defId).name + ")");
}

EvaluatedAttack EffectHost::eval_attack(Engine& e, int defId, Player who, int inst, bool asResponse) {
  (void)asResponse;
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  if (!h.spec.valid()) return {};
  LuaCtx c{&e, who, inst};
  return read_spec(h.spec["attack"], e, c);
}

bool EffectHost::has_continuous(int defId) const {
  if (defId < 0 || defId >= static_cast<int>(impl_->hooks.size())) return false;
  return !impl_->hooks[static_cast<size_t>(defId)].continuous.empty();
}

void EffectHost::run_continuous_attack(Engine& e, int defId, Player who, int inst, Attack& a,
                                       int pass) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  for (const sol::table& t : h.continuous) {
    std::string when = t.get_or("when", std::string("always"));
    std::string query = t.get_or("query", std::string("attack"));
    if (query != "attack") continue;
    // Official QA: value *replacement* (天地反驳 swap) precedes numeric additions.
    const bool replace = t.get_or("replace", false);
    if (pass == 0 && !replace) continue;
    if (pass == 1 && replace) continue;
    if (when == "expanded" && !e.enhance_active(inst)) continue;
    if (when == "used" && !(e.ci(inst).zone == Zone::Special && e.ci(inst).faceUp)) continue;
    sol::object fn = t["apply"];
    if (!fn.is<sol::function>()) continue;
    LuaCtx c{&e, who, inst};
    LuaAttack ha{&a, &e};
    auto res = fn.as<sol::function>()(c, ha);
    if (!res.valid())
      impl_->fail("continuous in card " + std::to_string(defId) + ": " + sol::error(res).what());
  }
}

void EffectHost::finalize_attack(Engine& e, Player attacker, Attack& a, bool consumePending) {
  auto run_continuous_pass = [&](int pass) {
    for (int oi = 0; oi < 2; ++oi) {
      Player owner = static_cast<Player>(oi);
      std::vector<int> cards;
      for (int inst : e.ps(owner).enhance) cards.push_back(inst);
      for (int inst : e.ps(owner).special)
        if (e.ci(inst).faceUp) cards.push_back(inst);
      for (int inst : cards) {
        const CardDef& d = e.def_of(inst);
        if (has_continuous(d.id)) run_continuous_attack(e, d.id, owner, inst, a, pass);
      }
    }
  };
  // 1) replacements (数值替换) first.
  run_continuous_pass(0);
  if (consumePending && !impl_->pendingOnResponse.empty()) {
    for (auto& p : impl_->pendingOnResponse) impl_->onResponse[&a].push_back(p);
    impl_->pendingOnResponse.clear();
  }

  // 2) Pending "next attack" modifiers owned by the attacker.
  std::vector<size_t> remove;
  for (size_t i = 0; i < impl_->pending.size(); ++i) {
    Impl::PendingMod& m = impl_->pending[i];
    if (m.owner != attacker) continue;
    LuaCtx c{&e, attacker, -1};
    LuaAttack ha{&a, &e};
    bool matched = true;
    if (m.match.valid()) {
      auto mr = m.match(c, ha);
      matched = mr.valid() && mr.get<bool>();
    }
    if (!matched) continue;
    if (m.apply.valid()) {
      auto ar = m.apply(c, ha);
      if (!ar.valid()) impl_->fail(std::string("pending mod apply: ") + sol::error(ar).what());
    }
    if (consumePending) remove.push_back(i);
  }
  if (!remove.empty()) {
    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
      impl_->pending.erase(impl_->pending.begin() + static_cast<long>(*it));
  }

  // 3) Remaining continuous attack modifiers.
  run_continuous_pass(1);
}

void EffectHost::clear_pending_mods(bool endOfTurnOnly) {
  if (!endOfTurnOnly) {
    impl_->pending.clear();
    return;
  }
  impl_->pending.erase(
      std::remove_if(impl_->pending.begin(), impl_->pending.end(),
                     [](const Impl::PendingMod& m) { return m.expires; }),
      impl_->pending.end());
}

size_t EffectHost::pending_mod_count() const { return impl_->pending.size(); }
void EffectHost::set_strict(bool v) { impl_->strict = v; }
int EffectHost::error_count() const { return impl_->errors; }

int EffectHost::eval_continuous_cost(Engine& e, Player owner, int inst, int cost) {
  const CardDef& d = e.def_of(inst);
  const auto& h = impl_->hooks[static_cast<size_t>(d.id)];
  int result = cost;
  for (const sol::table& t : h.continuous) {
    if (t.get_or("query", std::string("attack")) != "cost") continue;
    const std::string when = t.get_or("when", std::string("always"));
    const bool used = e.ci(inst).zone == Zone::Special && e.ci(inst).faceUp;
    if (when == "used" && !used) continue;
    if (when == "expanded" && !e.enhance_active(inst)) continue;
    if (when == "always" && !used && !e.enhance_active(inst)) continue;
    sol::object fn = t["apply"];
    if (!fn.is<sol::function>()) continue;
    LuaCost lc{result};
    LuaCtx c{&e, owner, inst};
    auto r = fn.as<sol::function>()(c, lc);
    if (!r.valid())
      impl_->fail("continuous cost in " + std::to_string(d.id) + ": " + sol::error(r).what());
    else
      result = lc.value;
  }
  return result;
}

int EffectHost::eval_nagi(Engine& e, int defId, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  sol::object f = h.spec["nagi"];
  if (!f.is<sol::function>()) return e.def(defId).nagi;
  LuaCtx c{&e, who, inst};
  auto res = f.as<sol::function>()(c);
  if (!res.valid()) return e.def(defId).nagi;
  return res.get<int>();
}

int EffectHost::eval_cost(Engine& e, int defId, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  sol::object f = h.spec["cost"];
  if (!f.is<sol::function>()) return -1;
  LuaCtx c{&e, who, inst};
  auto res = f.as<sol::function>()(c);
  if (!res.valid()) return -1;
  return res.get<int>();
}

bool EffectHost::eval_pred(Engine& e, int defId, const char* hook, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  sol::object f = h.spec[hook];
  if (!f.is<sol::function>()) return false;
  LuaCtx c{&e, who, inst};
  auto res = f.as<sol::function>()(c);
  return res.valid() && res.get<bool>();
}

ResetInfo EffectHost::reset_info(int defId) const {
  if (defId < 0 || defId >= static_cast<int>(impl_->hooks.size())) return {};
  return impl_->hooks[static_cast<size_t>(defId)].reset;
}

void EffectHost::fire(Engine& e, const std::string& event, Player subject, Attack* atk, int card,
                      bool first) {
  // Does this card have a trigger for `event` that passes the zone filter?
  auto matches = [&](Player owner, int inst) {
    (void)owner;
    const auto& h = impl_->hooks[static_cast<size_t>(e.def_of(inst).id)];
    for (const sol::table& t : h.triggers) {
      if (t.get_or("event", std::string()) != event) continue;
      const std::string wantZone = t.get_or("zone", std::string());
      const Zone z = e.ci(inst).zone;
      if (wantZone.empty()) {
        if (z != Zone::Enhance && z != Zone::Special && z != Zone::Removed) continue;
      } else if (wantZone == "discard") {
        if (z != Zone::Discard) continue;
      } else if (wantZone == "hand") {
        if (z != Zone::Hand) continue;
      }
      return true;
    }
    return false;
  };
  auto resolve = [&](Player owner, int inst) {
    const CardDef& d = e.def_of(inst);
    const auto& h = impl_->hooks[static_cast<size_t>(d.id)];
    for (const sol::table& t : h.triggers) {
      if (t.get_or("event", std::string()) != event) continue;
      const std::string wantZone = t.get_or("zone", std::string());
      const Zone z = e.ci(inst).zone;
      if (wantZone.empty()) {
        // Implicit triggers only fire while the card is in play: an expanded
        // 付与, a used 切札, or the active 变形 aura. Hand / discard triggers
        // must opt in with zone="hand" / zone="discard".
        if (z != Zone::Enhance && z != Zone::Special && z != Zone::Removed) continue;
      } else if (wantZone == "discard") {
        if (z != Zone::Discard) continue;
      } else if (wantZone == "hand") {
        if (z != Zone::Hand) continue;
      }
      LuaCtx c{&e, owner, inst};
      LuaEvent le{&e, event, subject, atk, card, first};
      sol::object cond = t["cond"];
      if (cond.is<sol::function>()) {
        auto r = cond.as<sol::function>()(c, le);
        if (!r.valid() || !r.get<bool>()) continue;
      }
      sol::object run = t["run"];
      if (run.is<sol::function>()) {
        auto r = run.as<sol::function>()(c, le);
        if (!r.valid())
          impl_->fail("trigger " + event + " in " + std::to_string(d.id) + ": " +
                      sol::error(r).what());
      }
    }
  };

  // Candidate (owner, card) pairs, active player first.
  std::vector<std::pair<Player, int>> cand;
  for (int oi = 0; oi < 2; ++oi) {
    Player owner = (oi == 0) ? e.st.active : opp(e.st.active);
    std::vector<int> cards;
    for (int inst : e.ps(owner).enhance) cards.push_back(inst);
    for (int inst : e.ps(owner).special)
      if (e.ci(inst).faceUp) cards.push_back(inst);
    for (int inst : e.ps(owner).hand) cards.push_back(inst);  // hand triggers (伞飞转)
    int aura = e.active_transform_inst(owner);                // 变形 aura triggers
    if (aura >= 0) cards.push_back(aura);
    // A card that wants to trigger from the discard pile opts in with zone="discard".
    for (int inst : e.ps(owner).discard) {
      const auto& dh = impl_->hooks[static_cast<size_t>(e.def_of(inst).id)];
      for (const sol::table& t : dh.triggers)
        if (t.get_or("zone", std::string()) == "discard") {
          cards.push_back(inst);
          break;
        }
    }
    for (int inst : cards)
      if (matches(owner, inst)) cand.push_back({owner, inst});
  }

  // 结束阶段的多个触发视为同一时刻触发：由当前玩家决定结算顺序。
  const bool ordered = (event == "end_phase_start" || event == "turn_end");
  if (!ordered || cand.size() <= 1) {
    for (const auto& [owner, inst] : cand) resolve(owner, inst);
    return;
  }
  while (!cand.empty()) {
    size_t pick = 0;
    if (cand.size() > 1) {
      Request r;
      r.kind = "option";
      r.prompt = "结束阶段：选择下一个结算的触发";
      for (const auto& [owner, inst] : cand) {
        std::string label = e.def_of(inst).name;
        if (owner != e.st.active) label += "（对手）";
        r.options.push_back({label, true, {}});
      }
      Decision d = e.decide(e.st.active, std::move(r));
      if (!d.indices.empty() && d.indices[0] >= 0 &&
          d.indices[0] < static_cast<int>(cand.size()))
        pick = static_cast<size_t>(d.indices[0]);
    }
    auto chosen = cand[pick];
    cand.erase(cand.begin() + static_cast<long>(pick));
    resolve(chosen.first, chosen.second);
  }
}

void EffectHost::run_on_response(Engine& e, Attack* a) {
  auto it = impl_->onResponse.find(a);
  if (it == impl_->onResponse.end()) return;
  for (auto& [who, f] : it->second) {
    LuaCtx c{&e, who, a->sourceInst};
    LuaAttack ha{a, &e};
    auto r = f(c, ha);
    if (!r.valid()) impl_->fail(std::string("on_response: ") + sol::error(r).what());
  }
}

void EffectHost::clear_attack_callbacks(Attack* a) {
  impl_->onResponse.erase(a);
  impl_->afterAttack.erase(a);
}

void EffectHost::run_after_attack(Engine& e, Attack* a) {
  auto it = impl_->afterAttack.find(a);
  if (it == impl_->afterAttack.end()) return;
  auto list = std::move(it->second);
  impl_->afterAttack.erase(it);
  for (auto& [owner, f] : list) {
    LuaCtx c{&e, owner, -1};
    LuaAttack ha{a, &e};
    auto r = f(c, ha);
    if (!r.valid()) impl_->fail(std::string("on_resolve: ") + sol::error(r).what());
  }
}

bool EffectHost::has_keiryo(int defId) const {
  if (defId < 0 || defId >= static_cast<int>(impl_->hooks.size())) return false;
  const sol::object& o = impl_->hooks[static_cast<size_t>(defId)].keiryo;
  return o.valid() && o.get_type() != sol::type::nil;
}

void EffectHost::call_keiryo(Engine& e, int defId, Player who, int inst, int branch) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  if (!h.keiryo.valid() || h.keiryo.get_type() == sol::type::nil) return;
  LuaCtx c{&e, who, inst};
  auto res = h.keiryo.as<sol::function>()(c, branch);
  if (!res.valid())
    impl_->fail("keiryo in " + std::to_string(defId) + ": " + sol::error(res).what());
}

void EffectHost::apply_part(Engine& e, int defId, Player who, Attack& a, int n, const char* hook) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  sol::object f = h.spec[hook];
  if (!f.is<sol::function>()) return;
  LuaCtx c{&e, who, -1};
  LuaAttack ha{&a, &e};
  auto res = f.as<sol::function>()(c, ha, n);
  if (!res.valid())
    impl_->fail(std::string("part ") + hook + " in " + std::to_string(defId) + ": " +
                sol::error(res).what());
}

bool EffectHost::eval_reset_cond(Engine& e, int defId, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  if (!h.reset_cond.valid()) return false;
  LuaCtx c{&e, who, inst};
  auto res = h.reset_cond(c);
  if (!res.valid()) return false;
  return res.get<bool>();
}

}  // namespace fy
