// Phase 2 tests for the Buffer Pool Manager, running on the real Pager.
// Page i on the fake "disk" is pre-filled with the byte ('A' + i).
#include "buffer_pool.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t NUM_PAGES = 16;
constexpr size_t CAPACITY = 4;
const char* DISK_PATH = "bp_test_disk.img";

static int g_failures = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failures++;
}

static char expectedByte(size_t id) { return static_cast<char>('A' + id); }

static void prepareDisk() {
    int fd = open(DISK_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    char buf[PAGE_SIZE];
    for (size_t i = 0; i < NUM_PAGES; i++) {
        memset(buf, expectedByte(i), PAGE_SIZE);
        pwrite(fd, buf, PAGE_SIZE, static_cast<off_t>(i * PAGE_SIZE));
    }
    close(fd);
}

static char diskByte(size_t pageId, size_t offset) {
    int fd = open(DISK_PATH, O_RDONLY);
    char c = 0;
    pread(fd, &c, 1, static_cast<off_t>(pageId * PAGE_SIZE + offset));
    close(fd);
    return c;
}

// Touch a page so it really is loaded by the pager.
static char touch(BufferPool& pool, size_t id) {
    char* p = static_cast<char*>(pool.fetchPage(id));
    return p ? p[0] : 0;
}

static void testRandomAccess(EvictionPolicy pol, const char* name) {
    printf("Random access with %s\n", name);
    prepareDisk();
    BufferPool pool(NUM_PAGES, CAPACITY, pol);
    srand(7);
    bool dataOk = true, capOk = true;
    for (int n = 0; n < 500; n++) {
        size_t id = rand() % NUM_PAGES;
        size_t off = rand() % PAGE_SIZE;
        char* p = static_cast<char*>(pool.fetchPage(id));
        if (!p || p[off] != expectedByte(id)) dataOk = false;
        if (pool.residentCount() > CAPACITY) capOk = false;
    }
    check(dataOk, "500 random fetches returned correct data");
    check(capOk, "pool never exceeded its capacity");
}

static void testLruVictim() {
    printf("LRU evicts the least recently used page\n");
    prepareDisk();
    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
    for (size_t id = 0; id < 4; id++) touch(pool, id);
    touch(pool, 0);   // page 0 becomes most recently used
    touch(pool, 4);   // pool full -> must evict page 1
    check(!pool.isResident(1), "page 1 (least recent) was evicted");
    check(pool.isResident(0), "page 0 (recently used) stayed");
    check(pool.residentCount() == CAPACITY, "pool holds exactly CAPACITY pages");
    const PoolStats& s = pool.stats();
    check(s.hits == 1 && s.misses == 5 && s.evictions == 1,
          "stats: 1 hit, 5 misses, 1 eviction");
}

static void testClockSecondChance() {
    printf("Clock gives referenced pages a second chance\n");
    prepareDisk();
    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::Clock);
    for (size_t id = 0; id < 4; id++) touch(pool, id);
    touch(pool, 4);   // all bits set -> sweep clears them, page 0 evicted
    check(!pool.isResident(0), "page 0 evicted after the first full sweep");
    touch(pool, 1);   // hit: page 1 gets its reference bit back
    touch(pool, 5);   // needs a victim: page 1 is skipped, page 2 goes
    check(pool.isResident(1), "page 1 survived (second chance)");
    check(!pool.isResident(2), "page 2 (bit already clear) was evicted");
}

static void testPinning() {
    printf("Pinned pages are never evicted\n");
    prepareDisk();
    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
    for (size_t id = 0; id < 4; id++) touch(pool, id);
    pool.pin(0);
    touch(pool, 4);   // page 0 is oldest but pinned -> page 1 goes
    check(pool.isResident(0) && !pool.isResident(1),
          "pinned page 0 stayed, page 1 evicted instead");
    pool.pin(2); pool.pin(3); pool.pin(4);
    check(pool.fetchPage(5) == nullptr,
          "fetch returns nullptr when every page is pinned");
    pool.unpin(0);
    check(pool.fetchPage(5) != nullptr, "fetch works again after unpinning");
}

static void testDirtyFlush() {
    printf("Dirty pages are flushed on eviction, clean ones are not\n");
    prepareDisk();
    BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);

    char* a = static_cast<char*>(pool.fetchPage(6));
    a[0] = 'Z';
    pool.markDirty(6);                       // modified and reported

    char* b = static_cast<char*>(pool.fetchPage(7));
    b[1] = 'X';                              // modified but NOT reported

    for (size_t id = 8; id < 16; id++) touch(pool, id);   // forces evictions

    check(!pool.isResident(6) && !pool.isResident(7), "pages 6 and 7 were evicted");
    check(diskByte(6, 0) == 'Z', "dirty page 6 reached the disk");
    check(diskByte(7, 1) == expectedByte(7), "unreported change to page 7 did not");
    check(pool.stats().dirtyFlushes == 1, "exactly 1 dirty flush recorded");
}

static void testInvalidAndShutdown() {
    printf("Invalid input and shutdown flush\n");
    prepareDisk();
    {
        BufferPool pool(NUM_PAGES, CAPACITY, EvictionPolicy::LRU);
        check(pool.fetchPage(NUM_PAGES) == nullptr, "out-of-range pageId returns nullptr");
        check(pool.fetchPage(100000) == nullptr, "huge pageId returns nullptr");
        check(!pool.markDirty(3), "markDirty on a page not in the pool returns false");

        char* p = static_cast<char*>(pool.fetchPage(2));
        p[0] = 'Q';
        pool.markDirty(2);
    }   // pool destroyed here -> must flush page 2
    check(diskByte(2, 0) == 'Q', "destructor flushed the dirty page");
}

int main() {
    prepareDisk();
    pager::init(NUM_PAGES, DISK_PATH);

    testRandomAccess(EvictionPolicy::LRU, "LRU");
    testRandomAccess(EvictionPolicy::Clock, "Clock");
    testLruVictim();
    testClockSecondChance();
    testPinning();
    testDirtyFlush();
    testInvalidAndShutdown();

    pager::shutdown();
    if (g_failures == 0) {
        printf("\nAll buffer pool tests passed.\n");
        return 0;
    }
    printf("\n%d check(s) FAILED.\n", g_failures);
    return 1;
}
