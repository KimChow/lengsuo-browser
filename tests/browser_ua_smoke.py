"""Synthetic loopback-only UA/UA-CH acceptance; no public services."""
import argparse
import html
import http.server
import json
import os
import re
import subprocess
import threading
from pathlib import Path

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--'+field, type=Path, required=True)
p.add_argument('--check-language', action='store_true')
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()

def fixture(file, name):
    payload = re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]+)\}', file.read_text(), re.S).group(1)
    return bytes(map(int, re.findall(r'\d+', payload)))

profile = root/'profile.wire'
profile_bytes = fixture(a.repo/'native/golden.inc', 'kGolden')
if a.check_language:
    assert profile_bytes.count(b'en-US') == 1
    profile_bytes = profile_bytes.replace(b'en-US', b'fr-FR')
profile.write_bytes(profile_bytes)
catalog = root/'catalog.wire'
raw = fixture(a.repo/'native/profile_catalog_test.cc', 'kGoldenCatalog')
catalog.write_bytes(raw)
page = '''<!doctype html><pre id="result">waiting</pre><script>
(async()=>{try {
const hints=['architecture','bitness','model','platformVersion','fullVersionList'];
const h=await navigator.userAgentData.getHighEntropyValues(hints);
const net=await (await fetch('/echo')).json();
const worker=await new Promise((resolve,reject)=>{const w=new Worker('/worker.js');w.onmessage=e=>{w.terminate();resolve(e.data)};w.onerror=e=>reject(Error(e.message));setTimeout(()=>reject(Error('worker timeout')),4000)});
document.querySelector('#result').textContent=JSON.stringify({ua:navigator.userAgent,language:navigator.language,languages:navigator.languages,locale:new Intl.DateTimeFormat().resolvedOptions().locale,brands:navigator.userAgentData.brands,mobile:navigator.userAgentData.mobile,platform:navigator.userAgentData.platform,high:h,net,worker});
}catch(e){document.querySelector('#result').textContent=JSON.stringify({error:String(e)})}})();
</script>'''
worker_script = '''(async()=>{try{postMessage({ua:navigator.userAgent,language:navigator.language,languages:navigator.languages,locale:new Intl.DateTimeFormat().resolvedOptions().locale,high:await navigator.userAgentData.getHighEntropyValues(['architecture','bitness','model','platformVersion','fullVersionList'])})}catch(e){postMessage({error:String(e)})}})();'''

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = (json.dumps({k.lower(): v for k, v in self.headers.items()
            if k.lower() in ('user-agent', 'accept-language') or k.lower().startswith('sec-ch-ua')})
            if self.path == '/echo' else worker_script if self.path == '/worker.js' else page).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json' if self.path == '/echo' else 'text/javascript' if self.path == '/worker.js' else 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Accept-CH', 'Sec-CH-UA-Full-Version-List, Sec-CH-UA-Platform-Version, Sec-CH-UA-Arch, Sec-CH-UA-Bitness, Sec-CH-UA-Model')
        self.end_headers()
        self.wfile.write(data)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:'+str(server.server_port)+'/'
results = []

def run(name, flags, expected):
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
        assert value['net']['user-agent'] == value['ua'], value
        assert value['worker']['ua'] == value['ua'], value
        if a.check_language and name in ('managed', 'profile-only'):
            assert value['language'] == 'fr-FR', value
            assert value['languages'] == ['fr-FR'], value
            assert value['worker']['language'] == 'fr-FR', value
            assert value['worker']['languages'] == ['fr-FR'], value
            assert value['locale'] == 'fr-FR', value
            assert value['worker']['locale'] == 'fr-FR', value
            assert value['net']['accept-language'] == 'fr-FR,fr;q=0.9', value
        if name == 'managed':
            assert value['ua'] == 'Synth/1.0 Test Env7', value
            assert value['net']['user-agent'] == value['ua'], value
            assert value['platform'] == 'TestOS' and value['high']['platformVersion'] == '10.0.0', value
            assert value['high']['architecture'] == 'x86' and value['high']['bitness'] == '64', value
            assert value['brands'] == [{'brand':'Alpha','version':'1.2'}, {'brand':'Beta','version':'3.4'}], value
            assert value['high']['fullVersionList'] == [{'brand':'Alpha','version':'1.2.7.8'}, {'brand':'Beta','version':'3.4.9.0'}], value
            assert value['net']['sec-ch-ua'] == '"Alpha";v="1.2", "Beta";v="3.4"', value
            assert value['net']['sec-ch-ua-platform'] == '"TestOS"', value
            assert value['net']['sec-ch-ua-full-version-list'] == '"Alpha";v="1.2.7.8", "Beta";v="3.4.9.0"', value
            for key, expected_value in [('arch', '"x86"'), ('bitness', '"64"'),
                    ('model', '""'), ('mobile', '?0'), ('platform-version', '"10.0.0"')]:
                assert value['net']['sec-ch-ua-'+key] == expected_value, value
            assert value['worker']['ua'] == value['ua'], value
            assert value['worker']['high'] == value['high'], value
        else:
            assert value['ua'] != 'Synth/1.0 Test Env7', value
        result['observed'] = value
    results.append(result)
    print(json.dumps(result), flush=True)

try:
    flags = ['--lengsuo-profile-wire='+str(profile), '--lengsuo-catalog-wire='+str(catalog)]
    run('managed', flags, 0)
    run('upstream', [], 0)
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
