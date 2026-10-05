#pragma once
// Core value types shared across the engine. Deliberately free of any Lua or
// JSON dependency so that the rules core stays independent of the effect host.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fy {

enum Player : int { P0 = 0, P1 = 1 };
inline Player opp(Player p) { return p == P0 ? P1 : P0; }

// Limbo == a card currently resolving, not yet in any real zone.
enum class Zone { Deck, Hand, Discard, Cover, Enhance, Special, Removed, Limbo };

enum class CardKind { Normal, Special };  // Special == 切札
enum class CardType { Attack, Action, Enhance };

// Card level flags (副类别 + 常见词条 that live on the card as a whole).
enum CardFlag : uint32_t {
  CF_FullPower = 1u << 0,  // 全力
  CF_Response = 1u << 1,   // 对应
  CF_Break = 1u << 2,      // 破绽
  CF_Terminal = 1u << 3,   // 终端
};

// Keywords attached to a particular attack instance.
enum AttackFlag : uint32_t {
  AF_Unrespondable = 1u << 0,  // 不可对
  AF_Lock = 1u << 1,           // 锁定
  AF_Overwhelm = 1u << 2,      // 超克
  AF_BothSides = 1u << 3,      // 两侧伤害：装伤与命伤同时结算
};

// A set of inclusive integer spans, e.g. {5,9} or {{1,3},{5,6}}.
struct Range {
  std::vector<std::pair<int, int>> spans;

  bool empty() const { return spans.empty(); }
  bool contains(int d) const {
    for (auto& [lo, hi] : spans)
      if (d >= lo && d <= hi) return true;
    return false;
  }
  void add(int lo, int hi) {
    if (lo > hi) std::swap(lo, hi);
    spans.push_back({lo, hi});
  }
  void extend_far(int n) {
    if (spans.empty()) return;
    auto it = std::max_element(spans.begin(), spans.end(),
                               [](auto& a, auto& b) { return a.second < b.second; });
    it->second += n;
  }
  void extend_near(int n) {
    if (spans.empty()) return;
    auto it = std::min_element(spans.begin(), spans.end(),
                               [](auto& a, auto& b) { return a.first < b.first; });
    it->first -= n;
    if (it->first < 0) it->first = 0;
  }
  void shrink_far(int n) {
    if (spans.empty()) return;
    auto it = std::max_element(spans.begin(), spans.end(),
                               [](auto& a, auto& b) { return a.second < b.second; });
    it->second -= n;
    if (it->second < it->first) spans.erase(it);
  }
  void shrink_near(int n) {
    if (spans.empty()) return;
    auto it = std::min_element(spans.begin(), spans.end(),
                               [](auto& a, auto& b) { return a.first < b.first; });
    it->first += n;
    if (it->first > it->second) spans.erase(it);
  }
  std::string to_string() const {
    std::string s;
    for (size_t i = 0; i < spans.size(); ++i) {
      if (i) s += ",";
      if (spans[i].first == spans[i].second)
        s += std::to_string(spans[i].first);
      else
        s += std::to_string(spans[i].first) + "-" + std::to_string(spans[i].second);
    }
    return s.empty() ? "-" : s;
  }
};

// X/Y damage. A missing side means "-" (that option is unavailable).
struct Damage {
  std::optional<int> aura;
  std::optional<int> life;
};

// A concrete attack definition (static approximation for CardDef / evaluated at runtime).
struct AttackSpec {
  Range range;
  Damage damage;
  uint32_t keywords = 0;
};

enum class AreaKind { Life, Aura, Flare, Distance, Dust, Card };

// Reference to a crystal container. Field areas (Distance/Dust) ignore p; Card
// areas refer to the crystals sitting on a specific enhancement instance.
struct AreaRef {
  AreaKind kind = AreaKind::Dust;
  Player p = P0;
  int inst = -1;

  static AreaRef life(Player p) { return {AreaKind::Life, p, -1}; }
  static AreaRef aura(Player p) { return {AreaKind::Aura, p, -1}; }
  static AreaRef flare(Player p) { return {AreaKind::Flare, p, -1}; }
  static AreaRef distance() { return {AreaKind::Distance, P0, -1}; }
  static AreaRef dust() { return {AreaKind::Dust, P0, -1}; }
  static AreaRef card(int inst) { return {AreaKind::Card, P0, inst}; }
};

enum class BasicAction { Advance, Retreat, Aura, Flare, Escape };

inline const char* basic_name(BasicAction a) {
  switch (a) {
    case BasicAction::Advance: return "advance";
    case BasicAction::Retreat: return "retreat";
    case BasicAction::Aura:    return "aura";
    case BasicAction::Flare:   return "flare";
    case BasicAction::Escape:  return "escape";
  }
  return "?";
}

}  // namespace fy
