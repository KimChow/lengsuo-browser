"""Font-dimension acceptance: the metric perturbation and its boundaries.

EXECUTION PLAN (research machine, not this authoring session):
  1. Requires a real headless Chromium built from this patch set:
       Linux:   python3 -I tests/browser_font_metrics_smoke.py \
                  --chrome <src>/out/lengsuo/chrome \
                  --repo <kernel-repo> --work <fresh-linux-dir>
       Windows: python3 -I tests\\browser_font_metrics_smoke.py \
                  --chrome <src>\\out\\lengsuo\\chrome.exe \
                  --repo <kernel-repo> --work <fresh-windows-dir>
  2. What is under test is the mechanism the ruling accepted
     (docs/cc-task-rulings.md §C): a deterministic perturbation of the measured
     glyph advance. The two earlier acceptance criteria (filter the availability
     set; false outside the set) were withdrawn -- upstream FontFaceSet::check()
     returns true for every system family, so that state does not exist.
  3. The cases assert exactly the four replacement criteria:
       * off / slot 0        -> measureText AND document.fonts.check() are
                                byte-identical to native (the red baseline);
       * switcher + value    -> measureText differs, deterministically: the same
                                profile twice is identical, a different profile
                                differs;
       * check()             -> unchanged in every case (never faked);
       * worker              -> OffscreenCanvas measureText agrees with the page
                                canvas in the same profile.
  4. The band is produced by native/font_metrics.cc, not hardcoded here: this
     test imports tests/font_metrics_ref.py and asserts the browser's observed
     delta is a whole number of 1/64 px steps inside the published bound, so a
     change to the band cannot pass unnoticed.
  5. Transport: the dimension reaches the renderer on
     --lengsuo-managed-fonts=<slot>:<mask>:<Family;Family;...>, forwarded
     verbatim by the browser's test-harness passthrough (same route
     --lengsuo-managed-fp/canvas use). It does NOT ride the profile wire: the
     font section is schema-2-only, and the browser intentionally rejects
     schema 2 until WebRTC policy injection lands (docs/webrtc-schema2-native.md).

Loopback HTTP only; fresh user-data-dir per case; no production constants.
"""
import argparse
import html
import http.server
import json
import re
import subprocess
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import font_metrics_ref  # noqa: E402  (path set above)

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--' + field, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

SAMPLE_TEXT = 'mmmmmmmmmm'
# Two distinct noise seeds: the "same profile twice" and "different profiles"
# assertions are driven off the seed, which is the profile's noise_seed.
SEED_A = '0123456789ABCDEF'
SEED_B = 'FEDCBA9876543210'
FAMILIES = 'Arial;Arial Black'

page = '''<!doctype html><pre id="result">waiting</pre><script>
const TEXT = %s;
const out = {};
function measure() {
  const c = document.createElement('canvas').getContext('2d');
  c.font = '16px serif';
  return c.measureText(TEXT).width;
}
out.page1 = measure();
out.page2 = measure();               // same profile twice: must be identical
out.check_system = document.fonts.check('16px serif');
out.check_absent = document.fonts.check('16px "__LengsuoAbsentFamilyX__"');
const worker = `
  self.onmessage = function () {
    const c = new OffscreenCanvas(300, 100).getContext('2d');
    c.font = '16px serif';
    self.postMessage(c.measureText(${JSON.stringify(TEXT)}).width);
  };
`;
let done = false;
try {
  const url = URL.createObjectURL(new Blob([worker], {type: 'text/javascript'}));
  const w = new Worker(url);
  w.onmessage = function (e) {
    out.worker = e.data;
    done = true;
    document.querySelector('#result').textContent = JSON.stringify(out);
  };
  w.onerror = function () { done = true;
    document.querySelector('#result').textContent = JSON.stringify(out); };
  w.postMessage(0);
  setTimeout(function () { if (!done) {
    document.querySelector('#result').textContent = JSON.stringify(out); } }, 2000);
} catch (e) {
  document.querySelector('#result').textContent = JSON.stringify(out);
}
</script>''' % json.dumps(SAMPLE_TEXT)


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
results = []


def run_case(name, seed, fonts_value):
    directory = root / (name + '.user-data-dir')
    directory.mkdir(exist_ok=False)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
               '--disable-background-networking', '--disable-component-update',
               '--disable-sync', '--disable-default-apps', '--no-proxy-server',
               '--user-data-dir=' + str(directory), '--dump-dom',
               '--virtual-time-budget=5000']
    if seed is not None:
        # The fp switch: seed:canvas:audio:rects:fonts:gpu:ua:webgl:vendor:
        # renderer:hwc:mem. The fonts slot is 3 (replace) when a value is given.
        command.append('--lengsuo-managed-fp=' +
                       ':'.join([seed, '0', '0', '0', '3', '0', '0', '0',
                                 '', '', '0', '0']))
    if fonts_value is not None:
        command.append('--lengsuo-managed-fonts=' + fonts_value)
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    try:
        completed = subprocess.run(command + [url], capture_output=True,
                                   text=True, timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        completed = None
    if completed is not None:
        (root / (name + '.stderr.log')).write_text(completed.stderr)
    case = {'case': name, 'fonts': fonts_value or 'off'}
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
            case['observed'] = json.loads(html.unescape(match.group(1)))
            case['status'] = 'OK'
    results.append(case)
    print(json.dumps(case), flush=True)
    return case


def is_step_aligned(delta):
    """True when delta is a whole number of 1/64 px steps inside the bound."""
    steps = delta / font_metrics_ref._QUANTUM
    within = abs(steps) <= font_metrics_ref._MAX_STEPS + 1e-6
    return within and abs(steps - round(steps)) < 1e-4


try:
    baseline = run_case('baseline-off', None, None)
    same1 = run_case('profile-a-1', SEED_A, '3:0:' + FAMILIES)
    same2 = run_case('profile-a-2', SEED_A, '3:0:' + FAMILIES)
    other = run_case('profile-b', SEED_B, '3:0:' + FAMILIES)
    dormant = run_case('dormant-slot0', SEED_A, '0:0:')

    native = baseline.get('observed', {})

    # 1) Red baseline: with no dimension the measurement is untouched, and this
    #    is the value every later case is compared against.
    if baseline.get('status') == 'OK':
        if native['page1'] != native['page2']:
            baseline['status'] = 'FAIL: measureText is not even stable natively'
        else:
            baseline['native_width'] = native['page1']

    # 2) Dormant (slot 0) must equal native: "the profile said nothing".
    if dormant.get('status') == 'OK' and baseline.get('status') == 'OK':
        if dormant['observed']['page1'] != native['page1']:
            dormant['status'] = ('FAIL: slot 0 changed the measurement '
                                 '(%.6f vs native %.6f)'
                                 % (dormant['observed']['page1'], native['page1']))

    # 3) The dimension must move the measurement, deterministically.
    for case in (same1, same2, other):
        if case.get('status') != 'OK':
            continue
        obs = case['observed']
        if obs['page1'] != obs['page2']:
            case['status'] = ('FAIL: two measurements in one profile disagree '
                              '(%.6f vs %.6f)' % (obs['page1'], obs['page2']))
            continue
        if baseline.get('status') == 'OK':
            delta = obs['page1'] - native['page1']
            case['delta'] = delta
            if delta == 0.0:
                case['status'] = ('FAIL: the font dimension did not change the '
                                  'width at all')
            elif not is_step_aligned(delta):
                case['status'] = ('FAIL: delta %.6f is not a whole 1/64 px step '
                                  'inside the published bound' % delta)

    # 4) check() is never faked: it must answer the same with and without the
    #    dimension installed, for both a system family and an absent one.
    if baseline.get('status') == 'OK':
        for case in (same1, same2, other, dormant):
            if case.get('status') != 'OK':
                continue
            obs = case['observed']
            for key in ('check_system', 'check_absent'):
                if obs[key] != native[key]:
                    case['status'] = ('FAIL: fonts.check() changed (%s: %r vs '
                                      'native %r); it must stay upstream'
                                      % (key, obs[key], native[key]))

    # 5) Same profile twice -> identical width; different profile -> different.
    if same1.get('status') == 'OK' and same2.get('status') == 'OK':
        if same1['observed']['page1'] != same2['observed']['page1']:
            same1['status'] = ('FAIL: the same profile produced different '
                               'widths across runs')
            same2['status'] = same1['status']
    if (same1.get('status') == 'OK' and other.get('status') == 'OK'
            and same1['observed']['page1'] == other['observed']['page1']):
        other['status'] = ('FAIL: two different profiles produced identical '
                           'widths')

    # 6) Worker consistency: OffscreenCanvas must see the same value.
    for case in (baseline, same1, other, dormant):
        if case.get('status') != 'OK':
            continue
        obs = case['observed']
        if 'worker' not in obs:
            case['status'] = 'FAIL: worker did not report a measurement'
        elif obs['worker'] != obs['page1']:
            case['status'] = ('FAIL: OffscreenCanvas disagrees with the page '
                              'canvas (%.6f vs %.6f)'
                              % (obs['worker'], obs['page1']))
finally:
    server.shutdown()
    server.server_close()

failures = [case['case'] for case in results if case['status'] != 'OK']
summary = {'platform': sys.platform, 'chrome': str(a.chrome),
           'passed': len(results) - len(failures), 'failed': failures,
           'ok': not failures and len(results) == 5, 'results': results}
(root / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps({'summary': 'ok' if summary['ok'] else summary['failed']}),
      flush=True)
sys.exit(0 if summary['ok'] else 1)
