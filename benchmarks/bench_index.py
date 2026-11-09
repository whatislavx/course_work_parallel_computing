#!/usr/bin/env python3
"""TCK-029: бенчмарк побудови індексу (NFR-001, NFR-002, NFR-023).

Вимірює T(p) для набору потоків. S(p) і E(p) рахує index_speedup.py з CSV усіх p разом,
бо T(1) має бути з окремого запуску. Прогрів (1 запуск) не враховується; далі R повторів.
Пік RSS читається з /proc/<pid>/status.

Використання: bench_index.py BASE_URL SERVER_PID OUT_CSV [--threads 1,2,4] [--repeats 5]
"""
import argparse
import csv
import json
import statistics
import time
import urllib.request


def build(base_url: str, threads: int) -> dict:
    body = json.dumps({"threads": threads}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/index", data=body, method="POST",
        headers={"Content-Type": "application/json"},
    )
    started = time.perf_counter()
    with urllib.request.urlopen(req, timeout=3600) as resp:
        payload = json.loads(resp.read())
    wall_ms = (time.perf_counter() - started) * 1000
    return {"time_ms": payload["time_ms"], "wall_ms": wall_ms, "docs": payload["documents_indexed"]}


def peak_rss_kb(pid: int) -> int:
    with open(f"/proc/{pid}/status", encoding="ascii") as handle:
        for line in handle:
            if line.startswith("VmHWM:"):
                return int(line.split()[1])
    return -1


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("server_pid", type=int)
    parser.add_argument("out_csv")
    parser.add_argument("--threads", default="1,2,4")
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()

    thread_set = [int(t) for t in args.threads.split(",")]
    build(args.base_url, thread_set[0])  # прогрів, не враховується
    rows = []
    for threads in thread_set:
        samples = [build(args.base_url, threads) for _ in range(args.repeats)]
        times = [s["time_ms"] for s in samples]
        rows.append({
            "threads": threads,
            "docs": samples[0]["docs"],
            "median_ms": statistics.median(times),
            "min_ms": min(times),
            "max_ms": max(times),
            "wall_median_ms": statistics.median(s["wall_ms"] for s in samples),
            "peak_rss_kb": peak_rss_kb(args.server_pid),
        })
    with open(args.out_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        print(row)


if __name__ == "__main__":
    main()
