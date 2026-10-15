#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: verifier <lastCommittedKey>\n"); return 2; }
    uint64_t last = std::strtoull(argv[1], nullptr, 10);
    const size_t N = 1024;
    pager::init(N, "demo.db");
    size_t ok = 0, missing = 0, wrong = 0;
    {
        BufferPool pool(N, 64, EvictionPolicy::LRU);
        KVStore kv(pool, N);
        kv.enableWal("demo.wal");
        // TODO: call A's recovery here once it exists.
        std::string v;
        for (uint64_t k = 0; k <= last; k++) {
            if (!kv.get(k, v)) missing++;
            else if (v != "value-" + std::to_string(k)) wrong++;
            else ok++;
        }
    }
    pager::shutdown();
    std::printf("checked keys 0..%lu: ok=%zu missing=%zu wrong=%zu\n", last, ok, missing, wrong);
    std::printf(missing == 0 && wrong == 0 ? "DEMO PASS\n" : "DEMO FAIL\n");
    return (missing == 0 && wrong == 0) ? 0 : 1;
}
