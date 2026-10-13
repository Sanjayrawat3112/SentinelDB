#include "storage_engine/kv_store.hpp"
#include "storage_engine/page_format.hpp"
#include "storage_engine/page_ops.hpp"
#include "buffer_pool.hpp"
#include <stdexcept>
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

bool KVStore::insert(uint64_t key, const std::string& value) {
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

bool KVStore::remove(uint64_t key) {
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