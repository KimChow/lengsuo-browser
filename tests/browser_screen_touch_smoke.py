"""Synthetic loopback-only Screen/DPR/touch acceptance; no public services.

EXECUTION PLAN (parent/kernel session, not this worker):
  1. This file requires ACTUAL headless Chromium on BOTH platforms before
     the Screen/DPR/touch feature is accepted:
       Linux:   python3 -I tests/browser_screen_touch_smoke.py \
                  --chrome <src>/out/lengsuo/chrome \
                  --repo <kernel-repo> --work <fresh-linux-dir>
       Windows: python -I tests\\browser_screen_touch_smoke.py \
                  --chrome <src>\\out\\lengsuo\\chrome.exe \
                  --repo <kernel-repo> --work <fresh-windows-dir>
  2. Both runs must exit 0 with summary.json ok=true and 3 cases
     (baseline-off / golden-managed / custom-managed). Do not accept on
     evidence from only one platform.
  3. No host-specific pixel values are hardcoded: the managed cases are
     compared (a) against values decoded from the LBPF1 wire bytes this
     script itself patches, and (b) against an unmodified fresh
     profile-off run used as the baseline (profile must observably change
     at least one of width/height/colorDepth/devicePixelRatio/
     maxTouchPoints away from it).
Fake golden LBPF1 only; loopback HTTP only; no production code changes.
"""
import argparse
import html
import http.server
import json
import re
import struct
import subprocess
import sys
import threading
from pathlib import Path

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--' + field, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

def fixture(path, name):
    # Extract a `constexpr unsigned char <name>[] = { ... };` byte array from
    # C++ source as literal bytes, WITHOUT executing any other test module.
    # (The previous implementation imported tests/browser_ua_smoke.py, whose
    # top-level body parses argv and runs browsers: fragile, side-effecting,
    # and it wrote stub artifacts into --work.) Scope the regex to the array
    # body only, so digits in surrounding code can never leak into the bytes.
    body = re.search(r'\b' + name + r'\[\]\s*=\s*\{([^}]*)\};',
                     path.read_text(), re.S).group(1)
    return bytes(map(int, re.findall(r'\d+', body)))

# Fixed LBPF1 field offsets (big-endian WireReader, native/wire_reader.h),
# verified against native/profile_wire.cc DecodeProfile and native/golden.inc:
# magic[0:5] schema[5:9] noise[9:13] env-id[13:20] pool[20:24] seed[24:32]
# platform[32] width[33:37] height[37:41] depth[41] dpr-milli[42:46]
# cpu[46:48] memory[48:52] max-touch-points[52] language[...]...
OFF = {'platform': 32, 'width': 33, 'height': 37, 'color_depth': 41,
       'dpr_milli': 42, 'max_touch_points': 52}
assert OFF == {'platform': 32, 'width': 33, 'height': 37, 'color_depth': 41,
               'dpr_milli': 42, 'max_touch_points': 52}, 'offsets drifted'

def wire_fields(wire):
    return {'width': struct.unpack_from('>I', wire, OFF['width'])[0],
            'height': struct.unpack_from('>I', wire, OFF['height'])[0],
            'colorDepth': wire[OFF['color_depth']],
            'devicePixelRatio': struct.unpack_from('>I', wire, OFF['dpr_milli'])[0] / 1000.0,
            'maxTouchPoints': wire[OFF['max_touch_points']]}

def patch_wire(wire, **updates):
    out = bytearray(wire)
    for key, value in updates.items():
        if key in ('color_depth', 'max_touch_points'):
            out[OFF[key]] = value
        else:
            struct.pack_into('>I', out, OFF[key], value)
    return bytes(out)

golden_profile = fixture(a.repo / 'native/golden.inc', 'kGolden')
golden_catalog = fixture(a.repo / 'native/profile_catalog_test.cc', 'kGoldenCatalog')
assert golden_profile[:5] == b'LBPF1', 'unexpected golden magic'
# Offset-regression guard: golden values recorded in docs/batch2 for this
# fixture (a repo constant, not a host measurement).
assert wire_fields(golden_profile) == {'width': 1920, 'height': 1080,
    'colorDepth': 24, 'devicePixelRatio': 1.0, 'maxTouchPoints': 0}, wire_fields(golden_profile)
CUSTOM = patch_wire(golden_profile, width=1366, height=768, color_depth=30,
                    dpr_milli=1500, max_touch_points=2)
assert wire_fields(CUSTOM) != wire_fields(golden_profile), 'custom profile must differ'
# DecodeProfile constraints hold for the patched wire (platform stays 0;
# mtp>0 is valid on every platform; depth in {8,24,30}; dpr%1000 keeps
# devicePixelRatio an exact float).
assert CUSTOM[OFF['color_depth']] in (8, 24, 30)
assert 1 <= struct.unpack_from('>I', CUSTOM, OFF['dpr_milli'])[0] <= 10000
assert CUSTOM[OFF['max_touch_points']] <= 10

profile_files = {}
for name, wire in [('golden', golden_profile), ('custom', CUSTOM)]:
    path = root / (name + '.profile.wire')
    path.write_bytes(wire)
    profile_files[name] = path
catalog_path = root / 'catalog.wire'
catalog_path.write_bytes(golden_catalog)

page = '''<!doctype html><pre id="result">waiting</pre><script>
document.querySelector('#result').textContent=JSON.stringify({
 width:window.screen.width,height:window.screen.height,colorDepth:window.screen.colorDepth,
 devicePixelRatio:window.devicePixelRatio,maxTouchPoints:navigator.maxTouchPoints});
</script>'''

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = page.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:' + str(server.server_port) + '/'
TIMEOUT_SECONDS = 60
keys = ('width', 'height', 'colorDepth', 'devicePixelRatio', 'maxTouchPoints')
results = []
failures = []

def run_case(name, profile_name):
    # Separate fresh user-data-dir per case (exist_ok=False); never reused.
    directory = root / (name + '.user-data-dir')
    directory.mkdir(exist_ok=False)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
               '--disable-background-networking', '--disable-component-update',
               '--disable-sync', '--disable-default-apps', '--no-proxy-server',
               '--user-data-dir=' + str(directory), '--dump-dom',
               '--virtual-time-budget=5000']
    if profile_name:
        command.append('--lengsuo-profile-wire=' + str(profile_files[profile_name]))
        command.append('--lengsuo-catalog-wire=' + str(catalog_path))
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    try:
        completed = subprocess.run(command + [url], capture_output=True,
                                   text=True, timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        completed = None
    if completed is not None:
        (root / (name + '.stderr.log')).write_text(completed.stderr)
    case = {'case': name, 'profile': profile_name or 'off'}
    if completed is None:
        case['status'] = 'FAIL: chrome timed out after %ds' % TIMEOUT_SECONDS
    elif completed.returncode != 0:
        case['status'] = 'FAIL: rc=%d stderr=%s' % (
            completed.returncode, completed.stderr[-500:])
    else:
        match = re.search(r'<pre id="result">(.*?)</pre>', completed.stdout, re.S)
        if not match:
            case['status'] = 'FAIL: no result element in dump-dom'
        else:
            observed = json.loads(html.unescape(match.group(1)))
            case['observed'] = observed
            if profile_name:
                expected = wire_fields(profile_files[profile_name].read_bytes())
                case['expected_from_wire'] = expected
                mismatch = {k: (observed[k], expected[k]) for k in keys
                            if observed[k] != expected[k]}
                case['status'] = 'OK' if not mismatch else (
                    'FAIL: wire-vs-page mismatch ' + json.dumps(mismatch))
            else:
                case['status'] = 'OK'
    results.append(case)
    print(json.dumps(case), flush=True)
    return case

try:
    baseline = run_case('baseline-off', None)
    golden_case = run_case('golden-managed', 'golden')
    custom_case = run_case('custom-managed', 'custom')
    # Profile-off baseline must be a real observation (no host constants).
    for case in (golden_case, custom_case):
        if case['status'] != 'OK' or baseline.get('status') != 'OK':
            continue
        diff = [k for k in keys if case['observed'][k] != baseline['observed'][k]]
        if not diff:
            case['status'] = ('FAIL: managed values identical to profile-off '
                              'baseline, override not observable')
        case['differs_from_baseline_on'] = diff if case['status'] == 'OK' else case['status']
    if golden_case['status'] == 'OK' and custom_case['status'] == 'OK':
        same = [k for k in keys
                if golden_case['observed'][k] == custom_case['observed'][k]]
        if len(same) == len(keys):
            for case in (golden_case, custom_case):
                case['status'] = 'FAIL: golden and custom profiles indistinguishable'
finally:
    server.shutdown()
    server.server_close()

failures = [case['case'] for case in results if case['status'] != 'OK']
summary = {'platform': sys.platform, 'chrome': str(a.chrome),
           'passed': len(results) - len(failures), 'failed': failures,
           'ok': not failures and len(results) == 3, 'results': results}
(root / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps({'summary': 'ok' if summary['ok'] else summary['failed']}), flush=True)
sys.exit(0 if summary['ok'] else 1)
