#!/usr/bin/env python3
"""TCK-030: throughput і затримка пошуку за кількістю клієнтів (NFR-003).

Для кожного K: прогрів, потім N запитів до /api/search з K паралельними клієнтами.
Throughput = успішні / загальний час. Затримки: медіана, p95, p99.

Використання: bench_throughput.py BASE_URL OUT_CSV [--clients 1,2,4,8,16] [--requests 200] [--query WORD] [--repeats 5]

Кожна серія (K, повтор) — окремий прогін; прогрів один раз на початку. OUT_CSV містить
усі прогони, OUT_CSV_summary.csv — медіану, мінімум і максимум пропускної здатності по K.
Клієнт працює на тому самому хості, що й сервер; для K ≥ 8 це може обмежувати результат.
"""
import argparse
import csv
import json
import statistics
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor


def search(base_url: str, query: str) -> tuple[bool, float]:
    body = json.dumps({"query": query, "mode": "keyword"}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/search", data=body, method="POST",
        headers={"Content-Type": "application/json"},
    )
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            ok = resp.status == 200
            resp.read()
    except Exception:  # помилка фіксується як невдалий запит, без зупинки вимірювання
        ok = False
    return ok, (time.perf_counter() - started) * 1000


def percentile(values: list[float], pct: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(pct / 100 * len(ordered)) - 1))
    return ordered[index]


def run_level(base_url: str, query: str, clients: int, requests: int) -> dict:
    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=clients) as pool:
        results = list(pool.map(lambda _: search(base_url, query), range(requests)))
    total_s = time.perf_counter() - started
    latencies = [ms for _, ms in results]
    successes = sum(1 for ok, _ in results if ok)
    return {
        "clients": clients,
        "total": requests,
        "successful": successes,
        "failed": requests - successes,
        "total_time_s": round(total_s, 3),
        "throughput_rps": round(successes / total_s, 2),
        "median_ms": round(statistics.median(latencies), 2),
        "p95_ms": round(percentile(latencies, 95), 2),
        "p99_ms": round(percentile(latencies, 99), 2),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("out_csv")
    parser.add_argument("--clients", default="1,2,4,8,16")
    parser.add_argument("--requests", type=int, default=200)
    parser.add_argument("--query", default="the")
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()

    levels = [int(c) for c in args.clients.split(",")]
    run_level(args.base_url, args.query, 1, 20)  # прогрів, не враховується
    rows = []
    for repeat in range(1, args.repeats + 1):
        for k in levels:
            row = run_level(args.base_url, args.query, k, args.requests)
            row["repeat"] = repeat
            rows.append(row)

    fields = ["repeat"] + [f for f in rows[0].keys() if f != "repeat"]
    with open(args.out_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    summary = []
    for k in levels:
        runs = [r for r in rows if r["clients"] == k]
        thr = [r["throughput_rps"] for r in runs]
        summary.append({
            "clients": k,
            "repeats": len(runs),
            "throughput_median_rps": round(statistics.median(thr), 2),
            "throughput_min_rps": round(min(thr), 2),
            "throughput_max_rps": round(max(thr), 2),
            "median_ms_median": round(statistics.median(r["median_ms"] for r in runs), 2),
            "p95_ms_median": round(statistics.median(r["p95_ms"] for r in runs), 2),
            "p99_ms_median": round(statistics.median(r["p99_ms"] for r in runs), 2),
            "failed_total": sum(r["failed"] for r in runs),
        })
    summary_path = args.out_csv.rsplit(".", 1)[0] + "_summary.csv"
    with open(summary_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        writer.writerows(summary)
    for row in summary:
        print(row)


if __name__ == "__main__":
    main()
