# 运行与评测

如何启动对局、配置规则包、接入被评测的智能体。

## 命令行参数

| 参数 | 说明 |
|---|---|
| `--seed N` | 随机种子（决定抽将、洗牌、全部随机） |
| `--limit N` | 回合数上限（默认 40，超出判和） |
| `--random` | 双方使用内置随机智能体；缺省为「取第一个合法动作」 |
| `--p0-cmd CMD` / `--p1-cmd CMD` | 外部智能体（子进程 JSON-lines，见 `agent-protocol.md`） |
| `--record FILE` | 录制决策日志（JSON） |
| `--replay FILE` | 按日志重放并**校验状态哈希**（确定性验证） |
| `--trace FILE` | 导出逐步完整状态 + 调用栈的 trace JSON |
| `--web FILE` | 生成自包含回放 HTML（见 `replay.md`） |
| `--standard` | 标准模式：三拾一舍抽将 + 眼前构筑（缺省为最初的决斗固定牌组） |
| `--preset NAME` | 规则包预设（下表） |
| `--variants on\|off` | 异相形态开关（覆盖预设） |
| `--packs a,b` | 限定内容包 |
| `--allow-custom` | 允许 custom 包 |
| `--content-dir DIR[:pack]` | 追加内容目录 |
| `--goddesses a,b` | 限定女神池 |
| 位置参数 | 直接指定单个 Lua 内容文件（固定牌组模式） |

**注意**：内容按**当前工作目录**解析（`content/`…），请在仓库根目录运行。

## 规则包预设

| 预设 | 内容 |
|---|---|
| `kigen-tatsujin` 起源战达人 | 女神 01–12，无异相 |
| `kigen-full` 起源战全扩（默认评测环境） | 01–26，无异相 |
| `gachi-tatsujin` 完全战达人 | 01–12 + 异相 |
| `gachi-full` 完全战全扩 | 全部 + 异相 |

中文名别名均可。三拾一舍阶段强制**每方各柱女神不重复**（不会出现同一女神的两个
形态）。跨女神禁用组合表在 `content/combo_bans.json`（`--bans FILE` 可替换）。

## 常用组合

```bash
# 评测环境标准局：两外部智能体
./build/furuyoni-cli --standard --p0-cmd "python3 my_agent.py" --p1-cmd "python3 my_agent.py"

# 特定女神对局（如 只用 刀×薙）
./build/furuyoni-cli --standard --goddesses yurina,saine --random --seed 7

# 完全战（含异相形态）
./build/furuyoni-cli --standard --preset gachi-full --random --seed 7
```

示例智能体：`examples/first_agent.py`（永远选第一个合法项）、
`examples/random_agent.py`（均匀随机）——可直接作为外部智能体的模板。
