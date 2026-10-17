"""Synthetic loopback-only timezone acceptance; no public services."""
import argparse
import html
import http.server
import json
import os
import re
import subprocess
import threading
import importlib.util
from pathlib import Path

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--'+field, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

def fixture(file, name):
    payload = re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]+)\}', file.read_text(), re.S).group(1)
    return bytes(map(int, re.findall(r'\d+', payload)))

profile = root/'profile.wire'
profile.write_bytes(fixture(a.repo/'native/golden.inc', 'kGolden'))
catalog = root/'catalog.wire'
raw = fixture(a.repo/'native/profile_catalog_test.cc', 'kGoldenCatalog')
catalog.write_bytes(raw)
page = '''<!doctype html><pre id="result">waiting</pre><script>
(async()=>{try{
const probe=()=>({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,offset:new Date().getTimezoneOffset()});
const worker=await new Promise((resolve,reject)=>{const w=new Worker('/worker.js');w.onmessage=e=>{w.terminate();resolve(e.data)};w.onerror=e=>reject(Error(e.message));setTimeout(()=>reject(Error('worker timeout')),4000)});
document.querySelector('#result').textContent=JSON.stringify({main:probe(),worker});
}catch(e){document.querySelector('#result').textContent=JSON.stringify({error:String(e)})}})();
</script>'''
worker_script = 'postMessage({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,offset:new Date().getTimezoneOffset()});'

probe = "({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,winter:new Date('2020-01-01T12:00:00Z').getTimezoneOffset(),summer:new Date('2020-07-01T12:00:00Z').getTimezoneOffset()})"
page = page.replace('({tz:Intl.DateTimeFormat().resolvedOptions().timeZone,offset:new Date().getTimezoneOffset()})', probe)
worker_script = 'postMessage('+probe+');'
spec = importlib.util.spec_from_file_location('wire_fixture', a.repo/'tests/golden_v2/gen_golden_v2.py')
encoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(encoder)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = (worker_script if self.path == '/worker.js' else page).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/javascript' if self.path == '/worker.js' else 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:'+str(server.server_port)+'/'
results = []

def run(name, flags, expected, zone=None, offsets=None):
    # Keep owned test directories for diagnosis; do not touch other profiles.
    directory = root/name
    directory.mkdir(exist_ok=False)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
        '--disable-background-networking', '--disable-component-update',
        '--disable-sync', '--disable-default-apps', '--no-proxy-server',
        '--user-data-dir='+str(directory), '--dump-dom', '--virtual-time-budget=5000']
    if os.name != 'nt':
        command.append('--no-sandbox')
    completed = subprocess.run(command+flags+[url], capture_output=True, text=True, timeout=60)
    (root/(name+'.stderr.log')).write_text(completed.stderr)
    assert completed.returncode == expected, (name, completed.returncode, completed.stderr[-1000:])
    result = {'case': name, 'rc': completed.returncode}
    if expected == 0:
        match = re.search(r'<pre id="result">(.*?)</pre>', completed.stdout, re.S)
        assert match, (name, completed.stdout)
        value = json.loads(html.unescape(match.group(1)))
        assert 'error' not in value, value
        if name in ('managed', 'profile-only') or zone:
            # Golden wire carries timezone "UTC": Date and Intl must agree
            # on the profile zone in the main frame and in workers.
            target = {'tz':zone or 'UTC', 'winter':(offsets or (0,0))[0],
                      'summer':(offsets or (0,0))[1]}
            assert value['main'] == target, value
            assert value['worker'] == target, value
        else:
            # Upstream case: valid non-empty host zone, worker in sync.
            assert value['main']['tz'], value
            assert value['main'] == value['worker'], value
        result['observed'] = value
    results.append(result)
    print(json.dumps(result), flush=True)

try:
    run('upstream', [], 0)
    for zone, offsets in [('America/New_York',(300,240)),
                          ('Europe/London',(0,-60)), ('Etc/GMT-8',(-480,-480)),
                          ('UTC',(0,0)), ('Mars/Olympus',None), ('GMT+08:00',None)]:
        name = zone.replace('/','-').replace(':','-')
        wire = root/(name+'.wire')
        wire.write_bytes(bytes(encoder.build({**encoder.FIELDS,
            'schema_version':1, 'timezone':zone})))
        run(name, ['--lengsuo-profile-wire='+str(wire)],
            0 if offsets else 5, zone if offsets else None, offsets)
    flags = ['--lengsuo-profile-wire='+str(profile), '--lengsuo-catalog-wire='+str(catalog)]
    run('managed', flags, 0)
    run('profile-only', [flags[0]], 0)
    run('catalog-without-profile', [flags[1]], 5)
    for name, wire in [('wrong-pool', raw[:12]+b'2'+raw[13:]),
            ('missing-slot', raw[:22]+bytes([8])+raw[23:]), ('invalid', b'invalid'), ('empty', b'')]:
        bad = root/(name+'.wire')
        bad.write_bytes(wire)
        run(name, [flags[0], '--lengsuo-catalog-wire='+str(bad)], 5)
    run('relative', [flags[0], '--lengsuo-catalog-wire=catalog.wire'], 5)
finally:
    server.shutdown()
    server.server_close()
(root/'summary.json').write_text(json.dumps({'passed':len(results), 'results':results}, indent=2))
