# 如何新增一名自定义女神

写给之后接手本仓库的你。假设你已读过 `README.md` 与 `rules/basic.md`，知道
命/装/气/距/虚、纳/献、切牌这些词的意思。本文从一个最小可跑的女神讲起，再到
光环/触发器/对应，最后讲什么时候需要动 C++。

验证环境入口（先跑起来再改）：

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure     # 全量测试（每次改动后必跑）
./build/furuyoni-cli --content-dir content:custom --allow-custom \
    --goddesses <你的女神id> --random --random   # 快速自战一局
```

---

## 0. 全景：一张卡的生命周期

你的女神 = 一个 Lua 文件 = 一个**卡表数组**。引擎按「命名阶段的结算管线」
（`src/engine/pipeline.cpp`）处理打出：

```
costStage(切牌费/额外费) → declareStage(声明期锁定/伪证) → interceptStage
(灯塔/潜水拦截) → resolveStage(攻击:对应窗口→修正→距离重检→伤害→攻击后
/ 付与:种植→给献→展开时→0献弃置 / 行动:on_play) → destinationStage(去向)
```

**分工边界（重要）**：
- **C++ 引擎** = 一切「规则」：区域与结晶移动（`tokens.cpp` 的 `token_move` 唯一
  入口）、时机、对应窗口、伤害、重铸、胜负。引擎原则上不认识任何具体牌名。
- **Lua 内容** = 一切「牌面文本」：这张牌打出的效果、光环、触发器。以最高的
  抽象表达，禁止绕过 ctx 直接改引擎内部状态。
- **机制框架**（蒸汽/冰晶/土壤/裂伤/股市/命运槽/戏剧/……）= C++ 的
  `src/engine/mechanics/<名字>.cpp`，各自注册自己的 ctx API 块。若你的女神
  引入**全新种类的区域/指示物/子流程**，才需要新开一个机制文件（见第 4 节）。

## 1. 最小女神：7 常规 + 4 切牌

复制 `src/tests/fixtures/custom_goddess.lua` 起步。要点：

```lua
return {
  -- 常规牌 num 1..7；kind="normal"
  { set = "mygirl", form = "O", num = 3, name = "疾风步", kind = "normal",
    type = "action",                                   -- attack / action / enhance
    on_play = function(ctx) ctx:move("dust", "aura", 1, ctx:player(), ctx:player()) end },
  -- 切牌 num 1..4；kind="special"，带 cost（气费）
  { set = "mygirl", form = "O", num = 2, name = "疾风奥义", kind = "special",
    type = "attack", cost = 2,
    attack = { range = {2, 4}, damage = { aura = 3, life = 3 } } },
}
```

- `set` = 女神 id；异相形态用 `set="mygirl.A1", form="A1"`，**同 num 覆盖**本格同名位。
- `type` 三类：`attack`（必须有 `attack` 表或函数）、`action`（`on_play`）、
  `enhance`（必须有 `nagi`，可带 `on_expand`/`on_discard`）。
- 副类别关键词：`full_power` / `response` / `terminal` / `breakable`（破绽）。
- 攻击距离可以离散：`range = { {1,1}, {3,3}, {5,5} }`；伤害单边 `life = nil` 即 `X/-`。
- 动态值写成函数，**声明时求值并锁定**：`cost = function(ctx) ... end`、
  `attack = function(ctx) return {...} end`、`damage.aura = function(ctx) ... end`。

把文件放进 `content/`，并在 `content/packs.json` 的 `custom` 数组里加文件名
（或用 `--content-dir content:custom` 直载）。

## 2. ctx 速查（写牌面效果只用这些）

完整清单以 `src/engine/effect_host.cpp` 与 `src/engine/mechanics/*.cpp` 为准
（每个机制文件注册自己的那一段）；下面是日常 90% 的部分。

**查询**：`life/aura/flare/vigor/distance/dust(p)`、`hand/discard_pile/...`（实例
id 列表）、`card_name(inst)`、`desperation(p)`（决死≤3）、`hasso(p)`（八相≤1）、
`frozen(p)`、`curse(p)`、`mirror(p)`、`is_my_turn()`、`responding_attack()`。

**结晶移动（统一接口）**：
```lua
ctx:move("dust", "aura", 1, p, p)     -- 广泛接口：from/to/n/属主；区域名见下
ctx:aura_damage(p, n)                 -- 特化接口：装伤（装→虚，走全部重定向策略）
ctx:life_damage(p, n)                 -- 命伤（命→敌气）
ctx:attack{ range=…, damage=…, after=function(c2,a) … end }   -- 衍生攻击=虚拟牌
```
区域名：`"life"/"aura"/"flare"/"distance"/"dust"/"market"/"waku"`；牌上结晶用
`ctx:move_to_card("dust", inst, n)` / `ctx:move_from_card(inst, "dust", n)` /
`ctx:drain_card_crystals(inst, n)`。

**决策（对智能体提问）**：`ctx:choose(prompt, {"选项A","选项B"})`（返回 1 基序号）、
`ctx:choose_cards(prompt, list, min, max)`、`ctx:choose_cards_for(谁, ...)`（让对手选）。

**公开/检视（统一信息原语，均为瞬时）**：
```lua
ctx:reveal_cards(viewer, owner, "hand")                        -- 纯公开：零选择信息请求
ctx:reveal_cards(viewer, owner, "cuts")                        -- 公开对手未使用切牌
ctx:reveal_cards(viewer, owner, "hand", prompt, 1, 1, filter)  -- 检视+选择：一次请求完成
```
"公开手牌"/"检视对手手牌，并～"是同一原语的两种用法：信息只经请求瞬时送达查看方
（进入其决策日志），观测接口始终按观看者过滤——**没有持续公开状态**（手牌与切牌
列表都会变化，持久公开的知识会过期）。展示特定的一张牌用 filter 限定范围。

**付与**：`on_expand`（献落位后触发，可读最终献数；可自加结晶免于 0 献弃置）、
`on_discard`（弃置时）、`continuous`（光环，见下）、`decay_to`（献离牌去向：
`"dust"` 默认 / `"distance"` / `"enemy_flare"` / `"waku"`）。

**光环与触发器**：
```lua
continuous = { { when = "expanded",      -- 展开中；"used" = 使用后切牌；"always"
                 query = "attack",       -- 修正攻击；"cost" 修正切牌费
                 apply = function(ctx, atk) atk:add { aura = 1 } end,
                 replace = true } }      -- 数值替换类（先于增减结算）
triggers = { { event = "turn_start",     -- 34 个事件名见 effect_host.cpp::fire 调用点
               zone = "discard",         -- 默认只在场上；手牌/弃牌堆要显式声明
               cond = function(ctx, ev) return ev:subject() == ctx:player() end,
               run = function(ctx, ev) ctx:draw(ctx:player(), 1) end } }
```

**本牌攻击自增益**：动态攻击值写在 `attack` 函数里（声明时求值并锁定）：
```lua
attack = function(ctx)
  local a = { range = {3,4}, damage = { aura = 2, life = 1 } }
  if ctx:desperation(ctx:player()) then a.damage.aura = 3 end   -- 决死类条件
  return a
end
```
牌在打出后、攻击结算前还要给**本牌这次攻击**加值的场合用 `ctx:self_boost(fn)`：
```lua
on_play = function(ctx)
  ctx:self_boost(function(c2, atk) atk:add { aura = 1 } end)   -- 只作用于本牌的下一次攻击
end
```

**集中力**：获得用 `ctx:gain_vigor(p, n)`（受畏缩/上限约束）；支付用
`ctx:cost_vigor(p, n)`；「集中力变 X」类定值效果用 `ctx:vigor_to(p, x)`。
没有裸 `set_vigor`——内容 API 审计测试会拒绝未注册方法的调用。

**切牌再起**：`reset = { kind = "end_turn", cond = function(ctx) ... end }`
或 `kind = "immediate"`（即再起）；事件驱动加 `on = "<事件名>"`。

## 3. 常见坑（都真实踩过）

1. **结晶守恒**：双方命+气+装+距+虚+牌上结晶恒等于 36。永远用 `ctx:move`
   系列，不要想"直接扣数字"——引擎的不变式检查（fuzz）会当场抓住。
2. **对应打出的牌不能再被对应**；它生成的衍生攻击可以。`response = true` 的牌
   也可以在自己回合正常打出。
3. **`on_expand` 在献落位之后**——读牌上结晶数是准的；纳 0 的牌靠它自救才留场。
4. 触发器默认**只在场上**生效；`zone = "hand"` / `"discard"` 必须显式写。
5. `ctx:choose*` 的所有选项都必须是**合法动作**——选项列表就是规则约束本身，
   别给玩家出"非法也可以选"的题。
6. 每个效果都要想清楚"是谁做的决定"：`choose_for(对手, ...)` 与 `choose(...)`
   的信息面不同。
7. 改完必跑 `ctest`；语义拿不准时查 `docs/rulings.md`（唯一权威裁定），
   再不行读 `rules/` 对应女神的卡面。

## 4. 什么时候要动 C++（新机制）

只有当你的女神引入**新的状态容器或子流程**（如新的区域、指示物种类、独立
的回合外流程）时才写 C++。参照现有模式，三步：

1. **建机制文件** `src/engine/mechanics/<name>.cpp`：
   - 把引擎函数写成 `Engine::xxx`（声明加到 `src/engine/engine.hpp` 对应分区）；
   - 文件末尾写 `void <name>_ctx(CtxTypes& t) { auto& ctx = t.ctx; ctx[...] = ...; }`。
2. **注册**：`mechanics/blocks.cpp` 加一行声明 + 一行调用；`CMakeLists.txt` 加源文件。
3. **能力位**（若女神有专属机制）：`engine.hpp` 的 `MechanicBit` 加一位，
   `engine_setup.cpp::mechanic_bits()` 加映射，setup 时按需初始化状态。

状态字段放 `src/core/state.hpp` 的 `PlayerState`/`CardDef`（哈希与观测记得同步，
或等结构化 visitor 落地后自动覆盖）。异樱类指示物请走 Token 模型
（`core/tokens.hpp` 的 `Token` + `tokens.cpp` 的异樱分支），不要另起炉灶。

## 5. 测试怎么写

- 数据审计：`src/tests/rules_matrix_tests.cpp` 校验 `rules/*.md` 与内容一致，
  新女神记得补条目。
- 行为测试：仿照 `engine_tests.cpp` 里任意 `TEST_CASE`——加载内容、摆水晶、
  打出牌、断言状态 + `crystals_total(e) == 36` + `check_invariants(e)` 为空。
- 冒烟：`--goddesses <id> --random --random --seed N` 自战若干局，
  `strictLua`（默认开）下任何 Lua 错误都会直接炸出来。

祝顺利。有疑问时，`grep -rn "ctx\[\"" src/engine/` 永远是最诚实的 API 文档。
