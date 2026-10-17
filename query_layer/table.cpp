#include "query_layer/table.hpp"
#include <cstring>

bool encodeRow(const Schema& schema, const Row& row, size_t maxBytes, std::string& out) {
    if (row.size() != schema.columns.size()) return false;
    out.clear();
    for (size_t c = 0; c < row.size(); c++) {
        if (row[c].type != schema.columns[c].type) return false;
        if (row[c].type == ColType::INT64) {
            int64_t v = row[c].i;
            out.append(reinterpret_cast<const char*>(&v), sizeof(v));
        } else {
            if (row[c].s.size() > 65535) return false;
            uint16_t len = static_cast<uint16_t>(row[c].s.size());
            out.append(reinterpret_cast<const char*>(&len), sizeof(len));
            out.append(row[c].s);
        }
        if (out.size() > maxBytes) return false;
    }
    return true;
}

bool decodeRow(const Schema& schema, const std::string& bytes, Row& out) {
    out.clear();
    size_t pos = 0;
    for (const Column& col : schema.columns) {
        if (col.type == ColType::INT64) {
            if (pos + sizeof(int64_t) > bytes.size()) return false;
            int64_t v;
            std::memcpy(&v, bytes.data() + pos, sizeof(v));
            pos += sizeof(v);
            out.push_back(Value::Int(v));
        } else {
            if (pos + sizeof(uint16_t) > bytes.size()) return false;
            uint16_t len;
            std::memcpy(&len, bytes.data() + pos, sizeof(len));
            pos += sizeof(len);
            if (pos + len > bytes.size()) return false;
            out.push_back(Value::Text(bytes.substr(pos, len)));
            pos += len;
        }
    }
    return pos == bytes.size();
}
