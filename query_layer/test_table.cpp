#include "query_layer/table.hpp"
#include "query_layer/naive_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

int main() {
    Schema sc{{{"id", ColType::INT64}, {"name", ColType::TEXT}, {"age", ColType::INT64}}};
    std::string bytes;
    Row out;

    Row r = {Value::Int(-7), Value::Text("Ayushi"), Value::Int(21)};
    CHECK(encodeRow(sc, r, 1000, bytes));
    CHECK(decodeRow(sc, bytes, out));
    CHECK(out.size() == 3 && out[0].i == -7 && out[1].s == "Ayushi" && out[2].i == 21);

    Row bad = {Value::Text("x"), Value::Text("y"), Value::Int(1)};
    CHECK(!encodeRow(sc, bad, 1000, bytes));
    Row big = {Value::Int(1), Value::Text(std::string(2000, 'z')), Value::Int(2)};
    CHECK(!encodeRow(sc, big, 1000, bytes));
    CHECK(encodeRow(sc, r, 1000, bytes));
    CHECK(!decodeRow(sc, bytes.substr(0, bytes.size() - 3), out));

    unlink("test_table.db");
    {
        NaiveStore eng("test_table.db", 64);
        Table<NaiveStore> t(eng, sc);
        Row g;
        CHECK(!t.get(1, g));
        CHECK(t.insert(1, r));
        CHECK(t.get(1, g) && g[1].s == "Ayushi" && g[0].i == -7);
        Row r2 = {Value::Int(5), Value::Text("Sanju"), Value::Int(22)};
        CHECK(t.insert(1, r2));
        CHECK(t.get(1, g) && g[1].s == "Sanju");
        CHECK(!t.insert(2, bad));
        CHECK(!t.insert(3, big));
        for (uint64_t k = 10; k < 510; k++)
            CHECK(t.insert(k, {Value::Int((int64_t)k), Value::Text("n" + std::to_string(k)), Value::Int(1)}));
        size_t n = 0;
        t.scan([&](uint64_t, const Row&) { n++; });
        CHECK(n == 501);
        CHECK(t.remove(1));
        CHECK(!t.get(1, g));
    }
    unlink("test_table.db");
    std::printf("ALL TABLE TESTS PASSED\n");
    return 0;
}
