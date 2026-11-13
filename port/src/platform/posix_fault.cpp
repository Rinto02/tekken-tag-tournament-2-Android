#include "platform.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

namespace ttt2::platform {

namespace {

constexpr int kMaxHooks = 8;
std::atomic<FaultHook> g_hooks[kMaxHooks];
std::atomic<int> g_hookCount{0};
std::FILE* g_log = nullptr;
struct sigaction g_old[NSIG];
bool g_installed = false;

uintptr_t FaultPc(void* ucontext) {
    auto* uc = static_cast<ucontext_t*>(ucontext);
#if defined(__aarch64__)
    return uintptr_t(uc->uc_mcontext.pc);
#elif defined(__x86_64__) && defined(REG_RIP)
    return uintptr_t(uc->uc_mcontext.gregs[REG_RIP]);
#else
    (void)uc;
    return 0;
#endif
}

void Dispatch(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGSEGV || sig == SIGBUS) {
        int n = g_hookCount.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i) {
            FaultHook h = g_hooks[i].load(std::memory_order_acquire);
            if (h && h(info->si_addr)) return;   // handled: the instruction is retried
        }
    }
    if (g_log) {
        // fprintf is not async-signal-safe, but this path ends the process anyway.
        std::fprintf(g_log, "[fatal] signal %d at pc %p, fault address %p\n", sig,
                     reinterpret_cast<void*>(FaultPc(ucontext)), info->si_addr);
        std::fflush(g_log);
    }
    // Restore the previous (default, or Android's debuggerd) action and re-raise so the
    // system still produces its tombstone / core dump.
    sigaction(sig, &g_old[sig], nullptr);
    raise(sig);
}

}  // namespace

bool RegisterFaultHook(FaultHook hook) {
    int i = g_hookCount.load();
    if (i >= kMaxHooks) return false;
    g_hooks[i].store(hook, std::memory_order_release);
    g_hookCount.store(i + 1, std::memory_order_release);
    return true;
}

void InstallCrashHandler(std::FILE* log) {
    g_log = log;
    if (g_installed) return;
    g_installed = true;

    // Alternate stack: a stack overflow must still be able to report itself.
    static char* altStack = static_cast<char*>(std::malloc(SIGSTKSZ * 4));
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = SIGSTKSZ * 4;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = Dispatch;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        sigaction(sig, &sa, &g_old[sig]);
}

}  // namespace ttt2::platform
