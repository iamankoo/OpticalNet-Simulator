#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ConcurrencyTestUtils.hpp"
#include "opticalnet/concurrency/ThreadPool.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(ThreadPool, ReportsTheRequestedWorkerCount) {
    for (std::size_t n : {1u, 2u, 5u}) EXPECT_EQ(ThreadPool(n).workerCount(), n);
}

TEST(ThreadPool, ZeroWorkersIsAProgrammingError) {
    EXPECT_THROW(ThreadPool(0), std::invalid_argument);
}

TEST(ThreadPool, StartsExactlyTheRequestedNumberOfWorkerThreads) {
    constexpr std::size_t kWorkers = 4;
    ThreadPool pool(kWorkers);
    Rendezvous rendezvous(kWorkers);
    std::mutex idsMutex;
    std::set<std::thread::id> ids;
    std::vector<std::future<bool>> futures;
    for (std::size_t i = 0; i < kWorkers; ++i) {
        futures.push_back(pool.submit([&] {
            {
                const std::lock_guard<std::mutex> lock(idsMutex);
                ids.insert(std::this_thread::get_id());
            }
            return rendezvous.arriveAndWait();
        }));
    }
    for (auto& f : futures) EXPECT_TRUE(f.get()) << "all workers must be running simultaneously";
    EXPECT_EQ(ids.size(), kWorkers) << "four distinct threads executed the tasks";
    EXPECT_EQ(ids.count(std::this_thread::get_id()), 0u) << "tasks do not run on the submitting thread";
}

TEST(ThreadPool, WorkersReallyRunTasksConcurrently) {
    constexpr std::size_t kWorkers = 6;
    ThreadPool pool(kWorkers);
    Rendezvous rendezvous(kWorkers);  // can only be satisfied if 6 tasks are in flight at once
    std::vector<std::future<bool>> futures;
    for (std::size_t i = 0; i < kWorkers; ++i) futures.push_back(pool.submit([&rendezvous] { return rendezvous.arriveAndWait(); }));
    for (auto& f : futures) EXPECT_TRUE(f.get());
}

TEST(ThreadPool, ExecutesEverySubmittedTask) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 2000; ++i) futures.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), 2000);
}

TEST(ThreadPool, ReturnsTaskResultsThroughFutures) {
    ThreadPool pool(3);
    auto number = pool.submit([] { return 6 * 7; });
    auto text = pool.submit([] { return std::string("done"); });
    auto nothing = pool.submit([] {});
    std::vector<std::future<int>> squares;
    for (int i = 0; i < 50; ++i) squares.push_back(pool.submit([i] { return i * i; }));
    EXPECT_EQ(number.get(), 42);
    EXPECT_EQ(text.get(), "done");
    EXPECT_NO_THROW(nothing.get());
    for (int i = 0; i < 50; ++i) EXPECT_EQ(squares[static_cast<std::size_t>(i)].get(), i * i) << "results match their own task, not completion order";
}

TEST(ThreadPool, MoreTasksThanWorkersNeverExceedsTheWorkerCount) {
    constexpr std::size_t kWorkers = 2;
    ThreadPool pool(kWorkers);
    std::atomic<int> running{0}, peak{0}, done{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 500; ++i) {
        futures.push_back(pool.submit([&] {
            const int now = running.fetch_add(1) + 1;
            int seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
            volatile int sink = 0;
            for (int k = 0; k < 1000; ++k) sink = sink + k;
            running.fetch_sub(1);
            done.fetch_add(1);
        }));
    }
    for (auto& f : futures) f.get();
    EXPECT_EQ(done.load(), 500);
    EXPECT_LE(peak.load(), static_cast<int>(kWorkers));
    EXPECT_GE(peak.load(), 1);
}

TEST(ThreadPool, TaskExceptionsPropagateThroughTheFutureAndTheNextTasksStillRun) {
    ThreadPool pool(2);
    auto bad = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    auto good = pool.submit([] { return 5; });
    EXPECT_THROW((void)bad.get(), std::runtime_error);
    EXPECT_EQ(good.get(), 5);
    EXPECT_EQ(pool.submit([] { return 9; }).get(), 9) << "the worker that ran the failing task is still alive";
}

TEST(ThreadPool, ExceptionMessageAndTypeArePreserved) {
    ThreadPool pool(1);
    auto f = pool.submit([]() -> int { throw std::out_of_range("index 7"); });
    try {
        (void)f.get();
        FAIL() << "expected an exception";
    } catch (const std::out_of_range& e) {
        EXPECT_STREQ(e.what(), "index 7");
    }
    auto g = pool.submit([]() -> int { throw 17; });
    EXPECT_THROW((void)g.get(), int);
}

TEST(ThreadPool, ShutdownRunsEveryAlreadyQueuedTask) {
    ThreadPool pool(1);
    std::promise<void> started, gate;
    std::atomic<int> ran{0};
    auto blocker = pool.submit([&] {
        started.set_value();
        gate.get_future().wait();
        ran.fetch_add(1);
    });
    started.get_future().wait();  // the single worker is now busy
    std::vector<std::future<void>> queued;
    for (int i = 0; i < 50; ++i) queued.push_back(pool.submit([&ran] { ran.fetch_add(1); }));
    EXPECT_EQ(pool.pendingTasks(), 50u);

    gate.set_value();
    pool.shutdown();  // must wait for all 51 tasks
    EXPECT_EQ(ran.load(), 51) << "no queued task may be dropped";
    EXPECT_EQ(pool.pendingTasks(), 0u);
    blocker.get();
    for (auto& f : queued) f.get();
}

TEST(ThreadPool, DestructorDrainsTheQueueAndJoinsEveryThread) {
    std::atomic<int> ran{0};
    std::vector<std::future<void>> futures;
    {
        ThreadPool pool(3);
        for (int i = 0; i < 300; ++i) futures.push_back(pool.submit([&ran] { ran.fetch_add(1); }));
    }  // destructor: everything queued runs, all threads joined
    EXPECT_EQ(ran.load(), 300);
    for (auto& f : futures) EXPECT_EQ(f.wait_for(std::chrono::seconds(0)), std::future_status::ready);
}

TEST(ThreadPool, SubmitAfterShutdownThrows) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW((void)pool.submit([] { return 1; }), std::logic_error);
}

TEST(ThreadPool, ShutdownIsIdempotent) {
    ThreadPool pool(2);
    auto f = pool.submit([] { return 3; });
    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown());
    EXPECT_EQ(f.get(), 3);
}

TEST(ThreadPool, ConcurrentShutdownCallsAreSafe) {
    ThreadPool pool(4);
    std::atomic<int> ran{0};
    for (int i = 0; i < 200; ++i) (void)pool.submit([&ran] { ran.fetch_add(1); });
    std::vector<std::thread> callers;
    for (int i = 0; i < 4; ++i) callers.emplace_back([&pool] { pool.shutdown(); });
    for (auto& t : callers) t.join();
    EXPECT_EQ(ran.load(), 200);
}

TEST(ThreadPool, ShutdownFromInsideAWorkerIsRejectedInsteadOfDeadlocking) {
    ThreadPool pool(1);
    auto f = pool.submit([&pool] { pool.shutdown(); });
    EXPECT_THROW(f.get(), std::logic_error);
}

TEST(ThreadPool, TasksCanBeSubmittedFromManyThreadsAtOnce) {
    ThreadPool pool(4);
    std::atomic<int> ran{0};
    std::mutex futuresMutex;
    std::vector<std::future<void>> futures;
    std::vector<std::thread> producers;
    for (int p = 0; p < 8; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < 500; ++i) {
                auto f = pool.submit([&ran] { ran.fetch_add(1); });
                const std::lock_guard<std::mutex> lock(futuresMutex);
                futures.push_back(std::move(f));
            }
        });
    }
    for (auto& t : producers) t.join();
    for (auto& f : futures) f.get();
    EXPECT_EQ(ran.load(), 4000);
}

TEST(ThreadPool, RepeatedCreationAndDestructionNeverDeadlocksOrLosesTasks) {
    std::atomic<int> ran{0};
    for (int round = 0; round < 100; ++round) {
        ThreadPool pool(1 + static_cast<std::size_t>(round % 4));
        for (int i = 0; i < 20; ++i) (void)pool.submit([&ran] { ran.fetch_add(1); });
    }
    EXPECT_EQ(ran.load(), 100 * 20);
}

TEST(ThreadPool, FuturesStayValidAfterThePoolIsGone) {
    std::future<int> f;
    {
        ThreadPool pool(2);
        f = pool.submit([] { return 123; });
    }
    EXPECT_EQ(f.get(), 123);
}

TEST(ThreadPool, TasksStartInSubmissionOrderOnASingleWorker) {
    ThreadPool pool(1);
    std::vector<int> order;  // only touched by the single worker, read after shutdown
    for (int i = 0; i < 100; ++i) (void)pool.submit([&order, i] { order.push_back(i); });
    pool.shutdown();
    ASSERT_EQ(order.size(), 100u);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
}
