#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include <cstdio>
#include <string>

int main() {
    const size_t N = 1024;
    pager::init(N, "demo.db");
    size_t okC = 0, missC = 0, wrongC = 0, leaked = 0;
    {
        BufferPool pool(N, 64, EvictionPolicy::LRU);
        KVStore kv(pool, N);
        kv.enableWal("demo.wal");
#ifdef WITH_RECOVERY
        RecoveryReport r = kv.recover();
        std::printf("recovery: scanned=%zu committed=%zu aborted=%zu losers=%zu redone=%zu undone=%zu\n",
                    (size_t)r.recordsScanned, (size_t)r.committedTxns, (size_t)r.abortedTxns,
                    (size_t)r.loserTxns, (size_t)r.changesRedone, (size_t)r.changesUndone);
#endif
        std::string v;
        for (uint64_t k = 0; k <= 99; k++) {
            if (!kv.get(k, v)) missC++;
            else if (v != "value-" + std::to_string(k)) wrongC++;
            else okC++;
        }
        for (uint64_t k = 100; k <= 104; k++) if (kv.get(k, v)) leaked++;
    }
    pager::shutdown();
    std::printf("committed keys 0..99: ok=%zu missing=%zu wrong=%zu\n", okC, missC, wrongC);
    std::printf("uncommitted keys 100..104 still visible: %zu (must be 0)\n", leaked);
    bool pass = (missC == 0 && wrongC == 0 && leaked == 0);
    std::printf(pass ? "DEMO PASS\n" : "DEMO FAIL\n");
    return pass ? 0 : 1;
}
