# 子代理施工说明（23–26 新人女神）

你在这个仓库里实现一位（或多位）新女神。**只按分配给你的范围写文件**，不要动别人负责的文件（见下）。
Lead 负责 `content/packs.json`、跨柱集成与最终验证。

## 1. 仓库与构建

- C++20 引擎：`src/engine/`、`src/core/`；内容全部在 Lua：`content/*.lua`。
- 构建 / 测试：
  - `cmake --build build`
  - `./build/furuyoni-tests`（全量；期望全绿，**不允许**新增失败）
  - `./build/furuyoni-tests --test-case="rules*"`（**规则-数据一致性审计**：会拿 `rules/NN-*.md` 里的静态攻击数据与你的 Lua 对表，必须绿）
  - `ctest --test-dir build`
  - 模糊/不变量：`FY_FUZZ_GAMES=300 ./build/furuyoni-tests --test-case="invariants*"`
  - ASan：`cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 ./build-asan/furuyoni-tests`
  - 深扫：`FY_FUZZ_GAMES=600 ./build/furuyoni-tests --test-case="invariants*,engine tolerates*,illegal*"`
  - CLI 冒烟：`./build/furuyoni-cli --standard --random --seed 3 --preset gachi-full`
- 用 `lua5.4 -e 'assert(loadfile("content/你的文件.lua"))'` 自查 Lua 语法。
- 规则矩阵审计（`src/tests/rules_matrix_tests.cpp`）由 **Lead** 维护：**不要**改它，也不要把你的柱加进去；
  你只保证按上面第 2 节的写法让静态卡面数据与 `rules/NN-*.md` 完全一致（Lead 随后会加进去并跑审计）。
- 用 `lua5.4 -e 'assert(loadfile("content/你的文件.lua"))'` 自查 Lua 语法。

## 2. 内容文件的写法（照抄现有风格）

- 参考实现：`content/kamuwi.lua`（最新、最简洁）、`content/yatsuha.lua`（复杂机制 + 完全态）、`content/utsuro.lua`。
- 每张牌是一个 Lua table：
  `{ set = "<女神>", form = "O"/"A1", num = <1..7 常规 / 1..4 切札>, name = "…", kind = "normal"/"special", type = "attack"/"enhance"/"action", cost = <切札费用>, nagi = <纳>, full_power = true, response = true, terminal = true, breakable = true, extra = true, … }`
- **常规 7 张（num 1..7）+ 切札 4 张（num 1..4）**；变格用 `form="A1"` 且用**同 kind+同 num** 覆盖本格。
- 追加牌（EX）必须 `extra = true`（不进构筑池），`num` 用 9xx。
- 触发器：`triggers = { { event="…", cond=function(ctx, ev) … end, run=function(ctx, ev) … end } }`；
  可用的 `cond`/`run` 参数：`ev:subject() ev:card() ev:first() ev:attacker() ev:attack()`。
  隐式触发器只在牌处于「付与区 / 已使用切札 / 变形光环」时生效；从手牌或弃牌堆触发的要写 `zone="hand"` / `zone="discard"`。
- 静态攻击写 `attack = { range = {lo,hi}, damage = {aura=.., life=..}, keywords = {"unrespondable","overwrite"…} }`；
  动态攻击写 `attack = function(ctx) return {...} end`（**审计只检查静态的**，动态的 X 类会被跳过，但 `rules/NN-*.md` 里写死的数字必须一致）。
- `on_play` / `on_expand`（付与展开时：献落位后触发，可读最终献数）/ `on_attack_after` / `on_discard` / `continuous = { { when="expanded", query="attack", apply=function(ctx, atk) … end } }` / `reset = { kind="end_turn"/"immediate", cond=function(ctx) return … end, on="<事件名>" }`。
- Lua 是 **strict** 的：任何运行时错误都会抛异常并判测试失败。不要调用不存在的 ctx 函数，不要索引 nil。
- 引擎的 `ctx[...]` 注册表（`src/engine/effect_host.cpp`）是 API 的**权威**：写代码前先 `grep 'ctx\["' src/engine/effect_host.cpp` 核对函数名与参数个数。

## 3. 你新增引擎能力时的规矩

- 新字段加在 `src/core/state.hpp`（`PlayerState` / `CardDef` / `CardInstance`），并在 `src/engine/effect_host.cpp` 的卡牌解析处读取（`d.xxx = t.get_or("xxx", …)`）。
- 新 ctx 函数加在 `src/engine/effect_host.cpp`，声明/实现在 `engine.hpp` + 对应 `engine.cpp`。
- **结晶守恒**：任何把结晶移出/移入的操作都必须走 `add_crystals` / `move_crystals`（`externalAdded_` 只用于"游戏外"）；不要把 `st.dust` / `ps(p).aura` 直接赋值增减（除非同时保证两边平衡）。随机/不变量测试会抓这个。
- `state_hash()`（`engine_observe.cpp`）必须包含你新增的**所有影响后续决策的状态**，否则回放校验会失效。
- `observation()`：公开信息要给两边；**秘密信息（例：潜水的选择、回忆区内容）只给本人**。
- 死亡/胜利统一走 `check_win()`；`check_win` 已支持 `curse >= 16`、`protects_enemy`。

## 4. 交通规则（重要）

- 一次只改**你被分配的文件**；`commit` 不需要。`content/packs.json`、`src/tests/rules_matrix_tests.cpp`、
  `docs/*.md` 由 Lead 维护，**不要动**。
- 改完必须：build 成功 + 全量测试绿 + `rules*` 绿 + 300 局不变量绿。
- 报告格式：完成度表、关键实现决策、你发现的规则歧义/无法实现点、以及你实际跑过的命令与结果。
- 发现规则与实现冲突时：**先在最终回复里列为问题**，不要默默按自己的理解改规则。

## 5. 权威文档

- `rules/23-akina.md` / `rules/24-shisui.md` / `rules/25-misora.md` / `rules/26-innealra.md`：该柱卡面（唯一权威）。
- `docs/rulings.md` **A.9**：23–26 的实现前裁定（投资/套现、裂伤结算、瞄准点、命运槽），**必须先读**。
- `basic.md`：通用规则。
- `docs/rulings.md`：**已裁定的所有规则问题**（含 13–21 全部裁定，务必先读 A 节，避免重复踩坑）。
- `docs/content-modules.md`：模块/预设/CLI/追加牌区位约定。
- `README.md` §4：卡牌字段与 ctx API 总览（可能略滞后，以 `effect_host.cpp` 为准）。
