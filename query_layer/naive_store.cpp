#include "query_layer/naive_store.hpp"
#include "storage_engine/page_format.hpp"
#include "storage_engine/page_ops.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void readPage(int fd, size_t id, void* buf) {
    ssize_t n = pread(fd, buf, PAGE_SIZE, static_cast<off_t>(id * PAGE_SIZE));
    if (n != static_cast<ssize_t>(PAGE_SIZE)) throw std::runtime_error("NaiveStore: pread failed");
}
void writePage(int fd, size_t id, const void* buf) {
    ssize_t n = pwrite(fd, buf, PAGE_SIZE, static_cast<off_t>(id * PAGE_SIZE));
    if (n != static_cast<ssize_t>(PAGE_SIZE)) throw std::runtime_error("NaiveStore: pwrite failed");
}
size_t homePageOf(uint64_t key, size_t numPages) {   // identical to KVStore::homePage
    uint64_t x = key;
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<size_t>(x % numPages);
}
} // namespace

NaiveStore::NaiveStore(const char* filePath, size_t numPages) : fd_(-1), numPages_(numPages) {
    fd_ = open(filePath, O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) throw std::runtime_error("NaiveStore: open failed");
    struct stat st;
    if (fstat(fd_, &st) != 0) throw std::runtime_error("NaiveStore: fstat failed");
    size_t want = numPages_ * PAGE_SIZE;
    if (static_cast<size_t>(st.st_size) < want && ftruncate(fd_, static_cast<off_t>(want)) != 0)
        throw std::runtime_error("NaiveStore: ftruncate failed");   // new bytes are zero = PAGE_FREE
}

NaiveStore::~NaiveStore() { if (fd_ >= 0) close(fd_); }

bool NaiveStore::insert(uint64_t key, const std::string& value) {
    if (value.size() > NAIVE_MAX_VALUE_SIZE) return false;
    alignas(8) char buf[PAGE_SIZE];
    std::string oldValue;
    size_t at = numPages_;
    {   // find
        size_t home = homePageOf(key, numPages_);
        for (size_t i = 0; i < numPages_; i++) {
            size_t id = (home + i) % numPages_;
            readPage(fd_, id, buf);
            if (pageGet(buf, key, oldValue)) { at = id; break; }
            if (!pageIsOverflowed(buf)) break;
        }
    }
    if (at != numPages_) {   // overwrite: remove old copy first
        readPage(fd_, at, buf);
        pageRemove(buf, key);
        writePage(fd_, at, buf);
    }
    size_t home = homePageOf(key, numPages_);
    for (size_t i = 0; i < numPages_; i++) {   // place
        size_t id = (home + i) % numPages_;
        readPage(fd_, id, buf);
        if (static_cast<PageHeader*>(static_cast<void*>(buf))->pageType == PAGE_FREE) pageInit(buf);
        if (pageInsert(buf, key, value)) { writePage(fd_, id, buf); return true; }
        pageSetOverflowed(buf);
        writePage(fd_, id, buf);
    }
    if (at != numPages_) {   // no room: put the old value back
        readPage(fd_, at, buf);
        pageInsert(buf, key, oldValue);
        writePage(fd_, at, buf);
    }
    return false;
}

bool NaiveStore::get(uint64_t key, std::string& outValue) {
    alignas(8) char buf[PAGE_SIZE];
    size_t home = homePageOf(key, numPages_);
    for (size_t i = 0; i < numPages_; i++) {
        size_t id = (home + i) % numPages_;
        readPage(fd_, id, buf);
        if (pageGet(buf, key, outValue)) return true;
        if (!pageIsOverflowed(buf)) break;
    }
    return false;
}

bool NaiveStore::remove(uint64_t key) {
    alignas(8) char buf[PAGE_SIZE];
    std::string tmp;
    size_t home = homePageOf(key, numPages_);
    for (size_t i = 0; i < numPages_; i++) {
        size_t id = (home + i) % numPages_;
        readPage(fd_, id, buf);
        if (pageGet(buf, key, tmp)) {
            pageRemove(buf, key);
            writePage(fd_, id, buf);
            return true;
        }
        if (!pageIsOverflowed(buf)) break;
    }
    return false;
}

void NaiveStore::scan(const std::function<void(uint64_t, const std::string&)>& callback) {
    alignas(8) char buf[PAGE_SIZE];
    for (size_t id = 0; id < numPages_; id++) {
        std::vector<std::pair<uint64_t, std::string>> records;
        readPage(fd_, id, buf);
        pageCollect(buf, records);
        for (const auto& r : records) callback(r.first, r.second);
    }
}
