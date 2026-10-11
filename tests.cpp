// Phase 1 tests: random access, repeated evict/refault, dirty write-back.
// Page i on the fake "disk" is pre-filled with the byte ('A' + i).
#include "pager.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t NUM_PAGES = 16;
const char* DISK_PATH = "test_disk.img";

static int g_failures = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failures++;
}

static char expectedByte(size_t pageId) {
    return static_cast<char>('A' + pageId);
}

static void prepareDisk() {
    int fd = open(DISK_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    char buf[PAGE_SIZE];
    for (size_t i = 0; i < NUM_PAGES; i++) {
        memset(buf, expectedByte(i), PAGE_SIZE);
        pwrite(fd, buf, PAGE_SIZE, static_cast<off_t>(i * PAGE_SIZE));
    }
    close(fd);
}

// Test 1: touch pages in random order; data must always be correct.
static void testRandomAccess() {
    printf("Test 1: random access\n");
    srand(42);
    bool allOk = true;
    for (int n = 0; n < 500; n++) {
        size_t id  = rand() % NUM_PAGES;
        size_t off = rand() % PAGE_SIZE;
        char* p = static_cast<char*>(pager::getPage(id));
        if (p[off] != expectedByte(id)) allOk = false;
    }
    check(allOk, "500 random reads all returned correct data");
}

// Test 2: evict and re-touch the same page over and over.
static void testEvictRefault() {
    printf("Test 2: repeated evict and refault\n");
    bool allOk = true;
    char* p = static_cast<char*>(pager::getPage(3));
    for (int n = 0; n < 1000; n++) {
        if (p[0] != expectedByte(3) || p[PAGE_SIZE - 1] != expectedByte(3)) {
            allOk = false;
        }
        pager::evictPage(3, /*isDirty=*/false);
    }
    check(allOk, "1000 evict/refault cycles all returned correct data");
}

// Test 3: modified pages must reach disk when evicted as dirty.
static void testDirtyWriteBack() {
    printf("Test 3: dirty page write-back\n");
    char* p = static_cast<char*>(pager::getPage(5));
    p[0] = 'Z';
    p[PAGE_SIZE - 1] = 'Y';
    pager::evictPage(5, /*isDirty=*/true);

    // Read the file directly, bypassing the pager.
    int fd = open(DISK_PATH, O_RDONLY);
    char buf[PAGE_SIZE];
    pread(fd, buf, PAGE_SIZE, static_cast<off_t>(5 * PAGE_SIZE));
    close(fd);
    check(buf[0] == 'Z' && buf[PAGE_SIZE - 1] == 'Y',
          "modified bytes reached the disk file");
    check(buf[100] == expectedByte(5),
          "untouched bytes on that page are unchanged");

    // Touching the page again should reload the modified version.
    check(p[0] == 'Z' && p[100] == expectedByte(5),
          "refault reloads the modified data");

    // Evicting as clean must discard unsaved changes.
    p[1] = 'X';
    pager::evictPage(5, /*isDirty=*/false);
    check(p[1] == expectedByte(5),
          "evicting as clean discards the unsaved change");
}

int main() {
    prepareDisk();
    pager::init(NUM_PAGES, DISK_PATH);

    testRandomAccess();
    testEvictRefault();
    testDirtyWriteBack();

    pager::shutdown();
    if (g_failures == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    }
    printf("\n%d check(s) FAILED.\n", g_failures);
    return 1;
}
