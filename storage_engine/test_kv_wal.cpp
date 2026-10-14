#include "storage_engine/kv_store.hpp"
#include "storage_engine/wal.hpp"
#include "buffer_pool.hpp"
#include "pager.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); failures++; } } while (0)

static size_t countType(const std::vector<LogRecord>& v, LogType t) {
    size_t n = 0;
    for (auto& r : v) if (r.type == t) n++;
    return n;
}

int main() {
    const char* dbPath = "kvwal_test_disk.img";
    const char* walPath = "kvwal_test.log";
    unlink(dbPath); unlink(walPath);
    const size_t NUM_PAGES = 8, CAPACITY = 3;
    pager::init(NUM_PAGES, dbPath);
    {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        KVStore kv(pool, NUM_PAGES);
        std::string out;

        std::printf("Transactions need a WAL\n");
        bool threw = false;
        try { kv.begin(); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw, "begin() without enableWal() throws");
        kv.enableWal(walPath);
        CHECK(kv.walEnabled(), "WAL is enabled");

        std::printf("Autocommit writes BEGIN, CHANGE, COMMIT\n");
        CHECK(kv.insert(1, "one"), "insert key 1");
        {
            Wal reader(walPath);
            auto recs = reader.readAll();
            CHECK(recs.size() == 3, "3 log records for one insert");
            CHECK(recs.size() == 3 && recs[0].type == LogType::BEGIN && recs[1].type == LogType::CHANGE && recs[2].type == LogType::COMMIT, "order is BEGIN, CHANGE, COMMIT");
            CHECK(recs.size() == 3 && !recs[1].hasBefore && recs[1].hasAfter && recs[1].after == "one" && recs[1].key == 1, "CHANGE has key and after-value only");
        }
        CHECK(kv.insert(1, "uno"), "overwrite key 1");
        CHECK(kv.remove(1), "remove key 1");
        CHECK(!kv.remove(1), "removing a missing key returns false");
        {
            Wal reader(walPath);
            auto recs = reader.readAll();
            CHECK(countType(recs, LogType::CHANGE) == 3, "missing-key remove wrote no log record");
            CHECK(countType(recs, LogType::COMMIT) == 3, "3 autocommit transactions");
            CHECK(recs[4].hasBefore && recs[4].hasAfter && recs[4].before == "one" && recs[4].after == "uno", "overwrite logged before and after");
        }

        std::printf("Commit\n");
        uint64_t t = kv.begin();
        CHECK(kv.inTransaction() && t != 0, "transaction is active");
        kv.insert(10, "a"); kv.insert(11, "b");
        kv.commit();
        CHECK(!kv.inTransaction(), "transaction finished");
        CHECK(kv.get(10, out) && out == "a" && kv.get(11, out) && out == "b", "committed data is visible");

        std::printf("Abort undoes everything\n");
        kv.insert(20, "keep");
        kv.begin();
        kv.insert(21, "temp");          // new key
        kv.insert(20, "changed");       // overwrite
        kv.remove(10);                  // delete
        CHECK(kv.get(21, out) && kv.get(20, out) && out == "changed" && !kv.get(10, out), "changes visible inside the transaction");
        kv.abort();
        CHECK(!kv.get(21, out), "new key is gone after abort");
        CHECK(kv.get(20, out) && out == "keep", "overwritten value restored");
        CHECK(kv.get(10, out) && out == "a", "removed key restored");
        CHECK(!kv.inTransaction(), "no active transaction after abort");
        {
            Wal reader(walPath);
            auto recs = reader.readAll();
            CHECK(recs.back().type == LogType::ABORT, "log ends with ABORT");
        }

        std::printf("Misuse is reported\n");
        threw = false;
        try { kv.commit(); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw, "commit() with no transaction throws");
        kv.begin();
        threw = false;
        try { kv.begin(); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw, "nested begin() throws");
        kv.commit();

        std::printf("Full table: failed insert changes nothing\n");
        std::string big(MAX_VALUE_SIZE, 'b');
        uint64_t k = 1000; size_t ok = 0;
        while (kv.insert(k, big)) { k++; if (++ok > 200) break; }
        CHECK(ok < 200, "table eventually fills up");
        CHECK(!kv.get(k, out), "the key that did not fit is absent");
        CHECK(kv.get(20, out) && out == "keep", "older data intact");
        kv.begin();
        CHECK(!kv.insert(k, big), "failed insert inside a transaction returns false");
        kv.abort();
        CHECK(kv.get(1000, out) && out == big, "abort after a failed insert leaves earlier data alone");
    }
    pager::shutdown();
    unlink(dbPath); unlink(walPath);
    if (failures == 0) { std::printf("\nAll kv_wal tests passed.\n"); return 0; }
    std::printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}