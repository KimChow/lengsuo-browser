"""One-off diagnostic: does the lang probe manage to POST its result back?

Kept out of the repository test suite on purpose; this is a scratch harness.
"""
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading
import time

P = pathlib.Path("/tmp/lang-probe.html").read_bytes()
POSTS = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        print("REQ", self.command, self.path, flush=True)

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(P)))
        self.end_headers()
        self.wfile.write(P)

    def do_POST(self):
        size = int(self.headers.get("Content-Length", 0))
        POSTS.append(self.rfile.read(size).decode("utf-8", "replace"))
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")


server = http.server.ThreadingHTTPServer(("127.0.0.1", 8903), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()

profile = tempfile.mkdtemp()
result = subprocess.run(
    ["/usr/bin/google-chrome-stable", "--headless=new", "--no-sandbox",
     "--disable-dev-shm-usage", "--no-first-run",
     "--user-data-dir=" + profile, "--lang=en-US",
     "--virtual-time-budget=8000",
     "http://127.0.0.1:8903/lang-probe.html?report=1"],
    capture_output=True, timeout=60)
time.sleep(3)
print("POSTS:", len(POSTS))
if POSTS:
    print("BODY:", POSTS[0][:400])
print("STDERR tail:", result.stderr.decode("utf-8", "replace")[-400:])
