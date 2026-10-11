#include "buffer_pool.hpp"
#include <functional>
#include <list>
#include <vector>

// Interface every eviction policy implements. The pool tells the policy
// about inserts, accesses and removals; the policy only decides the victim.
class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;
    virtual void onInsert(size_t id) = 0;
    virtual void onAccess(size_t id) = 0;
    virtual void onRemove(size_t id) = 0;
    // Pick a page that satisfies `evictable` (i.e. is not pinned).
    virtual bool pickVictim(const std::function<bool(size_t)>& evictable,
                            size_t& out) = 0;
};

namespace {

// LRU: evict the page that was used least recently.
class LruPolicy : public ReplacementPolicy {
public:
    void onInsert(size_t id) override {
        order_.push_front(id);
        pos_[id] = order_.begin();
    }
    void onAccess(size_t id) override {
        order_.splice(order_.begin(), order_, pos_.at(id));
    }
    void onRemove(size_t id) override {
        order_.erase(pos_.at(id));
        pos_.erase(id);
    }
    bool pickVictim(const std::function<bool(size_t)>& evictable,
                    size_t& out) override {
        for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
            if (evictable(*it)) { out = *it; return true; }
        }
        return false;
    }
private:
    std::list<size_t> order_;   // front = most recently used
    std::unordered_map<size_t, std::list<size_t>::iterator> pos_;
};

// Clock (second chance): a hand sweeps a circle of frames. A page with its
// reference bit set gets the bit cleared and is skipped once; a page whose
// bit is already clear is evicted.
class ClockPolicy : public ReplacementPolicy {
public:
    explicit ClockPolicy(size_t capacity) : slots_(capacity) {}

    void onInsert(size_t id) override {
        for (size_t i = 0; i < slots_.size(); i++) {
            if (!slots_[i].used) {
                slots_[i].used = true;
                slots_[i].ref = true;
                slots_[i].id = id;
                slotOf_[id] = i;
                return;
            }
        }
    }
    void onAccess(size_t id) override { slots_[slotOf_.at(id)].ref = true; }
    void onRemove(size_t id) override {
        slots_[slotOf_.at(id)].used = false;
        slotOf_.erase(id);
    }
    bool pickVictim(const std::function<bool(size_t)>& evictable,
                    size_t& out) override {
        // Two full sweeps are enough: the first clears reference bits.
        for (size_t step = 0; step < 2 * slots_.size(); step++) {
            Slot& s = slots_[hand_];
            hand_ = (hand_ + 1) % slots_.size();
            if (!s.used || !evictable(s.id)) continue;
            if (s.ref) { s.ref = false; continue; }
            out = s.id;
            return true;
        }
        return false;
    }
private:
    struct Slot {
        bool used = false;
        bool ref = false;
        size_t id = 0;
    };
    std::vector<Slot> slots_;
    std::unordered_map<size_t, size_t> slotOf_;
    size_t hand_ = 0;
};

} // namespace

BufferPool::BufferPool(size_t totalPages, size_t capacity, EvictionPolicy policy)
    : totalPages_(totalPages), capacity_(capacity) {
    if (policy == EvictionPolicy::LRU) {
        policy_ = std::make_unique<LruPolicy>();
    } else {
        policy_ = std::make_unique<ClockPolicy>(capacity);
    }
}

BufferPool::~BufferPool() { evictAll(); }

void* BufferPool::fetchPage(size_t pageId) {
    if (pageId >= totalPages_) return nullptr;

    if (frames_.count(pageId)) {
        stats_.hits++;
        policy_->onAccess(pageId);
        return pager::getPage(pageId);
    }

    stats_.misses++;
    if (frames_.size() >= capacity_ && !evictOne()) {
        return nullptr;   // pool full and everything is pinned
    }
    frames_[pageId] = Frame{};
    policy_->onInsert(pageId);
    return pager::getPage(pageId);
}

bool BufferPool::markDirty(size_t pageId) {
    auto it = frames_.find(pageId);
    if (it == frames_.end()) return false;
    it->second.dirty = true;
    return true;
}

bool BufferPool::pin(size_t pageId) {
    auto it = frames_.find(pageId);
    if (it == frames_.end()) return false;
    it->second.pinCount++;
    return true;
}

bool BufferPool::unpin(size_t pageId) {
    auto it = frames_.find(pageId);
    if (it == frames_.end() || it->second.pinCount == 0) return false;
    it->second.pinCount--;
    return true;
}

bool BufferPool::evictOne() {
    size_t victim = 0;
    auto evictable = [this](size_t id) { return frames_.at(id).pinCount == 0; };
    if (!policy_->pickVictim(evictable, victim)) return false;
    removeFrame(victim);
    return true;
}

void BufferPool::removeFrame(size_t pageId) {
    const Frame& f = frames_.at(pageId);
    pager::evictPage(pageId, f.dirty);   // flushes first if dirty
    stats_.evictions++;
    if (f.dirty) stats_.dirtyFlushes++;
    policy_->onRemove(pageId);
    frames_.erase(pageId);
}

void BufferPool::evictAll() {
    std::vector<size_t> ids;
    for (const auto& kv : frames_) ids.push_back(kv.first);
    for (size_t id : ids) removeFrame(id);
}

bool BufferPool::isResident(size_t pageId) const { return frames_.count(pageId) > 0; }
size_t BufferPool::residentCount() const { return frames_.size(); }
const PoolStats& BufferPool::stats() const { return stats_; }
