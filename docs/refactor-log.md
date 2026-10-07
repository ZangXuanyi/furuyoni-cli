# 2026-10 大重构日志（按 what.md）

权威顺序：what.md > docs/rulings.md > 代码现状；一切破坏性变更允许，全部记录于下。

## 破坏性变更总表（旧 replay/状态哈希一律失效）

| 变更 | 说明 |
|---|---|
| **付与结算顺序** | 打出付与：种植 → 给献（纳支付）→ **展开时** → 0 献弃置。展开时在献落位后触发、可读最终献数；旧序「展开时→放献」废止（裁定修订，见 rulings #4） |
| **Lua 钩子 `on_expand`** | `on_enter` 与 `on_expanded` 合并为单一钩子 `on_expand`（26 个内容模块全部迁移） |
| **状态哈希算法** | 摘要内容随内部重构变化（DamageRoute 参数化、能力位 mech、dramaMarked 等）；旧 replay 不可回放 |
| **公开/检视统一** | `ctx:reveal_cards(viewer, owner, zone, prompt?, mn?, mx?, filter?)` 取代 `ctx:reveal_hand`（空操作，已删）与 `ctx:reveal_opponent_cuts`（持久公开，语义错误已删——切牌列表可被 20-O-S4 扩充）。瞬时信息：纯公开走零选择 `reveal` 请求；协议新增 `reveal` 种类 |
| **死亡判定优先级** | 特胜特败 → 赖着不死 → 复活；诅咒特败与戏剧特胜无视复活（`dramaMarked_`） |
| **Lua API v2** | 删除裸写入器 `set_vigor`/`set_flare`（→ `cost_vigor`/`vigor_to`/`flare_to`）；新增 `self_boost`/`reveal_cards`/`aura_damage`/`life_damage`；旧结晶入口 `move_crystals`/`add_crystals`/`amount` 标 [[deprecated]] |
| **内部架构** | tokens.cpp（结晶/异樱唯一入口）、pipeline.cpp（命名阶段 + PlayFrame）、mechanics/（16 机制各归其文件）、effect_ctx.hpp（绑定共享层）、MechanicBit 能力注册表取代女神名前缀匹配 |

## 交付验收（2026-10-07，用户七项难点全部通过）

按用户给定的正确结果写的专项测试（engine_tests「难点*」），其中三处发现并修复：

1. **谎言的武器的重铸宣称缺质疑流程**（原注释自认"对手无从质疑"）——补全完整伪证
   流程：未质疑按声称结算；质疑且牌真是武器 → 对手焦躁+正常使用+回归（移出游戏、
   置回考古）；质疑且牌不是 → 宣称作罢。电子设置替换整次重铸、不进入宣称（原正确）。
2. **最终搜寻选"对手的"时对手自肥**——add_unused_cuts 拆为 (gainer, poolOwner)，
   对手的未选用切牌归使用者；Lua 绑定接受可选第二参。
3. **一闪的决死强化烘进声明值**——改为 self_boost 差值（声明期锁定），从而可被
   阴郁·埋葬（对手的攻击不受攻击修正）无效化，符合裁定。

已验证为正确的既有行为：引用不能选择炼成攻击（no_opponent_pick 过滤）；
久远之花不能打消晓且晓变 5/3（AF_PreventResponse 王牌 -1/-1）；此心所念之神与魂
非对应、可打消晓（attack_declared 触发器 + negate）。

测试侧发现（非引擎问题）：空决策会被 sanitize 判非法（容忍度 1 即判负）——
测试 agent 必须兜底返回合法选择；FindOptionAgent 已加兜底。

## 交付状态（2026-10-07 收官）

Phase 1（Token 系统）、Phase 2（结算管线+付与新顺序）、Phase 3（16 机制拆分+
能力注册表+弃用清理）、公开/检视统一、死亡优先级、Lua v2（裸写入器全清+惯用形
上收+内容 API 审计）均已落地。基线：223/223 用例（含死亡矩阵/付与顺序/reveal/
API 审计钉死测试）+ 600 局模糊 + ASan。

## 尚余（后续）

1. PlayerState/CardDef 重组为通用字段+每机制子结构；state_hash/observation/
   full_state_json 改为按机制注册的结构化 visitor（消灭四处手同步）。
2. card_names.hpp 消解：引擎侧 `has_named_active` 逐个改为 CardDef 能力位
   （由 Lua 数据声明）；drama 六条件数据化。
3. Lua v2 余项：交互式移动菜单的 choose_move 惯用形、responded_is_plain
   小工具（4 处轻微重复）；`set_algorithm`/`raira_gain`/`set_slot` 等
   机制专属写入器随各自 mechanics 文件继续收编。

---

以下为过程记录（裁定、差异审计、验证方法），保留备查。

## 过程记录 A：付与结算顺序变更审计

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

**2026-10-07 更新：第 1–4 项已全部完成**（16 个机制各归其源文件：steam/ice/
wound/market/soil/fate/drama/dive/barracks/curse/poison/keiryo/slots/parts/
memory + blocks 注册汇总；engine.cpp 3513→2165 行，effect_host.cpp 2011→1590 行）。
弃用清理亦已完成：amount/add_crystals/move_crystals 标 [[deprecated]]，引擎内
33 处调用迁移至 move()/adjust() 快捷方式与 token API；reveal_hand 确认为内容
依赖的显式空操作后保留。新增女神指南见 docs/adding-goddess.md。

剩余：

5. PlayerState/CardDef 重组为通用字段 + 每机制子结构；state_hash/observation/
   full_state_json 改为按机制注册的结构化 visitor（消灭四处手同步）
6. card_names.hpp 消解：引擎侧 `has_named_active` 检查逐个改为 CardDef 能力位
   （由 Lua 数据声明），仅保留测试用名字清单；drama 六条件数据化
7. Phase 4（Lua API v2）：self_boost/choose_move/responded_is_plain/span 工具上收，
  删除 set_vigor/set_flare 等裸写入器，ctx 分层（查询/决策/行动），26 模块迁移
   （ctx:aura_damage/ctx:life_damage 特化接口已就位）

验证方法备忘：新旧引擎对照必须**在各源码树目录下运行**（content 按 cwd 解析）；
hajimari 语料应 30/30 帧一致，standard 语料分歧应全部起于付与结算（Phase 2 审计表）。

## 公开/检视统一（2026-10-07 裁定）

- 裁定：公开/检视为**同一瞬时信息原语**；切牌检视不持久（20-O-S4 知音难觅会把
  构筑未用切牌加入切牌区，切牌列表可变，持久知识会过期）。
- 新接口 `ctx:reveal_cards(viewer, owner, zone, prompt?, mn?, mx?, filter?)`；
  删除 `ctx:reveal_hand`（原空操作）与 `ctx:reveal_opponent_cuts`+`revealOppSpecials_`
  （持久标志，语义错误）。
- D5：协议新增 `kind="reveal"` 零选择信息请求（纯公开形态）；对手手牌/未使用切牌
  从此在任何 observation 中都不可见（原 kururu 检视后持久可见）。
- D6：最终搜寻（kururu A1-S3）的切牌查看从 on_play 开头移入计数=2 分支，
  对齐 rules/10-kururu.md:28 卡面文本。
- 验证：216/216 用例（含 2 个新钉死测试：纯公开请求形态/检视选择等价+cuts 过滤）、
  600 局模糊、ASan 100 局全绿。

## Lua v2 第一批（2026-10-07）

- `ctx:cost_vigor(p, n)`（支付，下限 0）/ `ctx:vigor_to(p, n)`（「集中力变 X」，
  clamp 0..2）取代裸写入器 `ctx:set_vigor`——**已删除**，12 处内容调用按语义迁移。
  期间一次删除事故（连带误删 cower/lose_life 绑定）被测试当场抓住并恢复；
  raira 一处漏迁移因测试路径未覆盖而静默——由此新增**内容 API 审计测试**
  （rules_matrix_tests：内容调用的每个 ctx/atk/ev 方法必须已注册，静态全量扫描）。
- `ctx:self_boost(fn)`：本牌攻击自增益的惯用形（匹配本牌实例、本回合过期），
  取代内容侧 boost_self 复制（mizuki/megumi 已迁移）。发现：部分旧复制用单参
  `function(atk)` 接两参 match 调用，实际匹配到 ctx（碰巧等值、语义为「本回合
  任意攻击」）——self_boost 的工厂闭包为正确的两参形态。
- `responded_is_plain` 与移动菜单上收、其余 16 处 next_attack_mod 手写迁移：
  见剩余清单。
- 验证：223/223 + 400 局模糊 + ASan 100 局全绿。
