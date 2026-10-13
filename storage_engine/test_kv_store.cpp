#include "storage_engine/kv_store.hpp"
#include "buffer_pool.hpp"
#include "pager.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); failures++; } } while (0)

int main() {
    const char* path = "kv_test_disk.img";
    unlink(path);
    const size_t NUM_PAGES = 16, CAPACITY = 4;
    pager::init(NUM_PAGES, path);
    {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        std::string out;

        std::printf("Basic insert / get / remove\n");
        CHECK(kv.insert(1, "one"), "insert key 1");
        CHECK(kv.get(1, out) && out == "one", "get key 1");
        CHECK(!kv.get(2, out), "get missing key returns false");
        CHECK(kv.remove(1), "remove key 1");
        CHECK(!kv.get(1, out), "key 1 is gone");
        CHECK(!kv.remove(1), "removing again returns false");

        std::printf("Overwrite\n");
        CHECK(kv.insert(7, "old") && kv.insert(7, "newer value"), "insert same key twice");
        CHECK(kv.get(7, out) && out == "newer value", "get returns the new value");
        size_t count = 0;
        kv.scan([&](uint64_t, const std::string&) { count++; });
        CHECK(count == 1, "scan sees exactly one record (no duplicate)");
        CHECK(!kv.insert(8, std::string(MAX_VALUE_SIZE + 1, 'x')), "too-large value is rejected");

        std::printf("Many records with a tiny pool (forces eviction)\n");
        std::map<uint64_t, std::string> model;
        for (uint64_t k = 100; k < 400; k++) {
            std::string v = "value-" + std::to_string(k) + std::string(k % 50, 'z');
            if (kv.insert(k, v)) model[k] = v;
        }
        CHECK(model.size() == 300, "all 300 inserts succeeded");
        bool allOk = true;
        for (auto& kvp : model)
            if (!(kv.get(kvp.first, out) && out == kvp.second)) allOk = false;
        CHECK(allOk, "all 300 records read back correctly");
        CHECK(pool.residentCount() <= CAPACITY, "pool never exceeded its capacity");

        std::printf("Remove then probing still works\n");
        for (uint64_t k = 100; k < 400; k += 2) { kv.remove(k); model.erase(k); }
        allOk = true;
        for (uint64_t k = 100; k < 400; k++) {
            bool expect = model.count(k) > 0;
            bool got = kv.get(k, out);
            if (got != expect || (got && out != model[k])) allOk = false;
        }
        CHECK(allOk, "odd keys present, even keys gone");

        std::printf("Scan\n");
        std::map<uint64_t, std::string> seen;
        kv.scan([&](uint64_t k, const std::string& v) { seen[k] = v; });
        seen.erase(7);
        CHECK(seen == model, "scan returns exactly the live records");

        std::printf("Full table\n");
        std::string big(MAX_VALUE_SIZE, 'b');
        uint64_t k = 10000; size_t okCount = 0;
        while (kv.insert(k, big)) { k++; okCount++; if (okCount > 1000) break; }
        CHECK(okCount < 1000, "insert eventually reports failure when the table is full");
        CHECK(kv.get(150 + 1, out) && out == model[151], "older data still intact after the table filled");
        CHECK(kv.insert(151, "small"), "overwrite with a smaller value still works when full");
        CHECK(kv.get(151, out) && out == "small", "...and reads back");
    }
    pager::shutdown();
    unlink(path);
    if (failures == 0) { std::printf("\nAll kv_store tests passed.\n"); return 0; }
    std::printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}