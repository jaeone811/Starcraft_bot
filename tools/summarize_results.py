#!/usr/bin/env python3
"""Summarize BeginnerBot's results.csv per difficulty (and map / enemy race).

Usage:
    python tools/summarize_results.py "C:/StarCraft/bwapi-data/write/BeginnerBot/results.csv"

Only the Python standard library is used.
"""
import csv
import sys
from collections import defaultdict


def mean(values):
    return sum(values) / len(values) if values else 0.0


def main(path):
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        print("no games recorded")
        return 1

    groups = defaultdict(list)
    for r in rows:
        groups[(r["difficulty"], r["map"], r["enemy_race"])].append(r)

    header = ("diff", "map", "enemy", "games", "win%", "avg min", "max wrk", "minerals",
              "supply blk s", "idle rax s", "waves", "failed", "stuck", "unit stuck", "cmd fail", "exc")
    print(" | ".join(header))
    for (diff, map_name, enemy), games in sorted(groups.items()):
        num = lambda key: [float(g[key]) for g in games]
        wins = sum(1 for g in games if g["result"] == "win")
        print(" | ".join(str(x) for x in (
            diff, map_name, enemy, len(games),
            f"{100.0 * wins / len(games):.0f}",
            f"{mean(num('frames')) / 24 / 60:.1f}",
            f"{mean(num('max_workers')):.1f}",
            f"{mean(num('gathered_minerals')):.0f}",
            f"{mean(num('supply_blocked_frames')) / 24:.0f}",
            f"{mean(num('idle_production_frames')) / 24:.0f}",
            f"{mean(num('attack_waves')):.1f}",
            f"{mean(num('failed_waves')):.1f}",
            f"{mean(num('stuck_events')):.1f}",
            f"{mean(num('unit_stuck_events')):.1f}",
            f"{mean(num('command_failures')):.0f}",
            f"{mean(num('exceptions')):.0f}",
        )))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
