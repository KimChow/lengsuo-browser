// fp_native_compare.js — WebGL/hardwareConcurrency/deviceMemory 三态对比验收。
// 与 canvas_native_compare.py 同构：同一 chrome 二进制跑两态（native / profile），
// 断言有画像读数 == fp_overrides 人脸表中的确定值，且无画像读数与上游一致。
//
// 用法: node fp_native_compare.js --chrome <path> --work <dir> --seed <hex> --webgl <slot>
// 退出码: 0 = 全 PASS；1 = 有 FAIL；2 = 环境错误。
'use strict';
const { spawn } = require('child_process');
const http = require('http');
const fs = require('fs');
const path = require('path');

// ---- arg parsing ----
function arg(name, dflt) {
  const i = process.argv.indexOf('--' + name);
  return i > 0 ? process.argv[i + 1] : dflt;
}
const CHROME = arg('chrome');
const WORK = arg('work');
const SEED = arg('seed', '0123456789abcdef');
const WEBGL_SLOT = parseInt(arg('webgl', '1'), 10);
const HW_SLOT = parseInt(arg('hw', '1'), 10);
if (!CHROME || !WORK) {
  console.error('usage: node fp_native_compare.js --chrome <path> --work <dir>');
  process.exit(2);
}
fs.mkdirSync(WORK, { recursive: true });

// ---- expected faces (must mirror lengsuo/native/fp_overrides.cc) ----
const FACES = [
  ['Google Inc (NVIDIA)', 'ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)'],
  ['Google Inc (AMD)', 'ANGLE (AMD, AMD Radeon RX 6600 Direct3D11 vs_5_0 ps_5_0, D3D11)'],
  ['Google Inc (Intel)', 'ANGLE (Intel, Intel(R) UHD Graphics 770 Direct3D11 vs_5_0 ps_5_0, D3D11)'],
  ['Google Inc (NVIDIA)', 'ANGLE (NVIDIA, NVIDIA GeForce GTX 1660 SUPER Direct3D11 vs_5_0 ps_5_0, D3D11)'],
];
const CORES = [4, 6, 8, 12, 16];
const MEM = [4, 8];
const seedNum = BigInt('0x' + SEED);
const EXP_VENDOR = FACES[WEBGL_SLOT % FACES.length][0];
const EXP_RENDERER = FACES[WEBGL_SLOT % FACES.length][1];
const EXP_CORES = CORES[Number((seedNum >> BigInt((HW_SLOT % 7) * 3)) % BigInt(CORES.length))];
const EXP_MEM = MEM[Number((seedNum >> 17n) % BigInt(MEM.length))];

// ---- page: collect readings, POST back ----
const PAGE = `<!doctype html><meta charset="utf-8"><canvas id="c" width="4" height="4"></canvas><script>
const out = {};
try {
  const gl = document.getElementById('c').getContext('webgl');
  out.vendor = gl.getParameter(gl.VENDOR);
  out.renderer = gl.getParameter(gl.RENDERER);
  const dbg = gl.getExtension('WEBGL_debug_renderer_info');
  if (dbg) {
    out.unmaskedVendor = gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL);
    out.unmaskedRenderer = gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL);
  } else { out.extErr = 'no debug ext'; }
} catch (e) { out.glErr = String(e); }
out.cores = navigator.hardwareConcurrency;
out.mem = navigator.deviceMemory;
fetch('/report', { method: 'POST', body: JSON.stringify(out) }).finally(() => setTimeout(() => close(), 100));
</script>`;

const REPORTS = [];
class H extends http.Server {
  constructor() { super((req, res) => this.route(req, res)); }
  route(req, res) {
    if (req.method === 'POST' && req.url === '/report') {
      let b = '';
      req.on('data', (c) => (b += c));
      req.on('end', () => { REPORTS.push(JSON.parse(b)); res.end('ok'); });
      return;
    }
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(PAGE);
  }
}
const srv = new H();
srv.listen(0, '127.0.0.1');

// ---- runner ----
function grab(tag, extraFlags) {
  return new Promise((resolve) => {
    const n0 = REPORTS.length;
    const dir = path.join(WORK, tag);
    fs.mkdirSync(dir, { recursive: true });
    const args = ['--headless', '--no-sandbox', '--no-first-run',
      '--disable-sync', '--user-data-dir=' + dir, ...extraFlags, 'http://127.0.0.1:' + srv.address().port + '/'];
    const p = spawn(CHROME, args, { stdio: 'ignore' });
    const dl = Date.now() + 40000;
    const t = setInterval(() => {
      if (REPORTS.length > n0 || Date.now() > dl) {
        clearInterval(t);
        if (!p.killed) p.kill();
        resolve(REPORTS[n0] || null);
      }
    }, 150);
  });
}

(async () => {
  const results = [];
  const push = (name, ok, detail) => results.push({ case: name, status: ok ? 'PASS' : 'FAIL', detail });

  const native = await grab('native', []);
  const managed = await grab('profile', [
    '--lengsuo-managed-fp=' + SEED + ':0:0:0:0:' + HW_SLOT + ':0:' + WEBGL_SLOT,
  ]);

  if (!native) push('native-readback', false, 'no report from native run');
  if (!managed) push('profile-readback', false, 'no report from profile run');
  if (native && managed) {
    // 1. native must NOT contain any face string (upstream behavior intact)
    const nativeClean = FACES.every(([, r]) => native.renderer !== r && native.unmaskedRenderer !== r);
    push('native-not-overridden', nativeClean,
      nativeClean ? 'native renderer untouched' : `native renderer = ${native.renderer}`);

    // 2. managed VENDOR/RENDERER == expected face
    push('managed-vendor', managed.vendor === EXP_VENDOR, `got=${managed.vendor} want=${EXP_VENDOR}`);
    push('managed-renderer', managed.renderer === EXP_RENDERER, `got=${(managed.renderer || '').slice(0, 60)}`);
    push('managed-unmasked-vendor', managed.unmaskedVendor === EXP_VENDOR, `got=${managed.unmaskedVendor}`);
    push('managed-unmasked-renderer', managed.unmaskedRenderer === EXP_RENDERER, `got=${(managed.unmaskedRenderer || '').slice(0, 60)}`);

    // 3. hardwareConcurrency / deviceMemory
    push('managed-cores', managed.cores === EXP_CORES, `got=${managed.cores} want=${EXP_CORES}`);
    push('managed-mem', managed.mem === EXP_MEM, `got=${managed.mem} want=${EXP_MEM}`);

    // 4. determinism: same input twice -> same output
    const managed2 = await grab('profile2', [
      '--lengsuo-managed-fp=' + SEED + ':0:0:0:0:' + HW_SLOT + ':0:' + WEBGL_SLOT,
    ]);
    if (managed2) {
      push('deterministic', managed2.renderer === managed.renderer && managed2.cores === managed.cores,
        'two runs equal');
    }
  }

  const ok = results.every((r) => r.status === 'PASS');
  const summary = { ok, results };
  fs.writeFileSync(path.join(WORK, 'summary.json'), JSON.stringify(summary, null, 2));
  for (const r of results) console.log(r.status, r.case, r.detail || '');
  console.log(ok ? 'OVERALL PASS' : 'OVERALL FAIL');
  srv.close();
  process.exit(ok ? 0 : 1);
})();
