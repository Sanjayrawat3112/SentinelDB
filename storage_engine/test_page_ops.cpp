#include "storage_engine/page_ops.hpp"
#include "storage_engine/page_format.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  [PASS] %s\n", msg); \
    else { std::printf("  [FAIL] %s\n", msg); failures++; } } while (0)

alignas(8) static char buf[PAGE_SIZE];

int main() {
    std::string out;

    std::printf("Insert and get\n");
    pageInit(buf);
    CHECK(pageInsert(buf, 1, "hello"), "insert key 1");
    CHECK(pageInsert(buf, 2, "world"), "insert key 2");
    CHECK(pageGet(buf, 1, out) && out == "hello", "get key 1 returns hello");
    CHECK(pageGet(buf, 2, out) && out == "world", "get key 2 returns world");
    CHECK(!pageGet(buf, 99, out), "get missing key returns false");

    std::printf("Remove\n");
    CHECK(pageRemove(buf, 1), "remove key 1");
    CHECK(!pageGet(buf, 1, out), "key 1 is gone");
    CHECK(pageGet(buf, 2, out) && out == "world", "key 2 untouched");
    CHECK(!pageRemove(buf, 1), "removing key 1 again returns false");

    std::printf("Deleted slot is reused\n");
    PageHeader* h = reinterpret_cast<PageHeader*>(buf);
    uint16_t slotsBefore = h->slotCount;
    CHECK(pageInsert(buf, 3, "again"), "insert key 3");
    CHECK(h->slotCount == slotsBefore, "slot count did not grow");
    CHECK(pageGet(buf, 3, out) && out == "again", "key 3 readable");

    std::printf("Page fills up\n");
    pageInit(buf);
    std::string big(500, 'x');
    int n = 0;
    while (pageInsert(buf, n, big)) n++;
    CHECK(n >= 7 && n <= 8, "about 8 records of 500 bytes fit in 4096");
    CHECK(!pageInsert(buf, 1000, big), "insert fails when the page is full");

    std::printf("Compaction reclaims holes\n");
    CHECK(pageRemove(buf, 0) && pageRemove(buf, 2), "remove two records from the middle");
    CHECK(pageInsert(buf, 2000, big), "insert works again after deletes");
    bool allOk = true;
    for (int k = 0; k < n; k++) {
        if (k == 0 || k == 2) continue;
        if (!(pageGet(buf, k, out) && out == big)) allOk = false;
    }
    CHECK(allOk, "all surviving records still intact");

    std::printf("Empty value\n");
    pageInit(buf);
    CHECK(pageInsert(buf, 5, "") && pageGet(buf, 5, out) && out.empty(), "empty value works");

    if (failures == 0) { std::printf("\nAll page_ops tests passed.\n"); return 0; }
    std::printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}