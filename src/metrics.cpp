#include "metrics.h"
#include <sstream>

const std::array<uint64_t, Metrics::bucketCount> Metrics::bucketBounds = {
    50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000,
    50000, 100000, 250000, 500000, 1000000, UINT64_MAX
};

void Metrics::recordRequest(int status, size_t responseBytes, uint64_t latencyMicros) {
    totalRequests.fetch_add(1, std::memory_order_relaxed);
    bytesOut.fetch_add(responseBytes, std::memory_order_relaxed);

    if (status < 400) {
        responses2xx.fetch_add(1, std::memory_order_relaxed);
    }
    else if (status < 500) {
        responses4xx.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        responses5xx.fetch_add(1, std::memory_order_relaxed);
    }

    for (size_t i = 0; i < bucketCount; ++i) {
        if (latencyMicros <= bucketBounds[i]) {
            latencyBuckets[i].fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
}

void Metrics::connectionOpened() {
    activeConnections.fetch_add(1, std::memory_order_relaxed);
    totalConnections.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::connectionClosed() {
    activeConnections.fetch_sub(1, std::memory_order_relaxed);
}

// Linear interpolation inside the containing bucket. Bucket boundaries make
// this an approximation, which is the trade for O(1) recording.
double Metrics::percentile(double fraction, uint64_t total) const {
    if (total == 0) {
        return 0.0;
    }
    const uint64_t target = static_cast<uint64_t>(fraction * static_cast<double>(total));
    uint64_t seen = 0;
    for (size_t i = 0; i < bucketCount; ++i) {
        seen += latencyBuckets[i].load(std::memory_order_relaxed);
        if (seen >= target) {
            const uint64_t upper = (i + 1 == bucketCount) ? bucketBounds[i - 1] : bucketBounds[i];
            return static_cast<double>(upper) / 1000.0;  // milliseconds
        }
    }
    return static_cast<double>(bucketBounds[bucketCount - 2]) / 1000.0;
}

std::string Metrics::toJson(const std::string& mode, int threads,
                            uint64_t cacheHits, uint64_t cacheMisses) const {
    const uint64_t total = totalRequests.load(std::memory_order_relaxed);
    const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    const uint64_t cacheTotal = cacheHits + cacheMisses;

    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(2);
    json << "{"
         << "\"mode\":\"" << mode << "\","
         << "\"threads\":" << threads << ","
         << "\"uptimeMs\":" << uptime << ","
         << "\"totalRequests\":" << total << ","
         << "\"bytesOut\":" << bytesOut.load(std::memory_order_relaxed) << ","
         << "\"activeConnections\":" << activeConnections.load(std::memory_order_relaxed) << ","
         << "\"totalConnections\":" << totalConnections.load(std::memory_order_relaxed) << ","
         << "\"status\":{"
         << "\"ok\":" << responses2xx.load(std::memory_order_relaxed) << ","
         << "\"clientError\":" << responses4xx.load(std::memory_order_relaxed) << ","
         << "\"serverError\":" << responses5xx.load(std::memory_order_relaxed) << "},"
         << "\"latencyMs\":{"
         << "\"p50\":" << percentile(0.50, total) << ","
         << "\"p95\":" << percentile(0.95, total) << ","
         << "\"p99\":" << percentile(0.99, total) << "},"
         << "\"cache\":{"
         << "\"hits\":" << cacheHits << ","
         << "\"misses\":" << cacheMisses << ","
         << "\"hitRate\":" << (cacheTotal ? (100.0 * static_cast<double>(cacheHits) / static_cast<double>(cacheTotal)) : 0.0)
         << "}}";
    return json.str();
}
