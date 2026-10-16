#include "storage_engine/wal.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace {

// Frame on disk: [uint32 payloadLen][uint32 crc32(payload)][payload]
// Payload: [type u8][lsn u64][txnId u64][key u64][flags u8]
//          [beforeLen u32][before bytes][afterLen u32][after bytes]
constexpr size_t FRAME_HEADER = 8;
constexpr uint32_t MAX_PAYLOAD = 16 * 1024;   // sanity limit; real records are ~2 KB at most

uint32_t crc32(const char* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= static_cast<uint8_t>(d[i]);
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return ~c;
}

template <class T> void put(std::string& s, T v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T> T take(const char*& p) { T v; std::memcpy(&v, p, sizeof v); p += sizeof v; return v; }

std::string encode(const LogRecord& r) {
    std::string payload;
    put<uint8_t>(payload, static_cast<uint8_t>(r.type));
    put<uint64_t>(payload, r.lsn);
    put<uint64_t>(payload, r.txnId);
    put<uint64_t>(payload, r.key);
    put<uint8_t>(payload, static_cast<uint8_t>((r.hasBefore ? 1 : 0) | (r.hasAfter ? 2 : 0)));
    put<uint32_t>(payload, static_cast<uint32_t>(r.before.size()));
    payload += r.before;
    put<uint32_t>(payload, static_cast<uint32_t>(r.after.size()));
    payload += r.after;

    std::string frame;
    put<uint32_t>(frame, static_cast<uint32_t>(payload.size()));
    put<uint32_t>(frame, crc32(payload.data(), payload.size()));
    return frame + payload;
}

// Parses records from 'bytes' until the data ends or a record is damaged.
// Returns how many bytes were valid.
size_t parse(const std::string& bytes, std::vector<LogRecord>* out) {
    size_t pos = 0;
    while (bytes.size() - pos >= FRAME_HEADER) {
        const char* p = bytes.data() + pos;
        uint32_t len = take<uint32_t>(p);
        uint32_t crc = take<uint32_t>(p);
        if (len < 1 + 8 + 8 + 8 + 1 + 4 + 4 || len > MAX_PAYLOAD) break;
        if (bytes.size() - pos - FRAME_HEADER < len) break;          // record cut short
        if (crc32(p, len) != crc) break;                              // damaged record

        const char* q = p;
        LogRecord r;
        r.type = static_cast<LogType>(take<uint8_t>(q));
        r.lsn = take<uint64_t>(q);
        r.txnId = take<uint64_t>(q);
        r.key = take<uint64_t>(q);
        uint8_t flags = take<uint8_t>(q);
        r.hasBefore = flags & 1;
        r.hasAfter = flags & 2;
        uint32_t bl = take<uint32_t>(q);
        if (static_cast<size_t>(p + len - q) < static_cast<size_t>(bl) + 4) break;
        r.before.assign(q, bl); q += bl;
        uint32_t al = take<uint32_t>(q);
        if (static_cast<size_t>(p + len - q) < al) break;
        r.after.assign(q, al);

        if (out) out->push_back(std::move(r));
        pos += FRAME_HEADER + len;
    }
    return pos;
}

std::string readWholeFile(int fd) {
    std::string data;
    char buf[65536];
    off_t off = 0;
    for (;;) {
        ssize_t n = ::pread(fd, buf, sizeof buf, off);
        if (n < 0) { if (errno == EINTR) continue; throw std::runtime_error("Wal: read failed"); }
        if (n == 0) break;
        data.append(buf, static_cast<size_t>(n));
        off += n;
    }
    return data;
}

} // namespace

Wal::Wal(const std::string& path) : fd_(-1), nextLsn_(1) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) throw std::runtime_error("Wal: cannot open " + path);

    std::string data = readWholeFile(fd_);
    std::vector<LogRecord> recs;
    size_t valid = parse(data, &recs);
    if (valid < data.size()) {                      // torn / damaged tail: cut it off
        if (::ftruncate(fd_, static_cast<off_t>(valid)) != 0) throw std::runtime_error("Wal: truncate failed");
        ::fsync(fd_);
    }
    if (!recs.empty()) nextLsn_ = recs.back().lsn + 1;
}

Wal::~Wal() { if (fd_ >= 0) ::close(fd_); }

uint64_t Wal::append(const LogRecord& rec) {
    LogRecord r = rec;
    r.lsn = nextLsn_;
    std::string frame = encode(r);
    const char* p = frame.data();
    size_t left = frame.size();
    while (left > 0) {
        ssize_t n = ::write(fd_, p, left);
        if (n < 0) { if (errno == EINTR) continue; throw std::runtime_error("Wal: write failed"); }
        p += n; left -= static_cast<size_t>(n);
    }
    return nextLsn_++;
}

void Wal::flush() {
    if (::fsync(fd_) != 0) throw std::runtime_error("Wal: fsync failed");
}

std::vector<LogRecord> Wal::readAll() const {
    std::vector<LogRecord> recs;
    parse(readWholeFile(fd_), &recs);
    return recs;
}


void Wal::truncate() {
    if (::ftruncate(fd_, 0) != 0) throw std::runtime_error("Wal: truncate failed");
    if (::fsync(fd_) != 0) throw std::runtime_error("Wal: fsync failed");
}