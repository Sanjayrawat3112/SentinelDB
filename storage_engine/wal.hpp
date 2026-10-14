#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Write-ahead log: a separate append-only file. It does NOT use the Pager.
// Records are written with write() and made durable with fsync().
//
// Logging is LOGICAL: a CHANGE record stores the key plus the value before
// and after the change, not a page id.
//   insert new key : hasBefore=false, hasAfter=true
//   overwrite      : hasBefore=true,  hasAfter=true
//   remove         : hasBefore=true,  hasAfter=false
// Redo  = apply 'after'  (insert it, or remove the key if there is none).
// Undo  = apply 'before' (insert it, or remove the key if there is none).
// Both are idempotent, so recovery can safely run more than once.

enum class LogType : uint8_t { BEGIN = 1, COMMIT = 2, ABORT = 3, CHANGE = 4, CHECKPOINT = 5 };

struct LogRecord {
    LogType type = LogType::BEGIN;
    uint64_t lsn = 0;        // log sequence number, assigned by Wal::append
    uint64_t txnId = 0;
    uint64_t key = 0;        // CHANGE only
    bool hasBefore = false;  // CHANGE only
    bool hasAfter = false;   // CHANGE only
    std::string before;
    std::string after;
};

class Wal {
public:
    // Opens (or creates) the log. A half-written record at the end of the
    // file (a torn write from a crash) is detected by its checksum and cut off.
    explicit Wal(const std::string& path);
    ~Wal();
    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;

    // Appends a record and returns its LSN. NOT durable until flush().
    uint64_t append(const LogRecord& rec);

    // fsync: after this returns, every appended record survives a crash.
    void flush();

    // Reads every valid record from the start of the file.
    std::vector<LogRecord> readAll() const;

private:
    int fd_;
    uint64_t nextLsn_;
};