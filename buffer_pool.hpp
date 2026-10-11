#pragma once
#include "pager.hpp"
#include <cstddef>
#include <memory>
#include <unordered_map>

// Phase 2 - Buffer Pool Manager.
// Sits between the Storage Engine (above) and the Pager (below).
// It decides WHICH pages may be in memory (a fixed capacity) and which
// page to evict when the pool is full. The Pager does the actual
// loading/flushing via page faults.
//
// Rules for callers (the Storage Engine):
//  1. Call fetchPage() every time you need a page - that is how the pool
//     learns the page was used (repeat accesses cause no fault, so the
//     pool cannot see them any other way).
//  2. Call markDirty() after you modify a page, or the change is lost
//     when the page is evicted.
//  3. pin() a page while you hold its pointer, unpin() when done. An
//     unpinned page may be evicted at any time, after which the old
//     pointer must not be used.

enum class EvictionPolicy { LRU, Clock };

struct PoolStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
    size_t dirtyFlushes = 0;   // evictions that wrote the page to disk
};

class ReplacementPolicy;   // defined in buffer_pool.cpp

class BufferPool {
public:
    // totalPages must match the numPages given to pager::init().
    // capacity is the max number of pages allowed in memory at once.
    BufferPool(size_t totalPages, size_t capacity, EvictionPolicy policy);
    ~BufferPool();   // flushes dirty pages and evicts everything
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Returns a pointer to the page, or nullptr if pageId is out of range
    // or the pool is full and every page in it is pinned.
    void* fetchPage(size_t pageId);

    // These return false if the page is not currently in the pool.
    bool markDirty(size_t pageId);
    bool pin(size_t pageId);
    bool unpin(size_t pageId);

    // Writes back dirty pages and removes everything from the pool
    // (ignores pins). Used at shutdown.
    void evictAll();

    bool isResident(size_t pageId) const;
    size_t residentCount() const;
    const PoolStats& stats() const;

private:
    struct Frame {
        bool dirty = false;
        int pinCount = 0;
    };

    bool evictOne();
    void removeFrame(size_t pageId);

    size_t totalPages_;
    size_t capacity_;
    std::unordered_map<size_t, Frame> frames_;
    std::unique_ptr<ReplacementPolicy> policy_;
    PoolStats stats_;
};
