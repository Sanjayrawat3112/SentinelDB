#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include "query_layer/naive_store.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

static const size_t NUM_PAGES = 1024;
static const uint64_t NUM_KEYS = 20000;
static const size_t WARMUP_OPS = 2000;
static const size_t MEASURED_OPS = 20000;

using Clock_ = std::chrono::steady_clock;
static size_t g_found = 0;

static std::string makeValue(uint64_t k) {
    std::string v = "v" + std::to_string(k);
    v.resize(100, 'x');   // fixed ~100 bytes
    return v;
}

// op: 0 = get, 1 = insert/overwrite. Same (seed, workload) => same sequence for every engine.
struct Op { uint64_t key; int kind; };

static std::vector<Op> makeOps(const std::string& wl, size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint64_t key = (wl == "seq") ? (i % NUM_KEYS) : (rng() % NUM_KEYS);
        int kind = (wl == "mixed" && (rng() % 100) < 30) ? 1 : 0;
        ops.push_back({key, kind});
    }
    return ops;
}

template <class Store>
static void runOps(Store& st, const std::vector<Op>& ops, std::vector<double>* lat) {
    std::string out;
    for (const Op& op : ops) {
        auto t0 = Clock_::now();
        if (op.kind == 0) { if (st.get(op.key, out)) g_found++; }
        else st.insert(op.key, makeValue(op.key));
        auto t1 = Clock_::now();
        if (lat) lat->push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
}

template <class Store>
static void runAll(Store& st, const std::string& engine, const std::string& wl,
                   size_t capacity, unsigned seed, const BufferPool* pool) {
    for (uint64_t k = 0; k < NUM_KEYS; k++) st.insert(k, makeValue(k));   // setup, untimed
    runOps(st, makeOps(wl, WARMUP_OPS, seed + 1000), nullptr);            // warm-up, untimed

    PoolStats before{};
    if (pool) before = pool->stats();
    std::vector<Op> ops = makeOps(wl, MEASURED_OPS, seed);
    std::vector<double> lat;
    lat.reserve(ops.size());
    auto t0 = Clock_::now();
    runOps(st, ops, &lat);
    auto t1 = Clock_::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    size_t hits = 0, misses = 0, evictions = 0;
    if (pool) {
        const PoolStats& a = pool->stats();
        hits = a.hits - before.hits;
        misses = a.misses - before.misses;
        evictions = a.evictions - before.evictions;
    }
    std::fprintf(stderr, "found=%zu\n", g_found);
    std::sort(lat.begin(), lat.end());
    double p50 = lat[lat.size() / 2];
    double p99 = lat[(size_t)(lat.size() * 0.99)];
    std::printf("%s,%s,%zu,%u,%.6f,%.1f,%.2f,%.2f,%zu,%zu,%zu\n", engine.c_str(), wl.c_str(),
                capacity, seed, secs, ops.size() / secs, p50, p99, hits, misses, evictions);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <naive|sentinel> <random|seq|mixed> <capacity> <seed>\n", argv[0]);
        return 2;
    }
    std::string engine = argv[1], wl = argv[2];
    size_t capacity = std::strtoul(argv[3], nullptr, 10);
    unsigned seed = static_cast<unsigned>(std::strtoul(argv[4], nullptr, 10));
    if (wl != "random" && wl != "seq" && wl != "mixed") { std::fprintf(stderr, "bad workload\n"); return 2; }

    if (engine == "naive") {
        unlink("bench_naive.db");
        { NaiveStore st("bench_naive.db", NUM_PAGES); runAll(st, engine, wl, 0, seed, nullptr); }
        unlink("bench_naive.db");
    } else if (engine == "sentinel") {
        unlink("bench_sentinel.db");
        pager::init(NUM_PAGES, "bench_sentinel.db");
        {
            BufferPool pool(NUM_PAGES, capacity, EvictionPolicy::LRU);
            KVStore st(pool, NUM_PAGES);
            runAll(st, engine, wl, capacity, seed, &pool);
        }
        pager::shutdown();
        unlink("bench_sentinel.db");
    } else { std::fprintf(stderr, "bad engine\n"); return 2; }
    return 0;
}
