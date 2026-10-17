"""Loopback-only user-data-dir storage isolation smoke for the lengsuo Chromium build.

Validates that persistent cookies (Max-Age) and LocalStorage are scoped to the
--user-data-dir profile directory on a stable 127.0.0.1 origin. Synthetic data,
no external network, no real credentials. This does NOT test the product
runner's TUN/cache/extension isolation; it tests the Chromium profile storage
directories only.
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

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--chrome', type=Path, required=True, help='path to chrome binary')
p.add_argument('--work', type=Path, required=True, help='evidence/profile root OUTSIDE git')
p.add_argument('--timeout', type=int, default=90, help='per-invocation seconds')
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()
KEY = 'ls_iso_key'

PAGE = b'''<!doctype html><pre id="result">pending</pre><script>
(()=>{try{
const q=new URLSearchParams(location.search);
const mode=q.get('mode'), name=q.get('k'), val=q.get('v');
const map=()=>Object.fromEntries(document.cookie.split(/;\\s*/).filter(Boolean)
  .map(s=>{const i=s.indexOf('=');return [decodeURIComponent(s.slice(0,i)),
  decodeURIComponent(s.slice(i+1))];}));
if(mode==='set'){
  document.cookie=name+'='+encodeURIComponent(val)+'; path=/; max-age=3600';
  localStorage.setItem(name,val);
}
if(mode==='clear'){
  for(const n of Object.keys(map())){document.cookie=n+'=; path=/; max-age=0';}
  localStorage.removeItem(name);
}
const out={mode,cookie:map()[name]??null,allCookieNames:Object.keys(map()).sort(),
  ls:localStorage.getItem(name),lsKeys:Object.keys(localStorage).sort()};
document.getElementById('result').textContent='RESULT:'+JSON.stringify(out);
}catch(e){document.getElementById('result').textContent='RESULT:'+JSON.stringify({error:String(e)});}})();
</script>'''


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
# Stable origin for every invocation of this run; port stays fixed end-to-end.
ORIGIN = 'http://127.0.0.1:' + str(server.server_port)

results = []


def profile_files(directory):
    """Locate the per-profile cookie DB and LocalStorage store on disk.

    Chromium writes LocalStorage either as the legacy leveldb directory
    ('Local Storage/leveldb') or, in newer backends, as a SQLite file
    ('LocalStorage'). Record whichever layout the profile actually uses.
    """
    found = {}
    default = directory / 'Default'
    cookies = (default / 'Cookies', default / 'Network' / 'Cookies')
    found['cookie_db'] = next((str(p.relative_to(root)) for p in cookies
                               if p.is_file()), None)
    leveldb = default / 'Local Storage' / 'leveldb'
    sqlite = default / 'LocalStorage'
    for path in (leveldb, sqlite):
        if path.exists():
            found['local_storage'] = str(path.relative_to(root))
            break
    else:
        found['local_storage'] = None
    return found


def run(name, profile, mode, value=None, expect_rc=0):
    directory = root / profile
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
               '--disable-background-networking', '--disable-component-update',
               '--disable-sync', '--disable-default-apps', '--no-proxy-server',
               '--user-data-dir=' + str(directory), '--dump-dom',
               '--virtual-time-budget=6000']
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    url = ORIGIN + '/app?mode=' + mode + '&k=' + KEY
    if value is not None:
        url += '&v=' + value
    command.append(url)
    record = {'case': name, 'profile': profile, 'mode': mode, 'value': value,
              'command': command}
    completed = subprocess.run(command, capture_output=True, text=True,
                               timeout=a.timeout)
    record['rc'] = completed.returncode
    (root / (name + '.stderr.log')).write_text(completed.stderr)
    assert completed.returncode == expect_rc, (name, completed.returncode,
                                               completed.stderr[-1000:])
    match = re.search(r'RESULT:(\{.*?\})</pre>', html.unescape(completed.stdout), re.S)
    assert match, (name, completed.stdout[-2000:])
    observed = json.loads(match.group(1))
    assert 'error' not in observed, (name, observed)
    record['observed'] = observed
    record['profile_files'] = profile_files(directory)
    results.append(record)
    print(json.dumps(record), flush=True)
    return observed


def expect(observed, cookie, ls, extra_absent=None):
    """Exact equality checks; never weakened to tolerate missing/extra state."""
    assert observed['cookie'] == cookie, observed
    assert observed['ls'] == ls, observed
    if cookie is None:
        assert KEY not in observed['allCookieNames'], observed
    else:
        assert observed['allCookieNames'] == [KEY], observed
    if ls is None:
        assert observed['lsKeys'] == [], observed
    if extra_absent is not None:
        assert extra_absent not in (observed['cookie'], observed['ls']), observed


dir_a = root / 'profileA'
dir_b = root / 'profileB'
for d in (dir_a, dir_b):
    assert not d.exists(), 'refusing to reuse an existing profile: ' + str(d)

try:
    # 1. Fresh A: set persistent cookie + LocalStorage, read back in-session.
    obs = run('a_set_read', 'profileA', 'set', 'alpha')
    expect(obs, 'alpha', 'alpha')
    files = results[-1]['profile_files']
    assert files['cookie_db'] and files['local_storage'], files

    # 2. Fresh B: must see nothing of A's data (isolation assertion).
    obs = run('b_sees_nothing', 'profileB', 'read')
    expect(obs, None, None)

    # 3. B writes its own distinct value.
    obs = run('b_set_read', 'profileB', 'set', 'beta')
    expect(obs, 'beta', 'beta')

    # 4. Restart A: retains alpha, unaffected by B's beta.
    obs = run('a_reopen_after_b_write', 'profileA', 'read')
    expect(obs, 'alpha', 'alpha', extra_absent='beta')

    # 5. Restart B: retains beta, never saw alpha.
    obs = run('b_reopen', 'profileB', 'read')
    expect(obs, 'beta', 'beta', extra_absent='alpha')

    # 6. Clear A's test data via web APIs; in-session read is empty.
    obs = run('a_clear', 'profileA', 'clear')
    expect(obs, None, None)

    # 7. Restart A: the clear persisted to A's profile directories.
    obs = run('a_reopen_after_clear', 'profileA', 'read')
    expect(obs, None, None)

    # 8. B restarts again: still intact after A's clear (no cross-profile effect).
    obs = run('b_after_a_clear', 'profileB', 'read')
    expect(obs, 'beta', 'beta')

    # 9. Positive control for the read path: fresh profile C writes, closes,
    #    reopens on the next invocation and reads the same persisted value.
    obs = run('c_persist_set', 'profileC', 'set', 'gamma')
    expect(obs, 'gamma', 'gamma')
    obs = run('c_persist_reopen', 'profileC', 'read')
    expect(obs, 'gamma', 'gamma')
finally:
    server.shutdown()
    server.server_close()

summary = {'origin': ORIGIN, 'key': KEY, 'cases': results,
           'passed': len(results)}
(root / 'summary.json').write_text(json.dumps(summary, indent=2))
print('STORAGE_ISOLATION_OK', len(results), 'cases', flush=True)
