#pragma once
#include <cstddef>
#include <cstdint>

// Same value as the private PAGE_SIZE in pager.cpp. Do not change.
constexpr size_t PAGE_SIZE = 4096;

enum PageType : uint16_t {
    PAGE_FREE = 0,   // never used
    PAGE_DATA = 1    // holds records
};

// Sits at byte 0 of every page.
struct PageHeader {
    uint16_t pageType;
    uint16_t slotCount;      // number of slot entries (deleted ones included)
    uint16_t freeSpacePtr;   // offset where the record area starts; grows downward
};

// One entry per record, stored right after the header.
struct Slot {
    uint16_t offset;   // where the record starts in the page; 0 means deleted/empty
    uint16_t length;   // total record bytes
};

// Record layout at 'offset': [uint64_t key][uint16_t valueLen][value bytes]
constexpr size_t RECORD_HEADER_SIZE = sizeof(uint64_t) + sizeof(uint16_t);

static_assert(sizeof(PageHeader) == 6, "PageHeader must be 6 bytes");
static_assert(sizeof(Slot) == 4, "Slot must be 4 bytes");