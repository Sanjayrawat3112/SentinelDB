#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class BufferPool;

constexpr size_t MAX_VALUE_SIZE = 1000;   // bytes; a record must fit inside a 4096-byte page

// Hash-based key-value store. Page = bucket, linear probing on overflow.
// numPages must equal the totalPages given to the BufferPool.
// Single-threaded.
class KVStore {
public:
    KVStore(BufferPool& pool, size_t numPages);

    // Insert a new key, or overwrite the value if the key already exists.
    // Returns false if value.size() > MAX_VALUE_SIZE or there is no space left
    // (on failure an existing value is left unchanged).
    bool insert(uint64_t key, const std::string& value);

    // Copies the value into outValue. Returns false if the key is not found.
    bool get(uint64_t key, std::string& outValue);

    // Deletes the key. Returns false if the key was not found.
    bool remove(uint64_t key);

    // Calls callback once for every stored record (order is not guaranteed).
    // The callback must not modify the store.
    void scan(const std::function<void(uint64_t, const std::string&)>& callback);

private:
    // Returns the page where 'key' lives, or numPages_ if absent. Fills outValue.
    size_t find(uint64_t key, std::string& outValue);
    bool place(uint64_t key, const std::string& value);
    size_t homePage(uint64_t key) const;

    BufferPool& pool_;
    size_t numPages_;
};