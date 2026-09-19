#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

// Server-wide counters. Every field is a plain atomic incremented on the hot
// path: no locks, no allocation. Percentiles come from a fixed-bucket histogram
// rather than a sample list, so recording a latency is one atomic add and
// memory use is constant regardless of traffic.
class Metrics {
public:
    static constexpr size_t bucketCount = 15;

    void recordRequest(int status, size_t responseBytes, uint64_t latencyMicros);
    void connectionOpened();
    void connectionClosed();

    std::string toJson(const std::string& mode, int threads,
                       uint64_t cacheHits, uint64_t cacheMisses) const;

private:
    std::atomic<uint64_t> totalRequests{0};
    std::atomic<uint64_t> bytesOut{0};
    std::atomic<uint64_t> responses2xx{0};
    std::atomic<uint64_t> responses4xx{0};
    std::atomic<uint64_t> responses5xx{0};
    std::atomic<int64_t> activeConnections{0};
    std::atomic<uint64_t> totalConnections{0};
    std::array<std::atomic<uint64_t>, bucketCount> latencyBuckets{};
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();

    // Upper bound of each bucket in microseconds; the last bucket is unbounded.
    static const std::array<uint64_t, bucketCount> bucketBounds;
    double percentile(double fraction, uint64_t total) const;
};
