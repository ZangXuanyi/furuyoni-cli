# 代码架构

```
src/
  core/      无依赖内核：types.hpp（Zone/Area/Token/Attack…）、state.hpp
             （CardDef/CardInstance/GameState）、rng.hpp
  engine/
    engine.hpp/.cpp      回合循环、准备/主要/盖伏/结束阶段、通用查询
    tokens.cpp           统一 Token 系统：全部结晶/异樱移动的唯一入口
    pipeline.cpp         结算管线：打出一张牌的命名阶段 + PlayFrame 结算栈
    engine_attack.cpp    攻击构建/对应窗口/伤害（DamageRoute 参数化）/死亡判定
    engine_setup.cpp     三拾一舍、眼前构筑、初始化
    engine_observe.cpp   观察（按观看者过滤）/状态哈希/日志/trace
    engine_content.cpp   内容模块加载与规则包
    mechanics/           每个女神机制一个源文件（框架+数据全在 C++）：
                         steam(11) ice(15) soil(19) wound(24) market(23)
                         fate(26) drama(20) dive(17) barracks(18) curse(21)
                         poison(09) keiryo(07) slots(12) parts(05) memory(16)
                         blocks.cpp 汇总注册各自的 Lua ctx 方法块
    effect_host.cpp      Lua 宿主（卡表加载、钩子调度、事件总线、ctx 通用绑定）
    effect_ctx.hpp       Lua 绑定共享层（LuaCtx/Attack/Event + 机制注册接口）
  protocol/  Request/Decision、进程内智能体、子进程 JSON-lines 适配
  tools/     CLI 入口
content/     每柱女神一个 Lua 模块（牌面数据 + 效果文本）+ packs.json
rules/       卡面规则文本（权威）；docs/rulings.md 是语义裁定（唯一权威）
```

## 关键设计

* **决策即查询**：所有玩家选择统一为一个 `Request` → `Decision`；合法动作枚举
  同时用于「给智能体的选项」与「校验不可信输入」。
* **同步阻塞模型**：效果执行中直接向智能体发问并阻塞，嵌套决策靠 C++/Lua
  嵌套调用与结算栈（`PlayFrame`）自然处理。
* **统一 Token 系统**（`tokens.cpp`）：命/装/气/距/虚/游戏外/牌上/股市/惑/土壤/
  蒸汽区域 × 樱花/绿晶/种子/植株/蒸汽/冰晶/裂伤，全部经 `token_move`/
  `token_adjust` 移动——容量、重定向（迷烟/血飞沫/脆弱意志…）、守恒审计、
  区域事件集中在一处。旧入口 `move_crystals/add_crystals/amount` 已弃用。
* **结算管线**（`pipeline.cpp`）：costStage → declareStage（伪证/声明期锁定）→
  interceptStage（灯塔/潜水）→ resolveStage（攻击: 对应窗口→修正→距离重检→
  伤害→攻击后；付与: 种植→给献→展开时→0献弃置；行动）→ destinationStage。
  伤害路由经 `DamageRoute` 参数传递（无引擎全局）。
* **机制下沉**：女神专属框架（状态机/子流程）在 `mechanics/<名>.cpp`，
  各自注册 Lua API 块；牌面文本留在 Lua（`content/*.lua`）。
  能力注册表（`MechanicBit`）取代运行期女神名字符串匹配。
* **死亡判定优先级**：特胜特败 → 赖着不死（埋骨地/阡）→ 复活（仙霄鬼泉/
  最后的结晶）；诅咒特败与戏剧特胜无视复活（`dramaMarked_` 持久标记）。
* **确定性**：种子 + 决策日志 + 逐帧状态哈希；`state_hash` 覆盖全部
  决策相关状态。
