#!/usr/bin/env python3
"""Обчислює S(p) = T(1) / T(p) і E(p) = S(p) / p з CSV, які bench_index.py записав окремо для кожного p.

Використання: index_speedup.py OUT_CSV IN_CSV [IN_CSV ...]
Має бути рядок з threads=1: без нього прискорення не визначено.
"""
import argparse
import csv


def load_rows(paths: list[str]) -> list[dict]:
    rows = []
    for path in paths:
        with open(path, encoding="utf-8") as handle:
            rows.extend(csv.DictReader(handle))
    return sorted(rows, key=lambda row: int(row["threads"]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_csv")
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    rows = load_rows(args.inputs)
    baseline = next((float(r["median_ms"]) for r in rows if int(r["threads"]) == 1), None)
    if baseline is None:
        raise SystemExit("немає запуску з threads=1: прискорення не визначено")

    out = []
    for row in rows:
        threads = int(row["threads"])
        speedup = baseline / float(row["median_ms"])
        out.append({
            "threads": threads,
            "median_ms": row["median_ms"],
            "min_ms": row["min_ms"],
            "max_ms": row["max_ms"],
            "speedup": round(speedup, 3),
            "efficiency": round(speedup / threads, 3),
            "peak_rss_kb": row["peak_rss_kb"],
        })
    with open(args.out_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(out[0].keys()))
        writer.writeheader()
        writer.writerows(out)
    for row in out:
        print(row)


if __name__ == "__main__":
    main()
