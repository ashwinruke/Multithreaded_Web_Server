#!/usr/bin/env bash
# Sweeps both event-loop modes and nginx over the same static file.
# Usage: ./bench/run_bench.sh [duration] [connections]
set -uo pipefail

DURATION="${1:-15s}"
CONNECTIONS="${2:-200}"
WRK_THREADS=4
SERVER=./build/multithreaded-server
RESULTS=bench/results.md

command -v wrk >/dev/null || { echo "wrk not installed"; exit 1; }
[ -x "$SERVER" ] || { echo "$SERVER not found - build first"; exit 1; }

stop_server() { pkill -f multithreaded-server >/dev/null 2>&1; sleep 0.5; }
trap stop_server EXIT

# wrk --latency reports 50/75/90/99 - there is no 95th percentile line.
extract() {
    local out=$1 label=$2
    local rps p50 p75 p90 p99
    rps=$(grep 'Requests/sec' <<<"$out" | awk '{print $2}')
    p50=$(awk '/^ *50%/{print $2}' <<<"$out")
    p75=$(awk '/^ *75%/{print $2}' <<<"$out")
    p90=$(awk '/^ *90%/{print $2}' <<<"$out")
    p99=$(awk '/^ *99%/{print $2}' <<<"$out")

    if [ -z "$rps" ]; then
        echo "!! $label produced no result:" >&2
        echo "$out" >&2
        return 1
    fi
    printf '| %-22s | %10s | %8s | %8s | %8s | %8s |\n' \
        "$label" "$rps" "$p50" "$p75" "$p90" "$p99" | tee -a "$RESULTS"
}

measure() {  # mode threads port label
    local mode=$1 threads=$2 port=$3 label=$4
    stop_server
    "$SERVER" --mode "$mode" --threads "$threads" --port "$port" >/tmp/bench-server.log 2>&1 &
    sleep 1

    if ! curl -fsS -o /dev/null --max-time 3 "http://127.0.0.1:$port/demo/index.html"; then
        echo "!! $label: server did not come up on port $port" >&2
        cat /tmp/bench-server.log >&2
        stop_server
        return 1
    fi

    local out
    out=$(wrk -t$WRK_THREADS -c"$CONNECTIONS" -d"$DURATION" --latency "http://127.0.0.1:$port/demo/index.html" 2>&1)
    stop_server
    extract "$out" "$label"
}

measure_nginx() {
    local port=8099 root conf=/tmp/bench-nginx.conf
    root=$(pwd)/static
    cat > "$conf" <<NGINX
worker_processes auto;
error_log /tmp/nginx-bench-error.log;
pid /tmp/nginx-bench.pid;
events { worker_connections 4096; }
http {
  access_log off;
  include /etc/nginx/mime.types;
  server { listen $port; root $root; location / { index index.html; } }
}
NGINX
    if ! nginx -c "$conf" -p /tmp 2>/tmp/nginx-start.log; then
        echo "!! nginx failed to start:" >&2; cat /tmp/nginx-start.log >&2; return 1
    fi
    sleep 1
    local out
    out=$(wrk -t$WRK_THREADS -c"$CONNECTIONS" -d"$DURATION" --latency "http://127.0.0.1:$port/demo/index.html" 2>&1)
    nginx -c "$conf" -p /tmp -s stop >/dev/null 2>&1
    extract "$out" "nginx (baseline)"
}

{
    echo "# Benchmark results"
    echo
    echo "- Host: $(nproc) cores, $(uname -sr)"
    echo "- wrk: -t$WRK_THREADS -c$CONNECTIONS -d$DURATION, static \`index.html\`, keep-alive"
    echo "- Date: $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo
    echo '| Configuration          | req/sec    | p50      | p75      | p90      | p99      |'
    echo '|------------------------|------------|----------|----------|----------|----------|'
} > "$RESULTS"

for t in 1 2 4 8; do measure pool    "$t" 8090 "pool, $t workers"; done
for t in 1 2 4 8; do measure reactor "$t" 8091 "reactor, $t threads"; done
if command -v nginx >/dev/null; then measure_nginx; else echo "nginx not found, skipping baseline" >&2; fi

echo
echo "Written to $RESULTS"
