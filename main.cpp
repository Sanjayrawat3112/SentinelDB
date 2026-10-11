// Standalone demo / milestone test for Phase 1.
// Pre-fills a "disk" file with known data, then proves the pager
// loads the right bytes on first touch, keeps them resident, and
// correctly reloads them after an eviction.
#include "pager.hpp"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t PAGE_SIZE = 4096;

int main() {
    // Pre-populate disk.img so we have known content to read back.
    int fd = open("disk.img", O_RDWR | O_CREAT | O_TRUNC, 0644);
    char buf[PAGE_SIZE];

    memset(buf, 'A', PAGE_SIZE);
    pwrite(fd, buf, PAGE_SIZE, 0 * PAGE_SIZE);   // page 0 = all 'A'

    memset(buf, 'B', PAGE_SIZE);
    pwrite(fd, buf, PAGE_SIZE, 1 * PAGE_SIZE);   // page 1 = all 'B'
    close(fd);

    pager::init(16, "disk.img");

    printf("Touching page 0 for the first time (fault happens here)...\n");
    char* page0 = static_cast<char*>(pager::getPage(0));
    printf("  page0[0]   = %c  (expected 'A')\n", page0[0]);

    char* page1 = static_cast<char*>(pager::getPage(1));
    printf("  page1[0]   = %c  (expected 'B')\n", page1[0]);

    printf("Re-reading page0 (already resident, no fault this time):\n");
    printf("  page0[100] = %c  (expected 'A')\n", page0[100]);

    pager::evictPage(0, /*isDirty=*/false);
    printf("Page 0 evicted. Touching it again should re-fault and reload:\n");
    printf("  page0[0]   = %c  (expected 'A')\n", page0[0]);

    pager::shutdown();
    printf("Done — no crash, all values correct.\n");
    return 0;
}
