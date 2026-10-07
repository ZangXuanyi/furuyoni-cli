#!/usr/bin/env python3
"""哑回答器：轮询一个邮箱目录，对每个请求自动写"前 minSelect 个合法项"。

用途：
  1. 无 LLM 冒烟联调整条邮箱链路（两个桥 + 本脚本打满一整局）；
  2. 练习赛陪打（AI 亲自上时，对手座位可挂本脚本热身）；
  3. 决策格式的参考实现（LLM 可参考它写 answer）。

用法: python3 scripts/auto_answer.py --dir matches/m1/p0 [--policy first|random]
按 Ctrl-C 退出。
"""
import argparse
import json
import os
import random
import time


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--policy", choices=["first", "random"], default="first")
    ap.add_argument("--seed", type=int, default=12345)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    inbox = os.path.join(args.dir, "inbox")
    answer = os.path.join(args.dir, "answer")
    os.makedirs(inbox, exist_ok=True)
    os.makedirs(answer, exist_ok=True)
    print(f"[auto_answer] watching {inbox} (policy={args.policy})")
    while True:
        for name in sorted(os.listdir(inbox)):
            if not name.endswith(".json"):
                continue
            tag = name[:-5]
            ans = os.path.join(answer, f"{tag}.json")
            if os.path.exists(ans):
                continue
            try:
                with open(os.path.join(inbox, name), encoding="utf-8") as f:
                    req = json.load(f)
            except Exception:
                continue
            enabled = [i for i, o in enumerate(req.get("options", []))
                       if o.get("enabled", True)]
            mn = max(0, int(req.get("minSelect", 1)))
            sel = enabled[:mn]
            if args.policy == "random" and len(enabled) > mn:
                sel = rng.sample(enabled, mn)
            tmp = ans + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"indices": sel, "reason": "(auto)"}, f, ensure_ascii=False)
            os.replace(tmp, ans)
            print(f"[auto_answer] {tag}: {sel}")
        time.sleep(0.3)


if __name__ == "__main__":
    main()
