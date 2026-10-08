#!/usr/bin/env bash
# 一键开赛：引擎 + 两个 AI 座位桥，自动录制与 WebUI。
#
# 用法:
#   ./scripts/match.sh <名称> <种子> <p0邮箱目录> <p1邮箱目录> [额外引擎参数...]
# 例:
#   ./scripts/match.sh practice1 42 matches/practice1/p0 matches/practice1/p1
#
# 规则集默认 kigen-full（起源战全扩，评测标准环境）。
# 每个座位目录由 scripts/ai_bridge.py 自动创建；给 LLM 会话的手册见 docs/ai-play.md。
set -euo pipefail
cd "$(dirname "$0")/.."

NAME="${1:?用法: match.sh <名称> <种子> <p0目录> <p1目录> [引擎参数...]}"
SEED="${2:?缺少种子}"
P0DIR="${3:?缺少 p0 邮箱目录}"
P1DIR="${4:?缺少 p1 邮箱目录}"
shift 4 || true

OUT="matches/$NAME"
mkdir -p "$OUT" "$P0DIR" "$P1DIR"

echo "[match] $NAME seed=$SEED"
echo "[match] p0 邮箱: $P0DIR   (把该目录交给座位 0 的 Agent)"
echo "[match] p1 邮箱: $P1DIR   (把该目录交给座位 1 的 Agent)"

./build/furuyoni-cli --standard \
    --p0-cmd "python3 scripts/ai_bridge.py --seat 0 --dir $P0DIR --match-dir $OUT" \
    --p1-cmd "python3 scripts/ai_bridge.py --seat 1 --dir $P1DIR --match-dir $OUT" \
    --seed "$SEED" \
    --record "$OUT/game.json" --web "$OUT/replay.html" \
    "$@" 2>&1 | tee "$OUT/engine.log"

# 决出胜负后：把结果写进双方座位目录（Agent 侧也能看到）。
# 结果从决策日志 game.json 提取（自带 winner 字段），不依赖引擎 stdout——
# 引擎被管道/中断夺走 stdout 时也能正确播报。
RESULT=$(python3 - "$OUT/game.json" <<'PYEOF'
import json, sys
try:
    g = json.load(open(sys.argv[1]))
except Exception as e:
    print(f"（无法读取决策日志: {e}）"); raise SystemExit
w = g.get("winner")
if w == -1:
    print("平局（回合上限）")
elif w in (0, 1):
    print(f"Player{w} 胜（对手 Player{1 - w}）")
else:
    print(f"异常结束（winner={w}，详见 engine.log）")
print(f"回合数: {g.get('turn', '?')}　决策数: {len(g.get('entries', []))}")
PYEOF
) || RESULT="（结果提取失败，见 engine.log / game.json）"
for SD in "$P0DIR" "$P1DIR"; do
  printf '# 对局结果\n\n%s\n\n完整复盘: %s/replay.html；决策日志: %s/game.json\n' \
    "$RESULT" "$OUT" "$OUT" > "$SD/result.md"
  # 桥在协议流优雅关闭时会自己写；这里兜底（幂等）——覆盖被硬杀的情况。
  printf '# 对局结束\n\n%s\n详细内容见本目录 result.md 与 transcript.md。\n' \
    "$RESULT" > "$SD/inbox/GAME-OVER.md" 2>/dev/null || true
done
echo "[match] $RESULT"

echo
echo "[match] 完成。产物:"
echo "  WebUI 回放: $OUT/replay.html"
echo "  决策日志:   $OUT/game.json"
echo "  座位转录:   $P0DIR/transcript.md / $P1DIR/transcript.md"
echo "  结果提示:   $P0DIR/result.md / $P1DIR/result.md（含 GAME-OVER.md）"
