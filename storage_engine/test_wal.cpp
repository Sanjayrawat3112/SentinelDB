#include "storage_engine/wal.hpp"
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); failures++; } } while (0)

static long fileSize(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

static LogRecord change(uint64_t txn, uint64_t key, const char* before, const char* after) {
    LogRecord r; r.type = LogType::CHANGE; r.txnId = txn; r.key = key;
    if (before) { r.hasBefore = true; r.before = before; }
    if (after)  { r.hasAfter = true;  r.after = after; }
    return r;
}

int main() {
    const char* path = "wal_test.log";
    unlink(path);

    std::printf("Append and read back\n");
    {
        Wal wal(path);
        LogRecord b; b.type = LogType::BEGIN; b.txnId = 1;
        LogRecord c; c.type = LogType::COMMIT; c.txnId = 1;
        CHECK(wal.append(b) == 1, "first LSN is 1");
        wal.append(change(1, 42, nullptr, "hello"));
        wal.append(change(1, 42, "hello", "world"));
        wal.append(change(1, 42, "world", nullptr));
        CHECK(wal.append(c) == 5, "fifth LSN is 5");
        wal.flush();
        auto recs = wal.readAll();
        CHECK(recs.size() == 5, "5 records read back");
        CHECK(recs[1].type == LogType::CHANGE && recs[1].key == 42 && !recs[1].hasBefore && recs[1].hasAfter && recs[1].after == "hello", "insert record intact");
        CHECK(recs[2].hasBefore && recs[2].hasAfter && recs[2].before == "hello" && recs[2].after == "world", "overwrite record intact");
        CHECK(recs[3].hasBefore && !recs[3].hasAfter && recs[3].before == "world", "remove record intact");
        CHECK(recs[4].type == LogType::COMMIT && recs[4].txnId == 1 && recs[4].lsn == 5, "commit record intact");
    }

    std::printf("Reopen continues the LSN sequence\n");
    {
        Wal wal(path);
        CHECK(wal.readAll().size() == 5, "all 5 records still there");
        LogRecord b; b.type = LogType::BEGIN; b.txnId = 2;
        CHECK(wal.append(b) == 6, "next LSN is 6");
        wal.flush();
    }

    std::printf("Torn tail (crash in the middle of a write)\n");
    {
        long full = fileSize(path);
        truncate(path, full - 3);                 // chop 3 bytes off the last record
        Wal wal(path);
        CHECK(wal.readAll().size() == 5, "half-written record is dropped");
        CHECK(fileSize(path) < full - 3, "garbage tail was cut from the file");
        LogRecord b; b.type = LogType::BEGIN; b.txnId = 3;
        CHECK(wal.append(b) == 6, "LSN 6 is reused after the cut");
        wal.flush();
        CHECK(wal.readAll().size() == 6 && wal.readAll()[5].txnId == 3, "new record readable after the cut");
    }

    std::printf("Damaged record (flipped byte)\n");
    {
        int fd = open(path, O_RDWR);
        char ch; pread(fd, &ch, 1, 40);
        ch ^= 0xFF; pwrite(fd, &ch, 1, 40);
        close(fd);
        Wal wal(path);
        CHECK(wal.readAll().size() < 6, "reading stops at the damaged record");
    }

    unlink(path);
    if (failures == 0) { std::printf("\nAll wal tests passed.\n"); return 0; }
    std::printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}