#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Baseline: same hash/linear-probing layout as KVStore, but plain pread/pwrite,
// no custom paging, no buffer pool. Signatures mirror KVStore on purpose.
constexpr size_t NAIVE_MAX_VALUE_SIZE = 1000;

class NaiveStore {
public:
    NaiveStore(const char* filePath, size_t numPages, bool syncWrites = false);
    ~NaiveStore();

    bool insert(uint64_t key, const std::string& value);
    bool get(uint64_t key, std::string& outValue);
    bool remove(uint64_t key);
    void scan(const std::function<void(uint64_t, const std::string&)>& callback);

private:
    int fd_;
    size_t numPages_;
    bool sync_;
};
