"""Контрольована Ollama (FIX-04): HTTP-двійник для тестів.

Відповідає на будь-який запит заданим статусом і тілом після затримки, або мовчить (--silent).
Кожен запит записується в журнал (JSON Lines: метод, шлях, тіло), щоб тести могли його перевірити.
"""
import argparse
import http.server
import json
import pathlib
import threading
import time

DEFAULT_TAGS = json.dumps({"models": [{"name": "tinyllama:latest"}]})


def make_handler(args, log_lock):
    class Handler(http.server.BaseHTTPRequestHandler):
        def _handle(self):
            length = int(self.headers.get("Content-Length", 0) or 0)
            body = self.rfile.read(length).decode("utf-8", errors="replace") if length else ""
            if args.log:
                entry = {"method": self.command, "path": self.path, "body": body}
                with log_lock, open(args.log, "a", encoding="utf-8") as fh:
                    fh.write(json.dumps(entry, ensure_ascii=False) + "\n")
            if args.silent:
                # Прийняти з'єднання і не відповідати: чекаємо, доки клієнт не відпустить.
                time.sleep(args.silent_seconds)
                return
            if args.delay_ms:
                time.sleep(args.delay_ms / 1000.0)
            payload = args.body.encode("utf-8")
            self.send_response(args.status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        do_GET = _handle
        do_POST = _handle

        def log_message(self, *_):
            return

    return Handler


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=11434)
    p.add_argument("--status", type=int, default=200)
    p.add_argument("--body", default=DEFAULT_TAGS, help="JSON-рядок або шлях до файлу")
    p.add_argument("--delay-ms", type=int, default=0)
    p.add_argument("--silent", action="store_true")
    p.add_argument("--silent-seconds", type=float, default=60.0)
    p.add_argument("--log", default="", help="файл журналу запитів (JSON Lines)")
    args = p.parse_args()
    if pathlib.Path(args.body).is_file():
        args.body = pathlib.Path(args.body).read_text(encoding="utf-8")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args, threading.Lock()))
    server.serve_forever()


if __name__ == "__main__":
    main()
