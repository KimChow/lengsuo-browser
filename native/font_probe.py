"""Step 1 of Task C: measure the NATIVE `measureText` integer distribution.

The ruling (docs/cc-task-rulings.md §C) is explicit that the perturbation band
must be chosen from measurement, not guessed: font metrics are integer or 1/64
px values, too small a shift is undetectable and too large one moves glyphs
visibly. This script drives a real browser (no injection, stock or patched) and
reports, for a range of font sizes and strings:

  * the raw float widths and their fractional part in 1/64 units,
  * how many 1/64 steps each width sits from the nearest whole pixel,
  * the per-character increment (width / n_chars), which is what a glyph
    advance (the perturbation target) actually contributes.

Output is JSON on stdout plus a human table on stderr, so a run can be pasted
into the report as the evidence for the chosen band.

Usage (research machine):
    python3 -I native/font_probe.py --chrome <src>/out/lengsuo/chrome \
        --work /tmp/font-probe
    python3 -I native/font_probe.py --chrome <stock chrome> --work /tmp/fp2

Nothing is written outside --work; loopback HTTP only; no production constants.
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

SIZES = [12, 14, 16, 20, 24, 32, 48]
TEXTS = ['m', 'i', 'W', 'mmmmmmmmmm', 'Hello, World!', '0', 'llllllllll']
UNITS_PER_PX = 64  # Blink's LayoutUnit quantum


def build_page():
    return '''<!doctype html><pre id="result">waiting</pre><script>
const c = document.createElement('canvas').getContext('2d');
const sizes = %s;
const texts = %s;
const rows = [];
for (const size of sizes) {
  c.font = size + 'px serif';
  for (const t of texts) {
    const w = c.measureText(t).width;
    rows.push({size: size, text: t, width: w, n: t.length});
  }
}
document.querySelector('#result').textContent = JSON.stringify(rows);
</script>''' % (json.dumps(SIZES), json.dumps(TEXTS))


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = build_page().encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--chrome', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    user_data = args.work / 'user-data-dir'
    user_data.mkdir(exist_ok=True)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = 'http://127.0.0.1:%d/' % server.server_port

    command = [str(args.chrome.resolve()), '--headless', '--no-first-run',
               '--disable-background-networking', '--disable-component-update',
               '--disable-sync', '--disable-default-apps', '--no-proxy-server',
               '--user-data-dir=' + str(user_data), '--dump-dom',
               '--virtual-time-budget=5000']
    if sys.platform != 'win32':
        command.append('--no-sandbox')
    try:
        completed = subprocess.run(command + [url], capture_output=True,
                                   text=True, timeout=60)
    finally:
        server.shutdown()
        server.server_close()

    if completed.returncode != 0:
        print(json.dumps({'error': 'rc=%d' % completed.returncode,
                          'stderr': completed.stderr[-500:]}))
        return 1
    match = re.search(r'<pre id="result">(.*?)</pre>', completed.stdout, re.S)
    if not match:
        print(json.dumps({'error': 'no result element'}))
        return 1
    rows = json.loads(html.unescape(match.group(1)))

    analysis = []
    for row in rows:
        width = row['width']
        units = width * UNITS_PER_PX
        step = width / row['n'] if row['n'] else 0.0
        analysis.append({
            'size': row['size'], 'text': row['text'], 'n': row['n'],
            'width': width,
            'units_64': round(units, 3),
            'frac_units': round(units - round(units), 3),
            'per_char': round(step, 6),
            'per_char_units': round(step * UNITS_PER_PX, 3),
        })
    analysis.sort(key=lambda a: (a['size'], a['text']))

    print('%-5s %-13s %12s %10s %9s %11s' %
          ('size', 'text', 'width', 'units(1/64)', 'frac', 'per-char(1/64)'))
    for a in analysis:
        print('%-5d %-13s %12.6f %10.3f %9.3f %11.3f' %
              (a['size'], a['text'], a['width'], a['units_64'],
               a['frac_units'], a['per_char_units']), file=sys.stderr)

    # Summary statistics the band choice rests on.
    fracs = [abs(a['frac_units']) for a in analysis]
    per_char = [a['per_char_units'] for a in analysis if a['n'] == 1]
    summary = {
        'chrome': str(args.chrome),
        'platform': sys.platform,
        'units_per_px': UNITS_PER_PX,
        'rows': analysis,
        'stats': {
            'max_abs_frac_units': round(max(fracs), 3),
            'mean_abs_frac_units': round(sum(fracs) / len(fracs), 3),
            'min_per_char_units': round(min(per_char), 3),
            'max_per_char_units': round(max(per_char), 3),
        },
    }
    out = json.dumps(summary, indent=2)
    if args.json:
        args.json.write_text(out)
    print(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
