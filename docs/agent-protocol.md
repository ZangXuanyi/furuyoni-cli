# 智能体协议（JSON-lines）

引擎与外部智能体之间通过子进程的 stdin/stdout 交换**一行一个 JSON**：
引擎写出一行 `Request`，阻塞等智能体回一行 `Decision`。无需保活、无需异步；
嵌套决策（攻击中的对应、效果中的选择）按同样方式逐个发生。

## Request（引擎 → 智能体）

```jsonc
{
  "kind": "main",                 // 见下表
  "prompt": "main phase action",  // 人类可读提示
  "player": 0,                    // 你是哪一方
  "minSelect": 1, "maxSelect": 1,
  "options": [ { "label": "…", "enabled": true, "data": { … } } ],
  "state": { /* 按观看者过滤的观察 */ }
}
```

## Decision（智能体 → 引擎）

```json
{ "indices": [2] }
```

索引指向 `options`（0 基）。**引擎不信任智能体**：非法索引/禁用项/数量不符会被
剔除修复，且按 `Config.illegalTolerance` 计数（默认 1 次即判负）。

## 请求种类

| kind | 场景 | 说明 |
|---|---|---|
| `main` | 主要阶段动作 | 基本动作、打出牌、切札、结束阶段 |
| `option` | 效果二选一/多选一 | `options` 为字符串选项 |
| `cards` | 选牌 | `options[i].data.inst` 为牌实例 |
| `damage` | 承伤侧选择 | 装伤或命伤 |
| `response` | 对应窗口 | `pass` 或打出对应牌 |
| `build` | 眼前构筑 | 选 7 常规 + 3 切札 |
| `mulligan` | 换牌 | `minSelect` 可为 0 |
| `draft_pick` / `draft_ban` | 三拾一舍 | 选女神 / 禁女神 |
| `bluff` / `doubt` | 22 伪证 | 声称牌名 / 是否质疑 |
| `guess` | 10 最终搜寻 | 猜牌名 |
| `reveal` | **零选择信息请求** | 对手瞬时公开手牌/切牌；选项携带牌面数据，回**空选择** `{ "indices": [] }` 即可 |

`minSelect == maxSelect` 时按数量选取。`options[i].data` 携带结构化信息
（牌定义、全力/终端旗标、伤害侧等），便于程序化决策。

## 观察（state）

- 公开：双方命/装/冰晶/诅咒/气/集中力、距/虚/近身距离、回合与行动方、各区域
  数量、双方弃牌堆内容、付与区（含各牌献数/绿晶）、双方**使用后**切札、毒袋、
  兵舍、地图/戏剧、股市/惑/裂伤等机制区。
- 仅本人：手牌内容、盖牌堆内容、牌山内容（`deck[0]`=牌山顶，实际顺序）、
  未使用切札、潜水方向、策略、回忆区。
- **对手手牌与未使用切札不可见**（公开/检视类效果经 `reveal` 请求瞬时送达）。

主要选项标签为中文：`基本动作：前进/后退/装附/聚气/离脱（支付集中力|盖伏一张
手牌支付）`、`结束主要阶段`、`不对应（放弃）`；结构化键不变（`data.basic` 仍为
`advance/retreat/aura/flare/escape`）。

**并行阶段**：三拾、一舍、眼前构筑、换牌四个阶段双方**同时**收到请求（各自作答
互不阻塞）；其余阶段（对局内）仍为逐个询问。回放/日志顺序固定 P0→P1，确定性不受
线程时序影响。

## 最小实现（Python）

```python
import json, sys
for line in sys.stdin:
    req = json.loads(line)
    n = req.get("minSelect", 1)
    idx = [i for i, o in enumerate(req["options"]) if o.get("enabled", True)][:n]
    print(json.dumps({"indices": idx}), flush=True)
```

完整示例见 `examples/first_agent.py` 与 `examples/random_agent.py`。
