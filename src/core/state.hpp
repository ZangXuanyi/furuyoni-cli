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
  bool isPart = false;             // Oboro 零件（不进牌山/构筑）
  bool corePart = false;           // 核心零件 (MP) vs 附加零件 (CP)
  bool electronic = false;         // 具有电子设置关键字
  bool setupCard = false;          // 具有设置关键字
  bool isExtra = false;            // 追加牌 (EX)，不进构筑
  bool centrifugal = false;        // 离心
  bool zenkai = false;             // 全开（可作本回合唯一动作打出）
  bool isPoison = false;           // 毒牌
  bool playableFromCover = false;  // 回收利用：可从盖牌区如手牌使用
  int burnRequire = 0;             // 燃烧X：引擎不足 X 时不能打出
  bool isTransform = false;        // 变形光环牌（不进构筑）
  int distanceMod = 0;             // 展开中的持续距离修正（蹑足 -2）
  int copies = 1;                  // number of instances to create (灭灯毒 x2)
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
  bool assembled = false;  // for Parts: assembled (hidden from the opponent)
  bool usedThisTurn = false;  // 切牌: used this turn (大重力·无限 再起)
  int crystals = 0;        // crystals sitting on an enhancement / card area
  int sealedBy = -1;       // for Sealed: the host card it is sealed under
  int bagOwner = -1;       // for Poison: which player's 毒袋 it returns to
  std::vector<int> sealed; // cards sealed under this card
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
  bool usedLastCrystal = false;  // 最后的结晶：整局一次的死亡复活
  bool umbrella = true;          // Yukihi 变貌: true=伞, false=簪
  bool yukihi = false;           // has the 变貌 mechanic
  int strategy = 0;              // Shinra 策略: 0=神算, 1=鬼谋
  bool strategyKnown = true;     // whether the opponent knows the current 策略
  bool cannotAdvance = false;    // 遁术: cannot use 前进 this turn
  bool thallya = false;          // has the 蒸汽引擎
  int steamEngine = 0;           // 引擎模块
  int steamExhausted = 0;        // 用尽模块
  int steamOnDist = 0;           // 蒸汽放在距上 (距离 +1 each)
  int steamOnCrystal = 0;        // 蒸汽放在距的结晶上 (距离 -1 each)
  int transformDef = -1;         // current 变形 aura def, -1 none
  int transformCount = 0;        // 本局变形次数
  bool raira = false;            // has 风神/雷神
  int wind = 0;                  // 风神槽 (0..20)
  int thunder = 0;               // 雷神槽 (0..20)
  bool rairaGainRestricted = false;  // A1-S3 使用后：非雷螺牌不再加槽
  bool cutCostPermanent = false;     // 缠回 使用后：所有切牌费用 -1
  std::vector<int> parts;        // Oboro parts instances (Zone::Parts)
  std::vector<int> bag;          // Chikage 毒袋 instances (Zone::Bag)
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
