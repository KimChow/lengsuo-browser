"""Three-state probe: native vs managed vs determinism for WebGL params.

Launches the built chrome headless against a local page that reads GL params
via WebGL and reports them back; compares native / managed / repeat runs.
"""
import http.server
import json
import os
import subprocess
import threading
import time

# The gclient solution is named `src`, and the dependencies live there too, so
# the build tree is .../lengsuo-browser-dev/src - not chromium-src. Override with
# LENGSUO_CHROME when the tree lives elsewhere.
CHROME = os.environ.get(
    "LENGSUO_CHROME",
    "/home/ubuntu/lengsuo-browser-dev/src/out/lengsuo/chrome")
PAGE = """<!doctype html><html><body><script>
const out = {};
try {
  const gl = document.createElement('canvas').getContext('webgl');
  out.max_tex = gl.getParameter(gl.MAX_TEXTURE_SIZE);
  out.max_attribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
  out.max_varying = gl.getParameter(gl.MAX_VARYING_VECTORS);
  out.max_units = gl.getParameter(gl.MAX_COMBINED_TEXTURE_IMAGE_UNITS);
  out.active_texture = gl.getParameter(gl.ACTIVE_TEXTURE);
  out.viewport = Array.from(gl.getParameter(gl.VIEWPORT));
  out.vendor = gl.getParameter(gl.VENDOR);
  out.renderer = gl.getParameter(gl.RENDERER);
} catch (e) { out.err = String(e); }
fetch('/r', {method: 'POST', body: JSON.stringify(out)});
</script></body></html>"""

RESULTS = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        RESULTS.append(json.loads(self.rfile.read(length)))
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


def grab(tag, flags):
    start = len(RESULTS)
    profile = f"/tmp/w4p-{tag}"
    subprocess.run(["rm", "-rf", profile], check=False)
    proc = subprocess.Popen(
        [CHROME, "--headless", "--no-sandbox", f"--user-data-dir={profile}",
         "--disable-gpu", "--virtual-time-budget=15000"] + flags +
        ["http://127.0.0.1:%d/" % PORT],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 60
    while time.time() < deadline and len(RESULTS) <= start:
        if proc.poll() is not None:
            break
        time.sleep(0.2)
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=10)
    return RESULTS[start] if len(RESULTS) > start else None


PORT = 0
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
PORT = srv.server_port
threading.Thread(target=srv.serve_forever, daemon=True).start()

SWITCH = ("--lengsuo-managed-fp="
          "abcdef0123456789:0:0:0:0:1:0:1:Google Inc (NVIDIA):"
          "ANGLE (NVIDIA Test, D3D11):0:0")

native = grab("native", [])
managed = grab("managed", [SWITCH])
managed2 = grab("managed2", [SWITCH])

print("native :", json.dumps(native, ensure_ascii=False)[:300])
print("managed:", json.dumps(managed, ensure_ascii=False)[:300])

failures = []


def is_real_cap(value):
    """True when a shipping driver could report this capability cap.

    Drivers report powers of two, or a short list of architecture constants
    (MAX_VARYING_VECTORS=31, MAX_TEXTURE_IMAGE_UNITS=16, ...). An arbitrary
    integer in range - 6364 for an 8192 cap - does not exist in the wild and is
    therefore *more* distinctive than leaving the driver value alone.
    """
    if value <= 0:
        return False
    if value & (value - 1) == 0:
        return True
    return value in {16, 24, 30, 31, 32, 48, 60, 64, 96, 128, 256}


if not native or not managed or not managed2:
    failures.append("missing run data")
else:
    for key in ("max_tex", "max_attribs", "max_varying", "max_units"):
        delta = managed.get(key, 0) - native.get(key, 0)
        value = managed.get(key, 0)
        # Never above the driver value (a page trusting an inflated cap would
        # allocate resources that cannot exist), and at most one tier down.
        bound = max(native[key] // 2, 1)
        plausible = is_real_cap(value) or value == native[key]
        status = "PASS" if (-bound <= delta <= 0 and plausible) else "FAIL"
        note = "" if plausible else "  <- not a real driver value"
        print(f"{key}: {native[key]} -> {value} (delta {delta:+d}) {status}{note}")
        if status == "FAIL":
            failures.append(key)
    if managed.get("active_texture") != native.get("active_texture"):
        failures.append("active_texture changed")
    if managed.get("viewport") != native.get("viewport"):
        failures.append("viewport changed")
    print("state passthrough:",
          "PASS" if "active_texture changed" not in failures
          and "viewport changed" not in failures else "FAIL")
    same = all(managed2.get(k) == managed.get(k)
               for k in ("max_tex", "max_attribs", "max_varying", "max_units"))
    print("determinism:", "PASS" if same else "FAIL")
    if not same:
        failures.append("determinism")
    if (managed.get("vendor") != "Google Inc (NVIDIA)"
            or managed.get("renderer") != "ANGLE (NVIDIA Test, D3D11)"):
        failures.append("face strings")
        print("face strings: FAIL", managed.get("vendor"), "|", managed.get("renderer"))
    else:
        print("face strings: PASS")

srv.shutdown()
print("RESULT:", "FAIL " + ",".join(failures) if failures else "ALL PASS")
