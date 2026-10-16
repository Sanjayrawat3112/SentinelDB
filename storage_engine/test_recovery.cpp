#include "storage_engine/kv_store.hpp"
#include "storage_engine/wal.hpp"
#include "buffer_pool.hpp"
#include "pager.hpp"
#include <cstdio>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); failures++; } } while (0)

int main() {
    const char* dbPath = "recovery_test_disk.img";
    const char* walPath = "recovery_test.log";
    unlink(dbPath); unlink(walPath);
    const size_t NUM_PAGES = 8, CAPACITY = 3;
    pager::init(NUM_PAGES, dbPath);

    std::printf("Crash after two committed inserts\n");
    pid_t pid = fork();
    if (pid == 0) {
        // Child: do the work, then die without any cleanup (simulated crash).
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        kv.enableWal(walPath);
        kv.insert(1, "one");
        kv.insert(2, "two");
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child ran and 'crashed'");

    {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        kv.enableWal(walPath);
        RecoveryReport r = kv.recover();
        std::printf("  info: scanned=%zu committed=%zu aborted=%zu loser=%zu redone=%zu undone=%zu\n",
                    r.recordsScanned, r.committedTxns, r.abortedTxns, r.loserTxns,
                    r.changesRedone, r.changesUndone);
        std::string out;
        CHECK(r.committedTxns == 2, "recovery saw 2 committed transactions");
        CHECK(r.loserTxns == 0, "no loser transactions");
        CHECK(kv.get(1, out) && out == "one", "key 1 recovered");
        CHECK(kv.get(2, out) && out == "two", "key 2 recovered");
    }


    std::printf("Crash in the middle of an uncommitted transaction\n");
    unlink(dbPath); unlink(walPath);
    pager::init(NUM_PAGES, dbPath);
    pid = fork();
    if (pid == 0) {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        kv.enableWal(walPath);
        kv.insert(1, "one");          // committed
        kv.begin();
        kv.insert(2, "two");          // never committed
        kv.insert(1, "changed");      // never committed
        _exit(0);
    }
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child ran and 'crashed'");
    {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        kv.enableWal(walPath);
        RecoveryReport r = kv.recover();
        std::printf("  info: scanned=%zu committed=%zu aborted=%zu loser=%zu redone=%zu undone=%zu\n",
                    r.recordsScanned, r.committedTxns, r.abortedTxns, r.loserTxns,
                    r.changesRedone, r.changesUndone);
        std::string out;
        CHECK(r.loserTxns == 1, "one loser transaction found");
        CHECK(kv.get(1, out) && out == "one", "key 1 rolled back to committed value");
        CHECK(!kv.get(2, out), "key 2 (uncommitted insert) is gone");
    }

    if (failures) { std::printf("\n%d recovery test(s) FAILED.\n", failures); return 1; }
    std::printf("\nAll recovery tests passed.\n");
    return 0;
}
