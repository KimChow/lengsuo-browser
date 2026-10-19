#!/bin/bash
# Run every measurement this comparison needs, writing one JSON per case.
# Kept as a single remote script so a flaky SSH link cannot desync the run:
# each case starts its own server, launches the browser, waits for the report,
# and records a PASS/FAIL line in run.log.
set -u
BASE=/tmp/fpc
OUT=$BASE/out
LOG=$BASE/run.log
: > "$LOG"

CHROME=/home/ubuntu/lengsuo-browser-dev/chromium-src/out/lengsuo/chrome
PROFILE_SEED=abcdef0123456789
PROFILE_SW="$PROFILE_SEED:0:0:0:0:1:0:1:NVIDIA Corporation:ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 (0x00002504) Direct3D11 vs_5_0 ps_5_0, D3D11):24:32"

collect() {
    local label="$1" extra="$2" port="$3"
    local bin="${4:-$CHROME}"
    local prof="/tmp/probe-$label"
    local out="$OUT/$label.json"
    rm -rf "$prof"; rm -f "$out"
    pkill -f "fp_report_server.py --port $port" 2>/dev/null
    sleep 0.5
    /home/ubuntu/fpscan-venv/bin/python "$BASE/tools/fp_report_server.py" \
        --port "$port" --expect-label "$label" --out "$out" --timeout 100 \
        >> "$BASE/srv-$label.log" 2>&1 &
    local srv=$!
    for _ in $(seq 1 40); do
        curl -s -m 2 -o /dev/null "http://127.0.0.1:$port/__ping" && break
        sleep 0.5
    done
    timeout 75 /usr/bin/xvfb-run -a --server-args="-screen 0 1920x1080x24" \
        "$bin" --no-sandbox --disable-dev-shm-usage \
        --no-first-run --no-default-browser-check --disable-sync \
        --password-store=basic --enable-unsafe-swiftshader \
        --user-data-dir="$prof" $extra \
        "http://127.0.0.1:$port/fp-probe.html?report=1" \
        > "$BASE/br-$label.log" 2>&1
    local brc=$?
    wait $srv 2>/dev/null
    local src=$?
    if [ -s "$out" ]; then
        echo "PASS $label browser_rc=$brc server_rc=$src bytes=$(wc -c < "$out")" >> "$LOG"
    else
        echo "FAIL $label browser_rc=$brc server_rc=$src" >> "$LOG"
    fi
    pkill -f "user-data-dir=$prof" 2>/dev/null
    sleep 1
}

# 1) Official Chrome on THIS host: the only apples-to-apples control for
#    everything except version strings (this build is 144, ours is 155).
collect official-linux-144 "" 8801 /usr/bin/google-chrome-stable

# 2) Our build, no profile: shows what an uninjected visitor would see.
collect ours-default "" 8802

# 3) Our build with a profile matching the reference machine class.
collect ours-profile "--lengsuo-managed-fp=$PROFILE_SW" 8803

echo "ALL DONE" >> "$LOG"
