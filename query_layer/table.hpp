#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Column types supported (scope decided: INT64 and TEXT only).
enum class ColType { INT64, TEXT };

struct Column {
    std::string name;
    ColType type;
};

// A schema is an ordered list of columns. The primary key is NOT a column:
// it is the uint64_t key handed to the storage engine.
struct Schema {
    std::vector<Column> columns;
};

// One cell: either an integer or a text value, matching the column type.
struct Value {
    ColType type = ColType::INT64;
    int64_t i = 0;
    std::string s;

    static Value Int(int64_t v) { Value x; x.type = ColType::INT64; x.i = v; return x; }
    static Value Text(const std::string& v) { Value x; x.type = ColType::TEXT; x.s = v; return x; }
};

using Row = std::vector<Value>;

// Row <-> bytes. INT64 = 8 raw bytes; TEXT = 2-byte length + bytes.
// encodeRow returns false if the row does not match the schema or the
// encoded size exceeds maxBytes (storage engine value limit).
bool encodeRow(const Schema& schema, const Row& row, size_t maxBytes, std::string& out);
bool decodeRow(const Schema& schema, const std::string& bytes, Row& out);

// Works with any engine exposing insert/get/remove/scan like KVStore and
// NaiveStore (no inheritance needed).
template <class Engine>
class Table {
public:
    Table(Engine& engine, Schema schema, size_t maxBytes = 1000)
        : eng_(engine), schema_(std::move(schema)), maxBytes_(maxBytes) {}

    bool insert(uint64_t key, const Row& row) {
        std::string bytes;
        if (!encodeRow(schema_, row, maxBytes_, bytes)) return false;
        return eng_.insert(key, bytes);
    }
    bool get(uint64_t key, Row& out) {
        std::string bytes;
        if (!eng_.get(key, bytes)) return false;
        return decodeRow(schema_, bytes, out);
    }
    bool remove(uint64_t key) { return eng_.remove(key); }
    void scan(const std::function<void(uint64_t, const Row&)>& cb) {
        eng_.scan([&](uint64_t k, const std::string& bytes) {
            Row r;
            if (decodeRow(schema_, bytes, r)) cb(k, r);
        });
    }

private:
    Engine& eng_;
    Schema schema_;
    size_t maxBytes_;
};
