#!/usr/bin/env python3
"""Перевірка етапу query_expansion через WebSocket /ws/search (FR-019, FR-021).

Для кожного запиту відкриває WebSocket-з'єднання, надсилає {"query": ...}, читає кадри до
етапу done або error і фіксує:
  - час від надсилання до етапу query_expansion (мс);
  - список expanded_terms;
  - частку еталонних синонімів, знайдених серед термів (recall за словником REFERENCE).

Словник REFERENCE обирає автор для синтетичного корпусу. Це не об'єктивна міра якості:
вона показує лише, чи модель пропонує очікувані відповідники.

Використання:
  check_expansion.py [--host 127.0.0.1] [--port 8080] [--queries "car,mutex"] [--out result.csv]

Лише стандартна бібліотека Python: клієнт WebSocket за RFC 6455 реалізовано вручну.
"""
import argparse
import base64
import csv
import json
import os
import socket
import statistics
import struct
import time

REFERENCE = {
    "car": ["automobile", "vehicle"],
    "automobile": ["car", "vehicle"],
    "mutex": ["lock"],
    "semaphore": ["signal", "counter"],
    "parallel": ["concurrent", "concurrency"],
    "thread pool": ["threads", "worker"],
}


def recv_exact(sock, count):
    buf = bytearray()
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("з'єднання закрито")
        buf.extend(chunk)
    return bytes(buf)


def handshake(host, port, path, timeout):
    sock = socket.create_connection((host, port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(request.encode("ascii"))
    head = b""
    while b"\r\n\r\n" not in head:
        chunk = sock.recv(1)  # по байту, щоб не зчитати кадри після заголовків
        if not chunk:
            sock.close()
            raise ConnectionError("рукостискання перервано")
        head += chunk
    status_line = head.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if " 101 " not in status_line:
        sock.close()
        raise ConnectionError(f"рукостискання відхилено: {status_line}")
    return sock


def send_frame(sock, opcode, payload):
    header = bytearray([0x80 | opcode])
    length = len(payload)
    if length < 126:
        header.append(0x80 | length)
    elif length < 65536:
        header.append(0x80 | 126)
        header += struct.pack(">H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", length)
    mask = os.urandom(4)  # клієнтські кадри мають бути замасковані (RFC 6455, 5.3)
    header += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(bytes(header) + masked)


def recv_message(sock):
    parts = []
    while True:
        b1, b2 = recv_exact(sock, 2)
        fin = b1 & 0x80
        opcode = b1 & 0x0F
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack(">H", recv_exact(sock, 2))[0]
        elif length == 127:
            length = struct.unpack(">Q", recv_exact(sock, 8))[0]
        payload = recv_exact(sock, length) if length else b""
        if opcode == 0x9:  # ping -> pong
            send_frame(sock, 0xA, payload)
            continue
        if opcode == 0x8:
            raise ConnectionError("сервер закрив з'єднання")
        if opcode in (0x0, 0x1):
            parts.append(payload)
            if fin:
                return b"".join(parts).decode("utf-8")


def run_query(host, port, path, query, timeout):
    """Повертає (expanded_terms | None, expansion_ms | None, code, error_text)."""
    sock = handshake(host, port, path, timeout)
    try:
        started = time.perf_counter()
        send_frame(sock, 0x1, json.dumps({"query": query}).encode("utf-8"))
        expansion, expansion_ms, code = None, None, ""
        while True:
            message = json.loads(recv_message(sock))
            stage = message.get("stage")
            data = message.get("data", {})
            if stage == "query_expansion":
                expansion_ms = (time.perf_counter() - started) * 1000
                expansion = data.get("expanded_terms", [])
            elif stage == "error":
                code = data.get("code", "error")
                break
            elif stage == "done":
                break
        return expansion, expansion_ms, code
    finally:
        sock.close()


def evaluate(query, expansion, expansion_ms, code):
    terms = [t.lower() for t in (expansion or [])]
    reference = REFERENCE.get(query, [])
    hits = [r for r in reference if r in terms]
    recall = len(hits) / len(reference) if reference else ""
    return {
        "query": query,
        "status": code if code else "ok",
        "expansion_ms": round(expansion_ms, 1) if expansion_ms is not None else "",
        "n_terms": len(terms),
        "terms": " ".join(terms),
        "reference_hits": len(hits),
        "reference_total": len(reference),
        "recall": round(recall, 2) if recall != "" else "",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--path", default="/ws/search")
    parser.add_argument("--queries", default=",".join(REFERENCE.keys()))
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--out", default="")
    args = parser.parse_args()

    rows = []
    for query in [q.strip() for q in args.queries.split(",") if q.strip()]:
        try:
            expansion, expansion_ms, code = run_query(args.host, args.port, args.path, query, args.timeout)
            rows.append(evaluate(query, expansion, expansion_ms, code))
        except (OSError, ConnectionError, ValueError) as exc:
            rows.append({"query": query, "status": f"transport: {exc}", "expansion_ms": "",
                         "n_terms": 0, "terms": "", "reference_hits": 0,
                         "reference_total": len(REFERENCE.get(query, [])), "recall": ""})

    header = list(rows[0].keys())
    print("\t".join(header))
    for row in rows:
        print("\t".join(str(row[k]) for k in header))

    ok = [r for r in rows if r["status"] == "ok"]
    nonempty = [r for r in ok if r["n_terms"] > 0]
    recalls = [r["recall"] for r in ok if r["recall"] != ""]
    latencies = [r["expansion_ms"] for r in ok if r["expansion_ms"] != ""]
    print()
    print(f"запитів: {len(rows)}; успішних: {len(ok)}; з непорожнім розширенням: {len(nonempty)}")
    if latencies:
        print(f"медіана затримки query_expansion, мс: {statistics.median(latencies):.1f}")
    if recalls:
        print(f"середній recall за REFERENCE: {statistics.mean(recalls):.2f} (на {len(recalls)} запитах зі словником)")

    if args.out:
        with open(args.out, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
