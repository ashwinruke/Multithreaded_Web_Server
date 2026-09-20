#include "lru_cache.h"
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

TEST(LruCache, StoresAndRetrieves) {
    LRUCache cache(4);
    cache.put("a", "alpha");

    std::string value;
    ASSERT_TRUE(cache.get("a", value));
    EXPECT_EQ(value, "alpha");
}

TEST(LruCache, MissesUnknownKey) {
    LRUCache cache(4);
    std::string value;
    EXPECT_FALSE(cache.get("nothing", value));
}

TEST(LruCache, EvictsLeastRecentlyUsed) {
    LRUCache cache(2);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");  // evicts "a"

    std::string value;
    EXPECT_FALSE(cache.get("a", value));
    EXPECT_TRUE(cache.get("b", value));
    EXPECT_TRUE(cache.get("c", value));
}

// The behaviour that makes it LRU rather than FIFO: reading an entry protects
// it from the next eviction.
TEST(LruCache, ReadingPromotesEntry) {
    LRUCache cache(2);
    cache.put("a", "1");
    cache.put("b", "2");

    std::string value;
    ASSERT_TRUE(cache.get("a", value));  // "a" is now most recent
    cache.put("c", "3");                 // so "b" should go

    EXPECT_TRUE(cache.get("a", value));
    EXPECT_FALSE(cache.get("b", value));
}

TEST(LruCache, OverwriteUpdatesValueWithoutGrowing) {
    LRUCache cache(2);
    cache.put("a", "1");
    cache.put("a", "2");
    cache.put("b", "3");

    std::string value;
    ASSERT_TRUE(cache.get("a", value));
    EXPECT_EQ(value, "2");
    EXPECT_TRUE(cache.get("b", value));
}

TEST(LruCache, CountsHitsAndMisses) {
    LRUCache cache(2);
    cache.put("a", "1");

    std::string value;
    cache.get("a", value);
    cache.get("a", value);
    cache.get("absent", value);

    EXPECT_EQ(cache.hits(), 2u);
    EXPECT_EQ(cache.misses(), 1u);
}

TEST(LruCache, ZeroCapacityIsClampedToOne) {
    LRUCache cache(0);
    cache.put("a", "1");
    std::string value;
    EXPECT_TRUE(cache.get("a", value));
}

TEST(LruCache, HandlesEmptyValues) {
    LRUCache cache(2);
    cache.put("empty", "");

    std::string value = "stale";
    ASSERT_TRUE(cache.get("empty", value));
    EXPECT_EQ(value, "");
}

// Every worker thread shares one cache, so concurrent access must not corrupt
// it or exceed capacity.
TEST(LruCache, SurvivesConcurrentAccess) {
    LRUCache cache(32);
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&cache, t] {
            for (int i = 0; i < 500; ++i) {
                const std::string key = "k" + std::to_string((t * 500 + i) % 64);
                cache.put(key, "v" + std::to_string(i));
                std::string value;
                cache.get(key, value);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(cache.hits() + cache.misses(), 8u * 500u);
}
