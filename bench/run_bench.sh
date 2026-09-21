#!/usr/bin/env bash
# Benchmark harness.
#
#   ./bench/run_bench.sh sweep     [duration] [connections]   thread-scaling sweep
#   ./bench/run_bench.sh workloads [duration] [connections]   realistic workload matrix
#   ./bench/run_bench.sh all       [duration] [connections]   both (default)
#
# Results go to bench/results.md (sweep) and bench/workloads.md (matrix).
set -uo pipefail

MODE="${1:-all}"
DURATION="${2:-15s}"
CONNECTIONS="${3:-200}"
WRK_THREADS=4
CORES=$(nproc)
SERVER=./build/multithreaded-server
BENCH_DIR=bench
ASSET_DIR=static/bench-assets
SWEEP_RESULTS=$BENCH_DIR/results.md
WORKLOAD_RESULTS=$BENCH_DIR/workloads.md
SERVER_PORT=8090
NGINX_PORT=8099

command -v wrk >/dev/null || { echo "wrk not installed"; exit 1; }
[ -x "$SERVER" ] || { echo "$SERVER not found - build first"; exit 1; }

# ---------------------------------------------------------------- lifecycle --

# Only ever stop processes this script started, never a server the user is
# running by hand for the dashboard.
SERVER_PID=""
NGINX_CONF=/tmp/bench-nginx.conf

stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    SERVER_PID=""
    sleep 0.3
}

stop_nginx() {
    [ -f /tmp/nginx-bench.pid ] && nginx -c "$NGINX_CONF" -p /tmp -s stop >/dev/null 2>&1
    rm -f /tmp/nginx-bench.pid
    sleep 0.3
}

cleanup() { stop_server; stop_nginx; }
trap cleanup EXIT

# Fresh process per run, so the key/value store, metrics and file cache start
# empty and no run inherits another's warm state.
start_server() {  # mode threads
    stop_server
    "$SERVER" --mode "$1" --threads "$2" --port "$SERVER_PORT" >/tmp/bench-server.log 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 30); do
        curl -fsS -o /dev/null --max-time 1 "http://127.0.0.1:$SERVER_PORT/healthz" 2>/dev/null && return 0
        sleep 0.1
    done
    echo "!! server ($1, $2 threads) did not come up" >&2
    cat /tmp/bench-server.log >&2
    return 1
}

start_nginx() {
    stop_nginx
    local root; root=$(pwd)/static
    cat > "$NGINX_CONF" <<NGINX
worker_processes auto;
error_log /tmp/nginx-bench-error.log;
pid /tmp/nginx-bench.pid;
events { worker_connections 8192; }
http {
  access_log off;
  include /etc/nginx/mime.types;
  sendfile on;
  keepalive_requests 1000000;
  server { listen $NGINX_PORT; root $root; location / { index index.html; } }
}
NGINX
    if ! nginx -c "$NGINX_CONF" -p /tmp 2>/tmp/nginx-start.log; then
        echo "!! nginx failed to start:" >&2; cat /tmp/nginx-start.log >&2
        return 1
    fi
    sleep 0.5
}

# ----------------------------------------------------------------- measuring --

# wrk --latency reports 50/75/90/99; there is no 95th percentile line.
field() {  # output pattern column
    awk -v pat="$2" -v col="$3" '$0 ~ pat { print $col; exit }' <<<"$1"
}

# Socket errors plus non-2xx/3xx responses. Either means the numbers above
# them are not trustworthy for that row.
count_errors() {
    local out=$1 socket=0 status
    local line; line=$(grep 'Socket errors' <<<"$out")
    if [ -n "$line" ]; then
        socket=$(grep -oE '[0-9]+' <<<"$line" | awk '{ sum += $1 } END { print sum + 0 }')
    fi
    status=$(awk '/Non-2xx or 3xx responses/ { print $NF }' <<<"$out")
    echo $(( socket + ${status:-0} ))
}

run_wrk() {  # output-file label server url [extra wrk args...]
    local file=$1 label=$2 server=$3 url=$4; shift 4
    local out
    out=$(wrk -t$WRK_THREADS -c"$CONNECTIONS" -d"$DURATION" --latency "$@" "$url" 2>&1)

    local rps transfer p50 p99 errors
    rps=$(field "$out" 'Requests/sec' 2)
    transfer=$(field "$out" 'Transfer/sec' 2)
    p50=$(field "$out" '^ *50%' 2)
    p99=$(field "$out" '^ *99%' 2)
    errors=$(count_errors "$out")

    if [ -z "$rps" ]; then
        echo "!! $label / $server produced no result:" >&2
        echo "$out" >&2
        printf '| %-18s | %-16s | %10s | %10s | %8s | %8s | %7s |\n' \
            "$label" "$server" "failed" "-" "-" "-" "-" | tee -a "$file"
        return 1
    fi
    printf '| %-18s | %-16s | %10s | %10s | %8s | %8s | %7s |\n' \
        "$label" "$server" "$rps" "$transfer" "$p50" "$p99" "$errors" | tee -a "$file"
}

# -------------------------------------------------------------------- assets --

prepare_assets() {
    mkdir -p "$ASSET_DIR"
    # 1 MiB of incompressible data. Big enough that a response is dominated
    # by copying bytes rather than by per-request syscall overhead.
    if [ ! -f "$ASSET_DIR/large.bin" ]; then
        head -c 1048576 /dev/urandom > "$ASSET_DIR/large.bin"
    fi
}

# --------------------------------------------------------------------- sweep --

run_sweep() {
    {
        echo "# Thread-scaling sweep"
        echo
        echo "- Host: $CORES cores, $(uname -sr)"
        echo "- wrk: -t$WRK_THREADS -c$CONNECTIONS -d$DURATION, \`/demo/index.html\` (585 B, cached), keep-alive"
        echo "- Date: $(date -u '+%Y-%m-%d %H:%M UTC')"
        echo
        echo '| Configuration      | Server           | req/sec    | transfer   | p50      | p99      | errors  |'
        echo '|--------------------|------------------|------------|------------|----------|----------|---------|'
    } > "$SWEEP_RESULTS"

    local url="http://127.0.0.1:$SERVER_PORT/demo/index.html"
    for mode in pool reactor; do
        for t in 1 2 4 8; do
            start_server "$mode" "$t" && run_wrk "$SWEEP_RESULTS" "$t threads" "$mode" "$url"
        done
    done
    stop_server
    if command -v nginx >/dev/null && start_nginx; then
        run_wrk "$SWEEP_RESULTS" "auto" "nginx" "http://127.0.0.1:$NGINX_PORT/demo/index.html"
        stop_nginx
    fi
}

# ----------------------------------------------------------------- workloads --

# One row per server for each workload. nginx only appears where it can serve
# the same thing; it has no equivalent of the JSON API.
workload() {  # label path nginx(yes/no) [extra wrk args...]
    local label=$1 path=$2 with_nginx=$3; shift 3
    for mode in pool reactor; do
        start_server "$mode" "$CORES" &&
            run_wrk "$WORKLOAD_RESULTS" "$label" "$mode" "http://127.0.0.1:$SERVER_PORT$path" "$@"
    done
    stop_server
    if [ "$with_nginx" = yes ] && command -v nginx >/dev/null && start_nginx; then
        run_wrk "$WORKLOAD_RESULTS" "$label" "nginx" "http://127.0.0.1:$NGINX_PORT$path" "$@"
        stop_nginx
    fi
}

run_workloads() {
    prepare_assets
    {
        echo "# Workload matrix"
        echo
        echo "- Host: $CORES cores, $(uname -sr)"
        echo "- wrk: -t$WRK_THREADS -c$CONNECTIONS -d$DURATION; this server at $CORES threads in both modes"
        echo "- Date: $(date -u '+%Y-%m-%d %H:%M UTC')"
        echo
        echo '| Workload           | Server           | req/sec    | transfer   | p50      | p99      | errors  |'
        echo '|--------------------|------------------|------------|------------|----------|----------|---------|'
    } > "$WORKLOAD_RESULTS"

    workload "small static"   "/demo/index.html"           yes
    workload "large 1 MiB"    "/bench-assets/large.bin"    yes
    workload "dynamic JSON"   "/api/stats"                 no
    workload "POST write"     "/api/kv"                    no  -s "$BENCH_DIR/lua/post_kv.lua"
    workload "conn churn"     "/demo/index.html"           yes -H "Connection: close"
    workload "mixed traffic"  "/demo/index.html"           no  -s "$BENCH_DIR/lua/mixed.lua"

    {
        echo
        echo "Workloads:"
        echo
        echo "- **small static** - 585 B file from the LRU cache; measures per-request overhead"
        echo "- **large 1 MiB** - copy- and bandwidth-bound rather than syscall-bound"
        echo "- **dynamic JSON** - \`/api/stats\`, serialized per request"
        echo "- **POST write** - JSON body to \`/api/kv\`, contending on the store's mutex"
        echo "- **conn churn** - \`Connection: close\`, a new TCP connection per request"
        echo "- **mixed traffic** - 60% static, 20% stats, 15% KV reads, 5% KV writes"
    } >> "$WORKLOAD_RESULTS"
}

# ---------------------------------------------------------------------- main --

case "$MODE" in
    sweep)     run_sweep ;;
    workloads) run_workloads ;;
    all)       run_sweep; echo; run_workloads ;;
    *)         echo "usage: $0 [sweep|workloads|all] [duration] [connections]"; exit 1 ;;
esac

echo
[ "$MODE" != workloads ] && echo "Sweep:     $SWEEP_RESULTS"
[ "$MODE" != sweep ]     && echo "Workloads: $WORKLOAD_RESULTS"
