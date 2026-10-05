#pragma once
#include <string>
#include <vector>

#include "core/rng.hpp"
#include "core/types.hpp"

namespace fy {

// A card as defined by content. Immutable during a match.
struct CardDef {
  int id = -1;
  std::string set;         // deck-set id, e.g. "yurina" (O) or "yurina.A1"
  std::string goddess;     // goddess id, e.g. "yurina" (same across forms)
  std::string form = "O";  // "O" / "A1" / "A2"
  int local = 0;           // index within the set (used for variant replacement)
  std::string name;
  CardKind kind = CardKind::Normal;
  CardType type = CardType::Action;
  uint32_t flags = 0;      // CF_*
  int cost = -1;           // 切札 耗能X, -1 if none (may be dynamic via Lua)
  int nagi = -1;           // 纳X, -1 if none
  bool hasAttack = false;
  AttackSpec attack;       // static approximation (dynamic parts resolved via Lua at play time)
  std::vector<std::string> goddesses;  // 1 entry, or 2 for dual-goddess cards (合奏)
  bool armorFromCrystals = false;  // 结算伤害时卡上结晶视作装
  bool lockDistance = false;       // 牌效改变距无效 (迷烟)
  int auraMax = -1;                // 持有者装上限 (徒寄之八重樱 -> 8)
  std::string decayTo = "dust";    // where removed 献 goes (圈域 -> "distance")
  std::string text;
};

// A concrete copy of a card inside the match.
struct CardInstance {
  int inst = -1;
  int def = -1;
  Player owner = P0;
  Zone zone = Zone::Deck;
  bool faceUp = true;      // for Special: false == unused (face down), true == used
  int crystals = 0;        // crystals sitting on an enhancement / card area
};

struct PlayerState {
  int life = 10;
  int aura = 3;
  int flare = 0;
  int vigor = 0;
  bool cower = false;        // 畏缩
  bool cannotRespond = false;  // set by a 终端 played during the opponent's turn
  int lastLifeLost = 0;      // size of the most recent single life-damage instance
  int cardsPlayedThisTurn = 0;  // for 连射
  int handLimit = 2;         // 盖伏阶段保留上限 (神座渡 +X, only this turn)
  int cutCostDelta = 0;      // 本回合切札费用修正 (伴奏 -1)
  bool cannotAttack = false; // 本回合不能攻击 (二重奏)
  bool cannotBasic = false;  // 本回合不能执行基本动作 (吹弹阳明)
  bool firstTurnDone = false;
  std::vector<int> deck, hand, discard, cover, enhance, special;
};

struct GameState {
  PlayerState p[2];
  int distance = 10;
  int dust = 0;
  int nearDistance = 2;
  int maxLife = 10;
  int maxAura = 5;
  int turn = 1;
  Player active = P0;
  bool over = false;
  int winner = -1;  // 0/1, or -1 for a draw
  std::vector<CardInstance> insts;
  Rng rng;
};

}  // namespace fy
