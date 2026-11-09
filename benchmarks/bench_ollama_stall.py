#!/usr/bin/env python3
"""Сценарний тест каскаду відмови при зависанні Ollama (FR-021).

Сценарії:
  ok   — розширені запити (mode=expanded) при здоровій Ollama: базова пропускна здатність.
  hang — ті самі розширені запити при зависанні Ollama, плюс паралельні звичайні запити
         (mode=keyword) і /api/health. Вимірюється, чи блокуються звичайні запити через
         вичерпання слотів з'єднань (MAX_CONCURRENT_CONNECTIONS).

Метрики на групу: total, ok (200), busy (503 server_busy), other, throughput успішних/с,
медіана і p95 затримки. Затримки ok-запитів окремо від відмов.

Використання:
  bench_ollama_stall.py BASE_URL --scenario ok|hang [--stub URL] [--requests 300] [--clients 80]
                        [--probe-seconds 0] [--out bench_out/ollama_stall_ok.csv]
  --stub URL: якщо задано, перед сценарієм перемикає заглушку (POST URL/__mode).
"""
import argparse
import csv
import json
import statistics
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

TIMEOUT_S = 30


def set_stub_mode(stub_url: str, mode: str) -> None:
    req = urllib.request.Request(
        f"{stub_url}/__mode", data=json.dumps({"mode": mode}).encode(), method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
        resp.read()


def call(base_url: str, path: str, body: dict | None) -> tuple[int, float]:
    data = json.dumps(body).encode() if body is not None else None
    method = "POST" if body is not None else "GET"
    req = urllib.request.Request(
        f"{base_url}{path}", data=data, method=method,
        headers={"Content-Type": "application/json"},
    )
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            resp.read()
            status = resp.status
    except urllib.error.HTTPError as exc:
        exc.read()
        status = exc.code
    except (urllib.error.URLError, TimeoutError, OSError):
        status = -1
    return status, (time.perf_counter() - started) * 1000


def summarize(name: str, results: list[tuple[int, float]], wall_s: float) -> dict:
    ok_lat = sorted(lat for status, lat in results if status == 200)
    busy = sum(1 for status, _ in results if status == 503)
    other = len(results) - len(ok_lat) - busy
    def pct(q):
        if not ok_lat:
            return float("nan")
        idx = min(len(ok_lat) - 1, int(round(q * (len(ok_lat) - 1))))
        return ok_lat[idx]
    return {
        "group": name,
        "total": len(results),
        "ok": len(ok_lat),
        "busy_503": busy,
        "other": other,
        "throughput_ok_per_s": round(len(ok_lat) / wall_s, 2) if wall_s > 0 else 0.0,
        "median_ms": round(statistics.median(ok_lat), 1) if ok_lat else float("nan"),
        "p95_ms": round(pct(0.95), 1),
    }


def run_expanded(base_url: str, requests: int, clients: int) -> tuple[list, float]:
    body = {"query": "london", "mode": "expanded"}
    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=clients) as pool:
        results = list(pool.map(lambda _: call(base_url, "/api/search", body), range(requests)))
    return results, time.perf_counter() - started


def probe_keyword(base_url: str, stop: threading.Event, out: list) -> None:
    body = {"query": "london", "mode": "keyword"}
    while not stop.is_set():
        out.append(call(base_url, "/api/search", body))
        time.sleep(0.2)


def probe_health(base_url: str, stop: threading.Event, out: list) -> None:
    while not stop.is_set():
        out.append(call(base_url, "/api/health", None))
        time.sleep(0.5)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("--scenario", choices=["ok", "hang"], required=True)
    parser.add_argument("--stub", default="")
    parser.add_argument("--requests", type=int, default=300)
    parser.add_argument("--clients", type=int, default=80)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    if args.stub:
        set_stub_mode(args.stub, "ok" if args.scenario == "ok" else "hang")
    # Прогрів: один розширений запит, не враховується.
    call(args.base_url, "/api/search", {"query": "london", "mode": "expanded"})

    rows = []
    if args.scenario == "ok":
        results, wall = run_expanded(args.base_url, args.requests, args.clients)
        rows.append(summarize("expanded", results, wall))
    else:
        stop = threading.Event()
        keyword_out: list = []
        health_out: list = []
        threads = [
            threading.Thread(target=probe_keyword, args=(args.base_url, stop, keyword_out)),
            threading.Thread(target=probe_health, args=(args.base_url, stop, health_out)),
        ]
        for t in threads:
            t.start()
        results, wall = run_expanded(args.base_url, args.requests, args.clients)
        stop.set()
        for t in threads:
            t.join()
        rows.append(summarize("expanded", results, wall))
        rows.append(summarize("keyword_during_hang", keyword_out, wall))
        rows.append(summarize("health_during_hang", health_out, wall))

    header = list(rows[0].keys())
    print("\t".join(header))
    for row in rows:
        print("\t".join(str(row[k]) for k in header))
    if args.out:
        with open(args.out, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
