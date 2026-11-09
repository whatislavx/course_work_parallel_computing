#!/usr/bin/env python3
"""Заглушка Ollama для сценарного тесту відмови (FR-021).

Режими (перемикаються без перезапуску через POST /__mode):
  ok   — /api/generate відповідає одразу (--ok-delay-ms);
  hang — /api/generate не відповідає протягом --hang-seconds (довше за OLLAMA_TIMEOUT_MS).
/api/tags завжди швидкий: сервер використовує його для перевірки готовності (health.ollama).

Використання: stub_ollama.py --port 18434 [--hang-seconds 60] [--ok-delay-ms 20]
"""
import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE = {"mode": "ok", "ok_delay_ms": 20, "hang_seconds": 60.0}
LOCK = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, obj, status=200):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/tags":
            self._send({"models": [{"name": "qwen2.5:0.5b"}]})
        else:
            self._send({"error": "not found"}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        if self.path == "/__mode":
            payload = json.loads(raw or b"{}")
            with LOCK:
                STATE["mode"] = payload.get("mode", "ok")
            self._send({"mode": STATE["mode"]})
            return
        if self.path == "/api/generate":
            with LOCK:
                mode, ok_delay, hang = STATE["mode"], STATE["ok_delay_ms"], STATE["hang_seconds"]
            if mode == "hang":
                time.sleep(hang)
                self._send({"error": "stalled"}, 503)
                return
            time.sleep(ok_delay / 1000.0)
            self._send({"model": "qwen2.5:0.5b", "response": "city, capital, england", "done": True})
            return
        self._send({"error": "not found"}, 404)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18434)
    parser.add_argument("--hang-seconds", type=float, default=60.0)
    parser.add_argument("--ok-delay-ms", type=int, default=20)
    args = parser.parse_args()
    STATE["hang_seconds"] = args.hang_seconds
    STATE["ok_delay_ms"] = args.ok_delay_ms
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
