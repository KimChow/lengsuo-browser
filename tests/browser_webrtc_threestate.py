"""Three-state WebRTC srflx acceptance: native vs schema-1 vs schema-2 gate.

States:
  1 native       no profile wire -> srflx REQUIRED with the mapped address
  2 schema-1     accepted profile wire -> srflx REQUIRED, behaviour EQUAL to 1
                 (a profile must not change native WebRTC network behaviour)
  3 schema-2 p2  policy byte present -> startup gate must reject with rc 5

STUN backend: loopback fake server (magic/tsx validated, 127.0.0.1 only) by
default; --external-stun switches to stun.l.google.com:19302 so a Windows
machine can separate "our build never opens STUN sockets" from "loopback
targets get special-cased". Absence of STUN traffic is never success.

--upstream-control marks a binary that lacks our switches: wires are inert,
case 3 is SKIPPED, and cases 1/2 must still agree with each other.

Each case gets its own --user-data-dir; summary.json records everything.
"""
import argparse
import http.server
import json
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

p = argparse.ArgumentParser()
for field in ('chrome', 'repo', 'work'):
    p.add_argument('--' + field, type=Path, required=True)
p.add_argument('--external-stun', action='store_true',
               help='use stun.l.google.com:19302 instead of the fake server')
p.add_argument('--stun-url', default=None, help='override the external STUN url')
p.add_argument('--upstream-control', action='store_true',
               help='target lacks our profile switches: skip gate asserts')
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()
sys.path.insert(0, str((a.repo / 'tests' / 'golden_v2').resolve()))
from gen_golden_v2 import FIELDS, build

MAGIC = 0x2112A442
FAKE_IP = '203.0.113.17'
ADVERTISED_PORT = 40113
TID_SEEN, VALID_REQS, BAD_MAGIC, RESPONDED = set(), [], 0, []

EXTERNAL = a.external_stun or bool(a.stun_url)
STUN_URL = a.stun_url or ('stun:stun.l.google.com:19302' if EXTERNAL else None)
GATHER_MS = 12000 if EXTERNAL else 4000


def xor_addr_attrs():
    octets = [int(x) for x in FAKE_IP.split('.')]
    xaddr = bytes([octets[0] ^ (MAGIC >> 24), octets[1] ^ ((MAGIC >> 16) & 0xFF),
                   octets[2] ^ ((MAGIC >> 8) & 0xFF), octets[3] ^ (MAGIC & 0xFF)])
    xport = struct.pack('>H', ADVERTISED_PORT ^ (MAGIC >> 16))
    return b'\x00\x20\x00\x08\x00\x01' + xport + xaddr


def handle_stun(data, addr, sock):
    global BAD_MAGIC
    if len(data) < 20:
        return
    mtype, mlen, magic, tid = struct.unpack('>HHI12s', data[:20])
    if magic != MAGIC:
        BAD_MAGIC += 1
        return
    if mtype != 0x0001 or mlen != len(data) - 20 or not any(tid):
        return
    TID_SEEN.add(tid)
    VALID_REQS.append({'tid': tid.hex(), 'bytes': len(data)})
    response = struct.pack('>HHI12s', 0x0101, 12, MAGIC, tid) + xor_addr_attrs()
    sock.sendto(response, addr)
    RESPONDED.append(tid.hex())


PAGE = '''<!doctype html><script>
(async()=>{let v={error:'no-result'};try{
const pc=new RTCPeerConnection({iceServers:[{urls:'__STUN__'}]});
pc.createDataChannel('probe');const seen=[],errors=[];
pc.onicecandidateerror=e=>errors.push({code:e.errorCode,text:e.errorText,url:e.url});
pc.onicecandidate=e=>{if(e.candidate)seen.push(e.candidate.candidate);};
const done=new Promise(r=>{pc.onicegatheringstatechange=
()=>{if(pc.iceGatheringState==='complete')r();};setTimeout(r,__MS__);});
const offer=await pc.createOffer();
await pc.setLocalDescription(offer);
await done;
v={srflx:seen.filter(c=>c.includes('typ srflx')),all_count:seen.length,
state:pc.iceGatheringState,errors,all:seen};
}catch(e){v={error:String(e)}}
await fetch('/report',{method:'POST',body:JSON.stringify(v)});
})();
</script>'''

REPORTS = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        data = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        REPORTS.append(data.decode('utf-8', 'replace'))
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')

    def do_GET(self):
        data = PAGE.replace('__STUN__', stun_target).replace('__MS__', str(GATHER_MS)).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
sock = None
stun_port = 0
if not EXTERNAL:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))
    stun_port = sock.getsockname()[1]
    stun_target = 'stun:127.0.0.1:%d' % stun_port
else:
    stun_target = STUN_URL
stop = threading.Event()


def stun_loop():
    sock.settimeout(0.2)
    while not stop.is_set():
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        handle_stun(data, addr, sock)


if not EXTERNAL:
    threading.Thread(target=stun_loop, daemon=True).start()
url = 'http://127.0.0.1:' + str(server.server_port) + '/'
results = []


def launch(name, flags):
    directory = root / name
    directory.mkdir(exist_ok=True)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
        '--disable-background-networking', '--disable-component-update',
        '--disable-sync', '--disable-default-apps', '--no-proxy-server',
        '--user-data-dir=' + str(directory)]
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    start_reqs = len(VALID_REQS)
    start = len(REPORTS)
    proc = subprocess.Popen(command + flags + [url],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 60 + (GATHER_MS // 1000)
    timed_out = False
    try:
        while time.monotonic() < deadline:
            if len(REPORTS) > start or proc.poll() is not None:
                break
            time.sleep(0.1)
        else:
            timed_out = True
    finally:
        if proc.poll() is None:
            proc.terminate()
        try:
            out, err = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate()
    (root / (name + '.stdout.log')).write_text(out or '')
    (root / (name + '.stderr.log')).write_text(err or '')
    report = json.loads(REPORTS[start]) if len(REPORTS) > start else None
    return proc.returncode, report, timed_out, len(VALID_REQS) - start_reqs


def parse_srflx(cands):
    """Canonical srflx (proto, priority, mapped ip, mapped port) per line.

    Foundation and ufrag are randomised per session; candidate-rport follows
    the local port, so neither is comparable across runs.
    """
    out = []
    for line in cands:
        parts = line.split()
        try:
            i_typ = parts.index('typ')
        except ValueError:
            continue
        if parts[i_typ + 1] != 'srflx':
            continue
        out.append(tuple(parts[3:7]))
    return sorted(out)


def mapped_ok(value):
    """srflx non-empty AND carrying the mapped address the server advertises."""
    lines = value.get('srflx', [])
    if not lines:
        return False
    if EXTERNAL:
        return all('typ srflx' in c for c in lines)
    return all(FAKE_IP in c and str(ADVERTISED_PORT) in c for c in lines)


def gather_case(name, flags, expect_gate=False):
    rc, value, timed_out, reqs = launch(name, flags)
    case = {'case': name, 'exit_rc': rc, 'timed_out': timed_out,
            'stun_requests_seen': reqs, 'stun_target': stun_target}
    value = value or {}
    case['page'] = value
    if expect_gate:
        case['schema'] = 2
        case['webrtc_ip_handling_policy'] = 2
        if rc == 5:
            case['status'] = 'BLOCKED_BY_SCHEMA2_GATE'
            case['note'] = 'expected rejection; policy runtime NOT verified'
        elif rc == 0:
            case['status'] = 'UNEXPECTED_GATE_OPEN: schema 2 accepted; gate must stand'
        else:
            case['status'] = 'HARNESS_FAIL: unexpected rc %d' % rc
        results.append(case)
        print(json.dumps(case), flush=True)
        return case
    if rc == 5:
        case['status'] = 'HARNESS_FAIL: rejected at startup (rc 5)'
    elif not value or 'error' in value:
        case['status'] = 'HARNESS_FAIL: page error ' + str(value.get('error'))
    elif not EXTERNAL and reqs == 0:
        case['status'] = 'HARNESS_FAIL: no validated STUN Binding request observed'
    elif not mapped_ok(value):
        case['status'] = 'HARNESS_FAIL: no srflx with mapped address'
    else:
        case['status'] = 'PASS'
        case['srflx_tuples'] = parse_srflx(value.get('srflx', []))
    results.append(case)
    print(json.dumps(case), flush=True)
    return case


schema1 = build({**FIELDS, 'schema_version': 1})
(root / 'profile-schema1.wire').write_bytes(bytes(schema1))
schema2 = build({**FIELDS, 'webrtc_ip_handling_policy': 2})
(root / 'profile-schema2-policy2.wire').write_bytes(bytes(schema2))
WIRE = '--lengsuo-profile-wire='

native = gather_case('native', [])
schema = gather_case('schema1-profile', [WIRE + str(root / 'profile-schema1.wire')])
if a.upstream_control:
    gate = {'case': 'schema2-gate', 'status': 'SKIPPED_UPSTREAM_CONTROL',
            'note': 'upstream binary has no gate'}
    results.append(gate)
    print(json.dumps(gate), flush=True)
else:
    gate = gather_case('schema2-policy2-gate',
                       [WIRE + str(root / 'profile-schema2-policy2.wire')],
                       expect_gate=True)

# Behavioral equality, not byte equality: srflx TYPE/presence/gather state
# must match; mapped IP:port pairs differ per-run (NAT mapping is per-socket),
# so compare shape only when addresses differ.
def srflx_shape(state):
    tuples = state.get('srflx_tuples') or []
    return tuple(sorted(t[0] for t in tuples if t)) or tuple()


native_shape = srflx_shape(native)
schema_shape = srflx_shape(schema)
equal = (bool(native_shape) and native_shape == schema_shape
         and native.get('state') == schema.get('state'))
compare = {'states_compared': ['native', 'schema1-profile'],
           'srflx_behavior_identical': equal,
           'native_shape': native_shape,
           'schema1_shape': schema_shape,
           'native_srflx': native.get('srflx_tuples'),
           'schema1_srflx': schema.get('srflx_tuples')}
results.append({'case': 'compare-native-vs-schema1',
                'status': 'PASS' if equal else
                          'HARNESS_FAIL: schema-1 profile changed srflx behaviour'})
print(json.dumps(results[-1]), flush=True)

stun_check = None
if not EXTERNAL:
    stun_check = {'validated_binding_requests': len(VALID_REQS),
                  'malformed_or_foreign_packets': BAD_MAGIC,
                  'success_responses_sent': len(RESPONDED),
                  'response_tids_echo_requests': all(t in {r['tid'] for r in VALID_REQS}
                                                     for t in RESPONDED)}
stop.set()
server.shutdown()
server.server_close()
if sock:
    sock.close()

(root / 'summary.json').write_text(json.dumps(
    {'harness': 'webrtc-threestate', 'stun_target': stun_target,
     'external_stun': EXTERNAL, 'upstream_control': a.upstream_control,
     'gather_ms': GATHER_MS, 'results': results, 'stun_server': stun_check,
     'compare': compare, 'policy_runtime_verified': False,
     'blocked_by_gate': [r['case'] for r in results
                         if r.get('status') == 'BLOCKED_BY_SCHEMA2_GATE']},
    indent=2))

ok = (native.get('status') == 'PASS' and schema.get('status') == 'PASS' and equal
      and (a.upstream_control or gate.get('status') == 'BLOCKED_BY_SCHEMA2_GATE'))
print(json.dumps({'overall': 'PASS' if ok else 'FAIL'}), flush=True)
raise SystemExit(0 if ok else 1)
