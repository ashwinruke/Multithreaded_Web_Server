# Multi-threaded HTTP Server (C++)

An HTTP/1.1 server written from scratch in C++20 — no web frameworks, no HTTP
libraries. Non-blocking sockets, an `epoll` event loop in two interchangeable
concurrency models, an incremental request parser, keep-alive, routing, an LRU
file cache and an asynchronous logger.

## Build

Requires g++ 13+ (C++20), CMake 3.25+, Linux (`epoll` and `SO_REUSEPORT` are
Linux-specific; see *Portability* below).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bash
./build/multithreaded-server                              # settings from .env
./build/multithreaded-server --mode reactor --threads 8   # CLI override
```

`.env` at the repo root holds `SERVER_IP`, `SERVER_PORT`, `MAX_THREADS`,
`CACHE_CAPACITY`, `IDLE_TIMEOUT` and `LOOP_MODE`. Flags `--mode`, `--threads`,
`--port` and `--ip` override it, which is what the benchmark sweep uses.

```bash
curl -i  http://127.0.0.1:8080/              # live dashboard
curl -s  http://127.0.0.1:8080/api/stats     # server metrics as JSON
curl -s  http://127.0.0.1:8080/demo/         # static demo page
```

Ctrl-C stops the loop, closes live connections and joins every thread.

## Two concurrency models

Both are compiled in and selected at startup, so they can be measured against
each other on identical code paths — the parser, router and handlers are shared;
only the loop differs.

**`--mode pool`** — one `epoll` thread owns `accept()` and readiness
notification; a fixed worker pool does the reads, parsing, handler work and
writes. Connections are registered with `EPOLLONESHOT`, so a descriptor is
disarmed the moment it fires and exactly one worker can own it until that worker
rearms it. That removes the usual thundering-herd and double-ownership problems,
at the cost of a queue hop and a shared connection table behind a mutex.

**`--mode reactor`** — N independent reactors. Each thread opens its own
listening socket on the same port with `SO_REUSEPORT` and runs its own `epoll`
instance and its own connection table. The kernel balances new connections
across the listeners, so there is no shared queue, no mutex on the data path and
better cache locality; the tradeoff is that load balancing is the kernel's
hashing rather than true work stealing, so one slow handler can hold up its own
reactor.

```
pool                                  reactor
────                                  ───────
epoll thread ── accept                thread 0: epoll + listener ── conns
     │                                thread 1: epoll + listener ── conns
     └─ queue ──► worker 0            thread 2: epoll + listener ── conns
                  worker 1            thread 3: epoll + listener ── conns
                  worker N                   (SO_REUSEPORT, no shared state)
```

## Live dashboard and API

`/` serves a dashboard that reads the running server's own metrics once per
second and plots throughput over the last minute: requests/sec, open
connections, handler percentiles, cache hit rate and status-code mix. Point a
benchmark at the server and the page moves in real time.

| Route          | Method | Purpose                                      |
|----------------|--------|----------------------------------------------|
| `/`            | GET    | Dashboard                                    |
| `/api/stats`   | GET    | Metrics snapshot as JSON                     |
| `/api/kv`      | GET    | List entries, or one entry with `?key=`      |
| `/api/kv`      | POST   | Store a value (JSON or form-encoded body)    |
| `/api/kv`      | DELETE | Remove an entry by `?key=`                   |
| `/healthz`     | GET    | Liveness probe                               |
| `/demo/`       | GET    | Original static demo page                    |

```bash
curl -X POST -H 'Content-Type: application/json' \
     -d '{"key":"hello","value":"world"}' http://127.0.0.1:8080/api/kv
curl "http://127.0.0.1:8080/api/kv?key=hello"
```

**Metrics are lock-free on the hot path.** Counters are plain atomics and
latencies go into a fixed 15-bucket histogram, so recording a request is a
handful of atomic adds with no allocation and constant memory regardless of
traffic. Percentiles are computed at read time from bucket boundaries, which
makes them approximate — the deliberate trade for never touching a lock while
serving.

**The key/value store is bounded.** Entry count, key length and value length are
all capped (50 / 64 / 256) and it returns 507 when full, because the endpoint is
public on the deployed instance. It is guarded by a single mutex: shared mutable
state across every worker thread, which is what makes it a real test of the
concurrency model rather than a toy echo route.

## Design notes

**Non-blocking I/O with an incremental parser.** A read can return half a
request line, or three pipelined requests at once. Each connection owns its
buffers and parser state across `epoll` wakeups; the parser consumes whatever is
complete and reports `NeedMore` otherwise. Nothing assumes one `recv()` equals
one request — the assumption that breaks most toy servers under real load.

**Keep-alive.** HTTP/1.1 connections persist by default and are reused until the
client sends `Connection: close`, a protocol error makes the stream
untrustworthy, or the idle sweep reclaims them after `IDLE_TIMEOUT` seconds.

**Partial writes.** `send()` can accept fewer bytes than offered. The unsent
remainder stays in the connection's write buffer and the descriptor switches to
`EPOLLOUT` until it drains, so large responses are never truncated.

**Path traversal is rejected by canonicalization.** Targets are resolved with
`weakly_canonical` and must remain inside the static root: `/../../etc/passwd`
returns 403, while a missing file inside the root returns 404. The two are kept
distinct deliberately.

**Bounded inputs.** Request line, header block and body each have hard limits
(8 KB / 16 KB / 1 MB), returning 414, 431 and 413. Without them, a single client
can drive the server out of memory.

**The logger never blocks a request path.** Callers push a formatted line onto a
queue; one writer thread owns the file handle, so disk latency stays off the hot
path.

**The LRU cache uses `std::list` + `unordered_map`.** Splicing to the front is
O(1) and invalidates no iterators, so the map stores iterators directly and
eviction from the back is O(1).

## Benchmarks

```bash
./bench/run_bench.sh 15s 200     # duration, connections
```

Sweeps both modes at 1/2/4/8 threads plus an nginx baseline on the same file,
and writes `bench/results.md` with req/sec and p50/p95/p99.

## Portability

Linux only. `epoll` has no portable equivalent; `kqueue` (BSD/macOS) or IOCP
(Windows) would need a second `EventLoop` implementation. The interface is
already the seam where that would go.

## Roadmap

- [x] Non-blocking `epoll` core, both concurrency models
- [x] Incremental parser, keep-alive, pipelining, idle timeouts
- [x] Routing table with 405/Allow handling
- [x] Live metrics dashboard at `/` and `/api/stats`
- [x] In-memory JSON API endpoints
- [ ] Unit tests (GoogleTest) and GitHub Actions CI
- [ ] Published benchmark results vs nginx
