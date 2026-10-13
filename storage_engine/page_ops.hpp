#pragma once
#include <cstdint>
#include <string>

// Operations on ONE 4096-byte page held in a plain memory buffer.
// These functions know nothing about the Buffer Pool or the disk.

// Formats the buffer as an empty data page.
void pageInit(void* page);

// Adds a record. Returns false if the page has no room for it.
// The caller must make sure the key is not already in this page.
bool pageInsert(void* page, uint64_t key, const std::string& value);

// Copies the value of 'key' into outValue. Returns false if not found.
bool pageGet(const void* page, uint64_t key, std::string& outValue);

// Marks the record deleted. Returns false if the key is not in this page.
bool pageRemove(void* page, uint64_t key);

// Bytes available for a new record (after compaction), including its slot.
size_t pageFreeSpace(const void* page);


// "An insert skipped past this page." Used by the hash store's probing.
// The flag is never cleared by pageRemove: it works like a tombstone.
void pageSetOverflowed(void* page);
bool pageIsOverflowed(const void* page);