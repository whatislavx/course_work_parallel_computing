#!/usr/bin/env python3
"""Оцінка якості розширення запиту (qwen2.5:0.5b через сервер пошуку).

Для кожного запиту зі словника виконується --repeats звернень до /ws/search (етап
query_expansion). Метрики (по запитах, потім усереднення):
  - success_rate   : частка звернень без помилки;
  - non_empty_rate : частка звернень, де розширення непорожнє;
  - hit_rate       : частка звернень, де є хоча б один еталонний синонім;
  - recall         : частка еталонних синонімів, знайдених у термах;
  - latency        : медіана і p95 часу етапу query_expansion.
Порівняння нормалізоване: нижній регістр і відкидання закінчень множини (s, es),
тож cars враховується як car. Еталон — словник REFERENCE (складено автором).
Додатково з --ollama-url показується сира відповідь моделі на той самий промпт, що й у
сервері, щоб відрізнити слабку відповідь моделі від відкидання парсером.
Запуск: python3 eval_expansion.py --port 8081 --repeats 3 --out results/expansion_eval.csv
"""
import argparse
import csv
import json
import statistics
import urllib.request

from check_expansion import run_query

REFERENCE = {
    "car": ["automobile", "vehicle", "auto"],
    "automobile": ["car", "vehicle", "auto"],
    "mutex": ["lock", "mutual", "exclusion"],
    "semaphore": ["signal", "counter", "lock"],
    "parallel": ["concurrent", "concurrency", "simultaneous"],
    "thread": ["process", "task", "concurrency"],
    "database": ["db", "storage", "sql"],
    "network": ["internet", "connection", "protocol"],
    "memory": ["ram", "storage", "cache"],
    "compiler": ["compile", "translator", "build"],
    "server": ["host", "backend", "service"],
    "algorithm": ["procedure", "method", "computation"],
    "error": ["bug", "fault", "failure"],
    "fast": ["quick", "rapid", "speed"],
    "doctor": ["physician", "medic", "clinician"],
    "teacher": ["instructor", "educator", "professor"],
    "ocean": ["sea", "marine", "water"],
    "music": ["song", "melody", "audio"],
    "london": ["england", "city", "capital"],
    "money": ["cash", "currency", "finance"],
}

PROMPT = ("Suggest up to {n} related English search keywords for the query below. Reply with "
          "comma-separated words only.\nQuery: {q}")


def norm(word):
    w = word.lower().strip()
    if w.endswith("ies") and len(w) > 4:
        return w[:-3] + "y"
    if w.endswith("s") and not w.endswith("ss") and len(w) > 3:
        return w[:-1]
    return w


def hits(terms, reference):
    got = {norm(t) for t in terms}
    return [r for r in reference if norm(r) in got]


def raw_probe(url, model, query, n):
    body = json.dumps({"model": model, "prompt": PROMPT.format(n=n, q=query),
                       "stream": False}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", body,
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read()).get("response", "").strip()


def pct(values, q):
    values = sorted(values)
    if not values:
        return ""
    return values[min(len(values) - 1, int(round(q * (len(values) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--path", default="/ws/search")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--out", default="results/expansion_eval.csv")
    ap.add_argument("--ollama-url", default="", help="напр. http://127.0.0.1:11434 для сирої відповіді")
    ap.add_argument("--model", default="qwen2.5:0.5b")
    ap.add_argument("--max-terms", type=int, default=5)
    a = ap.parse_args()

    rows, all_ms = [], []
    for query, ref in REFERENCE.items():
        ok = nonempty = hit_any = 0
        recalls, ms_list = [], []
        sample = ""
        for _ in range(a.repeats):
            try:
                terms, ms, code = run_query(a.host, a.port, a.path, query, a.timeout)
            except Exception as exc:  # мережева помилка рахується як невдале звернення
                terms, ms, code = None, None, str(exc)
            if code or terms is None:
                continue
            ok += 1
            ms_list.append(ms)
            all_ms.append(ms)
            found = hits(terms, ref)
            nonempty += bool(terms)
            hit_any += bool(found)
            recalls.append(len(found) / len(ref))
            sample = sample or " ".join(terms)
        raw = ""
        if a.ollama_url:
            try:
                raw = raw_probe(a.ollama_url, a.model, query, a.max_terms).replace("\n", " ")[:120]
            except Exception as exc:
                raw = f"помилка: {exc}"
        rows.append({
            "query": query, "calls": a.repeats, "ok": ok, "non_empty": nonempty,
            "hit_any": hit_any,
            "recall": round(statistics.mean(recalls), 2) if recalls else "",
            "median_ms": round(statistics.median(ms_list), 1) if ms_list else "",
            "sample_terms": sample, "raw_model_reply": raw,
        })
        print(f"{query:12s} ok {ok}/{a.repeats} non-empty {nonempty} hit {hit_any} "
              f"recall {rows[-1]['recall']} | {sample} | raw: {raw}")

    total = sum(r["calls"] for r in rows)
    ok_total = sum(r["ok"] for r in rows)
    print("\n=== Підсумок ===")
    print(f"запитів: {len(rows)}, звернень: {total}, успішних: {ok_total}")
    if ok_total:
        print(f"непорожнє розширення: {sum(r['non_empty'] for r in rows) / ok_total:.2%}")
        print(f"є хоча б один еталонний синонім: {sum(r['hit_any'] for r in rows) / ok_total:.2%}")
        rec = [r["recall"] for r in rows if r["recall"] != ""]
        print(f"середній recall: {statistics.mean(rec):.2f}")
        print(f"затримка розширення: медіана {statistics.median(all_ms):.1f} мс, "
              f"p95 {pct(all_ms, 0.95):.1f} мс")
    import os
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    with open(a.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print("CSV:", a.out)


if __name__ == "__main__":
    main()
