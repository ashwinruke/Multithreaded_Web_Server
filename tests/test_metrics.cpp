#include "metrics.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {
// Minimal field lookup so the tests assert on the real JSON the server emits.
long long numberField(const std::string& json, const std::string& name) {
    const size_t start = json.find("\"" + name + "\":");
    if (start == std::string::npos) {
        return -1;
    }
    return std::stoll(json.substr(start + name.size() + 3));
}
}  // namespace

TEST(Metrics, CountsRequestsByStatusClass) {
    Metrics metrics;
    metrics.recordRequest(200, 10, 100);
    metrics.recordRequest(404, 20, 100);
    metrics.recordRequest(500, 30, 100);

    const std::string json = metrics.toJson("pool", 4, 0, 0);
    EXPECT_EQ(numberField(json, "totalRequests"), 3);
    EXPECT_EQ(numberField(json, "ok"), 1);
    EXPECT_EQ(numberField(json, "clientError"), 1);
    EXPECT_EQ(numberField(json, "serverError"), 1);
    EXPECT_EQ(numberField(json, "bytesOut"), 60);
}

TEST(Metrics, TracksActiveConnections) {
    Metrics metrics;
    metrics.connectionOpened();
    metrics.connectionOpened();
    metrics.connectionClosed();

    const std::string json = metrics.toJson("reactor", 8, 0, 0);
    EXPECT_EQ(numberField(json, "activeConnections"), 1);
    EXPECT_EQ(numberField(json, "totalConnections"), 2);
}

TEST(Metrics, ReportsModeAndThreads) {
    const std::string json = Metrics{}.toJson("reactor", 8, 0, 0);
    EXPECT_NE(json.find(R"("mode":"reactor")"), std::string::npos);
    EXPECT_EQ(numberField(json, "threads"), 8);
}

TEST(Metrics, ComputesCacheHitRate) {
    const std::string json = Metrics{}.toJson("pool", 1, 75, 25);
    EXPECT_NE(json.find(R"("hitRate":75.00)"), std::string::npos);
}

TEST(Metrics, EmptyHistogramDoesNotDivideByZero) {
    const std::string json = Metrics{}.toJson("pool", 1, 0, 0);
    EXPECT_NE(json.find(R"("p50":0.00)"), std::string::npos);
    EXPECT_NE(json.find(R"("hitRate":0.00)"), std::string::npos);
}

// Percentiles come from bucket boundaries, so they are approximate by design:
// the assertion is that they land in the right bucket, not on an exact value.
TEST(Metrics, PercentilesReflectDistribution) {
    Metrics metrics;
    for (int i = 0; i < 99; ++i) {
        metrics.recordRequest(200, 0, 60);       // ~0.1ms bucket
    }
    metrics.recordRequest(200, 0, 900000);       // one slow request, ~1000ms bucket

    const std::string json = metrics.toJson("pool", 1, 0, 0);
    const size_t p50 = json.find("\"p50\":0.10");
    const size_t p99 = json.find("\"p99\":");
    EXPECT_NE(p50, std::string::npos) << json;
    ASSERT_NE(p99, std::string::npos);
    EXPECT_GE(std::stod(json.substr(p99 + 6)), 0.10);
}

TEST(Metrics, LatenciesBeyondLastBucketAreRecorded) {
    Metrics metrics;
    metrics.recordRequest(200, 0, 5ULL * 1000 * 1000);  // 5 seconds
    EXPECT_EQ(numberField(metrics.toJson("pool", 1, 0, 0), "totalRequests"), 1);
}

// Counters are incremented from every worker thread without a lock, so they
// must not lose updates.
TEST(Metrics, ConcurrentRecordingLosesNothing) {
    Metrics metrics;
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&metrics] {
            for (int i = 0; i < 1000; ++i) {
                metrics.recordRequest(200, 1, 100);
                metrics.connectionOpened();
                metrics.connectionClosed();
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const std::string json = metrics.toJson("pool", 8, 0, 0);
    EXPECT_EQ(numberField(json, "totalRequests"), 8000);
    EXPECT_EQ(numberField(json, "bytesOut"), 8000);
    EXPECT_EQ(numberField(json, "activeConnections"), 0);
    EXPECT_EQ(numberField(json, "totalConnections"), 8000);
}
