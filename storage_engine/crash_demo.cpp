#include "storage_engine/kv_store.hpp"
#include "storage_engine/wal.hpp"
#include "buffer_pool.hpp"
#include "pager.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

static const char* DB = "crash_demo_disk.img";
static const char* WAL = "crash_demo.log";
static const char* ACK = "crash_demo_ack.log";
static const size_t NUM_PAGES = 64, CAPACITY = 8;

static std::string val(uint64_t k) { return "value-" + std::to_string(k); }

static int runWorkload() {
    unlink(DB); unlink(WAL); unlink(ACK);
    pager::init(NUM_PAGES, DB);
    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
    KVStore kv(pool, NUM_PAGES);
    kv.enableWal(WAL);
    FILE* ack = std::fopen(ACK, "w");
    std::printf("workload running (pid %d). Kill it with: kill -9 %d\n", (int)getpid(), (int)getpid());
    std::fflush(stdout);
    for (uint64_t i = 1; ; i++) {
        kv.begin();
        bool ok = kv.insert(2 * i, val(2 * i)) && kv.insert(2 * i + 1, val(2 * i + 1));
        if (!ok) {
            kv.abort();
            std::printf("table full at txn %llu, stopping (this was NOT a crash)\n", (unsigned long long)i);
            break;
        }
        kv.commit();
        std::fprintf(ack, "ACK %llu\n", (unsigned long long)i);
        std::fflush(ack);
        usleep(2000);
    }
    return 0;
}

static int verify() {
    uint64_t lastAck = 0;
    {
        std::ifstream f(ACK);
        std::string tag; uint64_t n;
        while (f >> tag >> n) if (tag == "ACK" && n > lastAck) lastAck = n;
    }
    std::printf("last acknowledged transaction before the crash: %llu\n", (unsigned long long)lastAck);
    pager::init(NUM_PAGES, DB);

    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
    KVStore kv(pool, NUM_PAGES);
    kv.enableWal(WAL);
    RecoveryReport r = kv.recover();
    std::printf("recovery: scanned=%zu committed=%zu aborted=%zu loser=%zu redone=%zu undone=%zu\n",
                r.recordsScanned, r.committedTxns, r.abortedTxns, r.loserTxns,
                r.changesRedone, r.changesUndone);

    int failures = 0;
    std::string out;
    for (uint64_t i = 1; i <= lastAck; i++) {
        bool a = kv.get(2 * i, out) && out == val(2 * i);
        bool b = kv.get(2 * i + 1, out) && out == val(2 * i + 1);
        if (!a || !b) { failures++; if (failures <= 5) std::printf("  [FAIL] acknowledged txn %llu is missing or wrong\n", (unsigned long long)i); }
    }
    std::printf("  checked %llu acknowledged transactions, %d problem(s)\n", (unsigned long long)lastAck, failures);

    uint64_t n = lastAck + 1;
    bool a = kv.get(2 * n, out), b = kv.get(2 * n + 1, out);
    if (a != b) { failures++; std::printf("  [FAIL] in-flight txn %llu is half-applied\n", (unsigned long long)n); }
    else std::printf("  [PASS] in-flight txn %llu is %s (all-or-nothing)\n", (unsigned long long)n, a ? "fully present" : "fully absent");

    for (uint64_t i = lastAck + 2; i <= lastAck + 50; i++) {
        if (kv.get(2 * i, out) || kv.get(2 * i + 1, out)) { failures++; std::printf("  [FAIL] unexpected data from txn %llu\n", (unsigned long long)i); break; }
    }

    if (failures) { std::printf("\nVERIFY FAILED (%d problem(s))\n", failures); return 1; }
    std::printf("\nVERIFY PASSED: every acknowledged transaction survived, nothing half-applied.\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "run") == 0) return runWorkload();
    if (argc == 2 && std::strcmp(argv[1], "verify") == 0) return verify();
    std::printf("usage: crash_demo run | verify\n");
    return 2;
}
