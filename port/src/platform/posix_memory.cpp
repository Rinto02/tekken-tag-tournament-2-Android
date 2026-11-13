#include "platform.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

namespace ttt2::platform {

size_t HostPageSize() {
    static const size_t ps = size_t(sysconf(_SC_PAGESIZE));
    return ps;
}

bool GuestMemoryReserve(GuestMemory& mem, size_t size) {
    // Zero-filled, lazily backed, never charged against overcommit up front.
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return false;
    mem.base = static_cast<uint8_t*>(p);
    mem.size = size;
    return true;
}

void GuestMemoryRelease(GuestMemory& mem) {
    if (mem.base) munmap(mem.base, mem.size);
    mem.base = nullptr;
    mem.size = 0;
}

// ---------------------------------------------------------------------------------------
// Write watch. Per host page: `armed` (page is currently read-only and being watched) and
// `dirty` (written since the last collect). The fault hook runs in signal context, so it
// only touches atomics and calls mprotect (async-signal-safe).
namespace {

struct Watch {
    uint8_t* base = nullptr;
    size_t size = 0;
    size_t pageSize = 0;
    size_t pages = 0;
    std::unique_ptr<std::atomic<uint8_t>[]> armed;
    std::unique_ptr<std::atomic<uint8_t>[]> dirty;
};

Watch g_watch;

bool WatchFault(void* addr) {
    Watch& w = g_watch;
    auto a = reinterpret_cast<uint8_t*>(addr);
    if (!w.base || a < w.base || a >= w.base + w.size) return false;
    size_t page = size_t(a - w.base) / w.pageSize;
    // The whole region is mapped read/write, so any fault inside it is a write to a page we
    // protected. If another thread already unprotected it, retrying is the right answer.
    if (w.armed[page].exchange(0, std::memory_order_acq_rel)) {
        w.dirty[page].store(1, std::memory_order_release);
        mprotect(w.base + page * w.pageSize, w.pageSize, PROT_READ | PROT_WRITE);
    }
    return true;
}

void Range(const Watch& w, size_t offset, size_t len, size_t& first, size_t& last) {
    if (offset > w.size) offset = w.size;
    if (len > w.size - offset) len = w.size - offset;
    first = offset / w.pageSize;
    last = len ? (offset + len - 1) / w.pageSize + 1 : first;   // exclusive
}

}  // namespace

bool WriteWatchInstall(GuestMemory& mem) {
    Watch& w = g_watch;
    w.base = mem.base;
    w.size = mem.size;
    w.pageSize = HostPageSize();
    w.pages = (mem.size + w.pageSize - 1) / w.pageSize;
    w.armed.reset(new std::atomic<uint8_t>[w.pages]());
    w.dirty.reset(new std::atomic<uint8_t>[w.pages]());
    return RegisterFaultHook(WatchFault);
}

void WriteWatchReset(size_t offset, size_t len) {
    Watch& w = g_watch;
    size_t first, last;
    Range(w, offset, len, first, last);
    for (size_t p = first; p < last; ++p) {
        w.dirty[p].store(0, std::memory_order_relaxed);
        w.armed[p].store(1, std::memory_order_release);   // arm before protecting
    }
    if (last > first)
        mprotect(w.base + first * w.pageSize, (last - first) * w.pageSize, PROT_READ);
}

size_t WriteWatchGet(size_t offset, size_t len, bool reset, size_t* out, size_t cap) {
    Watch& w = g_watch;
    size_t first, last, n = 0;
    Range(w, offset, len, first, last);
    for (size_t p = first; p < last && n < cap; ++p) {
        if (!w.dirty[p].load(std::memory_order_acquire)) continue;
        out[n++] = p * w.pageSize;
        if (reset) {
            w.dirty[p].store(0, std::memory_order_relaxed);
            w.armed[p].store(1, std::memory_order_release);
            mprotect(w.base + p * w.pageSize, w.pageSize, PROT_READ);
        }
    }
    return n;
}

}  // namespace ttt2::platform
