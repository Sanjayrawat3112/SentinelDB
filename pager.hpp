#pragma once
#include <cstddef>

// Phase 1 — Pager core.
// Exposes a region of memory whose pages are loaded from a backing
// "disk" file on first access, transparently, via SIGSEGV handling.
// The caller never checks "is this page loaded?" — touching the
// pointer returned by getPage() just works.
namespace pager {

    // Reserves `numPages` pages of memory (all initially inaccessible)
    // and installs the SIGSEGV handler that resolves faults against them.
    // backingFilePath is the file pages are read from / flushed to.
    void init(size_t numPages, const char* backingFilePath);

    // Returns a pointer to the given page within the reserved region.
    // No loading happens here — touching the memory is what triggers
    // the fault-driven load, the first time it's accessed.
    void* getPage(size_t pageId);

    // Called by the Buffer Pool Manager (Phase 2) once it has chosen to
    // evict a page. Flushes to disk first if dirty, then re-locks the
    // page's memory so the next access faults again.
    void evictPage(size_t pageId, bool isDirty);

    void shutdown();

} // namespace pager
