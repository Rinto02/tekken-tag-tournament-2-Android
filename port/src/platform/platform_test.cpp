// Host-side tests for the platform layer. Run on Linux in CI; the Android build only
// compiles this file (an NDK binary can't run on the CI host).
#include "platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace ttt2::platform;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static void TestMemory() {
    GuestMemory mem;
    CHECK(GuestMemoryReserve(mem, size_t(1) << 28));   // 256 MiB, lazily backed
    CHECK(mem.base != nullptr);
    CHECK(mem.base[0] == 0 && mem.base[mem.size - 1] == 0);   // zero-filled
    mem.base[12345] = 7;
    CHECK(mem.base[12345] == 7);
    GuestMemoryRelease(mem);
    CHECK(mem.base == nullptr);
}

static void TestWriteWatch() {
    GuestMemory mem;
    CHECK(GuestMemoryReserve(mem, size_t(64) << 20));
    InstallCrashHandler(stderr);
    CHECK(WriteWatchInstall(mem));
    const size_t ps = HostPageSize();

    WriteWatchReset(0, mem.size);
    size_t out[16];
    CHECK(WriteWatchGet(0, mem.size, false, out, 16) == 0);   // nothing written yet

    mem.base[ps * 3 + 10] = 1;       // page 3
    mem.base[ps * 3 + 20] = 2;       // same page: no second fault needed
    mem.base[ps * 9] = 1;            // page 9
    size_t n = WriteWatchGet(0, mem.size, false, out, 16);
    CHECK(n == 2);
    if (n == 2) { CHECK(out[0] == ps * 3); CHECK(out[1] == ps * 9); }
    CHECK(mem.base[ps * 3 + 10] == 1 && mem.base[ps * 3 + 20] == 2);   // data survived

    // Range query only sees pages inside the range.
    CHECK(WriteWatchGet(ps * 4, ps * 10, false, out, 16) == 1);

    // Collect with reset, then the same pages are clean until written again.
    CHECK(WriteWatchGet(0, mem.size, true, out, 16) == 2);
    CHECK(WriteWatchGet(0, mem.size, false, out, 16) == 0);
    mem.base[ps * 3] = 5;
    CHECK(WriteWatchGet(0, mem.size, false, out, 16) == 1);

    // Writes from another thread are tracked too.
    WriteWatchReset(0, mem.size);
    std::thread t([&] { mem.base[ps * 20] = 1; });
    t.join();
    n = WriteWatchGet(0, mem.size, false, out, 16);
    CHECK(n == 1 && out[0] == ps * 20);

    // Reading a watched page must not mark it dirty.
    WriteWatchReset(0, mem.size);
    volatile uint8_t sink = mem.base[ps * 30];
    (void)sink;
    CHECK(WriteWatchGet(0, mem.size, false, out, 16) == 0);
}

static void TestClockAndEvent() {
    CHECK(TickFrequency() == 1000000000ull);
    uint64_t a = Ticks();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint64_t b = Ticks();
    CHECK(b > a);
    CHECK((b - a) >= 4000000ull);   // at least ~4 ms in ns ticks

    Event autoEv(false, false);
    CHECK(!autoEv.Wait(10));        // times out, not signaled
    autoEv.Set();
    CHECK(autoEv.Wait(0));          // consumed
    CHECK(!autoEv.Wait(0));         // auto-reset: second wait finds it clear

    Event manualEv(true, true);
    CHECK(manualEv.Wait(0));
    CHECK(manualEv.Wait(0));        // manual-reset stays signaled
    manualEv.Reset();
    CHECK(!manualEv.Wait(0));

    Event wake(false, false);
    std::atomic<bool> woke{false};
    std::thread t([&] { woke = wake.Wait(-1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    wake.Set();
    t.join();
    CHECK(woke.load());
}

int main() {
    TestMemory();
    TestWriteWatch();
    TestClockAndEvent();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("platform tests passed");
    return 0;
}
