# Multi-threaded HTTP Server (C++)

An HTTP/1.1 server written from scratch in C++20 — no web frameworks, no HTTP
libraries. Raw POSIX sockets, a thread pool, an LRU file cache and an
asynchronous logger.

> Status: Linux port complete. The epoll event loop, keep-alive, routing,
> dynamic endpoints and benchmarks land in the milestones below.

## Build

Requires g++ 13+ (C++20), CMake 3.25+, Linux.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bash
./build/multithreaded-server
```

Settings live in `.env` at the repo root: `SERVER_IP`, `SERVER_PORT`,
`MAX_THREADS`, `CACHE_CAPACITY`. Static files are served from `static/`, and
request logs are written to `logs/server.log`. Both paths resolve relative to
the executable, so the server can be started from any directory.

```bash
curl -i  http://127.0.0.1:8080/            # index.html
curl -I  http://127.0.0.1:8080/style.css   # HEAD
```

Press Ctrl-C to shut down: the listening socket closes, worker threads drain
and join, and queued connections are released.

## Architecture (current)

```
main ──► Server.start()
           │
           ├── accept() loop (main thread)
           │        └── push client fd ──► queue ──┐
           │                                       │
           └── thread pool (MAX_THREADS workers) ◄─┘
                        │
                        ├── HttpParser    request line, headers, path safety
                        ├── HttpResponse  status, MIME type, headers
                        ├── FileManager   ──► LRUCache (thread-safe, O(1))
                        └── Logger        queue ──► dedicated writer thread
```

## Design notes

**Thread pool over thread-per-connection.** Threads are created once at
startup, not per request, so connection cost is a queue push rather than a
`clone()` syscall. The pool size caps memory and context-switching under load.

**Path traversal is rejected by canonicalization.** Request targets are
resolved with `std::filesystem::weakly_canonical` and the result must still sit
inside the static root, so `GET /../../etc/passwd` returns 403 rather than the
file. A missing file inside the root returns 404 — the two cases are kept
distinct on purpose.

**Partial writes are handled.** `send()` may accept fewer bytes than requested;
responses are written in a loop until the whole buffer is out, so large files
are not silently truncated.

**The logger never blocks a request thread.** Callers push a formatted line
onto a queue and return; one writer thread owns the file handle. Disk latency
stays off the request path.

**The LRU cache uses `std::list` + `unordered_map`.** Splicing a node to the
front is O(1) and invalidates no iterators, so the map can store iterators
directly. Eviction is O(1) from the back.

## Roadmap

- [ ] epoll event loop (single-acceptor and multi-reactor variants, selectable)
- [ ] Incremental parser: partial reads, pipelining, `Content-Length` bodies
- [ ] Keep-alive with idle timeouts
- [ ] Routing table and dynamic JSON endpoints
- [ ] Live metrics dashboard (`/api/stats`)
- [ ] Unit tests (GoogleTest) and GitHub Actions CI
- [ ] Benchmarks vs nginx: throughput and p50/p95/p99
