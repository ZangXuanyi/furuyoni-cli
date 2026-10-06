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

# 回放展示 WebUI（生成自包含 HTML，用浏览器打开）
./build/furuyoni-cli --standard --random --seed 42 --web /tmp/replay.html
# 由决策日志重放并生成 WebUI（会校验状态哈希）
./build/furuyoni-cli --standard --random --seed 42 --record /tmp/r.json
./build/furuyoni-cli --standard --replay /tmp/r.json --web /tmp/replay.html
# 导出原始 trace JSON（每步完整状态 + 调用栈）
./build/furuyoni-cli --standard --random --seed 42 --trace /tmp/trace.json
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

攻击句柄（`Attack`）新增：`extend_far(n)` `extend_near(n)` `shrink_far(n)`、`both_sides()`、`aura_damage()` `life_damage()`、`source_goddess()` `source_set()` `source_inst()` `source_is_goddess(g)`、`from_special()`、`no_special_response()`、`remove_unrespondable()`、`attacker_chooses_damage()`、`offer_evade(n)`。

Phase 3 新增字段与 API：
* 卡牌：`goddesses`/`goddess2`（双女神，如 `合奏`）、`aura_max`（`徒寄之八重樱`）、`triggers = {{event, cond, run}}`、`attack` 规格可含 `attacker_chooses_damage`（畏掠）与 `terminal`（电磁炮·黄）。
* `ctx`：`used_specials(p)` `used_special_count(p,g)` `card_is_goddess(inst,g)`、`reset_special(inst)` `die(p)` `end_current_main()` `max_aura(p)`、`add_hand_limit` `add_cut_cost_delta` `set_cannot_attack` `set_cannot_basic`、`legal_basics(p)` `free_basics_of(p,max,{...})`、`last_damage_side/amount/from_attack`、`store_int/load_int`（`神座渡` 的 X 锁定）、`on_resolve(fn)`（攻击结算后的延迟回调）。
* 事件对象 `ev`：`type() subject() first() card() attacker() attack()`。

---

## 4.5 回放展示 WebUI

`--web FILE` 会运行（或重放）一场对局并输出一个**自包含 HTML**（内嵌数据与脚本，无需服务器），用浏览器打开即可逐步查看：

* 上一步 / 下一步 / 首尾 / 滑块，方向键 ←→ / Home / End；每步显示**回合、阶段、行动方**。
* 场面：双方 命/装/气/集中力、距/虚/近身距离，以及畏缩、不能再对应等状态。
* **调用栈 / 对应栈**：当前正在结算的牌（顶牌高亮），可直接看到“攻击 → 对应牌 → …”的压栈。
* 双方 **手牌 / 切札（标注「使用后」）/ 付与区（标注结晶数）/ 弃牌堆 / 盖牌堆 / 牌山**（可展开）。
* 每个决策点展示**选项**与被选中的项。
* 页眉始终显示最终结果，格式如 `Player0 刀薙（扇） 胜 Player1 刀（薙）铳`（括号内为被禁用的那一柱）。

数据来自引擎的 `full_state_json()`（含隐藏信息，仅用于赛后回看）与 `trace_json()`；`--trace` 可导出该 JSON 供其它工具使用。注意三拾一舍强制**各柱女神不重复**（不会出现同一女神的两个变格）。

## 5. 规则裁定

所有语义裁定（早期编号 1~109 与 2026-10 新裁定）集中在 **[`docs/rulings.md`](docs/rulings.md)**，
它是查规则语义的唯一入口。`rules/` 是卡面文本，裁定文件是语义裁定，两者冲突时以裁定为准。

## 6. 测试

测试分四个文件（doctest，统一二进制）：

* `src/tests/tests.cpp` —— 基础规则与各女神机制的定点测试。
* `src/tests/engine_tests.cpp` —— **结构不变量**（区域互斥/归属/上下界/结晶守恒）、**对抗性决策**（越界/负数/数量不符/空选，要求永不 UB 且非法计数生效）、回放篡改检测，以及本轮审计发现缺陷的回归测试。
* `src/tests/rules_matrix_tests.cpp` —— **机械数据审计**：把 `rules/00..12.md` 的静态攻击（距离+伤害）逐条与 `content/*.lua` 的 `CardDef` 对比；并校验引擎特判的所有牌名都确实存在（改名会立刻失败）。
* 便利设施：`src/tests/test_util.hpp`、`invariants.hpp`、`agents.hpp`。

```bash
./build/furuyoni-tests                     # 全部
FY_FUZZ_GAMES=2500 ./build/furuyoni-tests  # 放大随机模糊测试
ctest --test-dir build --output-on-failure # unit + fuzz 两个目标
```

建议另跑 sanitizer 构建（本轮用它发现并修复了多处 UB）：

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 ./build-asan/furuyoni-tests
```

---

## 6.5 严格审计与已知问题

2026-10 对全仓库做了一次逐牌/逐规则的严格审计（12 柱 233 张牌 + `basic.md` + `hajimari`），
修复了数十处语义缺陷（不可信决策越界、攻击重复计数、切札付与生命周期、事件归属、
即再起失效、毒牌归属、`壮绝旅程` 费用光环、`电磁炮` 动态终端、Lua 错误静默等），
并把引擎按职责拆分为 `engine.cpp` / `engine_attack.cpp` / `engine_setup.cpp` /
`engine_observe.cpp`，硬编码牌名收敛到 `engine/card_names.hpp`。

* 修复清单与**待需求方裁定的规则歧义**：见 [`docs/known-issues.md`](docs/known-issues.md)。
* “中等重构”原则：Lua 内容接口与 CLI/JSON 协议保持兼容；每阶段改动都以
  不变量模糊测试 + 回放哈希 + 语料回归兜底。

## 7. 第三方与修改

* `third_party/sol2`（sol2 3.2.3，MIT）单头文件；附带其 `config.hpp`。
  为兼容 GCC 16 对 `sol::optional<T&>::emplace` 打了一处小补丁（该重载语义不可用，改为 `abort`）。
* `third_party/doctest`（MIT）。
* 引擎用 `lua5.4` 与 `nlohmann/json`（系统包）。
