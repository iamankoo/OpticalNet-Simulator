#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace opticalnet::testing {

// A rendezvous point for N threads. arriveAndWait() returns true only if all N threads have arrived, which can
// only happen if they are running AT THE SAME TIME. The timeout exists solely so a broken implementation makes
// the test fail instead of hang; correct behaviour never waits for it, and no result depends on timing.
class Rendezvous {
public:
    explicit Rendezvous(std::size_t parties) : parties_(parties) {}

    bool arriveAndWait(std::chrono::seconds timeout = std::chrono::seconds(30)) {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        cv_.notify_all();
        return cv_.wait_for(lock, timeout, [this] { return arrived_ >= parties_; });
    }

private:
    std::size_t parties_;
    std::size_t arrived_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace opticalnet::testing
