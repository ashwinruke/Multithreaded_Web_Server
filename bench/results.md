# Benchmark results

- Host: 8 cores, Linux 6.18.33.2-microsoft-standard-WSL2
- wrk: -t4 -c200 -d15s, static `index.html`, keep-alive
- Date: 2026-09-19 16:10 UTC

| Configuration          | req/sec    | p50      | p75      | p90      | p99      |
|------------------------|------------|----------|----------|----------|----------|
| pool, 1 workers        |   13630.84 |  14.28ms |  15.04ms |  16.30ms |  20.32ms |
| pool, 2 workers        |   34369.52 |   5.57ms |   6.25ms |   7.14ms |   9.57ms |
| pool, 4 workers        |   87563.28 |   1.88ms |   2.55ms |   3.71ms |   7.38ms |
| pool, 8 workers        |  112068.83 |   1.34ms |   2.12ms |   3.20ms |   8.87ms |
| reactor, 1 threads     |   13858.47 |  13.83ms |  14.84ms |  16.63ms |  24.26ms |
| reactor, 2 threads     |   24215.43 |   7.24ms |   9.80ms |  14.53ms |  32.24ms |
| reactor, 4 threads     |   82298.24 |   2.06ms |   2.83ms |   4.44ms |  13.02ms |
| reactor, 8 threads     |  114931.27 |   1.22ms |   2.91ms |   4.97ms |  12.60ms |
| nginx (baseline)       |  190590.96 | 472.00us |   1.53ms |   3.36ms |   8.38ms |
