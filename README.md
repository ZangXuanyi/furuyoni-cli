# 散樱乱武 CLI (furuyoni-cli)

《散樱乱武》S10-2 评测环境。目标：为被评测的智能体提供一个**受规则严格约束**、确定性、可回放的对局引擎。引擎本身不含打牌 AI。

当前进度：**Phase 1（基本规则 + 最初的决斗）已完成并可测试。**

---

## 1. 构建与运行

依赖：C++20 编译器、CMake ≥ 3.20、`pkg-config` 的 `lua5.4`、`nlohmann_json`。
sol2 / doctest 以单头文件形式 vendored 在 `third_party/`。

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure     # 或 ./build/furuyoni-tests
```

运行一局（内置智能体）：

```bash
./build/furuyoni-cli --random --seed 42                  # 最初的决斗，双方随机
./build/furuyoni-cli --seed 42                           # 双方“取第一个合法动作”
./build/furuyoni-cli --standard --random --seed 42       # 基础四柱：三拾一舍 + 构筑
./build/furuyoni-cli --standard --p0-cmd "python3 examples/random_agent.py" --p1-cmd "..."
```

接入外部智能体（JSON-lines 子进程）：

```bash
./build/furuyoni-cli \
  --p0-cmd "python3 examples/random_agent.py" \
  --p1-cmd "python3 examples/first_agent.py"
```

录制 / 回放：

```bash
./build/furuyoni-cli --random --seed 42 --record /tmp/opencode/r.json
./build/furuyoni-cli --replay /tmp/opencode/r.json          # 校验状态哈希
```

命令行参数：`--seed N`、`--limit N`、`--random`、`--p0-cmd CMD`、`--p1-cmd CMD`、`--record FILE`、`--replay FILE`，最后一个位置参数是 Lua 内容文件。

---

## 2. 总体架构

```
frontends   cli / (后续 TUI·GUI·Web·Docker Server)
protocol    Request / Decision / Observation(按玩家过滤) / AgentAdapter
            ├─ FirstAgent, RandomAgent            (进程内)
            ├─ SubprocessAgent                    (子进程 JSON-lines)
            └─ (后续) SocketAgent
engine      回合与阶段 / 合法动作枚举 / 攻击结算 / 响应窗口 /
            持续效果 / 破绽 / 切札再起 / RNG / 日志与状态哈希
effects     Lua 效果宿主 (sol2)：卡片行为的唯一实现处
core        ids / Range / Damage / CardDef / CardInstance / GameState / RNG
content     每套卡组一个 Lua 模块（数据 + 行为）
```

引擎**不认识任何具体牌名**：牌面数据与行为都在 Lua 里。这是“不硬编码”的落点。

### 关键设计

* **决策即查询**：所有玩家选择（主阶段动作、伤害侧、对应、构筑、换牌、效果二选一…）都统一为一个 `Request`，返回 `Decision`。合法动作枚举同时用于“给模型的选项”和“校验”。
* **同步阻塞模型**：效果执行时直接向智能体发问并阻塞等待，无需协程。嵌套决策（攻击中的对应、对应中的效果）通过 C++/Lua 嵌套调用自然处理。
* **按玩家过滤观察**：引擎内部知道一切，但发给玩家 P 的 `state` 只含 P 可见信息。
* **确定性**：所有随机走 `GameState.rng`（xorshift64，注入种子）；确定性回放 = 种子 + 决策日志 + 逐帧状态哈希。
* **单一职责的结算顺序**：攻击 = 声明（合法）→ 响应窗口 → 持续修正 → 距离重检（`锁定` 除外）→ 伤害结算 → `攻击后`。落空则跳过 `攻击后`；切札无论落空都保持展开。

---

## 3. 智能体协议（JSON-lines）

引擎向子进程 stdout 写一行 JSON（`Request`），子进程从 stdin 读一行 JSON 返回 `Decision`：

```jsonc
// 引擎 -> 智能体
{
  "kind": "main",                 // main | option | cards | damage | response | build | mulligan
  "prompt": "main phase action",
  "player": 0,
  "minSelect": 1, "maxSelect": 1,
  "options": [ { "label": "...", "enabled": true, "data": { ... } } ],
  "state": { /* 该玩家可见的观察 */ }
}
// 智能体 -> 引擎
{ "indices": [2] }                // 亦可 {"action": 2}
```

`minSelect == maxSelect` 时按数量选取；`mulligan` 的 `minSelect` 为 0。`options[i].data` 含有结构化信息（牌定义、是否全力/终端、伤害侧等），便于程序化决策。

观察 `state` 概要：双方 命/装/气/集中力、距/虚/近身距离、回合与当前玩家、自己手牌、各区域数量、弃牌堆与付与区（公开）、已使用切札；对手手牌与未使用切札隐藏。

参考实现见 `examples/first_agent.py`、`examples/random_agent.py`。

---

## 4. 卡牌编写（Lua）

每套卡组一个 Lua 模块，返回卡牌表。字段：

| 字段 | 说明 |
|---|---|
| `set` / `num` | 所属卡组、组内序号（`num` 不能用 `local`，Lua 关键字保留）|
| `name` | 牌名（可跨卡组重名）|
| `kind` | `normal`（常规）/ `special`（切札）|
| `type` | `attack` / `action` / `enhance` |
| `cost` | 切札耗能 X（气）|
| `nagi` | 付与“纳 X”|
| `full_power` / `response` / `breakable` / `terminal` | 全力 / 对应 / 破绽 / 终端（`break` 是关键字，故用 `breakable`）|
| `attack` | `{ range = {lo,hi} 或 {{lo,hi},...}, damage = { aura = n|fn, life = n|fn }, keywords = {"unrespondable","lock","overwhelm"} }` |
| `on_play` / `on_enter` / `on_discard` / `on_attack_after` / `on_use_after` | 行为钩子（`function(ctx) ... end`）|
| `continuous` | `{ { when="expanded"|"used"|"always", query="attack", apply=function(ctx, atk) ... end } }` |
| `reset` | `{ kind="end_turn"|"immediate", at_least=n }` 或 `{ kind="end_turn", cond=function(ctx) ... end }` |

`ctx` API（宿主函数）：

* 查询：`player()` `opp()` `life(p)` `aura(p)` `flare(p)` `vigor(p)` `hand_size(p)` `deck_size(p)` `discard_size(p)` `cover_size(p)` `distance()` `dust()` `desperation(p)`(命≤3) `hasso(p)`(装≤1)
* 决策：`choose(prompt, {"A","B"}) -> 1|2`、`choose_cards(prompt, {inst,...}, min, max) -> {inst,...}`
* 移动：`move(from, to, n [, pFrom [, pTo]])`，区域字符串 `life|aura|flare|distance|dust`
* 结算：`attack{range=...,damage=...,keywords=...}`、`deal_damage(target, aura, life)`
* 其他：`draw(p,n)` `gain_vigor(p,n)` `set_vigor(p,n)` `cower(p)` `lose_life(p,n)` `rebuild(p[,cost])` `discard_all_hand(p)`
* 对应：`responding_attack()` 返回被对应的攻击（或 nil），攻击对象支持 `:add{aura=,life=}` `:negate()` `:keyword(s)` `:attacker()` `:from_normal()`

示例：

```lua
-- 阴之阱：纳2、破绽、弃置时生成不可对攻击
{ set="hajimari.ukiro", num=9, name="阴之阱", kind="normal", type="enhance",
  nagi=2, breakable=true,
  on_discard = function(ctx)
    ctx:attack{ range={2,3}, damage={aura=3, life=2}, keywords={"unrespondable"} }
  end }

-- 精灵联动：纳3、终端、展开中我方攻击 +1/+0
{ set="hajimari.okika", num=9, name="精灵联动", kind="normal", type="enhance",
  nagi=3, terminal=true,
  continuous={ { when="expanded", query="attack",
    apply=function(ctx, atk) if atk:attacker()==ctx:player() then atk:add{aura=1} end end } } }
```

---

### Phase 2 新增字段与 API

卡牌字段（除已有外）：
* `goddess` / `form`（`"O"`/`"A1"`/`"A2"`）/ `num`：本格-变格替代的接口（变格 Phase 3 使用）。
* `attack` 可为函数 `function(ctx) return {range=…, damage=…, keywords=…} end`；`range`/`damage.aura`/`damage.life`/`keywords` 皆可为函数（在**声明时**求值并锁定，对应牌仍可再减少）。
* `cost` 可为函数（动态切费，如 `响鸣共振 8-敌装`）。
* `armor_as_crystals = true`：结算伤害时卡上结晶视作装，且**优先消耗**（进虚）。
* `lock_distance = true`：只要该牌展开，双方所有“牌效”改变【距】无效（基本动作不受影响）。
* `decay_to = "distance"`：该牌上的结晶被移除时进入距而非虚（`圈域`）。
* `playable = function(ctx)`：主阶段可否打出（如 `天音摇波的潜力` 限决死）。
* `respond = function(ctx)`：非对应牌可否当对应打出（`识破`），或限制对应目标（`终焉` 仅对应切牌）。
* `reset = { kind = "immediate", cond = function(ctx) … end }`：`即再起` 用 `ctx:last_life_lost()` 判断命值跨越（`浮舟宿`）。

`ctx` 新增：`rensha(p)` `shinkyou(p)` `desperation(p)` `hasso(p)` `last_life_lost(p)` `is_attack(inst)`、`hand(p)` `discard_pile(p)`、`to_deck_top(inst)` `to_deck_bottom(inst)` `discard_card(inst)`、`free_basics(p,max)` `do_basic(p,name)`、`choose_cards_for(p,…)`（让对手选择）、`next_attack_mod{ match=fn, apply=fn, this_turn=bool }`、`lose_life(p,n[,to])`。

攻击句柄（`Attack`）新增：`extend_far(n)` `extend_near(n)` `shrink_far(n)`、`both_sides()`、`aura_damage()` `life_damage()`、`source_goddess()` `source_set()` `source_inst()`、`from_special()`。

---

## 5. 规则裁定（本实现采用的确定语义）

以下来自与需求方的确认，作为后续阶段的基准：

1. `决死` = 自命 ≤ 3；`八相` = 自装 ≤ 1。已作为接口 `ctx:desperation/hasso` 暴露；26 女神各自的词条后续提供。
2. 对应牌**本身**不能被对应；常规打出时则可以被对应。对应牌生成的**虚拟攻击**可以被对应。
3. 同时触发多个效果时，**当前玩家**选择先结算哪一个。
4. `纳 X`：由付与牌控制者决定从虚/自装各取多少、顺序自选；不足 X 时尽量多取，>0 即留存，=0 则打出即进弃牌堆。打出付与的结算顺序为 **展开时 → 放献 →（若无献则）弃置时**；即“虚+自装=0”时依然触发 `展开时` 与 `弃置时`，只是没有 `展开中`。
   付与上的结晶掉落（含每回合开始 -1、牌离场）**进入虚**，不会被消耗；因此 `双方命+气+装 + 距 + 虚 + 付与上的结晶 == 36` 恒成立（除非某牌显式从游戏外取得/移出结晶）。
5. 攻击须在声明时合法；被对应导致距离改变后**重检**，唯有 `锁定` 跳过。攻击落空则 `攻击后` 不结算；切札无论如何都展开，`使用后` 光环仍生效。
6. X/Y 的装伤侧实际为 `min(X,5)`，`超克` 不取 min。
7. 重铸 = 1 命→1 气并洗弃牌堆+盖牌堆+牌山，不触发破绽；牌库充足时也可主动重铸，不足时也可不重铸。部分牌有免费重铸效果（后续）。
8. 每次失败的抽牌尝试各触发一次不可对应的【0-10 1/1】焦躁伤害。
9. 基本动作由玩家在“扣 1 集中力 / 盖 1 张手牌”间选择；切札不能用于盖伏支付。
10. `破绽`：付与展开时受到任何命伤（重铸除外）即将其全部结晶移到虚、盖伏该牌且不结算弃置时效果（进盖牌堆）。
11. 双方第一回合（T1、T2）都跳过准备阶段；游戏前各抽 3 并各换牌一次（秘密）。
12. 命归零判负；40 回合无结果判平局；特殊胜负留接口。
13. 三拾一舍 / 构筑 / 换牌均为**秘密且强制同步**（Phase 2+ 实现；Phase 1 为固定教学卡组，但构筑与换牌已实现秘密同步）。
14. 时间制度（每回合 3min + 留存 6min）与非法动作容忍度见 `Config`，Phase 1 预留未强制。

### Phase 2 追加裁定

15. `两侧伤害`：同时承受装伤与命伤（装不足则把全部装移虚，再接命伤），承受方无选择。
16. `视作装`：有效装 = 实装 + 该牌上结晶；承受装伤时**优先耗尽卡上结晶**（进虚）。
17. 条件强化时机：攻击自身的强化在**打出（声明）时**判定并锁定（仍可被对应减少）；`攻击后` 类效果在攻击后结算时判定。
18. `下一次攻击`修饰：作用于**任意**下一次攻击（含虚拟/衍生），**打出即消耗**（无论命中/被对应）；`回燃` 的带条件修饰保留至出现符合条件的攻击（跨回合）。
19. 牌效中的“执行基本动作”：无需集中力/盖牌，但仍受正常合法性与上限约束；`AA到BB` 一律“尽量多移动”。
20. `晴舞台` 实为 `终端` + `展开时：集中力变 2`（仅结束主要阶段，仍需盖伏）。
21. `圈域`：我方攻击距离远+1；其结晶移除（按正常规则）一律进【距】。
22. `迷烟` 对双方牌效生效。
23. `无穷之风`“公开手牌”仅当下一次公开（对 CLI 无持续影响）。
24. `连射` 计数：本回合打出的所有牌（含作为对应打出的），当前这张计入。
25. `气焰万丈`：作用于另一柱女神的任意攻击（含切札与衍生）。
26. 变格在**选女神时同时选形态**（Phase 3 实现；数据层已用 `goddess`/`form`/`num` 预留）。

已知实现注意：`千岁之鸟` 的免费重铸按强制结算；`冲音晶` 以 0 献对应时，其弃置时攻击应延后到被对应攻击结算完毕（Phase 2 未实现该极少数时序，其余正常）。

### 已实现的机制清单

Phase 1：构筑 7+3、秘密换牌、先后手、准备/主要/盖伏/结束四阶段、五种基本动作与两种支付、全力与终端（含“对手回合打出终端后本回合不能再对应”）、攻击管线与响应窗口、伤害 X/Y 选择与 `超克`、`锁定`、`不可对`、`打消`、付与 `纳` 与展开/弃置触发、`破绽`、持续攻击修正、切札耗能与 `再起`/`即再起`、畏缩、焦躁、重铸、命/装上限、回合上限平局、观察过滤、JSON-lines 子进程协议、确定性回放与状态哈希。全力与对应互斥（不存在作为对应打出的全力）。

Phase 2（基础四柱本格）：三拾一舍（秘密选 3 → 秘密禁 1 → 取 2）、双女神 14+8 构筑、五个本格卡组；被动词条 `决死`/`八相`/`连射`/`心境`；条件化攻击（`attack` 可为函数）；攻击距离修正（远/近扩、缩）；“下一次攻击”修饰队列；`两侧伤害`；`视作装` 的卡上结晶（优先消耗）；`迷烟` 的牌效改距无效；响应能力谓词（`识破`/`终焉`）；免费基本动作；动态切费；目标为对手的选择（`无穷之风`）；回库顶/库底、结束回合等工具。

### 已知的后续工作（非 Phase 1 阻断）

* 通用跨牌触发总线（`triggers`：攻击声明/命伤/距变化等事件）——目前仅有卡片自身钩子与 `continuous`。
* `距离扩大` 等 range 修正已有实现（`Range::extend_far`），尚无 Phase 1 卡使用。
* 作为对应打出的终端/全力牌的额外限制。
* `Config` 的时间与非法动作强制。
* 三拾一舍与多女神卡组装载（Phase 2+）。
* 切札型付与（`type=enhance && kind=special`）的完整处理。

---

## 6. 测试

`src/tests/tests.cpp`（doctest）覆盖：Range、内容装载、资源移动、伤害封顶与 `X/-`、`破绽`、构筑阶段合法性、随机对局结晶守恒、回放哈希一致、同种子确定性。

```bash
./build/furuyoni-tests
```

---

## 7. 第三方与修改

* `third_party/sol2`（sol2 3.2.3，MIT）单头文件；附带其 `config.hpp`。
  为兼容 GCC 16 对 `sol::optional<T&>::emplace` 打了一处小补丁（该重载语义不可用，改为 `abort`）。
* `third_party/doctest`（MIT）。
* 引擎用 `lua5.4` 与 `nlohmann/json`（系统包）。
