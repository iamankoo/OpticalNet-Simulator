#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace opticalnet {

// A fixed-size pool of worker threads executing submitted tasks from a FIFO queue.
//
//   submit(f)  -> std::future<R>    the task's return value, or its exception, travels through the future;
//                                   an exception never escapes into a worker thread and is never swallowed.
//   shutdown() / destructor         stop accepting new tasks, let the workers FINISH every task that was
//                                   already queued (nothing is dropped), then join every thread. Idempotent.
//
// Lifecycle guarantees: every thread is joined (none is detached, none outlives the pool), tasks are
// started in submission order, and a task may be submitted from any thread. Tasks must not call
// shutdown() on their own pool (that would make a worker join itself; it is detected and rejected).
//
// Misuse is a programming error and throws: std::invalid_argument for zero workers, std::logic_error for
// submit() after shutdown() and for shutdown() from inside a worker.
//
// Thread safety: all public members may be called concurrently. The pool knows nothing about what tasks do;
// callers are responsible for the data their tasks share.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <class F>
    [[nodiscard]] std::future<std::invoke_result_t<std::decay_t<F>>> submit(F&& task) {
        using R = std::invoke_result_t<std::decay_t<F>>;
        auto packaged = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
        std::future<R> future = packaged->get_future();
        enqueue([packaged] { (*packaged)(); });
        return future;
    }

    [[nodiscard]] std::size_t workerCount() const noexcept { return workers_.size(); }
    // Tasks queued but not yet started.
    [[nodiscard]] std::size_t pendingTasks() const;

    // Stop accepting tasks, run everything already queued, join all workers. Safe to call repeatedly.
    void shutdown();

private:
    void enqueue(std::function<void()> job);
    void workerLoop();

    std::vector<std::thread> workers_;  // fixed after construction
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mutex_;          // guards queue_ and stopping_
    std::condition_variable wake_;
    bool stopping_ = false;
    std::mutex shutdownMutex_;          // serialises concurrent shutdown() calls (a thread must not be joined twice)
};

}  // namespace opticalnet
