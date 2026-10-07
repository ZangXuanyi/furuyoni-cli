# 付与结算顺序变更审计（重构工作文档）

**裁定**（用户 2026-10-07）：以 `what.md` 字面为准——付与打出效果结算顺序为
**种植 → 给献（纳支付）→ 展开时 → 0 献立即弃置**。
旧实现（= rulings #4）为 展开时（`on_enter`）→ 放献 → `on_expanded` → 0 献弃置。
重构后 `on_enter`/`on_expanded` 合并为单一钩子 `on_expand`，在献落位后触发。

本文件逐卡审计时点后移的语义影响。Phase 2 落地时据此更新 rulings #4 与相关测试；
Phase 5 时本文件内容并入 rulings.md，届时删除本文件。

## A 类：展开时产生的结晶在旧序下可立即支付纳（结算流向改变）

| # | 卡 | 展开时效果 | 旧序流向 | 新序流向 |
|---|---|---|---|---|
| 1 | akina 直接金融 (O-N7, 纳2) | 1敌装→自装，可付1集中力重复 | 获得的装可作献 | 装留在自装 |
| 2 | hagane 引力场 (O-N7, 纳2) | 距→自装 1（全开2） | 同上 | 同上 |
| 3 | innealra 变迁 (N7, 纳1) | 1虚→自装 | 虚→装→献（净1虚上牌） | 纳先扣虚，再1虚→装 |
| 4 | innealra 万劫缠迫 (S4, 纳5) | 盖任意手牌，每张一次基本动作 | 基本动作可先装附/聚气改变纳来源 | 纳先行 |
| 5 | konuru (A1-S2, 纳?, 对应) | 1距→虚 + 冻结 | 虚+1 后纳可用 | 纳先行，距→虚在后 |
| 6 | megumi 芦苇 (O-N5, 纳1) | 1虚→距 | 虚-1 后纳可能不足 | 纳先行 |
| 7 | yukihi 结缘 (O-N7, 纳2) | 择一：1距→虚 或 1虚→距 | 先行，影响虚余量 | 纳先行 |
| 8 | tokoyo 风舞台 (O-N6, 纳2) | 2距→自装 | 获得的装可作献 | 装留在自装 |
| 9 | utsuro 遗灰咒 (O-N7, 纳2) | 3敌装→虚 | 虚+3 后纳可用 | 纳先行 |
| 10 | shinra 森罗判证 (O-S4, 纳6) | 2虚→自命 | 虚-2 后纳可能不足 | 纳先行 |

## B 类：状态生效时点变化（新序更合理）

| # | 卡 | 说明 |
|---|---|---|
| 11 | kamuwi 阡 (O-S2, 纳4) | 展开时攻击【3-4 3/3】+自伤1命。旧序下攻击结算时本牌尚在 Limbo，`protects_enemy`（本牌弃置前对手不会死亡）未生效；新序下已生效。视为修正旧实现的缺陷。 |

## C 类：无语义变化

对应对打出的攻击修正（冲音晶、阵雨·覆逆、可能性之枝、惴息悬影、天主八龙阁、缠毒揭叛旗）、
对手侧移动（乱拨 2敌气→距、空之翼 2敌装→距、遗灰咒除外已列A）、弃牌/盖伏/封印/宣言
（哀愁意志、虚幻意志、舍弃·希冀、封杀、论破、使徒、虚鱼、快速改装）、潜水/畏缩
（水雷、汪洋航道）、冻结对手（冻僵）、集中力（晴舞台 set 2）、自命→自气（仙霄鬼泉天元术）。

## 关键不变量（新顺序必须保持）

1. **0 献弃置检查在展开时之后**：反射装置（纳0，展开时按机巧放结晶上牌）、
   冰凌包覆（纳0，展开时 5虚到牌）、蔷薇（纳0，全力）依赖展开时自救——
   献=0 判定必须读取展开时之后的最终结晶数。what.md 顺序字面即如此。
2. **破绽与展开时互动**：新序下展开时效果若致控制者命伤，破绽将移除已放的献。
   现有牌池中不存在"破绽 + 展开时可自伤"的组合（水雷仅潜水、直接金融无自伤），
   记为新裁定空档，如未来出现按此处理。
3. 重展开（reuse）时旧献先返还（decayTo 路由），再走新顺序——两序一致。

## 基线快照（Phase 0）

- 全量 ctest：unit + fuzz 通过。
- `FY_FUZZ_GAMES=600`：5 用例 / 9600 断言全绿。

## Phase 1（Token 系统统一）已接受的语义统一差异

以下差异是「绕过策略的旁路」被并入统一入口后策略首次生效所致（修复而非破坏），
均经 211 用例 + 600 局模糊 + 40 局新旧引擎对照（哈希 40/40 一致）验证不触发回归：

| # | 位置 | 旧行为 | 新行为 |
|---|---|---|---|
| D1 | `ctx:move_from_card`（renri 终幕 / honoka 新幕来临） | 直接扣 `crystals` 字段：绕过牌上免疫（crystalImmune/crystalShield）、绿晶回流、终幕的 crystalMover 守卫（守卫形同虚设） | 经 `take_card_crystals` 策略：免疫生效、绿晶按规则回流、终幕「对手不能移动此牌结晶」首次真正生效 |
| D2 | `decay_crystals` 的 漫天的花道 分支（装满溢进气） | 裸写 `aura +=`：不触发 armor_full 事件 | 经 `token_adjust`：装变满瞬间触发 armor_full（吹雪式即再起） |
| D3 | 纳支付 | 裸 `add_crystals` 扣四源 | 经 `token_move(notes=false)`：状态/事件等价；notes 静默保持旧例（《樱花》《明转》/罗织不计数，Phase 2 复审是否应计） |

**对照验证方法**：`git worktree` 构建 HEAD 旧引擎，seed 100–139 各录 40 局，新旧引擎最终
状态哈希 40/40 一致（`/tmp/fy-corpus-{old,new}`，git worktree `/tmp/fy-old`）。

## Phase 2（结算管线 + 付与新顺序）验证记录

- 单元测试 213/213（含 2 个新时点钉死测试：引力场「展开时获得的装不能再支付纳」、
  寄花「展开时可读最终献数」）；`FY_FUZZ_GAMES=600` 模糊全绿；ASan 模糊 100 局全绿。
- hajimari 语料（seed 100–139）：30/30 帧序列与结果完全一致（哈希差异仅来自摘要中
  删除的 4 个已参数化字段——伤害路由全局→DamageRoute）。
- standard 语料（seed 200–229，随机抽将）：14/30 完全一致；16 局分歧**全部**起于某张
  付与的结算，且与上表审计清单一一对应（引力场/变迁/结缘/森罗判证/直接金融为真实
  结晶流向变化；哀愁意志/封杀/仙霄/冥沼式等为提示顺序变化——随机 agent 的决策索引
  随提示序列移动）。无不相关分歧。
- 新增已接受差异 D4：`reuse_special`（诸式理解式再发动）现在也压入结算栈帧
  （原来直接调 resolve_card_effect、不进栈）；仅哈希可见，无行为差异。
- 注意：对照时各引擎二进制必须在**各自源码树目录**下运行——`content/` 按 cwd 解析，
  混用目录会产生「引擎找 on_enter、内容已改名 on_expand」的怪胎构建（排查时踩过）。
- rulings #4 已按新裁定修订（种植 → 放献 → 展开时 → 0 献弃置）。

## Phase 3 进度（机制拆分）与剩余工作清单

已落地（每步 213/213 用例 + 模糊全绿后提交）：

- 基础设施：`engine/effect_ctx.hpp`（LuaCtx/LuaAttack/LuaCost/LuaEvent + 共享解析），
  `EffectHost` 构造后统一调用 `run_mechanic_ctx_blocks`（mechanics/blocks.cpp 汇总）。
- 能力注册表：`MechanicBit`/`mechanic_bits()`/`PlayerState.mech`（抽将与固定牌组两条
  setup 路径都置位），`has_misora/has_shisui/has_akina/has_innealra` 改查能力位。
- Wave 1：mechanics/steam.cpp（11）、ice.cpp（15）、wound.cpp（24）。
- Wave 2：mechanics/market.cpp（23）。

剩余（按 Wave 1/2 模式继续，纯代码搬移 + blocks.cpp 注册一行 + CMake 一行）：

1. mechanics/soil.cpp（19 种子/植株/假想树，engine.cpp 土壤段 + ctx 块 ~300 行）
2. mechanics/fate.cpp（26 命运槽/惑/共鸣，engine.cpp 2675-2801 段 + Innealra ctx 块）
3. mechanics/drama.cpp（20 地图/戏剧，engine.cpp 3085-3451 段 + ctx 块；顺手把节点表
   与六个戏剧条件改为数据表）
4. mechanics/curse.cpp（21）、dive.cpp（17）、barracks.cpp（18）、poison.cpp（09）、
   keiryo.cpp（07）、slots.cpp（12 风雷）、parts.cpp（05）、memory.cpp（16 镜映/回忆）
5. PlayerState/CardDef 重组为通用字段 + 每机制子结构；state_hash/observation/
   full_state_json 改为按机制注册的结构化 visitor（消灭四处手同步）
6. card_names.hpp 消解：引擎侧 `has_named_active` 检查逐个改为 CardDef 能力位
   （由 Lua 数据声明），仅保留测试用名字清单
7. Phase 4（Lua API v2）：self_boost/choose_move/responded_is_plain/span 工具上收，
  删除 set_vigor/set_flare 等裸写入器，ctx 分层（查询/决策/行动），26 模块迁移

验证方法备忘：新旧引擎对照必须**在各源码树目录下运行**（content 按 cwd 解析）；
hajimari 语料应 30/30 帧一致，standard 语料分歧应全部起于付与结算（Phase 2 审计表）。
