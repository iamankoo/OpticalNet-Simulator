#include "opticalnet/concurrency/ThreadPool.hpp"

namespace opticalnet {

ThreadPool::ThreadPool(std::size_t workerCount) {
    if (workerCount == 0) throw std::invalid_argument("ThreadPool needs at least one worker");
    workers_.reserve(workerCount);
    try {
        for (std::size_t i = 0; i < workerCount; ++i) workers_.emplace_back([this] { workerLoop(); });
    } catch (...) {
        // Could not start all threads: stop and join the ones that did start, then report the failure.
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() { shutdown(); }

std::size_t ThreadPool::pendingTasks() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void ThreadPool::enqueue(std::function<void()> job) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) throw std::logic_error("ThreadPool::submit after shutdown");
        queue_.push(std::move(job));
    }
    wake_.notify_one();
}

void ThreadPool::shutdown() {
    const std::lock_guard<std::mutex> serialise(shutdownMutex_);
    for (const std::thread& worker : workers_) {
        if (worker.get_id() == std::this_thread::get_id())
            throw std::logic_error("ThreadPool::shutdown called from one of its own workers");
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;  // stopping and nothing left: every queued task has been taken
            job = std::move(queue_.front());
            queue_.pop();
        }
        job();  // wraps a packaged_task: exceptions are stored in the future, never thrown here
    }
}

}  // namespace opticalnet
