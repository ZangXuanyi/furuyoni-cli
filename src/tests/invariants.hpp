#pragma once
// Structural invariants of a live Engine state. These are checked at decision
// points (i.e. whenever the engine is quiescent and waiting for an agent), so a
// violation means the engine itself produced an inconsistent state.
#include <algorithm>
#include <string>
#include <vector>

#include "engine/engine.hpp"
#include "tests/test_util.hpp"

namespace fy {
namespace test {

inline const char* zone_name(Zone z) {
  switch (z) {
    case Zone::Deck: return "deck";
    case Zone::Hand: return "hand";
    case Zone::Discard: return "discard";
    case Zone::Cover: return "cover";
    case Zone::Enhance: return "enhance";
    case Zone::Special: return "special";
    case Zone::Parts: return "parts";
    case Zone::Sealed: return "sealed";
    case Zone::Bag: return "bag";
    case Zone::Removed: return "removed";
    case Zone::Limbo: return "limbo";
  }
  return "?";
}

// Zones that are backed by a PlayerState vector; the others are "unlisted".
inline bool listed_zone(Zone z) {
  switch (z) {
    case Zone::Deck:
    case Zone::Hand:
    case Zone::Discard:
    case Zone::Cover:
    case Zone::Enhance:
    case Zone::Special:
    case Zone::Parts:
    case Zone::Bag: return true;
    default: return false;
  }
}

// Returns every violation found (empty == healthy). Never throws and never
// indexes out of bounds itself.
inline std::vector<std::string> check_invariants(const Engine& e) {
  std::vector<std::string> out;
  const GameState& st = e.st;
  const int n = static_cast<int>(st.insts.size());
  auto add = [&](std::string m) { out.push_back(std::move(m)); };

  auto describe = [&](int inst) {
    std::string s = "inst#" + std::to_string(inst);
    if (inst >= 0 && inst < n) {
      const CardInstance& c = st.insts[static_cast<size_t>(inst)];
      if (c.def >= 0 && c.def < static_cast<int>(e.defs.size()))
        s += "(" + e.defs[static_cast<size_t>(c.def)].name + ")";
      else
        s += "(bad-def " + std::to_string(c.def) + ")";
    }
    return s;
  };

  // ---- 1. every instance sits in exactly the vector matching its zone ----
  std::vector<int> seen(static_cast<size_t>(n), 0);
  auto scan = [&](Player owner, const std::vector<int>& v, Zone expect, const char* who) {
    for (int inst : v) {
      if (inst < 0 || inst >= n) {
        add(std::string("P") + std::to_string(owner) + " " + who + ": out-of-range inst " +
            std::to_string(inst));
        continue;
      }
      const CardInstance& c = st.insts[static_cast<size_t>(inst)];
      if (c.holder != owner)
        add(std::string("P") + std::to_string(owner) + " " + who + ": " + describe(inst) +
            " held by P" + std::to_string(static_cast<int>(c.holder)));
      if (c.zone != expect)
        add(std::string("P") + std::to_string(owner) + " " + who + ": " + describe(inst) +
            " listed in " + who + " but zone=" + zone_name(c.zone));
      seen[static_cast<size_t>(inst)]++;
    }
  };
  for (int pi = 0; pi < 2; ++pi) {
    const PlayerState& s = st.p[pi];
    scan(static_cast<Player>(pi), s.deck, Zone::Deck, "deck");
    scan(static_cast<Player>(pi), s.hand, Zone::Hand, "hand");
    scan(static_cast<Player>(pi), s.discard, Zone::Discard, "discard");
    scan(static_cast<Player>(pi), s.cover, Zone::Cover, "cover");
    scan(static_cast<Player>(pi), s.enhance, Zone::Enhance, "enhance");
    scan(static_cast<Player>(pi), s.special, Zone::Special, "special");
    scan(static_cast<Player>(pi), s.parts, Zone::Parts, "parts");
    scan(static_cast<Player>(pi), s.bag, Zone::Bag, "bag");
  }
  for (int inst = 0; inst < n; ++inst) {
    const CardInstance& c = st.insts[static_cast<size_t>(inst)];
    int k = seen[static_cast<size_t>(inst)];
    if (k > 1) add(describe(inst) + " appears in " + std::to_string(k) + " zone lists");
    if (listed_zone(c.zone)) {
      if (k == 0) add(describe(inst) + " has zone=" + zone_name(c.zone) + " but no zone list");
    } else {
      // Sealed instances live under their host; every other unlisted zone must
      // not appear in any list.
      if (k != 0) add(describe(inst) + " has zone=" + zone_name(c.zone) + " yet is listed");
      if (c.zone == Zone::Sealed && c.sealedBy < 0) add(describe(inst) + " sealed without a host");
    }
  }

  // ---- 2. numeric bounds ----
  for (int pi = 0; pi < 2; ++pi) {
    Player p = static_cast<Player>(pi);
    const PlayerState& s = st.p[pi];
    if (s.life < 0 || s.life > st.maxLife)
      add("P" + std::to_string(pi) + " life out of range: " + std::to_string(s.life));
    int ma = e.max_aura(p);
    if (s.aura < 0 || s.aura > ma)
      add("P" + std::to_string(pi) + " aura out of range: " + std::to_string(s.aura) + "/" +
          std::to_string(ma));
    if (s.flare < 0) add("P" + std::to_string(pi) + " negative flare");
    if (s.ice < 0 || s.aura + s.ice > ma)
      add("P" + std::to_string(pi) + " ice out of range: ice=" + std::to_string(s.ice) +
          " aura=" + std::to_string(s.aura) + "/" + std::to_string(ma));
    if (s.vigor < 0 || s.vigor > 2)
      add("P" + std::to_string(pi) + " vigor out of range: " + std::to_string(s.vigor));
    if (s.wind < 0 || s.wind > 20)
      add("P" + std::to_string(pi) + " wind out of range: " + std::to_string(s.wind));
    if (s.thunder < 0 || s.thunder > 20)
      add("P" + std::to_string(pi) + " thunder out of range: " + std::to_string(s.thunder));
    if (s.steamEngine < 0 || s.steamExhausted < 0 || s.steamOnDist < 0 || s.steamOnCrystal < 0)
      add("P" + std::to_string(pi) + " negative steam counter");
    if (s.handLimit < 0) add("P" + std::to_string(pi) + " negative hand limit");
  }
  if (st.distance < 0 || st.dust < 0)
    add("negative distance/dust: " + std::to_string(st.distance) + "/" + std::to_string(st.dust));
  if (e.distance() < 0) add("negative effective distance");

  for (int inst = 0; inst < n; ++inst) {
    const CardInstance& c = st.insts[static_cast<size_t>(inst)];
    if (c.def < 0 || c.def >= static_cast<int>(e.defs.size()))
      add(describe(inst) + " has no card definition");
    if (c.crystals < 0) add(describe(inst) + " negative crystals");
    if (c.zone == Zone::Deck || c.zone == Zone::Hand)
      if (e.defs[static_cast<size_t>(c.def)].kind == CardKind::Special)
        add(describe(inst) + " is a 切札 sitting in " + zone_name(c.zone));
    if (c.zone == Zone::Cover && c.faceUp)
      add(describe(inst) + " is face up in the cover pile");
  }

  // ---- 3. enhancement sanity ----
  for (int pi = 0; pi < 2; ++pi) {
    for (int inst : st.p[pi].enhance) {
      if (inst < 0 || inst >= n) continue;
      const CardDef& d = e.defs[static_cast<size_t>(st.insts[static_cast<size_t>(inst)].def)];
      if (d.type != CardType::Enhance)
        add(describe(inst) + " is in the enhance zone but type != enhance");
    }
  }

  // ---- 4. crystal conservation ----
  long long diff = crystals_total(e) - e.external_added();
  if (diff != 36)
    add("crystal total " + std::to_string(diff) + " (expected 36, external=" +
        std::to_string(e.external_added()) + ")");

  return out;
}

}  // namespace test
}  // namespace fy
