#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class BufferPool;

constexpr size_t MAX_VALUE_SIZE = 1000;   // bytes; a record must fit inside a 4096-byte page

class KVStore {
public:
    // Draft: the constructor may change later (WAL file path, etc.).
    // The four functions below are the stable API for the query layer.
    KVStore(BufferPool& pool, size_t numPages);

    // Insert a new key, or overwrite the value if the key already exists.
    // Returns false if value.size() > MAX_VALUE_SIZE or there is no space left.
    bool insert(uint64_t key, const std::string& value);

    // Copies the value into outValue. Returns false if the key is not found.
    bool get(uint64_t key, std::string& outValue);

    // Deletes the key. Returns false if the key was not found.
    bool remove(uint64_t key);

    // Calls callback once for every stored record (order is not guaranteed).
    void scan(const std::function<void(uint64_t, const std::string&)>& callback);
};