# Thread-scaling sweep

- Host: 8 cores, Linux 6.18.33.2-microsoft-standard-WSL2
- wrk: -t4 -c200 -d15s, `/demo/index.html` (585 B, cached), keep-alive
- Date: 2026-09-21 13:07 UTC

| Configuration      | Server           | req/sec    | transfer   | p50      | p99      | errors  |
|--------------------|------------------|------------|------------|----------|----------|---------|
| 1 threads          | pool             |   19311.57 |    12.41MB |  10.07ms |  13.22ms |       0 |
| 2 threads          | pool             |   39213.55 |    25.21MB |   4.97ms |   6.46ms |       0 |
| 4 threads          | pool             |   84376.22 |    54.24MB |   1.98ms |   6.47ms |       0 |
| 8 threads          | pool             |  112179.30 |    72.11MB |   1.35ms |   5.64ms |       0 |
| 1 threads          | reactor          |   15391.88 |     9.89MB |  11.88ms |  24.84ms |       0 |
| 2 threads          | reactor          |   35410.26 |    22.76MB |   5.54ms |   9.10ms |       0 |
| 4 threads          | reactor          |   73698.35 |    47.37MB |   2.50ms |   7.65ms |       0 |
| 8 threads          | reactor          |  121869.26 |    78.33MB |   1.13ms |  12.87ms |       0 |
| auto               | nginx            |  151420.61 |   118.85MB | 659.00us |   8.70ms |       0 |
