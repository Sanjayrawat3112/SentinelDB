#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class BufferPool;
class Wal;

constexpr size_t MAX_VALUE_SIZE = 1000;   // bytes; a record must fit inside a 4096-byte page

// Hash-based key-value store. Page = bucket, linear probing on overflow.
// numPages must equal the totalPages given to the BufferPool.
// Single-threaded: at most one transaction is active at a time.
//
// Without enableWal() it behaves as a plain store (no crash safety).
// With a WAL, every insert/remove is logged BEFORE the page is changed.
// Outside begin()/commit() each insert/remove is its own transaction (autocommit).
class KVStore {
public:
    KVStore(BufferPool& pool, size_t numPages);
    ~KVStore();
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // Insert a new key, or overwrite the value if the key already exists.
    // Returns false if value.size() > MAX_VALUE_SIZE or there is no space left
    // (on failure an existing value is left unchanged).
    bool insert(uint64_t key, const std::string& value);

    // Copies the value into outValue. Returns false if the key is not found.
    bool get(uint64_t key, std::string& outValue);

    // Deletes the key. Returns false if the key was not found.
    bool remove(uint64_t key);

    // Calls callback once for every stored record (order is not guaranteed).
    // The callback must not modify the store.
    void scan(const std::function<void(uint64_t, const std::string&)>& callback);

    // ---- Write-ahead logging and transactions ----

    // Opens the log file and turns logging on. Call once, right after construction.
    void enableWal(const std::string& walPath);
    bool walEnabled() const { return wal_ != nullptr; }

    // Explicit transactions (need enableWal). Only one can be active at a time.
    // begin() returns the transaction id. abort() undoes every change of the transaction.
    uint64_t begin();
    void commit();
    void abort();
    bool inTransaction() const { return activeTxn_ != 0; }

private:
    // Page-level work with no logging.
    size_t find(uint64_t key, std::string& outValue);
    bool place(uint64_t key, const std::string& value);
    bool applyInsert(uint64_t key, const std::string& value);
    bool applyRemove(uint64_t key);
    size_t homePage(uint64_t key) const;

    // Logging helpers.
    bool loggedChange(uint64_t key, bool hasBefore, const std::string& before,
                      bool hasAfter, const std::string& after);
    void requireWal() const;

    struct Change {                 // what one change did, kept so abort() can undo it
        uint64_t key;
        bool hasBefore;
        std::string before;
        bool hasAfter;
        std::string after;
    };

    BufferPool& pool_;
    size_t numPages_;
    std::unique_ptr<Wal> wal_;
    uint64_t activeTxn_ = 0;        // 0 = no active transaction
    std::vector<Change> undoLog_;   // changes of the active transaction
};