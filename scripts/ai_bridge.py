#!/usr/bin/env python3
"""AI 座位桥：引擎的 JSON-lines 协议 ⇄ 文件邮箱（LLM 会话读写）。

用法（经引擎接入）：
  ./build/furuyoni-cli --standard --p0-cmd \
      "python3 scripts/ai_bridge.py --seat 0 --dir matches/m1/p0" ...

邮箱目录布局（每个座位一个）：
  inbox/NNNN.md     第 N 号请求：渲染好的局面 + 编号选项表（LLM 读这个）
  inbox/NNNN.json   原始请求 JSON（需要精确 data 时读这个）
  answer/NNNN.json  LLM 写决策：{"indices":[...], "reason":"一句话理由"}
  inbox/NNNN.retry.md  决策不合法时的问题说明（改好后重写 answer 即可）
  transcript.md     全部请求/决策/理由流水（复盘用）

协议要点见 docs/agent-protocol.md。本桥在本地校验决策合法性（数量/去重/
enabled/越界），不合法会要求重写——协议层面永不判负；决策质量由 LLM 自负。
"""
import argparse
import json
import os
import sys
import time

POLL = 0.4  # answer 轮询间隔（秒）


def render_state(state: dict, seat: int) -> str:
    """把按观看者过滤的观察渲染为 Markdown 表。"""
    if not isinstance(state, dict) or not state:
        return "(无观察数据)"
    lines = []
    me = state.get("players", [None, None])[seat] if len(state.get("players", [])) > seat else None
    opp = state.get("players", [None, None])[1 - seat] if len(state.get("players", [])) > 1 - seat else None

    def row(name, a, b):
        return f"| {name} | {a} | {b} |"

    lines.append("| 区域 | 我方 | 对手 |")
    lines.append("|---|---|---|")
    if me or opp:
        g = lambda p, k: p.get(k, "?") if isinstance(p, dict) else "?"
        lines.append(row("命/装/气/集中力",
                         f"{g(me,'life')}/{g(me,'aura')}/{g(me,'flare')}/{g(me,'vigor')}",
                         f"{g(opp,'life')}/{g(opp,'aura')}/{g(opp,'flare')}/{g(opp,'vigor')}"))
        lines.append(row("手牌数", g(me, "hand_size") if "hand_size" in (me or {}) else len((me or {}).get("hand", [])),
                         g(opp, "hand_size") if "hand_size" in (opp or {}) else len((opp or {}).get("hand", []))))
        lines.append(row("牌山/弃牌/盖牌", f"{g(me,'deck_size')}/{g(me,'discard_size')}/{g(me,'cover_size')}",
                         f"{g(opp,'deck_size')}/{g(opp,'discard_size')}/{g(opp,'cover_size')}"))
    for k in ("distance", "dust", "nearDistance", "near_distance", "turn", "active", "phase"):
        if k in state:
            lines.append(f"- **{k}**: {state[k]}")
    # 自己的手牌明细（观察里有才显示）。
    hand = (me or {}).get("hand")
    if isinstance(hand, list) and hand:
        lines.append("")
        lines.append("**我的手牌**（来自观察）:")
        for i, c in enumerate(hand):
            if isinstance(c, dict):
                lines.append(f"- {c.get('name', c)}")
            else:
                lines.append(f"- {c}")
    return "\n".join(lines)


def render_request(seq: int, req: dict, seat: int) -> str:
    opts = req.get("options", [])
    lines = []
    lines.append(f"# 请求 #{seq:04d}（座位 {seat}）")
    lines.append("")
    lines.append(f"- 回合/提示: **{req.get('prompt','')}**（kind={req.get('kind','')}）")
    lines.append(f"- 选择数量: **{req.get('minSelect',1)} ~ {req.get('maxSelect',1)}** 个")
    lines.append("")
    lines.append("## 局面")
    lines.append(render_state(req.get("state", {}), seat))
    lines.append("")
    lines.append("## 选项（回答填编号）")
    for i, o in enumerate(opts):
        label = o.get("label", "?")
        en = "" if o.get("enabled", True) else " **[不可选]**"
        data = o.get("data")
        extra = ""
        if isinstance(data, dict) and data:
            keys = ("inst", "kind", "side", "zenkai", "full_power", "doubt", "def")
            parts = [f"{k}={data[k]}" for k in keys if k in data]
            if parts:
                extra = "  `" + " ".join(parts) + "`"
        lines.append(f"{i}. {label}{en}{extra}")
    lines.append("")
    lines.append("## 回答方式")
    mn, mx = req.get("minSelect", 1), req.get("maxSelect", 1)
    if mn == 0 and mx == 0:
        lines.append("这是信息型请求（如 reveal 公开）：写 `{\"indices\": [], \"reason\": \"已阅\"}`。")
    else:
        lines.append(f"把 `answer/{seq:04d}.json` 写为：`{{\"indices\": [编号...], \"reason\": \"一句话理由\"}}`"
                     f"（共 {mn}~{mx} 个、不可重复、带 [不可选] 的不能选）。")
    return "\n".join(lines)


def validate(req: dict, indices) -> str:
    """返回 None 表示合法，否则为问题描述。"""
    if not isinstance(indices, list):
        return f"indices 必须是数组，收到: {indices!r}"
    opts = req.get("options", [])
    n = len(opts)
    mn = max(0, int(req.get("minSelect", 1)))
    mx = int(req.get("maxSelect", mn)) if req.get("maxSelect", mn) >= 0 else n
    if len(indices) < mn:
        return f"选择数量不足: 需要 ≥{mn}，收到 {len(indices)}"
    if len(indices) > mx:
        return f"选择数量超限: 需要 ≤{mx}，收到 {len(indices)}"
    if len(set(indices)) != len(indices):
        return "存在重复编号"
    for i in indices:
        if not isinstance(i, int) or i < 0 or i >= n:
            return f"编号越界: {i}（合法范围 0~{n-1}）"
        if not opts[i].get("enabled", True):
            return f"编号 {i}（{opts[i].get('label')}）不可选"
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seat", type=int, required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--timeout", type=float, default=0, help="软超时秒数；到点兜底选第一个合法项（0=无限等）")
    args = ap.parse_args()

    d = args.dir
    os.makedirs(os.path.join(d, "inbox"), exist_ok=True)
    os.makedirs(os.path.join(d, "answer"), exist_ok=True)
    trans_path = os.path.join(d, "transcript.md")
    seq = 0
    with open(trans_path, "a", encoding="utf-8") as tf:
        tf.write(f"\n## 桥启动 seat={args.seat} dir={d} timeout={args.timeout}\n")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = json.loads(line)
        seq += 1
        tag = f"{seq:04d}"
        md = render_request(seq, req, args.seat)
        with open(os.path.join(d, "inbox", f"{tag}.md"), "w", encoding="utf-8") as f:
            f.write(md)
        with open(os.path.join(d, "inbox", f"{tag}.json"), "w", encoding="utf-8") as f:
            json.dump(req, f, ensure_ascii=False, indent=1)
        # 清掉可能的旧答案文件。
        ans_path = os.path.join(d, "answer", f"{tag}.json")

        decision, reason, how = None, "", ""
        deadline = time.time() + args.timeout if args.timeout > 0 else None
        while True:
            if os.path.exists(ans_path):
                time.sleep(0.05)
                try:
                    with open(ans_path, encoding="utf-8") as f:
                        raw = json.load(f)
                except Exception as e:  # JSON 解析失败 → 重试
                    problem = f"answer 不是合法 JSON: {e}"
                else:
                    indices = raw.get("indices") if isinstance(raw, dict) else raw
                    problem = validate(req, indices)
                    if problem is None:
                        decision = indices
                        reason = raw.get("reason", "") if isinstance(raw, dict) else ""
                        how = "llm"
                        break
                retry = os.path.join(d, "inbox", f"{tag}.retry.md")
                with open(retry, "w", encoding="utf-8") as f:
                    f.write(f"# 请求 {tag} 的回答不合法\n\n{problem}\n\n"
                            f"请修正后重写 `answer/{tag}.json`。\n")
                os.remove(ans_path)
            if deadline is not None and time.time() > deadline:
                enabled = [i for i, o in enumerate(req.get("options", [])) if o.get("enabled", True)]
                mn = max(0, int(req.get("minSelect", 1)))
                decision = enabled[:mn]
                reason = "(超时兜底)"
                how = "timeout"
                break
            time.sleep(POLL)

        os.remove(ans_path) if os.path.exists(ans_path) else None
        with open(trans_path, "a", encoding="utf-8") as tf:
            labels = [req.get("options", [{}] * 99)[i].get("label", "?") for i in decision]
            tf.write(f"\n### {tag} [{req.get('kind','')}] {req.get('prompt','')}\n"
                     f"- 决策({how}): {decision} → {labels}\n- 理由: {reason}\n")
        sys.stdout.write(json.dumps({"indices": decision}) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
