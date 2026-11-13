#include "platform.h"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <mutex>

namespace ttt2::platform {

uint64_t TickFrequency() { return 1000000000ull; }

uint64_t Ticks() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

uint64_t TickCount64Ms() { return Ticks() / 1000000ull; }

struct Event::Impl {
    std::mutex m;
    std::condition_variable cv;
    bool manual;
    bool signaled;
};

Event::Event(bool manualReset, bool initiallySignaled) : impl_(new Impl) {
    impl_->manual = manualReset;
    impl_->signaled = initiallySignaled;
}

Event::~Event() { delete impl_; }

void Event::Set() {
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->signaled = true;
    }
    // Manual-reset releases every waiter; auto-reset wakes them all to race for the one
    // signal, and exactly one of them consumes it.
    impl_->cv.notify_all();
}

void Event::Reset() {
    std::lock_guard<std::mutex> lock(impl_->m);
    impl_->signaled = false;
}

bool Event::Wait(int64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(impl_->m);
    auto ready = [this] { return impl_->signaled; };
    if (timeoutMs < 0) {
        impl_->cv.wait(lock, ready);
    } else if (!impl_->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)) {
        return false;
    }
    if (!impl_->manual) impl_->signaled = false;
    return true;
}

}  // namespace ttt2::platform
