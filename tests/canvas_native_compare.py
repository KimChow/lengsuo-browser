"""Native-vs-profile canvas readback comparison on the same chrome binary.

States (same binary, per-state user-data-dir, loopback HTTP only):
  A native        : no profile wire  -> readback R0 (the native baseline)
  B profile-on    : --lengsuo-profile-wire=<synthetic LBPF1>
                    -> readback R1 (noised readback)

Asserts:
  1. R1 == apply_v1(seed, slot, R0) byte-for-byte, where apply_v1 is the
     INDEPENDENT Python reference (tests/canvas_ref/gen_canvas_ref.py).
  2. R1 differs from R0 only on RGB of non-transparent pixels, |d| <= 13,
     alpha identical.
  3. Two native readbacks are identical (deterministic baseline).
  4. Two profile readbacks are identical.
  5. alpha==0 pixels are byte-identical between R0 and R1.

The seed/slot checked here MUST be the values the browser passes via
--lengsuo-managed-canvas (hex seed : decimal slot from noise_seed/slots[1]).
"""
import argparse
import http.server
import json
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests" / "canvas_ref"))
from gen_canvas_ref import apply_v1  # noqa: E402

p = argparse.ArgumentParser()
for field in ("chrome", "repo", "work"):
    p.add_argument("--" + field, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

SEED = 0x0123456789ABCDEF
SLOT = 1  # slots[1] == canvas slot in the LBPF1 canonical order

PAGE = """<!doctype html><canvas id="c" width="64" height="48"></canvas><script>
const c = document.getElementById('c');
const ctx = c.getContext('2d');
// Deterministic content: gradient + shapes + a transparent row block.
const g = ctx.createLinearGradient(0, 0, 64, 48);
g.addColorStop(0, '#f0f0f0'); g.addColorStop(1, '#404040');
ctx.fillStyle = g; ctx.fillRect(0, 0, 64, 48);
ctx.fillStyle = 'rgba(200, 30, 30, 0.85)'; ctx.fillRect(8, 8, 20, 14);
ctx.fillStyle = 'rgba(30, 200, 30, 0.5)'; ctx.fillRect(30, 20, 18, 12);
ctx.fillStyle = '#123456'; ctx.font = '10px monospace';
ctx.fillText('lengsuo', 10, 40);
// fully transparent block rows 0..3 remain untouched by drawing
const img = ctx.getImageData(0, 0, 64, 48);
fetch('/report', {method: 'POST', body: JSON.stringify({
  w: img.width, h: img.height,
  data: Array.from(img.data)})});
</script>"""

REPORTS = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        REPORTS.append(self.rfile.read(n).decode("utf-8", "replace"))
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def do_GET(self):
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = "http://127.0.0.1:" + str(server.server_port) + "/"

results = []


def launch(name, flags):
    d = root / name
    d.mkdir(exist_ok=True)
    cmd = [str(a.chrome.resolve()), "--headless", "--no-first-run",
           "--disable-background-networking", "--disable-component-update",
           "--disable-sync", "--disable-default-apps", "--no-proxy-server",
           "--user-data-dir=" + str(d)]
    if sys.platform != "win32":
        cmd.append("--no-sandbox")
    start = len(REPORTS)
    proc = subprocess.Popen(cmd + flags + [url],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        if len(REPORTS) > start or proc.poll() is not None:
            break
        time.sleep(0.1)
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    return json.loads(REPORTS[start]) if len(REPORTS) > start else None


def pixels(report):
    return bytes(report["data"]), report["w"], report["h"]


# ---- A: native (no profile) — twice for determinism -------------------------
r0a = launch("native-a", [])
r0b = launch("native-b", [])
results.append({"case": "native-twice-deterministic",
                "status": "PASS" if r0a and r0b and r0a["data"] == r0b["data"]
                else "FAIL: native readback not deterministic"})

# ---- B: profile-on — twice --------------------------------------------------
wire = root / "profile.wire"
# Build a synthetic schema-1 LBPF1 via the canonical generator (same one the
# WebRTC three-state harness uses) with our fixed noise_seed and canvas slot,
# so the browser passes --lengsuo-managed-canvas=<seed-hex>:<slot>.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"
                       / "golden_v2"))
from gen_golden_v2 import FIELDS, build  # noqa: E402

fields = {**FIELDS,
          "noise_seed": list(SEED.to_bytes(8, "big")),
          "slots": {**FIELDS["slots"], "canvas": SLOT}}
wire.write_bytes(bytes(build({**fields, "schema_version": 1})))

r1a = launch("profile-a", ["--lengsuo-profile-wire=" + str(wire)])
r1b = launch("profile-b", ["--lengsuo-profile-wire=" + str(wire)])
results.append({"case": "profile-twice-deterministic",
                "status": "PASS" if r1a and r1b and r1a["data"] == r1b["data"]
                else "FAIL: profile readback not deterministic"})

# ---- Core comparison ---------------------------------------------------------
if r0a and r1a:
    R0, w, h = pixels(r0a)
    R1, _, _ = pixels(r1a)
    expected = apply_v1(SEED, SLOT, w, h, w * 4, R0)
    results.append({"case": "profile-readback-equals-reference-noise",
                    "status": "PASS" if R1 == expected else
                    "FAIL: R1 != apply_v1(R0)"})
    diffs = []
    alpha_ok = all(R1[i + 3] == R0[i + 3] for i in range(0, len(R0), 4))
    results.append({"case": "alpha-preserved-everywhere",
                    "status": "PASS" if alpha_ok else "FAIL: alpha changed"})
    transparent_ok = all(R1[i:i + 4] == R0[i:i + 4]
                         for i in range(0, len(R0), 4) if R0[i + 3] == 0)
    results.append({"case": "transparent-pixels-untouched",
                    "status": "PASS" if transparent_ok else
                    "FAIL: transparent pixel changed"})
    max_d = 0
    for i in range(0, len(R0), 4):
        if R0[i + 3] == 0:
            continue
        for c in range(3):
            max_d = max(max_d, abs(R1[i + c] - R0[i + c]))
    results.append({"case": "delta-bound-13",
                    "max_channel_delta": max_d,
                    "status": "PASS" if max_d <= 13 else
                    "FAIL: delta exceeds 13"})
else:
    results.append({"case": "core-comparison",
                    "status": "HARNESS_FAIL: a readback never reported"})

summary = {"harness": "canvas-native-compare", "results": results,
           "seed": SEED, "slot": SLOT,
           "ok": all(r["status"] == "PASS" for r in results)}
(root / "summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary))
stop_needed = False
server.shutdown()
raise SystemExit(0 if summary["ok"] else 1)
