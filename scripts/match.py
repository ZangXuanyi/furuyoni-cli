#!/usr/bin/env python3
"""一键开赛（纯 Python，取代 match.sh）：引擎 + 桥 + 赛后通知。

用法:
  python3 scripts/match.py <名称> <种子> <p0目录> <p1目录> [引擎参数...]

例:
  python3 scripts/match.py m1 42 matches/m1/p0 matches/m1/p1 --preset gachi-full

保证：无论引擎正常/非正常退出，都向 p0/ 和 p1/ 各写一个 GAMEOVER 文件——
正常时告知输赢，非正常时告知「游戏非正常退出」。
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "build", "furuyoni-cli")
BRIDGE = os.path.join(ROOT, "scripts", "ai_bridge.py")

GRACE_SECONDS = 3  # 引擎退出后给桥的自行退出宽限期


def write_game_over(seat_dir: str, text: str) -> None:
    path = os.path.join(seat_dir, "GAMEOVER")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.strip() + "\n")
    except OSError:
        pass


def read_result(match_dir: str) -> str:
    """从引擎写的 result.md 或 game.json 取结果文本；都无则返回空。"""
    rp = os.path.join(match_dir, "result.md")
    if os.path.exists(rp):
        try:
            return open(rp, encoding="utf-8").read().strip()
        except OSError:
            pass
    gp = os.path.join(match_dir, "game.json")
    if os.path.exists(gp):
        try:
            g = json.load(open(gp))
            w = g.get("winner")
            if w in (0, 1):
                return f"Player{w} 胜（对手 Player{1 - w}）"
            return "平局（回合上限）"
        except Exception:
            pass
    return ""


def kill_process_group(pid: int) -> None:
    """杀掉整个进程组（引擎 + 由它派生的桥等），忽略已退出的。"""
    try:
        os.killpg(os.getpgid(pid), signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("name", help="比赛名称（matches/<name>/）")
    ap.add_argument("seed", help="随机种子")
    ap.add_argument("p0dir", help="座位 0 的邮箱目录")
    ap.add_argument("p1dir", help="座位 1 的邮箱目录")
    ap.add_argument("engine_args", nargs=argparse.REMAINDER,
                    help="透传给引擎的额外参数（如 --preset gachi-full）")
    args = ap.parse_args()

    out = os.path.join(ROOT, "matches", args.name)
    os.makedirs(out, exist_ok=True)
    for d in (args.p0dir, args.p1dir):
        os.makedirs(os.path.join(d, "inbox"), exist_ok=True)
        os.makedirs(os.path.join(d, "answer"), exist_ok=True)
        # 清掉上一局的 GAMEOVER
        go = os.path.join(d, "GAMEOVER")
        if os.path.exists(go):
            os.remove(go)

    cmd = [
        ENGINE, "--standard",
        "--p0-cmd", f"python3 {BRIDGE} --seat 0 --dir {os.path.abspath(args.p0dir)}",
        "--p1-cmd", f"python3 {BRIDGE} --seat 1 --dir {os.path.abspath(args.p1dir)}",
        "--seed", args.seed,
        "--record", os.path.join(out, "game.json"),
        "--web", os.path.join(out, "replay.html"),
    ] + list(args.engine_args)

    log_path = os.path.join(out, "engine.log")
    print(f"[match] {args.name} seed={args.seed}")
    print(f"[match] p0: {args.p0dir}   p1: {args.p1dir}")
    print(f"[match] 引擎: {' '.join(cmd[:4])} …")

    with open(log_path, "w") as log:
        proc = subprocess.Popen(
            cmd, stdout=log, stderr=subprocess.STDOUT,
            cwd=ROOT, start_new_session=True,  # 独立进程组，方便整体清理
        )
        rc = proc.wait()

    # 给桥一点时间自行退出（引擎退出 → stdin EOF → 桥应自行结束）。
    time.sleep(GRACE_SECONDS)

    # 无论引擎怎么退的，都强制清理残余（整组 SIGKILL，幂等）。
    kill_process_group(proc.pid)

    # 写 GAMEOVER：正常退出告知输赢，非正常告知异常。
    if rc == 0:
        result = read_result(out)
        if result:
            text = result
        else:
            text = "对局结束（引擎正常退出，但未能读取结果）"
    else:
        text = "游戏非正常退出（引擎退出码 " + str(rc) + "）"
    for d in (args.p0dir, args.p1dir):
        write_game_over(d, text)

    print(f"[match] 引擎退出码: {rc}")
    print(f"[match] 结果: {text}")
    print(f"[match] 产物: {out}/replay.html  {out}/game.json  {out}/engine.log")
    for d in (args.p0dir, args.p1dir):
        print(f"[match]   {d}/GAMEOVER")
    return 0


if __name__ == "__main__":
    sys.exit(main())
