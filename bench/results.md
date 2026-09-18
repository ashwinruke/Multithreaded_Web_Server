# Benchmark results

- Host: 8 cores, Linux 6.18.33.2-microsoft-standard-WSL2
- wrk: -t4 -c200 -d15s, static `index.html`, keep-alive
- Date: 2026-09-18 10:23 UTC

| Configuration          | req/sec    | p50      | p75      | p90      | p99      |
|------------------------|------------|----------|----------|----------|----------|
| pool, 1 workers        |   13889.84 |  14.29ms |  15.81ms |  17.28ms |  22.65ms |
| pool, 2 workers        |   39667.25 |   4.71ms |   5.50ms |   6.71ms |  10.06ms |
| pool, 4 workers        |   95422.42 |   1.72ms |   2.41ms |   3.62ms |   6.18ms |
| pool, 8 workers        |  108293.31 |   1.40ms |   2.19ms |   3.36ms |   9.37ms |
| reactor, 1 threads     |   13000.41 |  14.98ms |  16.07ms |  17.70ms |  22.81ms |
| reactor, 2 threads     |   34273.72 |   5.68ms |   6.31ms |   7.13ms |   9.98ms |
| reactor, 4 threads     |   98449.35 |   1.75ms |   2.48ms |   3.67ms |   6.16ms |
| reactor, 8 threads     |  138971.84 |   1.02ms |   2.30ms |   3.92ms |   8.13ms |
| nginx (baseline)       |  207059.44 | 466.00us |   1.42ms |   2.92ms |   6.49ms |
