// Platform layer for the Android (and generic POSIX) build.
//
// The Windows runtime calls Win32 directly (VirtualAlloc + MEM_WRITE_WATCH, SEH/VEH,
// QueryPerformanceCounter, kernel event objects). This header is the replacement surface:
// each piece here mirrors the Win32 behaviour the runtime relies on, implemented on POSIX
// so it builds with the Android NDK and also on a plain Linux host (where CI tests it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ttt2::platform {

// ---------------------------------------------------------------------------------------
// Guest memory: one big zero-filled, read/write reservation (Win32: VirtualAlloc with
// MEM_RESERVE | MEM_COMMIT). Backed lazily by the kernel (MAP_NORESERVE), so a 4 GiB guest
// space costs only the pages the game actually touches.
struct GuestMemory {
    uint8_t* base = nullptr;
    size_t   size = 0;
};

bool GuestMemoryReserve(GuestMemory& mem, size_t size);
void GuestMemoryRelease(GuestMemory& mem);
size_t HostPageSize();

// ---------------------------------------------------------------------------------------
// Write watch (Win32: MEM_WRITE_WATCH, ResetWriteWatch, GetWriteWatch). The GPU backend asks
// which guest pages the CPU wrote since it last uploaded a texture. Implemented with
// mprotect + a SIGSEGV hook: a watched page is made read-only, the first write faults, the
// hook records the page as dirty and makes it writable again.
//
// One watched region per process (the guest space), which is all the runtime needs.
bool WriteWatchInstall(GuestMemory& mem);
// Start watching [offset, offset+len): clears the dirty flags, makes the pages read-only.
void WriteWatchReset(size_t offset, size_t len);
// Collect dirty pages in [offset, offset+len) as byte offsets (page aligned) into `out`.
// Returns the number written (at most `cap`). When `reset` is true the returned pages are
// re-armed. Pages that did not fit stay dirty for the next call.
size_t WriteWatchGet(size_t offset, size_t len, bool reset, size_t* out, size_t cap);

// ---------------------------------------------------------------------------------------
// Fault hooks and crash reporting (Win32: AddVectoredExceptionHandler,
// SetUnhandledExceptionFilter). A hook sees the faulting address of a SIGSEGV/SIGBUS and
// returns true if it handled it (the faulting instruction is then retried).
using FaultHook = bool (*)(void* faultAddress);
bool RegisterFaultHook(FaultHook hook);
// Install SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT handlers on an alternate stack. Anything no
// hook handles is written to `log` (signal, fault address, pc) and the process dies with the
// default action, so tombstones and core dumps still happen.
void InstallCrashHandler(std::FILE* log);

// ---------------------------------------------------------------------------------------
// Clock (Win32: QueryPerformanceCounter / QueryPerformanceFrequency / GetTickCount64).
uint64_t TickFrequency();      // ticks per second (1e9: nanoseconds)
uint64_t Ticks();              // monotonic
uint64_t TickCount64Ms();      // monotonic milliseconds

// ---------------------------------------------------------------------------------------
// Kernel-style event (Win32: CreateEvent / SetEvent / ResetEvent / WaitForSingleObject).
class Event {
public:
    Event(bool manualReset, bool initiallySignaled);
    ~Event();
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    void Set();
    void Reset();
    // timeoutMs < 0 waits forever. Returns true if signaled, false on timeout. An
    // auto-reset event is consumed by exactly one successful wait.
    bool Wait(int64_t timeoutMs);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace ttt2::platform
