#!/usr/bin/env python3
"""Summarize benchmark JSON emitted by ``mdfh_benchmark --json <file>``.

The heavy lifting (feed generation, parsing, book reconstruction, timing) is all
done in C++. This script is deliberately thin: it reads one or more result files
and prints a human-readable latency/throughput table, optionally as Markdown so
the numbers can be pasted straight into the README.

Usage:
    python3 tools/analyze_bench.py results.json [more.json ...]
    python3 tools/analyze_bench.py --markdown results.json
"""
import argparse
import json
import sys
from typing import Any, Dict, List


def load(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def fmt_int(n: float) -> str:
    return f"{int(n):,}"


def summarize(rows: List[Dict[str, Any]], markdown: bool) -> str:
    headers = [
        "file", "messages", "symbols", "ST Mmsg/s", "avg ns",
        "p50", "p95", "p99", "MT Mmsg/s", "speedup", "deterministic",
    ]
    lines = []
    for r in rows:
        st = r["data"]["single_threaded"]
        mt = r["data"]["concurrent"]
        st_mps = st["throughput_msg_per_s"] / 1e6
        mt_mps = mt["throughput_msg_per_s"] / 1e6
        speedup = mt_mps / st_mps if st_mps else 0.0
        lines.append([
            r["name"],
            fmt_int(r["data"]["messages"]),
            str(r["data"]["symbols"]),
            f"{st_mps:.3f}",
            f"{st['avg_latency_ns']:.0f}",
            fmt_int(st["p50_ns"]),
            fmt_int(st["p95_ns"]),
            fmt_int(st["p99_ns"]),
            f"{mt_mps:.3f}",
            f"{speedup:.2f}x",
            "yes" if mt["deterministic"] else "NO",
        ])

    if markdown:
        out = ["| " + " | ".join(headers) + " |",
               "| " + " | ".join("---" for _ in headers) + " |"]
        for row in lines:
            out.append("| " + " | ".join(row) + " |")
        return "\n".join(out)

    widths = [max(len(h), *(len(row[i]) for row in lines)) for i, h in enumerate(headers)]
    def fmt_row(cells):
        return "  ".join(c.ljust(widths[i]) for i, c in enumerate(cells))
    out = [fmt_row(headers), fmt_row(["-" * w for w in widths])]
    out += [fmt_row(row) for row in lines]
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("files", nargs="+", help="benchmark JSON files")
    ap.add_argument("--markdown", action="store_true", help="emit a Markdown table")
    args = ap.parse_args()

    rows = []
    for path in args.files:
        try:
            rows.append({"name": path.split("/")[-1], "data": load(path)})
        except (OSError, json.JSONDecodeError) as exc:
            print(f"skipping {path}: {exc}", file=sys.stderr)
    if not rows:
        return 1
    print(summarize(rows, args.markdown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
