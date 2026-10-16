#include "storage_engine/kv_store.hpp"
#include "storage_engine/page_format.hpp"
#include "storage_engine/page_ops.hpp"
#include "storage_engine/wal.hpp"
#include "buffer_pool.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// Holds one page pinned for the duration of a scope: fetch -> pin -> ... -> unpin.
class PinnedPage {
public:
    PinnedPage(BufferPool& pool, size_t id) : pool_(pool), id_(id) {
        ptr_ = pool_.fetchPage(id_);
        if (!ptr_) throw std::runtime_error("KVStore: fetchPage failed");
        pool_.pin(id_);
    }
    ~PinnedPage() { pool_.unpin(id_); }
    PinnedPage(const PinnedPage&) = delete;
    PinnedPage& operator=(const PinnedPage&) = delete;

    void* get() const { return ptr_; }
    void markDirty() { pool_.markDirty(id_); }

private:
    BufferPool& pool_;
    size_t id_;
    void* ptr_;
};

} // namespace

KVStore::KVStore(BufferPool& pool, size_t numPages) : pool_(pool), numPages_(numPages) {}

KVStore::~KVStore() = default;

size_t KVStore::homePage(uint64_t key) const {
    uint64_t x = key;                       // splitmix64 finalizer: spreads similar keys apart
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<size_t>(x % numPages_);
}

size_t KVStore::find(uint64_t key, std::string& outValue) {
    size_t home = homePage(key);
    for (size_t i = 0; i < numPages_; i++) {
        size_t id = (home + i) % numPages_;
        PinnedPage pg(pool_, id);
        if (pageGet(pg.get(), key, outValue)) return id;
        if (!pageIsOverflowed(pg.get())) break;   // nobody was pushed past this page
    }
    return numPages_;
}

bool KVStore::place(uint64_t key, const std::string& value) {
    size_t home = homePage(key);
    for (size_t i = 0; i < numPages_; i++) {
        size_t id = (home + i) % numPages_;
        PinnedPage pg(pool_, id);
        PageHeader* h = static_cast<PageHeader*>(pg.get());
        if (h->pageType == PAGE_FREE) pageInit(pg.get());   // first use of this page
        if (pageInsert(pg.get(), key, value)) { pg.markDirty(); return true; }
        pageSetOverflowed(pg.get());                         // we are moving past this page
        pg.markDirty();
    }
    return false;
}

bool KVStore::applyInsert(uint64_t key, const std::string& value) {
    if (value.size() > MAX_VALUE_SIZE) return false;

    std::string oldValue;
    size_t at = find(key, oldValue);
    if (at != numPages_) {                       // overwrite: remove old copy first
        PinnedPage pg(pool_, at);
        pageRemove(pg.get(), key);
        pg.markDirty();
    }
    if (place(key, value)) return true;

    if (at != numPages_) {                       // no room: put the old value back
        PinnedPage pg(pool_, at);
        pageInsert(pg.get(), key, oldValue);
        pg.markDirty();
    }
    return false;
}

bool KVStore::get(uint64_t key, std::string& outValue) {
    return find(key, outValue) != numPages_;
}

bool KVStore::applyRemove(uint64_t key) {
    std::string tmp;
    size_t at = find(key, tmp);
    if (at == numPages_) return false;
    PinnedPage pg(pool_, at);
    pageRemove(pg.get(), key);
    pg.markDirty();
    return true;
}

void KVStore::scan(const std::function<void(uint64_t, const std::string&)>& callback) {
    for (size_t id = 0; id < numPages_; id++) {
        std::vector<std::pair<uint64_t, std::string>> records;
        {
            PinnedPage pg(pool_, id);
            pageCollect(pg.get(), records);
        }                                         // page unpinned before callbacks run
        for (const auto& r : records) callback(r.first, r.second);
    }
}

// ---------------------------------------------------------------------------
// Public operations: plain, or logged when a WAL is enabled.
// ---------------------------------------------------------------------------

bool KVStore::insert(uint64_t key, const std::string& value) {
    if (value.size() > MAX_VALUE_SIZE) return false;
    if (!wal_) return applyInsert(key, value);

    std::string before;
    bool had = find(key, before) != numPages_;
    return loggedChange(key, had, before, true, value);
}

bool KVStore::remove(uint64_t key) {
    if (!wal_) return applyRemove(key);

    std::string before;
    if (find(key, before) == numPages_) return false;
    return loggedChange(key, true, before, false, std::string());
}

// ---------------------------------------------------------------------------
// WAL and transactions
// ---------------------------------------------------------------------------

void KVStore::requireWal() const {
    if (!wal_) throw std::logic_error("KVStore: call enableWal() first");
}

void KVStore::enableWal(const std::string& walPath) {
    if (wal_) throw std::logic_error("KVStore: WAL already enabled");
    wal_.reset(new Wal(walPath));
}

uint64_t KVStore::begin() {
    requireWal();
    if (activeTxn_ != 0) throw std::logic_error("KVStore: a transaction is already active");
    LogRecord r;
    r.type = LogType::BEGIN;
    r.txnId = wal_->nextLsn();          // the BEGIN record's LSN doubles as the transaction id
    activeTxn_ = r.txnId;
    wal_->append(r);
    return activeTxn_;
}

void KVStore::commit() {
    requireWal();
    if (activeTxn_ == 0) throw std::logic_error("KVStore: no active transaction");
    LogRecord r;
    r.type = LogType::COMMIT;
    r.txnId = activeTxn_;
    wal_->append(r);
    wal_->flush();                      // the transaction is durable once this returns
    activeTxn_ = 0;
    undoLog_.clear();
}

// Logs one change, THEN applies it to the pages (the write-ahead rule).
bool KVStore::loggedChange(uint64_t key, bool hasBefore, const std::string& before,
                           bool hasAfter, const std::string& after) {
    bool autocommit = (activeTxn_ == 0);
    if (autocommit) begin();

    LogRecord r;
    r.type = LogType::CHANGE;
    r.txnId = activeTxn_;
    r.key = key;
    r.hasBefore = hasBefore;  r.before = before;
    r.hasAfter = hasAfter;    r.after = after;
    wal_->append(r);
    wal_->flush();                      // log is on disk BEFORE any page is touched

    bool ok = hasAfter ? applyInsert(key, after) : applyRemove(key);
    if (ok) {
        undoLog_.push_back(Change{key, hasBefore, before, hasAfter, after});
    } else {
        // The store had no room, so nothing changed. Log the opposite change so a
        // later redo of the log ends up with the same state as the live store.
        LogRecord c = r;
        c.hasBefore = r.hasAfter;  c.before = r.after;
        c.hasAfter = r.hasBefore;  c.after = r.before;
        wal_->append(c);
    }

    if (autocommit) commit();
    return ok;
}

// Undoes 'changes' (newest first) and finishes the transaction with an ABORT record.
// The undo is logged as ordinary changes ("compensation") so that recovery can
// simply repeat the log and reach the same state.
void KVStore::rollback(uint64_t txnId, const std::vector<Change>& changes) {
    for (size_t i = changes.size(); i-- > 0;) {
        const Change& c = changes[i];
        LogRecord r;
        r.type = LogType::CHANGE;
        r.txnId = txnId;
        r.key = c.key;
        r.hasBefore = c.hasAfter;  r.before = c.after;
        r.hasAfter = c.hasBefore;  r.after = c.before;
        wal_->append(r);
    }
    wal_->flush();                      // log first, then touch the pages

    for (size_t i = changes.size(); i-- > 0;) {
        const Change& c = changes[i];
        if (c.hasBefore) applyInsert(c.key, c.before);
        else applyRemove(c.key);
    }

    LogRecord a;
    a.type = LogType::ABORT;
    a.txnId = txnId;
    wal_->append(a);
    wal_->flush();
}

void KVStore::abort() {
    requireWal();
    if (activeTxn_ == 0) throw std::logic_error("KVStore: no active transaction");
    rollback(activeTxn_, undoLog_);
    activeTxn_ = 0;
    undoLog_.clear();
}

// ---------------------------------------------------------------------------
// Checkpoint and crash recovery
// ---------------------------------------------------------------------------

void KVStore::checkpoint() {
    requireWal();
    if (activeTxn_ != 0) throw std::logic_error("KVStore: cannot checkpoint during a transaction");
    pool_.evictAll();                   // every dirty page goes to the data file
    wal_->truncate();                   // the old log is no longer needed
    LogRecord r;
    r.type = LogType::CHECKPOINT;
    wal_->append(r);
    wal_->flush();
}

// Removes every stored copy of the given keys, wherever probing may have put them.
void KVStore::purgeKeys(const std::unordered_set<uint64_t>& keys) {
    for (size_t id = 0; id < numPages_; id++) {
        PinnedPage pg(pool_, id);
        std::vector<std::pair<uint64_t, std::string>> recs;
        pageCollect(pg.get(), recs);
        bool changed = false;
        for (const auto& r : recs)
            if (keys.count(r.first) && pageRemove(pg.get(), r.first)) changed = true;
        if (changed) pg.markDirty();
    }
}

RecoveryReport KVStore::recover() {
    requireWal();
    if (activeTxn_ != 0) throw std::logic_error("KVStore: cannot recover during a transaction");
    RecoveryReport rep;

    std::vector<LogRecord> log = wal_->readAll();
    size_t start = 0;                                    // replay from the last checkpoint
    for (size_t i = log.size(); i-- > 0;)
        if (log[i].type == LogType::CHECKPOINT) { start = i + 1; break; }
    rep.recordsScanned = log.size() - start;

    // ---- Analysis: who finished, who was still running, which keys were touched ----
    std::unordered_set<uint64_t> seen, finished, touched;
    for (size_t i = start; i < log.size(); i++) {
        const LogRecord& r = log[i];
        switch (r.type) {
            case LogType::BEGIN:  seen.insert(r.txnId); break;
            case LogType::CHANGE: seen.insert(r.txnId); touched.insert(r.key); break;
            case LogType::COMMIT: finished.insert(r.txnId); rep.committedTxns++; break;
            case LogType::ABORT:  finished.insert(r.txnId); rep.abortedTxns++; break;
            case LogType::CHECKPOINT: break;
        }
    }
    std::vector<uint64_t> losers;
    for (uint64_t t : seen) if (!finished.count(t)) losers.push_back(t);
    std::sort(losers.begin(), losers.end(), std::greater<uint64_t>());   // newest first

    // ---- Redo: repeat history. Drop every copy of the touched keys first (a crash
    //      can leave a key in two pages), then replay all logged changes in order. ----
    purgeKeys(touched);
    for (size_t i = start; i < log.size(); i++) {
        const LogRecord& r = log[i];
        if (r.type != LogType::CHANGE) continue;
        if (r.hasAfter) applyInsert(r.key, r.after);
        else applyRemove(r.key);
        rep.changesRedone++;
    }

    // ---- Undo: roll back transactions that never finished ----
    for (uint64_t t : losers) {
        std::vector<Change> mine;
        for (size_t i = start; i < log.size(); i++) {
            const LogRecord& r = log[i];
            if (r.type == LogType::CHANGE && r.txnId == t)
                mine.push_back(Change{r.key, r.hasBefore, r.before, r.hasAfter, r.after});
        }
        rollback(t, mine);
        rep.changesUndone += mine.size();
        rep.loserTxns++;
    }

    if (rep.recordsScanned > 0) checkpoint();            // make the recovered state durable
    return rep;
}