#!/usr/bin/env bash
# End-to-end check against a running binary: unit tests cover components,
# this covers the server actually speaking HTTP over a socket.
# Usage: ./tests/smoke_test.sh [path-to-binary]
set -uo pipefail

SERVER="${1:-./build/multithreaded-server}"
PORT="${PORT:-18080}"
FAILURES=0
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

check() {  # description expected actual
    if [ "$2" = "$3" ]; then
        printf '  ok    %-42s %s\n' "$1" "$3"
    else
        printf '  FAIL  %-42s expected %s, got %s\n' "$1" "$2" "$3"
        FAILURES=$((FAILURES + 1))
    fi
}

code() { curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$@"; }

run_mode() {
    local mode=$1
    echo "== $mode mode"

    "$SERVER" --mode "$mode" --threads 4 --port "$PORT" >/tmp/smoke-server.log 2>&1 &
    SERVER_PID=$!
    sleep 1

    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "  FAIL  server exited on startup"; cat /tmp/smoke-server.log
        FAILURES=$((FAILURES + 1)); return
    fi

    local base="http://127.0.0.1:$PORT"

    check "dashboard"            200 "$(code "$base/")"
    check "static demo page"     200 "$(code "$base/demo/index.html")"
    check "health probe"         200 "$(code "$base/healthz")"
    check "missing file"         404 "$(code "$base/nope.html")"
    check "path traversal"       403 "$(code --path-as-is "$base/../../../etc/passwd")"
    check "method not allowed"   405 "$(code -X PUT "$base/healthz")"
    check "unsupported encoding" 501 "$(code -X POST -H 'Transfer-Encoding: chunked' "$base/api/kv")"

    check "kv create"            201 "$(code -X POST -H 'Content-Type: application/json' -d '{"key":"smoke","value":"1"}' "$base/api/kv")"
    check "kv replace"           200 "$(code -X POST -H 'Content-Type: application/json' -d '{"key":"smoke","value":"2"}' "$base/api/kv")"
    check "kv read"              200 "$(code "$base/api/kv?key=smoke")"
    check "kv form-encoded"      201 "$(code -X POST -d 'key=form&value=a+b' "$base/api/kv")"
    check "kv malformed json"    400 "$(code -X POST -H 'Content-Type: application/json' -d '{"key":' "$base/api/kv")"
    check "kv delete"            200 "$(code -X DELETE "$base/api/kv?key=smoke")"
    check "kv deleted is gone"   404 "$(code "$base/api/kv?key=smoke")"

    local value
    value=$(curl -s --max-time 5 "$base/api/kv?key=form")
    check "form value decoded"   '{"key":"form","value":"a b"}' "$value"

    # Keep-alive: two requests must reuse one connection (num_connects == 0 on
    # the second), which is what curl reports when the handle is reused.
    local connects
    connects=$(curl -s -o /dev/null -w '%{num_connects}' --max-time 5 \
        "$base/healthz" "$base/demo/index.html" | tail -c 1)
    check "keep-alive reuses socket" 0 "$connects"

    # Pipelining and fragmented reads over a raw socket.
    if command -v python3 >/dev/null; then
        local pipelined
        pipelined=$(python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
def talk(chunks):
    s = socket.create_connection(("127.0.0.1", port)); s.settimeout(3)
    for c in chunks:
        s.sendall(c); time.sleep(0.05)
    out = b""
    try:
        while True:
            b = s.recv(65536)
            if not b: break
            out += b
    except socket.timeout:
        pass
    s.close(); return out
three = talk([b"GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n" * 3])
split = talk([b"GET /heal", b"thz HTTP/1.1\r\nHo", b"st: x\r\n\r\n"])
print(f"{three.count(b'HTTP/1.1 200')},{1 if b'HTTP/1.1 200' in split else 0}")
PY
)
        check "three pipelined requests" "3,1" "$pipelined"
    fi

    local stats
    stats=$(curl -s --max-time 5 "$base/api/stats")
    case "$stats" in
        *"\"mode\":\"$mode\""*) printf '  ok    %-42s reports mode\n' "metrics endpoint" ;;
        *) printf '  FAIL  %-42s %s\n' "metrics endpoint" "$stats"; FAILURES=$((FAILURES + 1)) ;;
    esac

    # SIGTERM must shut down cleanly rather than being killed.
    kill -TERM "$SERVER_PID" 2>/dev/null
    local waited=0
    while kill -0 "$SERVER_PID" 2>/dev/null && [ "$waited" -lt 50 ]; do
        sleep 0.1; waited=$((waited + 1))
    done
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        check "graceful shutdown" "exited" "still running"
        kill -9 "$SERVER_PID" 2>/dev/null
    else
        check "graceful shutdown" "exited" "exited"
    fi
    SERVER_PID=""
}

[ -x "$SERVER" ] || { echo "$SERVER not found - build first"; exit 1; }

run_mode pool
run_mode reactor

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "all smoke checks passed"
else
    echo "$FAILURES smoke check(s) failed"
fi
exit "$FAILURES"
