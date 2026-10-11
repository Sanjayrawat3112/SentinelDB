#include "storage_engine/page_ops.hpp"
#include "storage_engine/page_format.hpp"
#include <cstring>
#include <vector>

namespace {

PageHeader* hdr(void* p) { return static_cast<PageHeader*>(p); }
const PageHeader* hdr(const void* p) { return static_cast<const PageHeader*>(p); }
Slot* slots(void* p) { return reinterpret_cast<Slot*>(static_cast<char*>(p) + sizeof(PageHeader)); }
const Slot* slots(const void* p) { return reinterpret_cast<const Slot*>(static_cast<const char*>(p) + sizeof(PageHeader)); }

size_t slotDirEnd(const void* p) { return sizeof(PageHeader) + hdr(p)->slotCount * sizeof(Slot); }

uint64_t recKey(const void* p, const Slot& s) {
    uint64_t k; std::memcpy(&k, static_cast<const char*>(p) + s.offset, sizeof k); return k;
}
uint16_t recValLen(const void* p, const Slot& s) {
    uint16_t n; std::memcpy(&n, static_cast<const char*>(p) + s.offset + sizeof(uint64_t), sizeof n); return n;
}

// Moves all live records to the end of the page so deleted holes disappear.
void compact(void* p) {
    std::vector<char> copy(static_cast<char*>(p), static_cast<char*>(p) + PAGE_SIZE);
    uint16_t ptr = PAGE_SIZE;
    Slot* sl = slots(p);
    for (uint16_t i = 0; i < hdr(p)->slotCount; i++) {
        if (sl[i].offset == 0) continue;
        ptr -= sl[i].length;
        std::memcpy(static_cast<char*>(p) + ptr, copy.data() + sl[i].offset, sl[i].length);
        sl[i].offset = ptr;
    }
    hdr(p)->freeSpacePtr = ptr;
}

size_t liveBytes(const void* p) {
    size_t total = 0;
    const Slot* sl = slots(p);
    for (uint16_t i = 0; i < hdr(p)->slotCount; i++)
        if (sl[i].offset != 0) total += sl[i].length;
    return total;
}

} // namespace

void pageInit(void* page) {
    std::memset(page, 0, PAGE_SIZE);
    hdr(page)->pageType = PAGE_DATA;
    hdr(page)->slotCount = 0;
    hdr(page)->freeSpacePtr = PAGE_SIZE;
}

size_t pageFreeSpace(const void* page) {
    // Space left if every hole were squeezed out.
    size_t used = slotDirEnd(page) + liveBytes(page);
    return used >= PAGE_SIZE ? 0 : PAGE_SIZE - used;
}

bool pageInsert(void* page, uint64_t key, const std::string& value) {
    size_t recLen = RECORD_HEADER_SIZE + value.size();

    // Reuse a deleted slot if there is one, otherwise a new slot is needed.
    Slot* sl = slots(page);
    int reuse = -1;
    for (uint16_t i = 0; i < hdr(page)->slotCount; i++)
        if (sl[i].offset == 0) { reuse = i; break; }
    size_t slotCost = (reuse >= 0) ? 0 : sizeof(Slot);

    if (pageFreeSpace(page) < recLen + slotCost) return false;

    // Enough space overall, but maybe not in one piece: compact if needed.
    size_t contiguous = hdr(page)->freeSpacePtr - slotDirEnd(page);
    if (contiguous < recLen + slotCost) compact(page);

    uint16_t newOffset = hdr(page)->freeSpacePtr - recLen;
    char* dst = static_cast<char*>(page) + newOffset;
    uint16_t vlen = static_cast<uint16_t>(value.size());
    std::memcpy(dst, &key, sizeof key);
    std::memcpy(dst + sizeof key, &vlen, sizeof vlen);
    std::memcpy(dst + RECORD_HEADER_SIZE, value.data(), value.size());

    if (reuse < 0) { reuse = hdr(page)->slotCount; hdr(page)->slotCount++; }
    sl = slots(page);
    sl[reuse].offset = newOffset;
    sl[reuse].length = static_cast<uint16_t>(recLen);
    hdr(page)->freeSpacePtr = newOffset;
    return true;
}

bool pageGet(const void* page, uint64_t key, std::string& outValue) {
    const Slot* sl = slots(page);
    for (uint16_t i = 0; i < hdr(page)->slotCount; i++) {
        if (sl[i].offset == 0) continue;
        if (recKey(page, sl[i]) == key) {
            const char* v = static_cast<const char*>(page) + sl[i].offset + RECORD_HEADER_SIZE;
            outValue.assign(v, recValLen(page, sl[i]));
            return true;
        }
    }
    return false;
}

bool pageRemove(void* page, uint64_t key) {
    Slot* sl = slots(page);
    for (uint16_t i = 0; i < hdr(page)->slotCount; i++) {
        if (sl[i].offset == 0) continue;
        if (recKey(page, sl[i]) == key) { sl[i].offset = 0; sl[i].length = 0; return true; }
    }
    return false;
}