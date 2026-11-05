#!/usr/bin/env python3
"""Collects the client logs written by run-experiment.sh into two CSV files.

  experiments/<exp>/results/runs.csv     one row per run
  experiments/<exp>/results/summary.csv  one row per (variant, total, delay):
                                         reliability and median / min / max
                                         score over all seeds and runs

Usage (from the repo root, in either VM or on any machine with Python 3):
  python3 experiments/summarize.py concurrency
"""
import csv
import statistics
import sys
from pathlib import Path

EXPERIMENTS = Path(__file__).resolve().parent


def parse(path):
    """Returns a dict with the header settings and the final results of one log."""
    lines = path.read_text(errors="replace").splitlines()
    if not lines or not lines[0].startswith("# "):
        return None
    row = dict(item.split("=", 1) for item in lines[0][2:].split() if "=" in item)
    row["file"] = path.name
    results = [l for l in lines if l.startswith("Results:")]
    if results:
        # Client prints: "Results: <reliability %> <score>"
        _, reliability, score = results[-1].split()[:3]
        row["reliability"] = float(reliability)
        row["score"] = int(score)
    else:
        row["reliability"] = None  # unfinished run (client crashed / was stopped)
        row["score"] = None
    return row


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 experiments/summarize.py <experiment>   e.g. concurrency")
    results = EXPERIMENTS / sys.argv[1] / "results"
    rows = [r for r in (parse(p) for p in sorted(results.glob("*.txt"))) if r]
    if not rows:
        sys.exit(f"No result files in {results}")

    fields = ["exp", "variant", "run", "seed", "total", "delay", "lambda", "rep",
              "difficulty", "reliability", "score", "commit", "date", "file"]
    with open(results / "runs.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    groups = {}
    for r in rows:
        groups.setdefault((r["variant"], int(r["total"]), int(r["delay"])), []).append(r)

    with open(results / "summary.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["variant", "total", "delay", "runs", "unfinished",
                    "min_reliability", "median_score", "min_score", "max_score"])
        for (variant, total, delay), rs in sorted(groups.items()):
            done = [r for r in rs if r["score"] is not None]
            scores = [r["score"] for r in done]
            w.writerow([
                variant, total, delay, len(rs), len(rs) - len(done),
                min((r["reliability"] for r in done), default=""),
                int(statistics.median(scores)) if scores else "",
                min(scores, default=""), max(scores, default=""),
            ])

    print((results / "summary.csv").read_text())
    print(f"Wrote {results / 'runs.csv'} and {results / 'summary.csv'}")


if __name__ == "__main__":
    main()