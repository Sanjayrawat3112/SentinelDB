#include "pager.hpp"
#include "buffer_pool.hpp"
#include "storage_engine/kv_store.hpp"
#include "query_layer/naive_store.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

static const size_t NUM_PAGES = 1024;
static const uint64_t NUM_KEYS = 2000;
static const size_t WARMUP_OPS = 200;
static const size_t MEASURED_OPS = 2000;
using Clock_ = std::chrono::steady_clock;
static size_t g_found = 0;

static std::string makeValue(uint64_t k) {
    std::string v = "v" + std::to_string(k);
    v.resize(100, 'x');
    return v;
}

struct Op { uint64_t key; int kind; };

static std::vector<Op> makeOps(size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint64_t key = rng() % NUM_KEYS;
        int kind = (rng() % 100) < 30 ? 1 : 0;
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
static void runAll(Store& st, const std::string& engine, size_t capacity,
                   unsigned seed, const BufferPool* pool) {
    for (uint64_t k = 0; k < NUM_KEYS; k++) st.insert(k, makeValue(k));   // setup, untimed
    runOps(st, makeOps(WARMUP_OPS, seed + 1000), nullptr);                // warm-up, untimed

    PoolStats before{};
    if (pool) before = pool->stats();
    std::vector<Op> ops = makeOps(MEASURED_OPS, seed);
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
    std::printf("%s,mixed_durable,%zu,%u,%.6f,%.1f,%.2f,%.2f,%zu,%zu,%zu\n", engine.c_str(),
                capacity, seed, secs, ops.size() / secs, p50, p99, hits, misses, evictions);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <naive|sentinel> <capacity> <seed>\n", argv[0]);
        return 2;
    }
    std::string engine = argv[1];
    size_t capacity = std::strtoul(argv[2], nullptr, 10);
    unsigned seed = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));

    if (engine == "naive") {
        unlink("bench_naive.db");
        { NaiveStore st("bench_naive.db", NUM_PAGES, true); runAll(st, engine, 0, seed, nullptr); }
        unlink("bench_naive.db");
    } else if (engine == "sentinel") {
        unlink("bench_sentinel.db");
        unlink("bench_sentinel.wal");
        pager::init(NUM_PAGES, "bench_sentinel.db");
        {
            BufferPool pool(NUM_PAGES, capacity, EvictionPolicy::LRU);
            KVStore st(pool, NUM_PAGES);
            st.enableWal("bench_sentinel.wal");
            runAll(st, engine, capacity, seed, &pool);
        }
        pager::shutdown();
        unlink("bench_sentinel.db");
        unlink("bench_sentinel.wal");
    } else { std::fprintf(stderr, "bad engine\n"); return 2; }
    return 0;
}
