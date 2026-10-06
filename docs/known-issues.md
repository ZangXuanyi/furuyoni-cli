# 已知简化与遗留事项

规则裁定（含全部已确认语义）集中在 **[`rulings.md`](rulings.md)**；内容模块与预设规则包见
**[`content-modules.md`](content-modules.md)**。本文件只记录**尚未完全实现**或**有意近似**的部分，以及维护须知。

## 1. 有意近似 / 尚未完全实现

| 项 | 现状 | 说明 |
|---|---|---|
| `引用` / `全知全能` 等“以任意顺序” | 按固定的引擎顺序结算 | 不影响合法性，只影响同名时机的选择顺序 |
| `诡辩` 借来的**行动/攻击牌** | 使用后在**对方**弃牌堆（正确）；但 Sealed/封印类交互未特判 | 极端组合下可能出现归属歧义 |
| 卡牌“你可以”类效果 | 已按需求方裁定逐条区分强制/可选（见 rulings.md A.1） | 新增卡面时需明确 |
| `夜叉` | 已按裁定实现为“**对手**的下一次准备阶段少抽一张” | — |
| `提婆` | 已实现“对手弃牌数量变为 0 以外的偶数的那一刻” | — |

## 2. 维护须知

* **新增/修改卡面时**：先更新 `rules/`，再改 `content/*.lua`；`rules_matrix_tests.cpp` 会逐条对比
  静态攻击的距离与伤害，`content names referenced by the engine all exist` 会校验引擎特判的牌名。
* **引擎若要特判某张牌**：牌名常量写在 `src/engine/card_names.hpp`，并在上述测试的名单里登记。
* **事件的 zone 语义**：触发器默认只在“在场”时生效；手牌/弃牌堆触发器需显式写 `zone = "hand"` / `zone = "discard"`。
* **归属**：`CardInstance.holder` 决定牌当前在谁的区里（光环/献/结算归属），`owner` 决定离场后的去向。
* **严格模式**：`Config.strictLua`（默认开）会让第一个 Lua 错误直接抛异常；测试断言每局 `lua_error_count() == 0`。

## 3. 验证套件

```bash
./build/furuyoni-tests                          # 全部（unit）
FY_FUZZ_GAMES=3000 ./build/furuyoni-tests       # 深扫（不变量 + 对抗决策）
ctest --test-dir build --output-on-failure
# sanitizer
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 ./build-asan/furuyoni-tests
```
