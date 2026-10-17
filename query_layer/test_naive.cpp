#include "query_layer/naive_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

int main() {
    const char* path = "test_naive.db";
    unlink(path);
    {
        NaiveStore s(path, 64);
        std::string v;
        CHECK(!s.get(1, v));
        CHECK(s.insert(1, "hello"));
        CHECK(s.get(1, v) && v == "hello");
        CHECK(s.insert(1, "world"));
        CHECK(s.get(1, v) && v == "world");
        CHECK(s.remove(1));
        CHECK(!s.get(1, v));
        CHECK(!s.remove(1));
        CHECK(!s.insert(2, std::string(1001, 'x')));

        for (uint64_t k = 100; k < 2100; k++) CHECK(s.insert(k, "val" + std::to_string(k)));
        for (uint64_t k = 100; k < 2100; k++) CHECK(s.get(k, v) && v == "val" + std::to_string(k));
        size_t n = 0;
        s.scan([&](uint64_t, const std::string&) { n++; });
        CHECK(n == 2000);
    }

    {   // reopen: data must still be on disk
        NaiveStore s(path, 64);
        std::string v;
        CHECK(s.get(500, v) && v == "val500");
    }
    unlink(path);
    std::printf("ALL NAIVE TESTS PASSED\n");
    return 0;
}
