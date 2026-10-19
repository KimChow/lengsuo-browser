"""Font-dimension acceptance: declared families must really take effect.

EXECUTION PLAN (research machine, not this authoring session):
  1. Requires a real headless Chromium built from this patch set:
       Linux:   python3 -I tests/browser_font_dimension_smoke.py \
                  --chrome <src>/out/lengsuo/chrome \
                  --repo <kernel-repo> --work <fresh-linux-dir>
       Windows: python -I tests\\browser_font_dimension_smoke.py \
                  --chrome <src>\\out\\lengsuo\\chrome.exe \
                  --repo <kernel-repo> --work <fresh-windows-dir>
  2. The original Task C acceptance ("@font-face-set outside -> check() false")
     was WITHDRAWN (docs/cc-task-rulings.md §C): upstream FontFaceSet::check()
     returns true for every family the page did not itself register, so that
     assertion was never implementable. This test implements the replacement:
     gamma2 -- the profile's declared families are made to resolve to real
     installed typefaces instead of the default fallback.
  3. The observable difference is measureText width. Natively a declared-but-
     absent family and an undeclared-absent family BOTH fall back to the
     default font and measure identically (verified against Google Chrome 144
     and an unmodified build, see report). After injection the declared family
     is aliased onto a CSS generic family (serif / sans-serif / monospace,
     chosen by a stable hash of the declared name) and must therefore be
     distinguishable from the undeclared one. No host-specific width is
     hardcoded: the assertion is "declared != undeclared", plus "no-set case
     stays at the native baseline".
  4. Transport: the font set reaches the renderer on
     --lengsuo-managed-fonts=<Family,...>, forwarded verbatim by the browser's
     test-harness passthrough (same route --lengsuo-managed-fp/canvas use). It
     does NOT ride the profile wire: the font section is schema-2-only, and the
     browser intentionally rejects schema 2 until WebRTC policy injection lands
     (docs/webrtc-schema2-native.md). See the report for why.

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

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--' + field, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

# Two family names that (a) are never installed on any host and (b) are far
# apart in the substitution hash so the test does not accidentally pass by
# mapping both onto the same real family.
DECLARED = '__LengsuoDeclaredAlpha__'
# A second declared family, used to prove the page value tracks the set
# contents rather than being a constant.
other_declared = '__LengsuoDeclaredBeta__'
UNDECLARED = '__LengsuoUndeclaredOmega__'
SAMPLE_TEXT = 'mmmmmmmmmm'

page = '''<!doctype html><pre id="result">waiting</pre><script>
const c = document.createElement('canvas').getContext('2d');
const TEXT = %s;
const families = [%s, %s, %s, 'serif', 'sans-serif'];
const out = {};
for (const f of families) {
  c.font = '16px "' + f + '"';
  out[f] = c.measureText(TEXT).width;
}
document.querySelector('#result').textContent = JSON.stringify(out);
</script>''' % (json.dumps(SAMPLE_TEXT), json.dumps(DECLARED),
                json.dumps(other_declared), json.dumps(UNDECLARED))


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


def run_case(name, managed_fonts):
    directory = root / (name + '.user-data-dir')
    directory.mkdir(exist_ok=False)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
               '--disable-background-networking', '--disable-component-update',
               '--disable-sync', '--disable-default-apps', '--no-proxy-server',
               '--user-data-dir=' + str(directory), '--dump-dom',
               '--virtual-time-budget=5000']
    if managed_fonts is not None:
        command.append('--lengsuo-managed-fonts=' + managed_fonts)
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    try:
        completed = subprocess.run(command + [url], capture_output=True,
                                   text=True, timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        completed = None
    if completed is not None:
        (root / (name + '.stderr.log')).write_text(completed.stderr)
    case = {'case': name, 'managed_fonts': managed_fonts or 'off'}
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


try:
    baseline = run_case('baseline-off', None)
    declared = run_case('declared-set', DECLARED)
    other = run_case('other-set', other_declared)

    # 1) Native: with no profile, the declared and undeclared names are
    #    indistinguishable (both fall back to the default font). This must be
    #    the case in a stock build too; if it is not, the host really has one
    #    of these names and the probe is invalid -- fail loudly, do not paper
    #    over it.
    if baseline.get('status') == 'OK':
        obs = baseline['observed']
        if obs[DECLARED] != obs[UNDECLARED]:
            baseline['status'] = (
                'FAIL: probe names are not both absent natively '
                '(%r vs %r); pick different names' % (obs[DECLARED], obs[UNDECLARED]))
        else:
            baseline['declared_matches_undeclared'] = True

    # 2) Managed: the declared family must now differ from the undeclared one.
    #    That difference IS the font dimension taking effect.
    for case, name in ((declared, DECLARED), (other, other_declared)):
        if case.get('status') != 'OK':
            continue
        obs = case['observed']
        if obs[name] == obs[UNDECLARED]:
            case['status'] = (
                'FAIL: declared family %r still measures like the undeclared '
                'one (%.6f); the profile did not take effect'
                % (name, obs[name]))
        else:
            case['declared_width'] = obs[name]
            case['undeclared_width'] = obs[UNDECLARED]
            case['delta'] = obs[name] - obs[UNDECLARED]

    # 3) The effect must track the profile: the two profiles declare different
    #    families and must not be forced onto one identical width (a scheme
    #    that ignored the name would pass 2) but is not "the profile's fonts").
    if declared.get('status') == 'OK' and other.get('status') == 'OK':
        d = declared['observed']
        o = other['observed']
        if d[DECLARED] == o[other_declared]:
            declared['status'] = ('FAIL: two profiles declaring different '
                                  'families produced identical widths')
            other['status'] = declared['status']
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
