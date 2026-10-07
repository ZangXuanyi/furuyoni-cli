# AI 亲自上场指南（文件邮箱桥）

让一个 LLM 会话（ZCode 新窗口 / DeepSeek Harness，甚至人类）作为智能体直接打牌。
**不改引擎、不需要 API 密钥**：桥脚本把引擎的协议流翻译成邮箱目录里的文件，
Agent 只需读写文件。

## 一图流

```
引擎 --stdin--> scripts/ai_bridge.py --> 邮箱目录/inbox/NNNN.md   (局面+选项, Agent 读)
                                       <-- 邮箱目录/answer/NNNN.json (Agent 写决策)
        <--stdout-- {"indices":[...]}
```

## 操作方流程（你来执行）

1. 开赛（每局一条命令；规则集默认 kigen-full 起源战全扩）：
   ```bash
   ./scripts/match.sh practice1 42 matches/practice1/p0 matches/practice1/p1
   ```
2. 为**每个座位**开一个 Agent 会话（ZCode 新窗口或 DeepSeek Harness），把下面的
   系统提示粘给它（替换 `<邮箱目录>` 与 `<座位号>`）。
3. 打完看 `$OUT/replay.html`（WebUI 逐步复盘，含结算栈）与各座位 `transcript.md`。
4. 练习赛建议 3~5 局、换种子；之后进入第二场「限时写脚本」——直接
   `--p0-cmd "python3 队伍脚本.py"` 接入，协议同 `docs/agent-protocol.md`。

## 给 LLM Agent 的系统提示（模板，直接粘贴）

> 你是散樱乱武对局的智能体，座位 `<座位号>`（0 先手 / 1 后手）。
> 你的邮箱目录是 `<邮箱目录>`（绝对路径），引擎通过它与你会话。
>
> **规则**：卡面文本在 `rules/`（basic.md 是基础规则，先读它）；协议说明在
> `docs/agent-protocol.md`。你只能依赖自己观察里的信息——对手手牌与未使用切牌
> 不可见；**禁止**读取引擎进程内存/对方邮箱/transcript 以外的任何对局产物。
>
> **工作循环**（循环执行直到 inbox 不再出现新请求）：
> 1. 轮询 `<邮箱目录>/inbox/`，找尚未回答的最新 `NNNN.md`（answer/ 里没有对应
>    文件的就是待办；`NNNN.retry.md` 表示你上次的回答不合法，读它修正）。
> 2. 读 `NNNN.md`：局面、编号选项、选择数量约束。需要精确数据再读同号 `.json`。
> 3. 想清楚策略（水晶经济/距离控制/斩杀线/对手可能的对应对策），把
>    `<邮箱目录>/answer/NNNN.json` 写为：
>    ```json
>    {"indices": [选中的编号], "reason": "一句话理由"}
>    ```
>    注意：数量必须在 minSelect~maxSelect 之间、不重复、`[不可选]` 的不能选；
>    数量约束为 0 的信息型请求（如对手公开手牌）写 `"indices": []`。
>    **写文件要原子**：先写 `NNNN.json.tmp` 再改名，避免桥读到半截文件。
> 4. 回到第 1 步。每步决策建议 ≤ 60 秒；大局（build/重铸/斩杀抉择）可放宽。
>
> 常见请求种类：`main`（主要阶段动作）、`response`（对应窗口，pass=选项 0）、
> `damage`（承伤侧）、`build`（眼前构筑 7+3）、`mulligan`、`reveal`（零选择）。
> 你的决策质量完全由你负责；桥只保证协议合法性（非法回答会被要求重写，不会判负）。

## 座位目录结构

| 文件 | 谁写 | 内容 |
|---|---|---|
| `inbox/NNNN.md` | 桥 | 渲染好的局面 + 编号选项（主要阅读对象） |
| `inbox/NNNN.json` | 桥 | 原始请求（需要精确 data 时用） |
| `inbox/NNNN.retry.md` | 桥 | 上次回答不合法的原因 |
| `answer/NNNN.json` | **Agent** | `{"indices":[...],"reason":"..."}` |
| `transcript.md` | 桥 | 全程流水（复盘用） |

## 无 LLM 自检（联调）

两个座位都挂哑回答器即可打满一整局，验证链路：

```bash
./scripts/match.sh smoke 7 matches/smoke/p0 matches/smoke/p1 &
sleep 1
python3 scripts/auto_answer.py --dir matches/smoke/p0 &
python3 scripts/auto_answer.py --dir matches/smoke/p1
```

也可单侧挂 `auto_answer.py` 作为陪打对手。
