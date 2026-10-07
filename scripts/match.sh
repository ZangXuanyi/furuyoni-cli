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
    --p0-cmd "python3 scripts/ai_bridge.py --seat 0 --dir $P0DIR" \
    --p1-cmd "python3 scripts/ai_bridge.py --seat 1 --dir $P1DIR" \
    --seed "$SEED" \
    --record "$OUT/game.json" --web "$OUT/replay.html" \
    "$@" 2>&1 | tee "$OUT/engine.log"

echo
echo "[match] 完成。产物:"
echo "  WebUI 回放: $OUT/replay.html"
echo "  决策日志:   $OUT/game.json"
echo "  座位转录:   $P0DIR/transcript.md / $P1DIR/transcript.md"
