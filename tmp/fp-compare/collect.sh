#!/bin/bash
# Collect one zero-automation fingerprint probe reading.
#
#   $1  label            e.g. ours-default
#   $2  output json path
#   $3  extra chrome args (may be empty)
#   $4  binary path (defaults to our build)
#
# The browser is launched the way a user launches it: no CDP port, no
# --enable-automation. The page itself POSTs its reading back to the local
# server, so every value is one a real site would see.
set -u

LABEL="$1"
OUT="$2"
EXTRA="${3:-}"
BIN="${4:-/home/ubuntu/lengsuo-browser-dev/chromium-src/out/lengsuo/chrome}"
PORT="${PORT:-8791}"
PROFILE="/tmp/probe-$LABEL"
BASE="/tmp/fpc"

pkill -f "fp_report_server.py --port $PORT" 2>/dev/null
sleep 1
rm -rf "$PROFILE"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

/home/ubuntu/fpscan-venv/bin/python "$BASE/tools/fp_report_server.py" \
    --port "$PORT" --expect-label "$LABEL" --out "$OUT" --timeout 110 \
    > "$BASE/srv-$LABEL.log" 2>&1 &
SRV=$!

# Wait for the listener instead of guessing with sleep.
for _ in $(seq 1 40); do
    if curl -s -m 2 -o /dev/null "http://127.0.0.1:$PORT/__ping"; then
        break
    fi
    sleep 0.5
done

# --password-store=basic and the first-run switches matter here: without them
# the browser blocks on a keyring prompt that no one can answer under Xvfb, so
# it never navigates and the page never reports.
timeout 90 /usr/bin/xvfb-run -a --server-args="-screen 0 1920x1080x24" \
    "$BIN" --no-sandbox --disable-dev-shm-usage \
    --no-first-run --no-default-browser-check --disable-sync \
    --password-store=basic --disable-features=Translate \
    --user-data-dir="$PROFILE" $EXTRA \
    "http://127.0.0.1:$PORT/fp-probe.html?report=1" \
    > "$BASE/br-$LABEL.log" 2>&1
BRC=$?

wait $SRV
SRC=$?

echo "=== $LABEL ==="
echo "browser_rc=$BRC server_rc=$SRC"
cat "$BASE/srv-$LABEL.log"
if [ -s "$OUT" ]; then
    echo "OUTPUT_OK $(wc -c < "$OUT") bytes"
else
    echo "NO OUTPUT"
    tail -20 "$BASE/br-$LABEL.log" 2>/dev/null
fi
