#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include <cstdio>
#include <string>

int main() {
    const size_t N = 1024;
    pager::init(N, "demo.db");
    BufferPool pool(N, 64, EvictionPolicy::LRU);
    KVStore kv(pool, N);
    kv.enableWal("demo.wal");
    for (uint64_t k = 0; k < 100000; k++) {
        if (!kv.insert(k, "value-" + std::to_string(k))) return 1;
        std::printf("COMMITTED %lu\n", k);
        std::fflush(stdout);
    }
    return 0;
}
