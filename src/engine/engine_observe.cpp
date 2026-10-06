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

nlohmann::json Engine::full_state_json() const {
  using nlohmann::json;
  json j;
  j["turn"] = st.turn;
  j["active"] = static_cast<int>(st.active);
  j["phase"] = phase_;
  j["distance"] = distance();
  j["dust"] = st.dust;
  j["nearDistance"] = near_distance();
  j["over"] = st.over;
  j["winner"] = st.winner;
  json stack = json::array();
  for (const StackEntry& s : callStack_) {
    const CardDef& d = defs[static_cast<size_t>(s.def)];
    stack.push_back({{"name", d.name}, {"owner", static_cast<int>(s.owner)}, {"set", d.set}});
  }
  j["stack"] = stack;
  auto card = [&](int inst) -> json {
    const CardDef& d = def_of(inst);
    return {{"name", d.name},
            {"kind", d.kind == CardKind::Normal ? "normal" : "special"},
            {"type", d.type == CardType::Attack   ? "attack"
                     : d.type == CardType::Enhance ? "enhance"
                                                   : "action"},
            {"cost", d.cost},
            {"set", d.set}};
  };
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const PlayerState& s = ps(p);
    json pj;
    pj["life"] = s.life;
    pj["aura"] = s.aura;
    pj["ice"] = s.ice;  // 冻结冰晶（公开信息）
    pj["curse"] = s.curse;  // 诅咒（公开信息）
    pj["flare"] = s.flare;
    pj["vigor"] = s.vigor;
    pj["cower"] = s.cower;
    pj["cannotRespond"] = s.cannotRespond;
    pj["umbrella"] = s.umbrella;
    pj["yukihi"] = s.yukihi;
    pj["strategy"] = s.strategy;
    pj["strategyKnown"] = s.strategyKnown;
    pj["sets"] = playerSets_[pi];
    pj["hand"] = json::array();
    for (int i : s.hand) pj["hand"].push_back(card(i));
    pj["special"] = json::array();
    for (int i : s.special) {
      json c = card(i);
      c["used"] = ci(i).faceUp;
      pj["special"].push_back(c);
    }
    pj["discard"] = json::array();
    for (int i : s.discard) pj["discard"].push_back(card(i));
    pj["cover"] = json::array();
    for (int i : s.cover) pj["cover"].push_back(card(i));
    pj["deck"] = json::array();
    for (int i : s.deck) pj["deck"].push_back(card(i));
    pj["enhance"] = json::array();
    for (int i : s.enhance) {
      json c = card(i);
      c["crystals"] = ci(i).crystals;
      pj["enhance"].push_back(c);
    }
    pj["parts"] = json::array();
    for (int i : s.parts) {
      json c = card(i);
      c["assembled"] = ci(i).assembled;
      c["core"] = def_of(i).corePart;
      pj["parts"].push_back(c);
    }
    j["players"].push_back(pj);
  }
  return j;
}

nlohmann::json Engine::trace_json() const {
  using nlohmann::json;
  json j;
  j["seed"] = cfg.seed;
  j["mode"] = cfg.mode;
  j["frames"] = frames_;
  json draft;
  if (hasDraft_[0] || hasDraft_[1]) {
    for (int i = 0; i < 2; ++i) {
      json picks = json::array();
      for (const auto& [g, f] : draftPicks_[i]) {
        json e;
        e["goddess"] = g;
        e["form"] = f;
        e["display"] = goddess_display(g, f);
        e["banned"] = (draftBans_[i] == std::make_pair(g, f));
        picks.push_back(e);
      }
      draft[std::to_string(i)] = picks;
    }
    j["draft"] = draft;
  }
  j["result"] = {{"winner", st.winner}, {"text", result_text()}};
  return j;
}

std::string Engine::result_text() const {
  if (hasDraft_[0] && hasDraft_[1]) {
    auto fmt = [&](int i) {
      std::string s;
      for (const auto& [g, f] : draftPicks_[i]) {
        std::string n = goddess_display(g, f);
        if (draftBans_[i] == std::make_pair(g, f)) n = "（" + n + "）";
        s += n;
      }
      return s;
    };
    std::string a = "Player0 " + fmt(0);
    std::string b = "Player1 " + fmt(1);
    if (st.winner == 0) return a + " 胜 " + b;
    if (st.winner == 1) return b + " 胜 " + a;
    return a + " 平局 " + b;
  }
  std::string a = "Player0";
  std::string b = "Player1";
  if (st.winner == 0) return a + " 胜 " + b;
  if (st.winner == 1) return b + " 胜 " + a;
  return a + " 平局 " + b;
}

void Engine::start_recording() {
  recording_ = true;
  replaying_ = false;
  journal_.clear();
  replay_pos_ = 0;
}

nlohmann::json Engine::journal_json() const {
  using nlohmann::json;
  json j;
  j["seed"] = cfg.seed;
  j["mode"] = cfg.mode;
  j["hash"] = state_hash();
  j["turn"] = st.turn;
  j["winner"] = st.winner;
  j["entries"] = json::array();
  for (const JournalEntry& e : journal_) {
    json je;
    je["player"] = static_cast<int>(e.player);
    je["kind"] = e.kind;
    je["indices"] = e.indices;
    j["entries"].push_back(je);
  }
  return j;
}

void Engine::load_journal(const nlohmann::json& j) {
  journal_.clear();
  replay_pos_ = 0;
  recording_ = false;
  replaying_ = true;
  for (const auto& je : j.at("entries")) {
    JournalEntry e;
    e.player = static_cast<Player>(je.at("player").get<int>());
    e.kind = je.at("kind").get<std::string>();
    e.indices = je.at("indices").get<std::vector<int>>();
    journal_.push_back(std::move(e));
  }
}

uint64_t Engine::state_hash() const {
  // Exhaustive digest: every zone (contents and order), every instance field,
  // every per-turn counter and the RNG. A weak hash would make "replay hashes
  // match" a meaningless check, so anything observable must be included.
  std::string s;
  s.reserve(4096);
  auto put = [&](auto v) { s += std::to_string(v); s += ','; };
  auto putb = [&](bool b) { s += b ? '1' : '0'; s += ','; };
  auto puts = [&](const std::string& v) { s += v; s += '\x1f'; };
  auto putv = [&](const std::vector<int>& v) {
    put(v.size());
    for (int i : v) put(i);
  };

  put(st.turn);
  put(static_cast<int>(st.active));
  put(st.distance);
  put(st.dust);
  put(st.nearDistance);
  put(st.maxLife);
  put(st.maxAura);
  putb(st.over);
  put(st.winner);
  puts(phase_);
  put(static_cast<unsigned long long>(st.rng.s));
  put(callStack_.size());
  for (const StackEntry& e : callStack_) {
    put(e.def);
    put(static_cast<int>(e.owner));
    putb(e.fromCover);
  }

  for (int i = 0; i < 2; ++i) {
    const PlayerState& p = st.p[i];
    put(p.life); put(p.aura); put(p.ice); put(p.flare); put(p.vigor);
    putb(p.cower); putb(p.cannotRespond); put(p.lastLifeLost); put(p.cardsPlayedThisTurn);
    put(p.handLimit); put(p.cutCostDelta); putb(p.cannotAttack); putb(p.cannotBasic);
    putb(p.usedLastCrystal); putb(p.umbrella); putb(p.yukihi);
    put(p.strategy); putb(p.strategyKnown); putb(p.cannotAdvance); putb(p.thallya);
    put(p.steamEngine); put(p.steamExhausted); put(p.steamOnDist); put(p.steamOnCrystal);
    put(p.transformDef); put(p.transformCount); putb(p.raira); put(p.wind); put(p.thunder);
    putb(p.rairaGainRestricted); putb(p.cutCostPermanent); putb(p.firstTurnDone);
    putb(p.nextDrawOne);
    put(static_cast<int>(playerSets_[i].size()));
    for (const std::string& set : playerSets_[i]) puts(set);
    putv(p.deck); putv(p.hand); putv(p.discard); putv(p.cover);
    putv(p.enhance); putv(p.special); putv(p.parts); putv(p.bag); putv(p.memory);
    put(p.cardsPlayedTotal); put(p.curse); putb(p.hasCurse); puts(p.extraAttackCostGoddess);
  }

  for (const CardInstance& c : st.insts) {
    put(c.inst); put(c.def); put(static_cast<int>(c.owner)); put(static_cast<int>(c.holder));
    put(static_cast<int>(c.zone));
    putb(c.faceUp); putb(c.assembled); putb(c.usedThisTurn); put(c.crystals);
    put(c.sealedBy); put(c.bagOwner); putv(c.sealed);
  }

  // Per-turn event bookkeeping (all "first time this turn" triggers).
  for (int i = 0; i < 2; ++i) {
    put(attacksThisTurn_[i]); put(auraChangesThisTurn_[i]); put(auraChangeFired_[i]);
    put(attackFirstFired_[i]); putb(auraDamagedThisTurn_[i]); put(normalNonYukihi_[i]);
    putb(attackedThisTurn_[i]); putb(playedCentrifugalThisTurn_[i]);
    putb(playedLianchengThisTurn_[i]); putb(didBasicThisTurn_[i]);
    putb(rebuiltThisTurn_[i]); putb(usedFullPowerThisTurn_[i]);
    putb(revealOppSpecials_[i]); putb(ashuraExtraUsed_[i]);
  }
  put(distanceAtTurnStart_);
  putb(zenkaiActive_);
  putb(poisonForce_);
  put(pendingNagiAdjust_);
  putb(damageToDistance_);
  putb(keisouDoubled_);
  putb(forceUnrespondable_);
  put(externalAdded_);
  put(static_cast<int>(effects_->pending_mod_count()));
  put(static_cast<int>(vars_.size()));
  for (const auto& [key, value] : vars_) {
    put(key.first);
    puts(key.second);
    put(value);
  }

  uint64_t h = 1469598103934665603ull;
  for (unsigned char ch : s) {
    h ^= ch;
    h *= 1099511628211ull;
  }
  return h;
}

nlohmann::json Engine::observation(Player v) const {
  using nlohmann::json;
  json j;
  j["you"] = static_cast<int>(v);
  j["turn"] = st.turn;
  j["active"] = static_cast<int>(st.active);
  j["distance"] = distance();
  j["dust"] = st.dust;
  j["nearDistance"] = near_distance();
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const PlayerState& s = ps(p);
    json pj;
    pj["life"] = s.life;
    pj["aura"] = s.aura;
    pj["ice"] = s.ice;  // 冻结冰晶（公开信息）
    pj["curse"] = s.curse;  // 诅咒（公开信息）
    pj["flare"] = s.flare;
    pj["vigor"] = s.vigor;
    pj["cower"] = s.cower;
    pj["cannotRespond"] = s.cannotRespond;
    pj["cardsPlayedThisTurn"] = s.cardsPlayedThisTurn;
    pj["umbrella"] = s.umbrella;
    pj["yukihi"] = s.yukihi;
    if (pi == static_cast<int>(v) || s.strategyKnown) pj["strategy"] = s.strategy;
    pj["sets"] = playerSets_[pi];
    pj["handCount"] = static_cast<int>(s.hand.size());
    pj["deckCount"] = static_cast<int>(s.deck.size());
    pj["coverCount"] = static_cast<int>(s.cover.size());
    pj["memoryCount"] = static_cast<int>(s.memory.size());  // 内容对对手保密
    if (pi == static_cast<int>(v)) {
      json mem = json::array();
      for (int inst : s.memory) mem.push_back(def_of(inst).name);
      pj["memory"] = mem;
    }
    json disc = json::array();
    for (int inst : s.discard) disc.push_back(def_of(inst).name);
    pj["discard"] = disc;
    json enh = json::array();
    for (int inst : s.enhance) {
      json e;
      e["owner"] = pi;
      e["inst"] = inst;
      e["name"] = def_of(inst).name;
      e["crystals"] = ci(inst).crystals;
      enh.push_back(e);
    }
    pj["enhance"] = enh;
    json sp = json::array();
    for (int inst : s.special) {
      const CardInstance& c = ci(inst);
      json e;
      e["owner"] = pi;
      if (pi == static_cast<int>(v) || c.faceUp || revealOppSpecials_[v]) {
        e["inst"] = inst;
        e["name"] = def_of(inst).name;
        e["used"] = c.faceUp;
      } else {
        e["hidden"] = true;
      }
      sp.push_back(e);
    }
    pj["special"] = sp;
    // 毒袋 is public information (rules/09-chikage.md).
    json bag = json::array();
    for (int inst : s.bag) bag.push_back(def_of(inst).name);
    pj["bag"] = bag;
    if (pi == static_cast<int>(v)) {
      json parts = json::array();
      for (int inst : s.parts) {
        json e;
        e["inst"] = inst;
        e["name"] = def_of(inst).name;
        e["assembled"] = ci(inst).assembled;
        parts.push_back(e);
      }
      pj["parts"] = parts;
    } else {
      pj["assembledCount"] = assembled_count(p);
      pj["partsTotal"] = static_cast<int>(s.parts.size());
    }
    if (pi == static_cast<int>(v)) {
      json h = json::array();
      for (int inst : s.hand) {
        json e = card_json(def_of(inst));
        e["inst"] = inst;
        h.push_back(e);
      }
      pj["hand"] = h;
    }
    j["players"][pi] = pj;
  }
  return j;
}

}  // namespace fy
