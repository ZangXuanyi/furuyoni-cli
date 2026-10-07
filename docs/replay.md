# 录制、回放与检视

## 确定性回放

引擎的确定性三要素：注入种子（`GameState.rng`，xorshift64）+ 决策日志 +
逐决策状态哈希。因此**同种子 + 同决策序列 = 完全相同的对局**。

```bash
# 录制
./build/furuyoni-cli --standard --random --seed 42 --record /tmp/game.json

# 回放（校验每个决策点的状态哈希；不匹配会报错退出）
./build/furuyoni-cli --replay /tmp/game.json

# 回放并生成 WebUI
./build/furuyoni-cli --replay /tmp/game.json --web /tmp/replay.html
```

注意：状态哈希算法随引擎内部演进（见 `refactor-log.md` 破坏性变更表），
**旧版本录制的日志不能在新版本回放**。

## Trace JSON

`--trace FILE` 导出每步完整状态（含隐藏信息与**调用栈/对应栈**）的 JSON，
供外部工具消费。

## 回放 WebUI

`--web FILE` 生成**自包含 HTML**（内嵌数据与脚本，无需服务器），浏览器打开即可：

* 上一步/下一步/首尾/滑块，方向键 ←→ / Home / End；每步显示回合、阶段、行动方。
* 场面：双方命/装/气/集中力、距/虚/近身距离，以及畏缩、不能再对应等状态。
* **结算栈**：当前正在结算的牌（顶牌高亮），可直接看到「攻击 → 对应牌 → …」
  的压栈过程。
* 双方手牌/切札（标注使用后）/付与区（标注献数）/弃牌堆/盖牌堆/牌山（可展开）。
* 每个决策点展示选项与被选中的项；页眉显示最终结果
  （如 `Player0 刀薙（扇） 胜 …`，括号内为被禁用的一柱）。
