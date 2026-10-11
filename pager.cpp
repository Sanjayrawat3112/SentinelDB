#include "pager.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {

constexpr size_t PAGE_SIZE = 4096;

void*  g_base      = nullptr;   // start of our reserved region
size_t g_numPages  = 0;
int    g_backingFd = -1;        // "disk" file

struct sigaction g_oldAction;   // previous SIGSEGV handler, to chain to

bool isOurAddress(void* addr) {
    auto a    = reinterpret_cast<uintptr_t>(addr);
    auto base = reinterpret_cast<uintptr_t>(g_base);
    return a >= base && a < base + g_numPages * PAGE_SIZE;
}

// The actual fault handler.
// IMPORTANT: only async-signal-safe functions are allowed in here —
// no malloc/new, no printf, no std::string. mprotect() and pread()
// are both on the async-signal-safe list, which is why we use them.
void segvHandler(int /*sig*/, siginfo_t* info, void* /*ucontext*/) {
    void* faultAddr = info->si_addr;

    if (!isOurAddress(faultAddr)) {
        // Not one of our pages — this is a genuine bug elsewhere in the
        // program. Restore the real default handler and let it crash
        // normally instead of silently swallowing a real segfault.
        sigaction(SIGSEGV, &g_oldAction, nullptr);
        return;
    }

    uintptr_t offset = reinterpret_cast<uintptr_t>(faultAddr)
                      - reinterpret_cast<uintptr_t>(g_base);
    size_t pageId   = offset / PAGE_SIZE;
    void*  pageAddr = static_cast<char*>(g_base) + pageId * PAGE_SIZE;

    // Temporarily unlock so we can write the loaded bytes into it.
    mprotect(pageAddr, PAGE_SIZE, PROT_READ | PROT_WRITE);

    // pread (unlike fread/fopen) is async-signal-safe.
    pread(g_backingFd, pageAddr, PAGE_SIZE, static_cast<off_t>(pageId * PAGE_SIZE));

    // Return normally: the CPU re-executes the instruction that faulted,
    // and this time it succeeds because the page is now resident.
}

} // namespace

namespace pager {

void init(size_t numPages, const char* backingFilePath) {
    g_numPages = numPages;

    g_base = mmap(nullptr, numPages * PAGE_SIZE,
                  PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_base == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    g_backingFd = open(backingFilePath, O_RDWR | O_CREAT, 0644);
    if (g_backingFd < 0) {
        perror("open backing file");
        exit(1);
    }

    struct sigaction sa {};
    sa.sa_sigaction = segvHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_oldAction);
}

void* getPage(size_t pageId) {
    // TODO (Phase 2): bounds-check pageId against g_numPages.
    return static_cast<char*>(g_base) + pageId * PAGE_SIZE;
}

void evictPage(size_t pageId, bool isDirty) {
    void* pageAddr = static_cast<char*>(g_base) + pageId * PAGE_SIZE;

    if (isDirty) {
        pwrite(g_backingFd, pageAddr, PAGE_SIZE, static_cast<off_t>(pageId * PAGE_SIZE));
    }

    mprotect(pageAddr, PAGE_SIZE, PROT_NONE);
}

void shutdown() {
    munmap(g_base, g_numPages * PAGE_SIZE);
    close(g_backingFd);
}

} // namespace pager
