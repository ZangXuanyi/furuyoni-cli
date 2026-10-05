#include "engine/effect_host.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "engine/engine.hpp"
#include "protocol/agent.hpp"

namespace fy {

namespace {

uint32_t kwflag(const std::string& s) {
  if (s == "unrespondable" || s == "不可对") return AF_Unrespondable;
  if (s == "lock" || s == "锁定") return AF_Lock;
  if (s == "overwhelm" || s == "超克") return AF_Overwhelm;
  if (s == "both_sides" || s == "两侧") return AF_BothSides;
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
    ea.damage.life = read_int(dt["life"], e, c);
  }
  ea.keywords = read_keywords(t["keywords"], e, c);
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
  struct Hook {
    sol::object on_play, on_enter, on_discard, on_attack_after, on_use_after;
    sol::table spec;
    std::vector<sol::table> continuous;
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
  atkutil["aura_damage"] = [](LuaAttack& h, sol::this_state ts) -> sol::object {
    if (!h.a->aura) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, *h.a->aura + h.a->auraDelta);
  };
  atkutil["life_damage"] = [](LuaAttack& h, sol::this_state ts) -> sol::object {
    if (!h.a->life) return sol::make_object(ts.L, sol::nil);
    return sol::make_object(ts.L, *h.a->life + h.a->lifeDelta);
  };
  atkutil["source_inst"] = [](LuaAttack& h) { return h.a->sourceInst; };
  atkutil["from_normal"] = [](LuaAttack& h) -> bool {
    if (h.a->sourceInst < 0 || !h.e) return false;
    return h.e->def_of(h.a->sourceInst).kind == CardKind::Normal;
  };
  atkutil["from_special"] = [](LuaAttack& h) -> bool {
    if (h.a->sourceInst < 0) return h.a->fromSpecial;
    return h.e->def_of(h.a->sourceInst).kind == CardKind::Special;
  };
  atkutil["source_goddess"] = [](LuaAttack& h) -> std::string {
    if (h.a->sourceInst < 0 || !h.e) return std::string();
    return h.e->def_of(h.a->sourceInst).goddess;
  };
  atkutil["source_set"] = [](LuaAttack& h) -> std::string {
    if (h.a->sourceInst < 0 || !h.e) return std::string();
    return h.e->def_of(h.a->sourceInst).set;
  };

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
  ctx["distance"] = [](LuaCtx& c) { return c.e->st.distance; };
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
  ctx["to_deck_top"] = [](LuaCtx& c, int inst) { c.e->move_card_top(inst); };
  ctx["to_deck_bottom"] = [](LuaCtx& c, int inst) { c.e->move_card_bottom(inst); };
  ctx["discard_card"] = [](LuaCtx& c, int inst) { c.e->move_card(inst, Zone::Discard); };
  ctx["reveal_hand"] = [](LuaCtx& c, int p) { c.e->reveal_hand(static_cast<Player>(p)); };

  ctx["move"] = [](LuaCtx& c, std::string from, std::string to, int n, sol::optional<int> pf,
                   sol::optional<int> pt) -> int {
    Player pFrom = pf ? static_cast<Player>(*pf) : c.who;
    Player pTo = pt ? static_cast<Player>(*pt) : pFrom;
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
  ctx["attack"] = [this](LuaCtx& c, sol::table spec) {
    Attack a;
    a.attacker = c.who;
    a.sourceInst = c.source;
    if (c.source >= 0) a.fromSpecial = c.e->def_of(c.source).kind == CardKind::Special;
    a.fromResponse = false;
    EvaluatedAttack ea = read_spec(spec, *c.e, c);
    a.range = ea.range;
    a.aura = ea.damage.aura;
    a.life = ea.damage.life;
    a.keywords = ea.keywords;
    finalize_attack(*c.e, c.who, a, true);
    c.e->resolve_attack(a);
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
    d.form = t.get_or("form", std::string("O"));
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
    sol::object cont = t["continuous"];
    if (cont.is<sol::table>())
      for (auto& c2 : static_cast<sol::table>(cont))
        if (c2.second.is<sol::table>()) h.continuous.push_back(c2.second);
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
    }
    impl_->hooks.push_back(std::move(h));
  }
}

bool EffectHost::has(int defId, const char* hook) const {
  if (defId < 0 || defId >= static_cast<int>(impl_->hooks.size())) return false;
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  auto ok = [](const sol::object& o) { return o.valid() && o.get_type() != sol::type::nil; };
  std::string hh(hook);
  if (hh == "on_play") return ok(h.on_play);
  if (hh == "on_enter") return ok(h.on_enter);
  if (hh == "on_discard") return ok(h.on_discard);
  if (hh == "on_attack_after") return ok(h.on_attack_after);
  if (hh == "on_use_after") return ok(h.on_use_after);
  return false;
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
  if (!obj || !obj->valid() || obj->get_type() == sol::type::nil) return;

  LuaCtx c{&e, who, inst};
  sol::protected_function f = obj->as<sol::function>();
  auto res = f(c);
  if (!res.valid())
    std::fprintf(stderr, "[lua error] %s in card %d (%s)\n", sol::error(res).what(), defId,
                 e.def(defId).name.c_str());
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

void EffectHost::run_continuous_attack(Engine& e, int defId, Player who, int inst, Attack& a) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  for (const sol::table& t : h.continuous) {
    std::string when = t.get_or("when", std::string("always"));
    std::string query = t.get_or("query", std::string("attack"));
    if (query != "attack") continue;
    if (when == "expanded" && e.ci(inst).zone != Zone::Enhance) continue;
    if (when == "used" && !(e.ci(inst).zone == Zone::Special && e.ci(inst).faceUp)) continue;
    sol::object fn = t["apply"];
    if (!fn.is<sol::function>()) continue;
    LuaCtx c{&e, who, inst};
    LuaAttack ha{&a, &e};
    auto res = fn.as<sol::function>()(c, ha);
    if (!res.valid())
      std::fprintf(stderr, "[lua error] continuous in card %d: %s\n", defId, sol::error(res).what());
  }
}

void EffectHost::finalize_attack(Engine& e, Player attacker, Attack& a, bool consumePending) {
  // Pending "next attack" modifiers owned by the attacker.
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
      if (!ar.valid())
        std::fprintf(stderr, "[lua error] pending mod apply: %s\n", sol::error(ar).what());
    }
    if (consumePending) remove.push_back(i);
  }
  if (!remove.empty()) {
    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
      impl_->pending.erase(impl_->pending.begin() + static_cast<long>(*it));
  }

  // Active continuous attack modifiers from every in-play card.
  for (int oi = 0; oi < 2; ++oi) {
    Player owner = static_cast<Player>(oi);
    std::vector<int> cards;
    for (int inst : e.ps(owner).enhance) cards.push_back(inst);
    for (int inst : e.ps(owner).special)
      if (e.ci(inst).faceUp) cards.push_back(inst);
    for (int inst : cards) {
      const CardDef& d = e.def_of(inst);
      if (has_continuous(d.id)) run_continuous_attack(e, d.id, owner, inst, a);
    }
  }
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

bool EffectHost::eval_reset_cond(Engine& e, int defId, Player who, int inst) {
  const auto& h = impl_->hooks[static_cast<size_t>(defId)];
  if (!h.reset_cond.valid()) return false;
  LuaCtx c{&e, who, inst};
  auto res = h.reset_cond(c);
  if (!res.valid()) return false;
  return res.get<bool>();
}

}  // namespace fy
