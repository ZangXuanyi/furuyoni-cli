#pragma once
#include <cstdint>
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
  bool unsealable = false;         // 不可封印（炼成攻击）
  bool noOpponentPick = false;     // 不可被对手选择（炼成攻击）
  int enemyNagiMod = 0;            // 虚伪: 对手新展开的付与牌纳 -1
  bool damageImmune = false;       // 夙愿: 你不会受到任何伤害
  bool decayToOwnerAura = false;   // 漫天的花道: 结晶离场改为进持有者的装（满则进气）
  bool absorbAuraBasic = false;    // 双掌生花: 装附的结晶可改为放到此牌上
  bool keepCrystalsOnReset = false;  // 熠熠见繁樱: 再起时保留牌上的结晶
  bool iceAsArmor = false;         // 冰凌包覆: 自己装中的冰晶视作装
  bool maySkipCrystalLoss = false; // 寒冰荆棘: 回合开始时可选择不移除本牌结晶
  bool enemyNoFlare = false;       // 冻僵: 展开中对手不能聚气
  bool dynamicNagi = false;        // 纳 由 Lua 函数动态给出（残烛式）
  bool soloSpecials = false;       // 八叶 A1/AA1: 该形态的切札线整体取代 O 的切札线
  std::string upgrade;             // 完全态: 升级后的牌名（八叶）
  bool reverseMoves = false;       // 映界: 你所有要移动樱花结晶的牌都可以反向执行
  bool complete = false;           // 八叶: 这张牌是完全态（升级版）
  // 「限制距离X-Y」= 打出这张牌时当前距必须 ∈ [X,Y]（类似攻击牌的距离限制）。
  // 八叶镜陨茕樱 0-7 / 空之翼 0-3 / 乱拨 0-3 / 旌旗护身 0-4 / 正解 0-7。
  int limitDistanceLo = -1;
  int limitDistanceHi = -1;
  bool responseOnly = false;       // 格杀: 仅限对应打出
  bool denyEnemyAura = false;      // 血飞沫: 进入敌装的结晶改为进虚
  bool protectsEnemy = false;      // 阡: 本牌弃置前对手不会死亡
  bool memoryDraw = false;         // 此目所及之物与世: 可不抽牌而取回忆区
  bool memoryRebuildShield = false;// 此目所及之物与世: 重铸命伤可改为移除回忆区一张牌
  bool compass = false;            // 罗盘: 我方攻击距离追加离散值 5 / 对手攻击删除离散值 5
  bool keepCrystalsUnlessTailwind = false;  // 弄潮: 仅自己回合且顺风时才能移除本牌上的结晶
  bool interceptNonAttack = false; // 子午灯塔: 对手回合内对手从手牌使用非攻击牌时改为弃置本牌
  int distanceMod = 0;             // 展开中的持续距离修正（蹑足 -2）
  int nearDistanceMod = 0;         // 达人距离修正（圈域 +1 / 引力场 -1）
  int copies = 1;                  // number of instances to create (灭灯毒 x2)
  std::string decayTo = "dust";    // where removed 献 goes (圈域 -> "distance")
  bool soldier = false;            // 18-Mizuki 士兵牌（开局置于兵舍，不进构筑池）
  bool terminalRewrite = false;    // 18-Mizuki O-S4: 改写 终端/全力 词条的光环
  // ---- 20-Kanawe: 地图 / 戏剧 ----------------------------------------------
  bool isDrama = false;            // 戏剧牌（不进构筑，开局放入戏剧区）
  int dramaSlot = 0;               // 1..6 = O-T1..O-T6
  bool cutBan = false;             // 封杀: 展开中禁止对手使用同名切牌
  // ---- 19-Megumi: 耕种 / 假想树 ------------------------------------------
  int growth = -1;                 // 生长X（-1 = 无该词条）
  bool greenDistance = false;      // 芦苇: 有效距离 + 本牌上的绿色结晶数
  bool crystalImmune = false;      // 终结之果实: 本牌结晶不可被其它手段移除
  bool redirectCrystals = false;   // 终结之果实: 其它付与牌要移除的结晶改为移到此牌
  bool keepCrystalsOnOppTurn = false;  // 蔷薇: 对手回合内不能移除本牌上的结晶
  bool triggerFromRemoved = false;     // 假想树的牌移出游戏后其触发器仍生效
  // ---- 22-Renri 夜山恋离: 伪证 / 回归 / 史前遗物 ----------------------------
  bool bluff = false;               // 伪证牌: 可以被声称的牌名（构陷/夸口/…）
  bool relic = false;               // 史前遗物（谎言的武器 / 刀刃的本质 / 最初的樱花）
  bool regression = false;          // 回归: 对手质疑失败时可以移出游戏
  bool kaoguReturn = false;         // 考古: 回归时从游戏外回到弃牌堆
  bool rebuildClaim = false;        // 谎言的武器: 重铸牌库时可宣称盖牌区一张背面牌为它并如设置打出
  bool startUsed = false;           // 游戏开始时设为使用后状态（道化的觉悟）
  bool enemyImpatienceUp = false;   // 对手受到的焦躁伤害变为 2/1（道化的觉悟）
  bool enemyCrystalImmune = false;  // 对手不能移动这张牌上的樱花结晶（终幕）
  int nagiFromLife = 0;             // 纳支付中至少要有 N 个结晶来自持有者的命
  // ---- 25-Misora 观空: 瞄准点 / 追踪 ----
  bool distanceIsAim = false;    // 蔽目重云: 展开中当前距离变为持有者的瞄准点
  bool noAdvanceEscape = false;  // 蔽目重云: 展开中持有者不能前进或离脱
  bool noReuse = false;          // 观空穹仪: 不能被其它牌的效果再次发动
  // ---- 24-Shisui 桑畑志水: 裂伤费用 / 埋骨地 ----
  int woundCost = -1;            // 切札费用 {X}: 向自气放置 X 个裂伤指示物（-1 = 无）
  bool noDeath = false;          // 埋骨地: 展开中持有者的命为 0 也不会死亡
  // ---- 23-Akina 源上安琪娜: 资本 / 股价 / 投资 ----
  bool investmentTicket = false; // 投资券：可被「投资」翻至背面向上（恫吓/直接金融/正解）
  bool stockCost = false;        // 切札费用 = 当前股价，且不能被任何费用修正改变
  bool reuseWhileAhead = false;  // 差列递归: 资本 > 对手时必须再使用一次（照常付费）
  bool crystalShield = false;    // 仙霄鬼泉: 本牌结晶不能被本牌以外的任何方式移除
  bool cashSubstitute = false;   // 正解「使用后」: 每回合开始可用 1自装到自气 替代套现
  // ---- 26-Innealra 诺伦: 三把枪 / 命运槽 / 惑 / 纠葛 ---------------------------
  // 共有牌（挥枪/雨露霜雪/变迁/万劫缠迫）用 forms 列出适用的形态；
  // form 仍是其基准形态（决定 pack/构筑显示）。
  std::vector<std::string> forms;
  bool isFate = false;           // 命运（隐藏 CardDef，不进构筑/不实例化）
  int fateSlot = 0;              // 开局放入的命运槽（0 过去 / 1 现在 / 2 未来 / 3 待启）
  bool fateEntangler = false;    // 万劫缠迫：展开中纠葛持有者的所有命运
  bool suppressEnemyAttackMods = false;  // 阴郁·埋葬: 对手的攻击不受攻击修正
  bool nagiFromDistance = false; // 舍弃·希冀: 这张牌的献可以从距中选择
  bool rebuildFreeze = false;    // 栖身·垂暮: 对手下一次重铸时弃牌堆不移动
  bool fragileWill = false;      // 脆弱意志: 对手装附外的装获得改为进此牌；基本装附则移除此牌1结晶
  std::string text;
};

// A concrete copy of a card inside the match.
struct CardInstance {
  int inst = -1;
  int def = -1;
  Player owner = P0;   // true owner: where the card goes once it leaves the field
  Player holder = P0;  // whose zone lists currently contain it (== owner unless borrowed)
  Zone zone = Zone::Deck;
  bool faceUp = true;      // for Special: false == unused (face down), true == used
  bool assembled = false;  // for Parts: assembled (hidden from the opponent)
  bool usedThisTurn = false;  // 切牌: used this turn (大重力·无限 再起)
  int crystals = 0;        // crystals sitting on an enhancement / card area
  int green = 0;           // 19-Megumi: 绿色结晶（视作樱花结晶，但移除时优先移除樱花）
  int sealedBy = -1;       // for Sealed: the host card it is sealed under
  int bagOwner = -1;       // for Poison: which player's 毒袋 it returns to
  std::vector<int> sealed; // cards sealed under this card
  bool soldier = false;    // 18-Mizuki: a 兵舍 soldier (faceUp == 已动员)
};

struct PlayerState {
  uint32_t mech = 0;  // 机制能力位（engine.hpp 的 MechanicBit；setup 时置位）
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
  std::vector<int> memory;       // 八叶 回忆区 (Zone::Memory, face down / hidden)
  int cardsPlayedTotal = 0;      // 本局打出的牌数（万叶仍未识）
  int curse = 0;                 // 神居 诅咒（>= 16 即死亡）
  bool hasCurse = false;         // 这局使用了神居（才会在回合开始累积诅咒）
  std::string extraAttackCostGoddess;  // 尸: 本回合下次攻击需额外弃一张该女神的牌
  bool nextDrawOne = false;  // 夜叉: the next start phase draws only one card
  int ice = 0;                  // 冻结: ice crystals occupying 装 slots (not 装)
  int tempDistanceMod = 0;      // 影飞翅: this turn only (effective distance)
  int tempNearDistanceMod = 0;  // 影飞翅: this turn only (达人距离)
  bool skipMainPhase = false;   // 踽踽虚路行: lose the next main phase
  bool suppressAuraRedirect = false;  // 双掌生花 打出时的那次装附不替换
  bool firstTurnDone = false;
  // ---- 18-Mizuki: 兵舍 / 阵地 / 对应计数 -------------------------------------
  std::vector<int> barracks;           // 士兵实例（兵舍；zone==Limbo；faceUp==已动员）
  bool distChanged = false;            // 本回合内有效距离是否变化过（阵地）
  bool respondedThisTurn = false;      // 本回合进行过对应
  bool respondedLastTurn = false;      // 上一回合进行过对应
  int attackCardsPlayedThisTurn = 0;   // 本回合打出的攻击牌数
  int normalAttacksThisTurn = 0;       // 本回合宣告的通常牌攻击数
  int responsesPlayedThisTurn = 0;     // 本回合打出的对应牌数
  // ---- 17-Hastumi: 航海 / 潜水 ---------------------------------------------
  bool tailwind = true;          // 航海: 本回合顺风（回合开始时判定）
  bool forcedTailwind = false;   // 潜水闪避: 下回合固定顺风
  bool oppAttackedLastTurn = false;  // 航海: 上一回合对手进行过攻击
  int dive = 0;                  // 潜水: 0=无, 1=前进, 2=后退（对对手保密）
  // ---- 19-Megumi: 耕种 / 土壤 / 假想树 --------------------------------------
  bool hasSoil = false;          // 这局使用了泷河希（启用土壤机制）
  int soilSeeds = 0;             // 土壤「种子」（开局 5）
  int soilPlants = 0;            // 土壤「植株」
  std::vector<int> tree;         // 假想树 6 格（0/1 占用）: 1 格 + 2 格 + 3 格
  bool treeActive = false;       // 假想树已加入游戏
  int nextGrowth = 0;            // 脱粒: 本回合下一张非希付与牌获得的生长X
  // ---- 20-Kanawe: 地图 / 戏剧 ----------------------------------------------
  std::string node = "O2";       // 当前地图节点
  std::vector<int> dramas;       // 6 张戏剧实例（O-T1..O-T6，zone==Removed）
  int dramaPrepared = -1;        // 戏剧栏中的实例（-1 = 空）
  bool dramaProgressedThisTurn = false;  // 本回合内推进过戏剧
  bool dramaProgressedLastTurn = false;  // 上一回合内推进过戏剧（疾书弗尽）
  bool noDramaThisTurn = false;          // 演出: 本回合不能完成戏剧
  // ---- 22-Renri 夜山恋离: 伪证 ---------------------------------------------
  bool doubtFailedThisTurn = false;      // 这个玩家本回合质疑失败过
  // ---- 25-Misora 观空: 瞄准点 ----------------------------------------------
  int aim = -1;              // 瞄准点（回合结束时记录的当前距；-1 = 不存在）
  // ---- 24-Shisui 桑畑志水: 裂伤 ---------------------------------------------
  // 裂伤指示物不占据位置: wound[area][source] = 该区域中由 source 造成的裂伤数。
  // area: 0=装 / 1=气 / 2=命（见 WoundArea）。
  int wound[3][2] = {{0, 0}, {0, 0}, {0, 0}};
  int damageTakenThisTurn = 0;  // 本回合内受到伤害的次数（含裂伤伤害化）
  // ---- 23-Akina 源上安琪娜: 股市 / 股价 / 本回合结算标记 -----------------------
  int market = 0;                // 股市（樱花结晶，公开信息；仅安琪娜玩家有效）
  int stockPrice = 2;            // 股价：初始 2，取值域 [1,4]
  bool algorithmThisTurn = false;  // 算法：本回合内所有攻击 lo-1 / hi-1
  bool cashOutThisTurn = false;    // 本回合内是否套现过（回合结束投资的条件）
  // ---- 26-Innealra 诺伦: 惑 / 命运槽 / 本回合计数 ----------------------------
  int waku = 0;                  // 惑（樱花结晶，公开信息；仅诺伦玩家使用）
  int fate[4] = {-1, -1, -1, -1};  // 命运槽: 0 过去 / 1 现在 / 2 未来 / 3 待启（存 def id）
  bool fatesEntangled = false;   // 纠葛（万劫缠迫展开中）
  int resonanceCountThisTurn = 0;      // 本回合共鸣次数
  bool usedNonInnealraThisTurn = false;  // 本回合使用过非诺伦的牌
  int usedNormalThisTurn = 0;          // 本回合使用的通常牌数
  bool cannotUseNormals = false;       // 修省（纠葛）: 本回合不能使用通常牌
  bool cannotRetreat = false;          // 悔恨: 本回合不能后退
  bool nextRebuildFreeze = false;      // 栖身·垂暮: 下一次重铸时弃牌堆不移动
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
