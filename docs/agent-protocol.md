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

双方命/装/气/集中力、距/虚/近身距离、回合与当前玩家、**自己**的手牌、各区域
数量、公开的弃牌堆与付与区（含献数）、已使用切札；**对手手牌与未使用切札不可见**
（公开/检视类效果经 `reveal` 请求瞬时送达，见 `adding-goddess.md`）。

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
