# Workload matrix

- Host: 8 cores, Linux 6.18.33.2-microsoft-standard-WSL2
- wrk: -t4 -c200 -d15s; this server at 8 threads in both modes
- Date: 2026-09-21 13:09 UTC

| Workload           | Server           | req/sec    | transfer   | p50      | p99      | errors  |
|--------------------|------------------|------------|------------|----------|----------|---------|
| small static       | pool             |  112580.94 |    72.36MB |   1.35ms |   5.39ms |       0 |
| small static       | reactor          |  118258.59 |    76.01MB |   1.18ms |  10.27ms |       0 |
| small static       | nginx            |  156549.00 |   122.87MB | 623.00us |   7.13ms |       0 |
| large 1 MiB        | pool             |     954.23 |     0.93GB | 205.23ms | 234.76ms |       0 |
| large 1 MiB        | reactor          |     937.80 |     0.92GB | 194.78ms | 436.88ms |       0 |
| large 1 MiB        | nginx            |    9404.83 |     9.19GB |  10.50ms |  23.81ms |       0 |
| dynamic JSON       | pool             |  147741.85 |    53.27MB |   0.85ms |   5.07ms |       0 |
| dynamic JSON       | reactor          |  157237.90 |    57.15MB | 712.00us |   7.16ms |       0 |
| POST write         | pool             |   99252.92 |    12.23MB |   1.51ms |   5.32ms |       0 |
| POST write         | reactor          |  118708.55 |    14.63MB |   1.38ms |   6.62ms |       0 |
| conn churn         | pool             |   49179.14 |    31.38MB |   2.19ms |   6.84ms |       0 |
| conn churn         | reactor          |   50186.96 |    32.02MB |   1.60ms |  12.29ms |       0 |
| conn churn         | nginx            |   44875.52 |    35.01MB |   1.58ms |   8.91ms |       0 |
| mixed traffic      | pool             |   76572.46 |    44.18MB |   2.01ms |  10.47ms |       0 |
| mixed traffic      | reactor          |   90654.70 |    52.35MB |   1.85ms |   8.29ms |       0 |

Workloads:

- **small static** - 585 B file from the LRU cache; measures per-request overhead
- **large 1 MiB** - copy- and bandwidth-bound rather than syscall-bound
- **dynamic JSON** - `/api/stats`, serialized per request
- **POST write** - JSON body to `/api/kv`, contending on the store's mutex
- **conn churn** - `Connection: close`, a new TCP connection per request
- **mixed traffic** - 60% static, 20% stats, 15% KV reads, 5% KV writes
