"""Loopback-only WebRTC acceptance smoke; synthetic STUN on 127.0.0.1 only.

Positive control (schema-1 wire, profile accepted) must observe a REAL STUN
Binding request validated against cookie 0x2112A442 plus an srflx candidate
carrying the fake mapped address. Schema-2 streams are expected to be
rejected at startup (rc 5) while the browser gate stands: that outcome is
reported as BLOCKED_BY_SCHEMA2_GATE, never as policy verification.
No external STUN/TURN or public traffic; the responder replies only to the
peer that asked. Absence of traffic is NOT success.
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
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
root = a.work.resolve()
sys.path.insert(0, str((a.repo / 'tests' / 'golden_v2').resolve()))
from gen_golden_v2 import FIELDS, build  # repo-owned tests-only encoder

MAGIC = 0x2112A442
FAKE_IP = '203.0.113.17'
ADVERTISED_PORT = 40113  # XORed with the cookie in success responses
TID_SEEN, VALID_REQS, BAD_MAGIC, RESPONDED = set(), [], 0, []


def xor_addr_attrs():
    """XOR-MAPPED-ADDRESS (0x0020) for FAKE_IP:ADVERTISED_PORT under MAGIC."""
    octets = [int(x) for x in FAKE_IP.split('.')]
    xaddr = bytes([octets[0] ^ (MAGIC >> 24), octets[1] ^ ((MAGIC >> 16) & 0xFF),
                   octets[2] ^ ((MAGIC >> 8) & 0xFF), octets[3] ^ (MAGIC & 0xFF)])
    xport = struct.pack('>H', ADVERTISED_PORT ^ (MAGIC >> 16))
    return b'\x00\x20\x00\x08\x00\x01' + xport + xaddr


def handle_stun(data, addr, sock):
    """Validate a Binding request; answer RFC 5389 success with XOR address."""
    global BAD_MAGIC
    if len(data) < 20:
        return
    mtype, mlen, magic, tid = struct.unpack('>HHI12s', data[:20])
    if magic != MAGIC:
        BAD_MAGIC += 1
        return
    if mtype != 0x0001 or mlen != len(data) - 20 or not any(tid):
        return  # not a well-formed STUN Binding request with a live tid
    TID_SEEN.add(tid)
    VALID_REQS.append({'tid': tid.hex(), 'bytes': len(data)})
    response = struct.pack('>HHI12s', 0x0101, 12, MAGIC, tid) + xor_addr_attrs()
    sock.sendto(response, addr)
    RESPONDED.append(tid.hex())


PAGE = '''<!doctype html><script>
(async()=>{let v={error:'no-result'};try{
const pc=new RTCPeerConnection({iceServers:[{urls:'stun:127.0.0.1:__PORT__'}]});
pc.createDataChannel('probe');const seen=[],errors=[];pc.onicecandidateerror=e=>errors.push({code:e.errorCode,text:e.errorText,url:e.url});let done=new Promise(r=>{pc.onicegatheringstatechange=
()=>{if(pc.iceGatheringState==='complete')r();};setTimeout(r,4000);}); // bounded gather
pc.onicecandidate=e=>{if(e.candidate)seen.push(e.candidate.candidate);};
const offer=await pc.createOffer();
await pc.setLocalDescription(offer);
await done;
v={srflx:seen.filter(c=>c.includes('typ srflx')),host:seen.length,
state:pc.iceGatheringState,added:(pc.localDescription||offer).sdp.includes('m=application'),
sigstate:pc.signalingState,
sdp:((pc.localDescription||offer).sdp.split(String.fromCharCode(13,10)).filter(x=>x.startsWith('m='))).join(' | '),
connstate:pc.connectionState,ice:pc.iceConnectionState,errors,all:seen};
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
        data = PAGE.replace('__PORT__', str(stun_port)).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', 0))
stun_port = sock.getsockname()[1]
stop = threading.Event()


def stun_loop():
    sock.settimeout(0.2)
    while not stop.is_set():
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        handle_stun(data, addr, sock)


threading.Thread(target=stun_loop, daemon=True).start()
url = 'http://127.0.0.1:' + str(server.server_port) + '/'
results = []


def launch(name, flags):
    """Run chrome until the page POSTs its gather report (bounded, 60s)."""
    directory = root / name
    directory.mkdir(exist_ok=True)
    command = [str(a.chrome.resolve()), '--headless', '--no-first-run',
        '--disable-background-networking', '--disable-component-update',
        '--disable-sync', '--disable-default-apps', '--no-proxy-server',
        '--user-data-dir=' + str(directory)]
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    start = len(REPORTS)
    proc = subprocess.Popen(command + flags + [url],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 60  # subprocess bound, per contract
    timed_out = False
    try:
        while time.monotonic() < deadline:
            if len(REPORTS) > start or proc.poll() is not None:
                break
            time.sleep(0.1)
        else:
            timed_out = True
    finally:
        # Terminate only the chrome instance we own; never leave it running.
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
    return proc.returncode, report, timed_out


def write_summary(extra=None):
    payload = {'harness': 'webrtc-loopback-smoke', 'results': results}
    if extra:
        payload.update(extra)
    (root / 'summary.json').write_text(json.dumps(payload, indent=2))


def positive_control(wire):
    """Schema-1 profile accepted; requires real STUN traffic AND srflx."""
    before = len(VALID_REQS)
    rc, value, timed_out = launch('positive-control',
                                  [])
    case = {'case': 'positive-control', 'exit_rc': rc, 'timed_out': timed_out,
            'stun_requests_seen': len(VALID_REQS) - before}
    if rc == 5:
        case['status'] = 'HARNESS_FAIL: schema-1 profile rejected (rc 5)'
    elif value is None:
        case['status'] = 'HARNESS_FAIL: page never reported its gather result'
        value = {}
    else:
        value = json.loads(value) if isinstance(value, str) else value
    case['page'] = value
    if 'status' in case:
        results.append(case)
        print(json.dumps(case), flush=True)
        return
    if 'error' in value:
        case['status'] = 'HARNESS_FAIL: page error ' + value['error']
    elif len(VALID_REQS) - before == 0:
        # No traffic is explicitly NOT success.
        case['status'] = 'HARNESS_FAIL: no validated STUN Binding request observed'
    elif not value['srflx']:
        case['status'] = 'HARNESS_FAIL: STUN answered but no srflx candidate'
    elif not any(FAKE_IP in c for c in value['srflx']):
        case['status'] = 'HARNESS_FAIL: srflx lacks mapped ' + FAKE_IP
    else:
        case['status'] = 'PASS_POSITIVE_CONTROL'
    results.append(case)
    print(json.dumps(case), flush=True)


def schema2_gate(wire_name, policy):
    stream = build({**FIELDS, 'webrtc_ip_handling_policy': policy})
    wire = root / ('profile-schema2-policy%d.wire' % policy)
    wire.write_bytes(bytes(stream))
    rc, _report, timed_out = launch(wire_name,
                                    ['--lengsuo-profile-wire=' + str(wire)])
    case = {'case': wire_name, 'rc': rc, 'timed_out': timed_out,
            'schema': 2, 'webrtc_ip_handling_policy': policy}
    if rc == 5:
        case['status'] = 'BLOCKED_BY_SCHEMA2_GATE'
        case['note'] = 'expected rejection; policy runtime NOT verified'
    elif rc == 0:
        case['status'] = ('UNEXPECTED_GATE_OPEN: schema 2 accepted while the '
                          'rejection gate should stand; runtime injection is '
                          'not yet implemented, treat as harness failure')
    else:
        case['status'] = 'HARNESS_FAIL: unexpected rc %d' % rc
    results.append(case)
    print(json.dumps(case), flush=True)


try:
    # Independent UDP self-control; never counted as browser STUN traffic.
    tid = b'fake-probe01'
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(2)
    try:
        udp.sendto(struct.pack('>HHI12s',1,0,MAGIC,tid),('127.0.0.1',stun_port))
        reply,_ = udp.recvfrom(2048)
        kind,size,cookie,echo = struct.unpack('>HHI12s',reply[:20])
        assert (kind,size,cookie,echo)==(0x0101,len(reply)-20,MAGIC,tid)
        assert reply[20:]==xor_addr_attrs()
        print(json.dumps({'udp_self_test':'PASS','response_bytes':len(reply)}),flush=True)
    finally:
        udp.close()
    schema1 = root / 'profile-schema1.wire'
    schema1.write_bytes(bytes(build({**FIELDS, 'schema_version': 1})))
    positive_control(schema1)
    for policy in (0, 1, 2, 3):
        schema2_gate('schema2-policy%d' % policy, policy)
finally:
    stop.set()
    server.shutdown()
    server.server_close()
    sock.close()

stun_check = {'validated_binding_requests': len(VALID_REQS),
              'malformed_or_foreign_packets': BAD_MAGIC,
              'success_responses_sent': len(RESPONDED),
              'response_tids_echo_requests': all(t in {r['tid'] for r in VALID_REQS}
                                                 for t in RESPONDED)}
write_summary({'stun_server': stun_check,
               'policy_runtime_verified': False,
               'blocked_by_gate': [r['case'] for r in results
                                   if r.get('status') == 'BLOCKED_BY_SCHEMA2_GATE']})
print(json.dumps(stun_check), flush=True)

# A failed positive control must fail the process, regardless of gate rejections.
ok = len(results)==5 and results[0].get("status")=="PASS_POSITIVE_CONTROL" and all(x.get("status")=="BLOCKED_BY_SCHEMA2_GATE" for x in results[1:])
raise SystemExit(0 if ok else 1)
