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
enum class Zone { Deck, Hand, Discard, Cover, Enhance, Special, Parts, Sealed, Bag, Removed, Limbo,
                  Memory };  // Memory = 回忆区（八叶，扣置且对对手保密）

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
  AF_NoSpecialResponse = 1u << 4,   // "切牌不可对"：只能被常规牌对应
  AF_NoNormalResponse = 1u << 5,    // "通常牌不可对"：只能被切牌对应
  AF_NoEnhanceResponse = 1u << 6,   // "付与牌不可对"
  AF_NoAttackResponse = 1u << 7,    // "攻击牌不可对"
  AF_NoActionResponse = 1u << 8,    // "行动牌不可对"
  AF_NoNegate = 1u << 9,            // "不可打消"：可被对应但不会被 打消
  AF_ToDistance = 1u << 10,         // 倒车：本应进入气/虚的伤害结晶改为进入距
  AF_PreventResponse = 1u << 11,    // 晓：防止此次对应（对应牌照付费用但无效）
};

// 机巧 colors (攻击=红, 行动=蓝, 付与=绿, 对应=紫, 全力=黄).
enum Color : uint32_t {
  COL_RED = 1u << 0,
  COL_BLUE = 1u << 1,
  COL_GREEN = 1u << 2,
  COL_PURPLE = 1u << 3,
  COL_YELLOW = 1u << 4,
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
  // 23-Akina O-N5 算法: 本回合内所有攻击获得「距离扩大（近1）」与「距离缩小（远1）」。
  // 按 rules/01-yurina.md 的说明，距离是整数的集合：扩大（近1）= 加入 min-1；
  // 缩小（远1）= 去掉 max。因此顺序为先扩大近端、再缩小远端：
  //   区间 [lo,hi] -> [lo-1,hi-1]；离散值 v -> [v-1,v-1]；1,3,5 -> 0,1,3。
  void algorithm_shift() {
    extend_near(1);
    shrink_far(1);
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
  bool wound = false;  // 24-Shisui 裂伤攻击: 【{X/Y}】X/Y are wound markers, not damage
};

// 24-Shisui 裂伤 (wound markers): the three areas a marker can sit in. Wound
// markers do not occupy a slot and are tracked per (area, causing player).
enum WoundArea : int { kWoundAura = 0, kWoundFlare = 1, kWoundLife = 2 };

// Waku (26-Innealra 惑) is another per-player crystal zone holding 樱花结晶.
enum class AreaKind { Life, Aura, Flare, Distance, Dust, Card, Market, Waku };

// Reference to a crystal container. Field areas (Distance/Dust) ignore p; Card
// areas refer to the crystals sitting on a specific enhancement instance.
// Market (23-Akina 股市) is a per-player crystal zone holding 樱花结晶.
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
  static AreaRef market(Player p) { return {AreaKind::Market, p, -1}; }
  static AreaRef waku(Player p) { return {AreaKind::Waku, p, -1}; }  // 26-Innealra 惑
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
