#include "engine/engine.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
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
    pj["tailwind"] = s.tailwind;  // 航海（回放用完整状态）
    pj["dive"] = s.dive;          // 潜水（含秘密选择）
    pj["aim"] = s.aim;            // 25-Misora 瞄准点
    pj["wounds"] = {{"aura", {s.wound[kWoundAura][0], s.wound[kWoundAura][1]}},
                    {"flare", {s.wound[kWoundFlare][0], s.wound[kWoundFlare][1]}},
                    {"life", {s.wound[kWoundLife][0], s.wound[kWoundLife][1]}}};
    pj["damageTakenThisTurn"] = s.damageTakenThisTurn;
    // 23-Akina 股市 / 股价 / 本回合算法（公开信息）
    pj["market"] = s.market;
    pj["stockPrice"] = s.stockPrice;
    pj["algorithmThisTurn"] = s.algorithmThisTurn;
    pj["cashOutThisTurn"] = s.cashOutThisTurn;
    // 26-Innealra 诺伦: 惑 / 命运槽 / 纠葛 / 共鸣计数（公开信息）
    pj["waku"] = s.waku;
    pj["fate"] = json::array();
    for (int k = 0; k < 4; ++k)
      pj["fate"].push_back(s.fate[k] >= 0 ? def(s.fate[k]).name : std::string());
    pj["fatesEntangled"] = s.fatesEntangled;
    pj["resonanceCountThisTurn"] = s.resonanceCountThisTurn;
    pj["usedNonInnealraThisTurn"] = s.usedNonInnealraThisTurn;
    pj["usedNormalThisTurn"] = s.usedNormalThisTurn;
    pj["sets"] = playerSets_[pi];
    // 19-Megumi: 土壤 / 假想树（回放用完整状态）
    pj["hasSoil"] = s.hasSoil;
    pj["seeds"] = s.soilSeeds;
    pj["plants"] = s.soilPlants;
    pj["tree"] = s.tree;
    pj["treeActive"] = s.treeActive;
    // 20-Kanawe: 地图 / 戏剧（回放用完整状态）
    pj["node"] = s.node;
    pj["dramaPrepared"] = s.dramaPrepared;
    pj["dramaProgressedThisTurn"] = s.dramaProgressedThisTurn;
    pj["dramaProgressedLastTurn"] = s.dramaProgressedLastTurn;
    pj["noDramaThisTurn"] = s.noDramaThisTurn;
    pj["dramas"] = json::array();
    for (int i : s.dramas) {
      json e;
      e["name"] = def_of(i).name;
      e["slot"] = def_of(i).dramaSlot;
      e["tag"] = load_int(i, "tag", 0);
      e["progress"] = load_int(i, "progress", 0);
      e["tier"] = load_int(i, "tier", 0);
      pj["dramas"].push_back(e);
    }
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
      c["green"] = ci(i).green;  // 19-Megumi
      pj["enhance"].push_back(c);
    }
    pj["parts"] = json::array();
    for (int i : s.parts) {
      json c = card(i);
      c["assembled"] = ci(i).assembled;
      c["core"] = def_of(i).corePart;
      pj["parts"].push_back(c);
    }
    pj["barracks"] = json::array();  // 18-Mizuki（完整状态含兵舍）
    for (int i : s.barracks) {
      json c = card(i);
      c["mobilized"] = ci(i).faceUp;
      pj["barracks"].push_back(c);
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
    put(p.mech);  // 机制能力位
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
    putb(p.tailwind); putb(p.forcedTailwind); putb(p.oppAttackedLastTurn); put(p.dive);
    put(p.aim);  // 25-Misora 瞄准点
    // 24-Shisui 裂伤指示物（不占位置的公开信息）与本回合受伤次数。
    for (int a = 0; a < 3; ++a)
      for (int s = 0; s < 2; ++s) put(p.wound[a][s]);
    put(p.damageTakenThisTurn);
    // 23-Akina: 股市 / 股价 / 本回合套现与算法标记
    put(p.market); put(p.stockPrice); putb(p.algorithmThisTurn); putb(p.cashOutThisTurn);
    // 26-Innealra: 惑 / 命运槽 / 纠葛 / 本回合共鸣与使用记录
    put(p.waku);
    for (int k = 0; k < 4; ++k) put(p.fate[k]);
    putb(p.fatesEntangled); put(p.resonanceCountThisTurn);
    putb(p.usedNonInnealraThisTurn); put(p.usedNormalThisTurn);
    putb(p.cannotUseNormals); putb(p.cannotRetreat); putb(p.nextRebuildFreeze);
    // 18-Mizuki: 兵舍 / 阵地 / 对应计数
    putv(p.barracks); putb(p.distChanged); putb(p.respondedThisTurn);
    putb(p.respondedLastTurn); put(p.attackCardsPlayedThisTurn);
    put(p.normalAttacksThisTurn); put(p.responsesPlayedThisTurn);
    // 19-Megumi: 土壤 / 假想树
    putb(p.hasSoil); put(p.soilSeeds); put(p.soilPlants); putv(p.tree);
    putb(p.treeActive); put(p.nextGrowth);
    // 20-Kanawe: 地图 / 戏剧
    puts(p.node); put(p.dramaPrepared); putb(p.dramaProgressedThisTurn);
    putb(p.dramaProgressedLastTurn); putb(p.noDramaThisTurn); putv(p.dramas);
    // 22-Renri: 本回合质疑失败标记
    putb(p.doubtFailedThisTurn);
  }

  for (const CardInstance& c : st.insts) {
    put(c.inst); put(c.def); put(static_cast<int>(c.owner)); put(static_cast<int>(c.holder));
    put(static_cast<int>(c.zone));
    putb(c.faceUp); putb(c.assembled); putb(c.usedThisTurn); put(c.crystals);
    put(c.green);
    put(c.sealedBy); put(c.bagOwner); putv(c.sealed); putb(c.soldier);
  }

  // Per-turn event bookkeeping (all "first time this turn" triggers).
  for (int i = 0; i < 2; ++i) {
    put(attacksThisTurn_[i]); put(auraChangesThisTurn_[i]); put(auraChangeFired_[i]);
    put(attackFirstFired_[i]); putb(auraDamagedThisTurn_[i]); put(normalNonYukihi_[i]);
    putb(attackedThisTurn_[i]); putb(playedCentrifugalThisTurn_[i]);
    putb(playedLianchengThisTurn_[i]); putb(didBasicThisTurn_[i]);
    putb(rebuiltThisTurn_[i]); putb(usedFullPowerThisTurn_[i]);
    putb(ashuraExtraUsed_[i]);
    put(generatedAttacks_[i]); put(enemyLifeDamageFired_[i]); put(enemyAuraDamageFired_[i]);
    putb(lifeChangedThisTurn_);
    put(lifeChangeMaxThisTurn_);
    put(crystalBatchAny_);
    put(crystalBatchNonCard_);
    put(cardCrystalMovesThisTurn_);
    putb(pendingAdvance_[i]);
    putb(pendingAdvanceTier_[i]);
  }
  put(distanceAtTurnStart_);
  put(distanceBaseline_);
  putb(zenkaiActive_);
  putb(poisonForce_);
  put(pendingNagiAdjust_);
  putb(keisouDoubled_);
  putb(forceUnrespondable_);
  put(externalAdded_);
  // 26-Innealra: 瞬态结算状态（命运槽解析 / 被对应标记）；伤害路由已是参数
  put(fateResolvingSlot_);
  putb(fateFromTurnStart_);
  putb(lastAtkResponded_);
  putb(inBasicAction_);
  put(static_cast<int>(basicActor_));
  put(static_cast<int>(effects_->pending_mod_count()));
  // 22-Renri 夜山恋离: 伪证 / 回归 / 复制 的瞬态状态
  put(pendingResolveAs_);
  put(resolveOverrideInst_);
  putb(bluffActive_);
  put(bluffInst_);
  put(bluffClaimDef_);
  putb(bluffNotDoubted_);
  putb(bluffDoubtFailed_);
  putb(crystalLeftDustThisTurn_);
  put(crystalMover_);
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
    pj["tailwind"] = s.tailwind;       // 顺风/逆风为公开信息
    pj["oppAttackedLastTurn"] = s.oppAttackedLastTurn;
    pj["diving"] = s.dive != 0;        // 是否处于潜水状态
    if (pi == static_cast<int>(v)) pj["dive"] = s.dive;  // 前进/后退的选择对对手保密
    pj["aim"] = s.aim;                 // 25-Misora 瞄准点（公开信息）
    // 24-Shisui: 裂伤指示物不占位置、可以出现在任意一方的装/气/命，均为公开
    // 信息；数组下标 = 造成该裂伤的来源玩家。
    pj["wounds"] = {{"aura", {s.wound[kWoundAura][0], s.wound[kWoundAura][1]}},
                    {"flare", {s.wound[kWoundFlare][0], s.wound[kWoundFlare][1]}},
                    {"life", {s.wound[kWoundLife][0], s.wound[kWoundLife][1]}}};
    pj["damageTakenThisTurn"] = s.damageTakenThisTurn;  // 公开信息
    // 23-Akina 股市 / 股价（公开信息）；资本仅对安琪娜玩家有意义。
    pj["market"] = s.market;
    pj["stockPrice"] = s.stockPrice;
    pj["algorithmThisTurn"] = s.algorithmThisTurn;
    pj["capital"] = capital(p);
    // 26-Innealra 诺伦: 惑 / 命运槽内容 / 纠葛 / 共鸣计数（均为公开信息）
    pj["waku"] = s.waku;
    {
      json fates = json::array();
      for (int k = 0; k < 4; ++k)
        fates.push_back(s.fate[k] >= 0 ? def(s.fate[k]).name : std::string());
      pj["fates"] = fates;
    }
    pj["fatesEntangled"] = fates_entangled(p);
    pj["resonanceCountThisTurn"] = s.resonanceCountThisTurn;
    pj["umbrella"] = s.umbrella;
    pj["yukihi"] = s.yukihi;
    pj["doubtFailedThisTurn"] = s.doubtFailedThisTurn;  // 22-Renri（公开信息）
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
      e["green"] = ci(inst).green;  // 19-Megumi
      enh.push_back(e);
    }
    pj["enhance"] = enh;
    json sp = json::array();
    for (int inst : s.special) {
      const CardInstance& c = ci(inst);
      json e;
      e["owner"] = pi;
      if (pi == static_cast<int>(v) || c.faceUp) {
        e["inst"] = inst;
        e["name"] = def_of(inst).name;
        e["used"] = c.faceUp;
        if (c.green > 0) e["green"] = c.green;  // 19-Megumi: 牌上绿色结晶为公开信息
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
    // 18-Mizuki 兵舍: 士兵的构成与是否已动员都是公开信息。
    json bq = json::array();
    for (int inst : s.barracks) {
      json e;
      e["owner"] = pi;
      e["inst"] = inst;
      e["name"] = def_of(inst).name;
      e["mobilized"] = ci(inst).faceUp;
      bq.push_back(e);
    }
    pj["barracks"] = bq;
    pj["barracksMobilized"] = barracks_mobilized_count(p);
    pj["position"] = !s.distChanged;  // 阵地：本回合距离是否未变
    // 19-Megumi 土壤 / 假想树：双方均为公开信息。
    pj["hasSoil"] = s.hasSoil;
    pj["seeds"] = s.soilSeeds;
    pj["plants"] = s.soilPlants;
    pj["treeActive"] = s.treeActive;
    json tr = json::array();
    for (int v : s.tree) tr.push_back(v);
    pj["tree"] = tr;
    // 20-Kanawe 地图 / 戏剧：双方均为公开信息（实体版地图与戏剧栏都摆在桌上）。
    pj["node"] = s.node;
    json dr = json::array();
    for (int i : s.dramas) {
      json e;
      e["inst"] = i;
      e["slot"] = def_of(i).dramaSlot;
      e["name"] = def_of(i).name;
      e["tag"] = load_int(i, "tag", 0);            // 0 未完成 / 1 戏剧栏 / 2 已完成
      e["progress"] = load_int(i, "progress", 0);
      e["tier"] = load_int(i, "tier", 0);
      e["prepared"] = (i == s.dramaPrepared);
      dr.push_back(e);
    }
    pj["dramas"] = dr;
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
