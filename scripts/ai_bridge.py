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
import atexit
import json
import os
import signal
import sys
import time

POLL = 0.4  # answer 轮询间隔（秒）


def _names(lst):
    return [x if isinstance(x, str) else str(x.get("name", x)) if isinstance(x, dict) else str(x)
            for x in lst] if lst else []


def render_state(state: dict, seat: int) -> str:
    """把按观看者过滤的观察渲染为完整 Markdown（引擎观察字段见 engine_observe.cpp）。"""
    if not isinstance(state, dict) or not state:
        return "(无观察数据)"
    L = []
    ps = state.get("players", [])
    me = ps[seat] if len(ps) > seat else {}
    op = ps[1 - seat] if len(ps) > 1 - seat else {}

    def num(p, k):
        v = p.get(k, "?")
        return v if not isinstance(v, list) else len(v)

    # 公开基本面
    L.append("## 公开局面")
    L.append("| 区域 | 我方 | 对手 |")
    L.append("|---|---|---|")
    L.append(f"| 命/装/冰晶/诅咒 | {num(me,'life')}/{num(me,'aura')}/{num(me,'ice')}/{num(me,'curse')} |"
             f" {num(op,'life')}/{num(op,'aura')}/{num(op,'ice')}/{num(op,'curse')} |")
    L.append(f"| 气/集中力{'/畏缩' if me.get('cower') or op.get('cower') else ''} |"
             f" {num(me,'flare')}/{num(me,'vigor')}{'/畏缩' if me.get('cower') else ''} |"
             f" {num(op,'flare')}/{num(op,'vigor')}{'/畏缩' if op.get('cower') else ''} |")
    L.append(f"| 手牌/牌山/弃牌/盖牌 数量 | {num(me,'handCount')}/{num(me,'deckCount')}/"
             f"{num(me,'discardCount', ) if 'discardCount' in me else len(me.get('discard',[]))}/"
             f"{num(me,'coverCount')} | {num(op,'handCount')}/{num(op,'deckCount')}/"
             f"{len(op.get('discard',[]))}/{num(op,'coverCount')} |")
    for k, cn in [("market", "股市"), ("waku", "惑"), ("aim", "瞄准点"),
                  ("seeds", "种子"), ("plants", "植株"), ("node", "地图节点"),
                  ("stockPrice", "股价")]:
        if (isinstance(me, dict) and me.get(k) not in (None, 0, -1, "")) or            (isinstance(op, dict) and op.get(k) not in (None, 0, -1, "")):
            L.append(f"| {cn} | {me.get(k, 0)} | {op.get(k, 0)} |")
    L.append("")
    L.append(f"距 **{state.get('distance','?')}** / 虚 **{state.get('dust','?')}** / "
             f"近身距离 **{state.get('nearDistance','?')}** / "
             f"第 **{state.get('turn','?')}** 回合，行动方 P{state.get('active','?')}")

    # 付与区（公开，含献数）
    L.append("")
    L.append("## 付与区（公开；括号内为献）")
    for tag, p in [("我方", me), ("对手", op)]:
        enh = p.get("enhance", [])
        if enh:
            cells = []
            for e in enh:
                cry = e.get("crystals", 0)
                grn = e.get("green", 0)
                extra = f"+{grn}绿" if grn else ""
                cells.append(f"{e.get('name','?')}（{cry}{extra}）")
            L.append(f"- {tag}: " + "、".join(cells))
        else:
            L.append(f"- {tag}: （无）")

    # 切牌区：使用后的公开；未使用的仅自己可见
    L.append("")
    L.append("## 切牌区")
    for tag, p in [("我方", me), ("对手", op)]:
        cells = []
        for c in p.get("special", []):
            if c.get("hidden"):
                continue
            cells.append(f"{c.get('name','?')}（{'使用后' if c.get('used') else '未使用'}）")
        if p.get("barracks"):
            for b in p["barracks"]:
                cells.append(f"[兵舍]{b.get('name','?')}（{'已动员' if b.get('mobilized') else '未动员'}）")
        L.append(f"- {tag}: " + ("、".join(cells) if cells else "（无可见）"))

    # 弃牌堆（公开）
    L.append("")
    L.append("## 弃牌堆（公开）")
    L.append(f"- 我方: {'、'.join(_names(me.get('discard'))) or '（空）'}")
    L.append(f"- 对手: {'、'.join(_names(op.get('discard'))) or '（空）'}")

    # 私有信息：手牌 / 盖牌堆 / 牌山 / 毒袋等
    L.append("")
    L.append("## 我的私有信息")
    L.append(f"- 手牌: {'、'.join(_names(me.get('hand'))) or '（空）'}")
    L.append(f"- 盖牌堆: {'、'.join(_names(me.get('cover'))) or '（空）'}")
    deck = _names(me.get('deck'))
    if deck:
        L.append(f"- 牌山（顶→底，共 {len(deck)}）: {'、'.join(deck)}")
    if me.get("bag"):
        L.append(f"- 毒袋: {'、'.join(_names(me.get('bag')))}")
    if me.get("memory"):
        L.append(f"- 回忆区: {'、'.join(_names(me.get('memory')))}")
    if me.get("wounds"):
        w = me.get("wounds")
        parts = [f"{cn}{w.get(k,[0,0])}" for k, cn in [("aura","装"),("flare","气"),("life","命")]
                 if isinstance(w, dict) and any(w.get(k,[0,0]))]
        if parts:
            L.append(f"- 裂伤: {'、'.join(parts)}（数组=[P0造成, P1造成]）")
    return "\n".join(L)


def render_attack(data: dict) -> str:
    """对应窗口携带的被对应攻击摘要（Request.data.attack）。"""
    atk = data.get("attack") if isinstance(data, dict) else None
    if not atk:
        return ""
    kws = "、".join(atk.get("keywords", [])) or "无"
    return (f"\n## ⚔ 正在对应的攻击\n- **{atk.get('source','?')}** 【{atk.get('range','?')} "
            f"{atk.get('damage','?')}】 词条: {kws}（攻击方 P{atk.get('attacker','?')}）\n")


def render_request(seq: int, req: dict, seat: int) -> str:
    opts = req.get("options", [])
    lines = []
    lines.append(f"# 请求 #{seq:04d}（座位 {seat}）")
    lines.append("")
    lines.append(f"- 回合/提示: **{req.get('prompt','')}**（kind={req.get('kind','')}）")
    lines.append(f"- 选择数量: **{req.get('minSelect',1)} ~ {req.get('maxSelect',1)}** 个")
    lines.append("")
    lines.append(render_attack(req.get("data", {})))
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


_cleanup_dir = None  # 桥的座位目录（atexit / 信号处理器用）


def _find_result_text(d: str) -> str:
    """从比赛目录（--match-dir 或座位目录的父目录）取引擎写的 result.md。"""
    cands = []
    env_md = os.environ.get("FY_MATCH_DIR", "")
    if env_md:
        cands.append(env_md)
    cands.append(os.path.dirname(os.path.abspath(d)))
    for cand in cands:
        rp = os.path.join(cand, "result.md")
        if os.path.exists(rp):
            try:
                return open(rp, encoding="utf-8").read().strip()
            except OSError:
                pass
    return ""


def _write_game_over(d: str) -> None:
    """写 result.md + GAME-OVER.md（幂等；信号处理器与 EOF 路径共用）。"""
    if d is None:
        return
    text = _find_result_text(d)
    try:
        if text:
            with open(os.path.join(d, "result.md"), "w", encoding="utf-8") as f:
                f.write(text + "\n")
        with open(os.path.join(d, "inbox", "GAME-OVER.md"), "w", encoding="utf-8") as f:
            if text:
                f.write("# 对局结束\n\n" + text + "\n\n"
                        "复盘见比赛目录 replay.html；完整决策流水在 transcript.md。\n")
            else:
                f.write("# 对局结束\n\n引擎已关闭本座位的协议流。\n"
                        "结果见比赛目录 result.md / replay.html。\n")
    except OSError:
        pass


def _on_signal(signum, frame):
    _write_game_over(_cleanup_dir)
    sys.exit(0)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seat", type=int, required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--timeout", type=float, default=0, help="软超时秒数；到点兜底选第一个合法项（0=无限等）")
    ap.add_argument("--match-dir", default="", help="比赛输出目录（含引擎写的 result.md；桥退出时自动取回并写入座位目录）")
    args = ap.parse_args()

    d = os.path.abspath(args.dir)
    global _cleanup_dir
    _cleanup_dir = d
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, _on_signal)
    atexit.register(lambda: _write_game_over(d))
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

    # stdin 关闭 = 对局结束。
    _write_game_over(d)


if __name__ == "__main__":
    main()
