#pragma once
// 统一的「区域 × 异樱」模型（what.md 第 1 条）。
//
// 所有可移动的游戏对象——樱花结晶与异樱（蒸汽/冰晶/种子/植株/绿晶/裂伤）——
// 都以 (AreaRef, Token) 寻址，并经 Engine::token_move 这唯一入口移动。
// 存储仍是 GameState 上的既有字段（Phase 3 再重组存储布局），本层统一的是
// 地址、策略（容量/重定向/守恒/事件）与命名操作。
//
// 实现见 src/engine/tokens.cpp。
#include "core/types.hpp"

namespace fy {

// 异樱与樱花结晶的种类。Sakura 是通用结晶；其余按女神机制各归其位：
//   Green  绿色结晶（19-Megumi）：在牌上时视作樱花结晶，离牌回流
//   Seed   种子（19-Megumi 土壤）
//   Plant  植株（19-Megumi 土壤）
//   Steam  蒸汽（11-Thallya：引擎/用尽/距上）
//   Ice    冰晶（15-Konuru：占据装位、不承伤）
//   Wound  裂伤（24-Shisui：不占位，按施加者记账）
enum class Token : unsigned char {
  Sakura, Green, Seed, Plant, Steam, Ice, Wound,
};

// token_move 的请求。fromKind/toKind 允许不同（异樱在区域间流转时变形，
// 例如 种子→植株、植株→牌上绿晶、牌上绿晶→种子）。
//   cardEffect=false 表示基本动作等非牌效移动（迷烟只拦截牌效移距）。
//   notes=false 表示不计入《樱花》《明转》/罗织（纳支付沿用旧例，Phase 2 复审）。
//   takeMode 仅当 from 为 Card 时有意义：牌上结晶的移除语境（豁免判定用）。
//   cause 仅在 armor_full 事件中作为来源牌实例携带（冻结）。
struct MoveReq {
  AreaRef from, to;
  Token fromKind = Token::Sakura;
  Token toKind = Token::Sakura;
  int n = 1;
  bool cardEffect = true;
  bool notes = true;
  int takeMode = 0;   // Engine::CrystalTakeMode；为避免头文件循环用 int
  int cause = -1;     // armor_full 事件的来源牌
};

namespace area {

inline bool shared(AreaKind k) {
  return k == AreaKind::Distance || k == AreaKind::Dust || k == AreaKind::External;
}
inline bool steam(AreaKind k) {
  return k == AreaKind::SteamEngine || k == AreaKind::SteamExhausted ||
         k == AreaKind::SteamOnDist || k == AreaKind::SteamOnCrystal;
}
// 裂伤可以落座的三类区域（与 WoundArea 下标一致）。
inline bool wound_area(AreaKind k) {
  return k == AreaKind::Aura || k == AreaKind::Flare || k == AreaKind::Life;
}
inline int wound_index(AreaKind k) {
  return k == AreaKind::Aura ? kWoundAura : k == AreaKind::Flare ? kWoundFlare : kWoundLife;
}

// 观测/Lua 使用的区域名（"life"/"aura"/...；见 tokens.cpp 的 area_from_name）。
const char* name(AreaKind k);

}  // namespace area

}  // namespace fy
