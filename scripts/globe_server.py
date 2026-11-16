#!/usr/bin/env python3
"""Loopback-only static host and JSON bridge for the Arctra Cesium globe."""

from __future__ import annotations

import argparse
import http.server
import json
import pathlib
import subprocess
import threading
import urllib.parse
import webbrowser


class Worker:
    def __init__(self, executable: pathlib.Path, cache: pathlib.Path) -> None:
        self._process = subprocess.Popen(
            [str(executable), str(cache)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1,
        )
        self._lock = threading.Lock()

    def evaluate(self, request: bytes) -> bytes:
        if len(request) > 1_000_000:
            raise ValueError("request is too large")
        value = json.loads(request)
        with self._lock:
            assert self._process.stdin is not None and self._process.stdout is not None
            self._process.stdin.write(json.dumps(value, separators=(",", ":")) + "\n")
            self._process.stdin.flush()
            response = self._process.stdout.readline()
        if not response:
            raise RuntimeError("Arctra globe worker exited")
        return response.encode("utf-8")

    def close(self) -> None:
        self._process.terminate()
        try:
            self._process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self._process.kill()


def handler_type(root: pathlib.Path, worker: Worker):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def do_POST(self) -> None:  # noqa: N802 - stdlib callback name
            if urllib.parse.urlsplit(self.path).path != "/api/live":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                response = worker.evaluate(self.rfile.read(length))
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(response)))
                self.end_headers()
                self.wfile.write(response)
            except Exception as error:  # keep the local UI alive with an actionable response
                response = json.dumps({"schemaVersion": 1, "ok": False, "error": str(error)}).encode()
                self.send_response(500)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(response)))
                self.end_headers()
                self.wfile.write(response)

        def end_headers(self) -> None:
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            super().end_headers()

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True, type=pathlib.Path)
    parser.add_argument("--web-root", required=True, type=pathlib.Path)
    parser.add_argument("--cache", default=pathlib.Path(".arctra-cache/cpf"), type=pathlib.Path)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    if not (args.web_root / "index.html").is_file():
        parser.error(f"globe assets are not built: {args.web_root / 'index.html'}")
    worker = Worker(args.worker.resolve(), args.cache.resolve())
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), handler_type(args.web_root.resolve(), worker))
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"Arctra globe: {url}", flush=True)
    if not args.no_open:
        threading.Timer(0.2, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        worker.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
