# 测试体系

统一 doctest 二进制（`furuyoni-tests`），四个测试文件各司其职：

| 文件 | 职责 |
|---|---|
| `src/tests/tests.cpp` | 基础规则与付与/破绽/再起等定点测试 |
| `src/tests/engine_tests.cpp` | 结构不变量（区域互斥/归属/上下界/**结晶守恒=36**）、对抗性决策（越界/负数/空选永不 UB）、各女神机制回归、**死亡矩阵**（特胜特败/赖着不死/复活优先级）、付与顺序钉死、reveal_cards 信息原语、回放篡改检测、随机自弈模糊 |
| `src/tests/rules_matrix_tests.cpp` | **机械数据审计**：`rules/*.md` 静态攻击数据逐条比对 `content/*.lua`；引擎特判牌名存在性校验；**内容 API 审计**（内容调用的每个 ctx/atk/ev 方法必须已注册——删绑定前必过） |
| `src/tests/{test_util,invariants,agents}.hpp` | 共享设施 |

## 日常

```bash
cmake --build build && ./build/furuyoni-tests          # 全部（~200 用例）
ctest --test-dir build --output-on-failure             # unit + fuzz
FY_FUZZ_GAMES=600 ./build/furuyoni-tests --test-case="invariants*,engine tolerates*"
```

模糊自弈：随机种子 × 随机决策，每局逐步校验不变量；`FY_FUZZ_GAMES` 放大规模。

## ASan/UBSan

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 FY_FUZZ_GAMES=100 ./build-asan/furuyoni-tests
```

## 新旧引擎对照（重构时）

1. `git worktree add /tmp/fy-old <旧提交>` 分别构建；
2. **必须各自在源码树目录下运行**（`content/` 按 cwd 解析，跨目录会产生
   引擎/内容版本错配的怪胎构建）；
3. 对比 `--trace` 的帧序列（含每步观察）与结果：hajimari 语料应逐帧一致，
   standard 语料的分歧应逐一归因到已知裁定变更（见 `refactor-log.md`）。

## 裁定变更的钉死测试

规则裁定落地时同步加测试钉死（示例）：付与新顺序（`付与顺序:*`）、死亡优先级
（`死亡矩阵:*`）、公开/检视（`reveal_cards:*`）。改这些区域前先跑对应用例。
