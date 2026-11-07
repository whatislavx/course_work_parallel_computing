#include "pool/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using search::pool::ThreadPool;
using namespace std::chrono_literals;

// Чекає, доки кількість учасників досягне n (або спливе тайм-аут). Гарантує, що n задач
// справді виконуються одночасно.
void rendezvous(std::atomic<int> &arrived, int n) {
    arrived.fetch_add(1);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (arrived.load() < n && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

} // namespace

// TEST-UT-07 ---------------------------------------------------------------------------------

TEST(ThreadPool, RejectsZeroThreadsAndZeroCapacity) {
    EXPECT_THROW(ThreadPool(0, 4), std::invalid_argument);
    EXPECT_THROW(ThreadPool(2, 0), std::invalid_argument);
}

TEST(ThreadPool, CreatesRequestedNumberOfWorkers) {
    ThreadPool pool(3, 8);
    EXPECT_EQ(pool.thread_count(), 3U);
}

TEST(ThreadPool, RunsEveryTaskExactlyOnce) {
    constexpr int kTasks = 1000;
    std::vector<std::atomic<int>> hits(kTasks);
    for (auto &h : hits) {
        h.store(0);
    }
    ThreadPool pool(4, 16);
    std::vector<std::future<void>> results;
    for (int i = 0; i < kTasks; ++i) {
        results.push_back(
            pool.submit([&hits, i] { hits[static_cast<std::size_t>(i)].fetch_add(1); }));
    }
    for (auto &r : results) {
        r.get();
    }
    for (const auto &h : hits) {
        EXPECT_EQ(h.load(), 1);
    }
}

TEST(ThreadPool, AcceptsConcurrentSubmissionsFromManyThreads) {
    constexpr int kSubmitters = 4;
    constexpr int kPerSubmitter = 250;
    std::atomic<int> executed{0};
    ThreadPool pool(4, 32);

    std::vector<std::thread> submitters;
    for (int s = 0; s < kSubmitters; ++s) {
        submitters.emplace_back([&pool, &executed] {
            for (int i = 0; i < kPerSubmitter; ++i) {
                pool.submit([&executed] { executed.fetch_add(1); });
            }
        });
    }
    for (auto &t : submitters) {
        t.join();
    }
    pool.wait_idle();
    EXPECT_EQ(executed.load(), kSubmitters * kPerSubmitter);
}

TEST(ThreadPool, RunsAllWorkersConcurrentlyButNoMore) {
    constexpr int kWorkers = 3;
    ThreadPool pool(kWorkers, 64);
    std::atomic<int> arrived{0};
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};

    std::vector<std::future<void>> results;
    for (int i = 0; i < 30; ++i) {
        results.push_back(pool.submit([&] {
            const int now = active.fetch_add(1) + 1;
            int seen = max_active.load();
            while (now > seen && !max_active.compare_exchange_weak(seen, now)) {
            }
            if (arrived.load() < kWorkers) {
                rendezvous(arrived, kWorkers);
            }
            active.fetch_sub(1);
        }));
    }
    for (auto &r : results) {
        r.get();
    }
    EXPECT_LE(max_active.load(), kWorkers);
    EXPECT_EQ(max_active.load(), kWorkers);
}

// TEST-UT-08 ---------------------------------------------------------------------------------

TEST(ThreadPool, TaskExceptionIsDeliveredToSubmitter) {
    ThreadPool pool(2, 4);
    auto result = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(static_cast<void>(result.get()), std::runtime_error);
}

TEST(ThreadPool, SurvivesManyExceptionsWithAllWorkersAlive) {
    constexpr int kWorkers = 3;
    ThreadPool pool(kWorkers, 16);
    std::vector<std::future<void>> failures;
    for (int i = 0; i < 100; ++i) {
        failures.push_back(pool.submit([] { throw std::logic_error("x"); }));
    }
    for (auto &f : failures) {
        EXPECT_THROW(f.get(), std::logic_error);
    }

    // Якщо хоч один робочий потік загинув, рандеву з kWorkers учасників не відбудеться.
    std::atomic<int> arrived{0};
    std::vector<std::future<void>> check;
    for (int i = 0; i < kWorkers; ++i) {
        check.push_back(pool.submit([&arrived] { rendezvous(arrived, kWorkers); }));
    }
    for (auto &c : check) {
        c.get();
    }
    EXPECT_EQ(arrived.load(), kWorkers);
}

// TEST-UT-09 ---------------------------------------------------------------------------------

TEST(ThreadPool, ShutdownRunsEveryAcceptedTask) {
    std::atomic<int> executed{0};
    {
        ThreadPool pool(4, 2000);
        for (int i = 0; i < 1000; ++i) {
            pool.submit([&executed] { executed.fetch_add(1); });
        }
        pool.shutdown();
    }
    EXPECT_EQ(executed.load(), 1000);
}

TEST(ThreadPool, DoubleShutdownIsSafe) {
    ThreadPool pool(2, 4);
    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown());
}

TEST(ThreadPool, SubmitAfterShutdownIsRejected) {
    ThreadPool pool(2, 4);
    pool.shutdown();
    EXPECT_THROW(pool.submit([] {}), std::runtime_error);
}

TEST(ThreadPool, ShutdownReleasesBlockedSubmitWithinOneSecond) {
    ThreadPool pool(1, 1);
    std::atomic<bool> release{false};
    // Перша задача займає єдиний потік, друга — єдине місце в черзі.
    auto busy = pool.submit([&release] {
        while (!release.load()) {
            std::this_thread::sleep_for(1ms);
        }
    });
    auto queued = pool.submit([] {});

    std::atomic<bool> submit_rejected{false};
    std::thread blocked([&pool, &submit_rejected] {
        try {
            pool.submit([] {});
        } catch (const std::runtime_error &) {
            submit_rejected.store(true);
        }
    });
    std::this_thread::sleep_for(50ms); // даємо blocked submit зайняти очікування

    const auto started = std::chrono::steady_clock::now();
    std::thread stopper([&pool] { pool.shutdown(); });

    const auto deadline = started + 1s;
    while (!submit_rejected.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    const bool rejected_in_time = submit_rejected.load();

    release.store(true); // дозволяємо shutdown приєднати потік
    stopper.join();
    blocked.join();
    EXPECT_TRUE(rejected_in_time);
    busy.get();
    queued.get();
}

TEST(ThreadPool, CreateAndShutdownRepeatedly) {
    for (int round = 0; round < 100; ++round) {
        ThreadPool pool(2, 4);
        pool.submit([] {}).get();
    }
    SUCCEED();
}

// TEST-UT-15 ---------------------------------------------------------------------------------

TEST(ThreadPool, WaitIdleReturnsOnlyAfterSlowestTaskAndResultsAreVisible) {
    ThreadPool pool(4, 64);
    std::vector<int> results(8, 0);
    std::atomic<bool> slow_done{false};

    pool.submit([&slow_done] {
        std::this_thread::sleep_for(100ms);
        slow_done.store(true);
    });
    for (std::size_t i = 0; i < results.size(); ++i) {
        pool.submit([&results, i] { results[i] = static_cast<int>(i) + 1; });
    }
    pool.wait_idle();

    EXPECT_TRUE(slow_done.load());
    for (std::size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i], static_cast<int>(i) + 1);
    }
}
