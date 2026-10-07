# 散樱乱武 CLI (furuyoni-cli)

《散樱乱武》S10-2 评测环境：一个**受规则严格约束**、确定性、可回放的对局引擎，
用于评测外部智能体的打牌决策。引擎本身不含打牌 AI；全部 26 柱女神 + 最初的决斗
已实现（新幕 S10-2 全扩）。

## 构建

依赖：C++20、CMake ≥ 3.20、pkg-config 的 `lua5.4`、`nlohmann_json`
（sol2 / doctest 已 vendored 于 `third_party/`）。

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 基本用法

```bash
# 内置智能体自战（最初的决斗 / 标准三拾一舍规则包）
./build/furuyoni-cli --random --seed 42
./build/furuyoni-cli --standard --random --seed 42

# 接入外部智能体（JSON-lines 子进程）
./build/furuyoni-cli --standard \
    --p0-cmd "python3 examples/random_agent.py" \
    --p1-cmd "python3 examples/first_agent.py"

# 生成回放 WebUI（自包含 HTML，浏览器打开逐步查看）
./build/furuyoni-cli --standard --random --seed 42 --web /tmp/replay.html
```

命令行参数与规则包细节见 [`docs/running.md`](docs/running.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/running.md`](docs/running.md) | 运行与评测：参数全集、规则包/预设、外部智能体接入 |
| [`docs/ai-play.md`](docs/ai-play.md) | AI 亲自上场：文件邮箱桥、LLM 提示模板、一键开赛 |
| [`docs/agent-protocol.md`](docs/agent-protocol.md) | 智能体协议：JSON-lines 请求/决策、观察信息面 |
| [`docs/replay.md`](docs/replay.md) | 录制/回放/trace/WebUI 检视 |
| [`docs/adding-goddess.md`](docs/adding-goddess.md) | 如何新增女神（Lua 卡面 + 何时写 C++ 机制） |
| [`docs/architecture.md`](docs/architecture.md) | 代码架构与关键设计（Token 系统/结算管线/机制模块） |
| [`docs/testing.md`](docs/testing.md) | 测试体系：单元/模糊/ASan/语料对照方法 |
| [`docs/rulings.md`](docs/rulings.md) | **规则语义裁定的唯一权威**（与 `rules/` 卡面冲突时以裁定为准） |
| [`docs/content-modules.md`](docs/content-modules.md) | 内容模块/包/预设系统 |
| [`docs/refactor-log.md`](docs/refactor-log.md) | 2026-10 大重构日志与破坏性变更清单 |

## 第三方

sol2 3.2.3（MIT，含 GCC16 兼容补丁）与 doctest（MIT）vendored 于 `third_party/`；
运行时依赖系统 `lua5.4` 与 `nlohmann/json`。许可证见 `LICENSE`（AGPL-3.0）。
