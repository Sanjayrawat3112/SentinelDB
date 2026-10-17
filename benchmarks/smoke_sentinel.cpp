#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include <cstdio>
#include <unistd.h>

int main() {
    const size_t N = 1024;
    unlink("smoke.db");
    pager::init(N, "smoke.db");
    {
        BufferPool pool(N, 64, EvictionPolicy::LRU);
        KVStore kv(pool, N);
        std::string v;
        for (uint64_t k = 0; k < 2000; k++)
            if (!kv.insert(k, "val" + std::to_string(k))) { std::printf("insert FAIL %lu\n", k); return 1; }
        for (uint64_t k = 0; k < 2000; k++)
            if (!kv.get(k, v) || v != "val" + std::to_string(k)) { std::printf("get FAIL %lu\n", k); return 1; }
        const PoolStats& s = pool.stats();
        std::printf("hits=%zu misses=%zu evictions=%zu flushes=%zu\n", s.hits, s.misses, s.evictions, s.dirtyFlushes);
        std::printf("SENTINEL SMOKE OK\n");
    }
    pager::shutdown();
    unlink("smoke.db");
    return 0;
}
