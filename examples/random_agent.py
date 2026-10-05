#!/usr/bin/env python3
"""Minimal JSON-lines agent: picks enabled options uniformly at random."""
import json
import random
import sys


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = json.loads(line)
        want = int(req.get("minSelect", 1))
        enabled = [i for i, o in enumerate(req["options"]) if o.get("enabled", True)]
        random.shuffle(enabled)
        sel = enabled[:want] if want > 0 else []
        sys.stdout.write(json.dumps({"indices": sel}) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
