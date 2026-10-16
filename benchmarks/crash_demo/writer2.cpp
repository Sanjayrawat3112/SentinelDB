#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include <cstdio>
#include <string>
#include <unistd.h>

// Phase 1: 20 committed transactions (5 keys each).
// Phase 2: one transaction left OPEN (keys written, never committed), then sleep.
// Kill it with: kill -9 <pid>
int main() {
    const size_t N = 1024;
    pager::init(N, "demo.db");
    BufferPool pool(N, 64, EvictionPolicy::LRU);
    KVStore kv(pool, N);
    kv.enableWal("demo.wal");
    uint64_t key = 0;
    for (int t = 0; t < 20; t++) {
        kv.begin();
        uint64_t first = key;
        for (int i = 0; i < 5; i++, key++) kv.insert(key, "value-" + std::to_string(key));
        kv.commit();
        for (uint64_t k = first; k < key; k++) std::printf("COMMITTED %lu\n", k);
        std::fflush(stdout);
    }
    kv.begin();
    for (int i = 0; i < 5; i++, key++) {
        kv.insert(key, "value-" + std::to_string(key));
        std::printf("UNCOMMITTED %lu\n", key);
        std::fflush(stdout);
    }
    std::printf("READY_TO_KILL (transaction left open)\n");
    std::fflush(stdout);
    sleep(600);
    return 0;
}
