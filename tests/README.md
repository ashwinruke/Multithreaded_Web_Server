# Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # 82 unit tests
./tests/smoke_test.sh                        # 38 end-to-end checks, both modes
```

`-DBUILD_TESTS=OFF` skips GoogleTest entirely (it is fetched at configure time,
so that flag also makes the build work offline).

## What is covered

| File               | Focus                                                          |
|--------------------|----------------------------------------------------------------|
| `test_parser.cpp`  | Incremental parsing, pipelining, limits, malformed input        |
| `test_router.cpp`  | Dispatch, 405 + `Allow`, HEAD via GET, fallback                 |
| `test_cache.cpp`   | LRU eviction order, promotion on read, counters, thread safety  |
| `test_api.cpp`     | URL/JSON/form decoding, KV semantics, bounds, concurrent writes |
| `test_static.cpp`  | File serving, MIME types, path traversal, cache integration     |
| `test_metrics.cpp` | Counters, percentiles, lock-free recording under contention     |

The parser gets the most attention because it is where the subtle bugs live: a
server that assumes one `recv()` equals one request works perfectly against
`curl` and falls apart under load. `SplitAtEveryByteStillParses` fragments a
request at every possible byte offset and asserts all of them parse identically.

## Sanitizers

Run before touching anything concurrent:

```bash
cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j && ./build-tsan/unit_tests

cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-asan -j && ./build-asan/unit_tests
```

Both are clean and both run in CI on every push.
