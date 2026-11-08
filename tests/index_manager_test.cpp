#include "index/index_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;
using search::index::IndexManager;
using search::index::IndexState;

class TempDataset {
  public:
    TempDataset() {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("search-manager-" + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDataset() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path &path() const { return path_; }

    void write(const std::string &name, const std::string &content) const {
        std::ofstream(path_ / name) << content;
    }

  private:
    fs::path path_;
};

} // namespace

// TEST-UT-14: до першої побудови — Empty, знімка немає, статистика нульова.
TEST(IndexManager, InitialStateIsEmptyWithZeroStats) {
    IndexManager manager;
    EXPECT_EQ(manager.state(), IndexState::Empty);
    EXPECT_EQ(manager.snapshot(), nullptr);
    const auto stats = manager.stats();
    EXPECT_EQ(stats.documents, 0U);
    EXPECT_EQ(stats.index_build_time_ms, 0U);
    EXPECT_EQ(stats.threads_used, 0U);
}

TEST(IndexManager, SuccessfulBuildBecomesReadyWithConsistentStats) {
    TempDataset data;
    data.write("a.txt", "parallel index");
    data.write("b.txt", "parallel thread");
    IndexManager manager;

    ASSERT_TRUE(manager.start_build(data.path(), 2));
    manager.wait_for_build();

    EXPECT_EQ(manager.state(), IndexState::Ready);
    const auto stats = manager.stats();
    EXPECT_EQ(stats.documents, 2U);
    EXPECT_EQ(stats.threads_used, 2U);
    ASSERT_NE(manager.snapshot(), nullptr);
    EXPECT_EQ(manager.snapshot()->documents_containing("parallel").size(), 2U);
    EXPECT_FALSE(manager.last_error().has_value());
}

// BDD-IDX-12 / OQ-10: під час перебудови пошук бачить старий індекс; після заміни — новий.
TEST(IndexManager, SnapshotTakenBeforeRebuildKeepsOldResults) {
    TempDataset data;
    data.write("old.txt", "alpha");
    IndexManager manager;
    ASSERT_TRUE(manager.start_build(data.path(), 1));
    manager.wait_for_build();
    const auto old_snapshot = manager.snapshot();
    ASSERT_NE(old_snapshot, nullptr);

    data.write("new.txt", "beta");
    ASSERT_TRUE(manager.start_build(data.path(), 1));
    manager.wait_for_build();

    EXPECT_EQ(old_snapshot->document_count(), 1U);
    EXPECT_EQ(old_snapshot->documents_containing("beta").size(), 0U);
    EXPECT_EQ(manager.snapshot()->document_count(), 2U);
    EXPECT_EQ(manager.snapshot()->documents_containing("beta").size(), 1U);
}

// BDD-IDX-12: збій побудови не чіпає попередній активний індекс.
TEST(IndexManager, FailedRebuildKeepsPreviousIndexActive) {
    TempDataset data;
    data.write("a.txt", "alpha");
    IndexManager manager;
    ASSERT_TRUE(manager.start_build(data.path(), 2));
    manager.wait_for_build();

    const auto before = manager.snapshot();
    ASSERT_TRUE(manager.start_build(data.path() / "missing", 2));
    manager.wait_for_build();

    EXPECT_EQ(manager.state(), IndexState::Ready);
    ASSERT_NE(manager.snapshot(), nullptr);
    EXPECT_EQ(manager.snapshot(), before); // той самий знімок, а не інший індекс
    EXPECT_EQ(manager.snapshot()->document_count(), 1U);
    EXPECT_TRUE(manager.last_error().has_value());
    EXPECT_EQ(manager.stats().documents, 1U);
}

TEST(IndexManager, FailedFirstBuildLeavesEmptyState) {
    TempDataset data;
    IndexManager manager;
    ASSERT_TRUE(manager.start_build(data.path() / "missing", 1));
    manager.wait_for_build();

    EXPECT_EQ(manager.state(), IndexState::Empty);
    EXPECT_EQ(manager.snapshot(), nullptr);
    EXPECT_TRUE(manager.last_error().has_value());
}

// OQ-22: порожній каталог — успішна побудова з нулем документів.
TEST(IndexManager, EmptyDatasetBuildsZeroDocuments) {
    TempDataset data;
    IndexManager manager;
    ASSERT_TRUE(manager.start_build(data.path(), 3));
    manager.wait_for_build();
    EXPECT_EQ(manager.state(), IndexState::Ready);
    EXPECT_EQ(manager.stats().documents, 0U);
}

TEST(IndexManager, ZeroThreadsIsRejected) {
    IndexManager manager;
    EXPECT_THROW(static_cast<void>(manager.start_build(".", 0)), std::invalid_argument);
}

// Регресія рев'ю TCK-013: виняток з callback не має виходити з потоку побудови.
TEST(IndexManagerReviewFixes, CallbackExceptionDoesNotTerminateProcess) {
    const auto dir = std::filesystem::temp_directory_path() / "index_manager_cb_throw";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "a.txt") << "alpha beta\n"; }

    search::index::IndexManager manager;
    std::atomic<bool> called{false};
    ASSERT_TRUE(manager.start_build(dir, 1, [&called](const search::index::BuildResult &) {
        called = true;
        throw std::runtime_error("callback failure");
    }));
    manager.wait_for_build();
    // Процес живий: стан коректний і побудова відбулася.
    EXPECT_EQ(manager.state(), search::index::IndexState::Ready);
    std::filesystem::remove_all(dir);
}

// Регресія: start_build із власного callback заборонено явним винятком, а не deadlock.
TEST(IndexManagerReviewFixes, StartBuildFromOwnCallbackThrowsLogicError) {
    const auto dir = std::filesystem::temp_directory_path() / "index_manager_reentrant";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "a.txt") << "alpha\n"; }

    search::index::IndexManager manager;
    std::atomic<bool> threw{false};
    ASSERT_TRUE(manager.start_build(dir, 1, [&](const search::index::BuildResult &) {
        try {
            manager.start_build(dir, 1);
        } catch (const std::logic_error &) {
            threw = true;
        }
    }));
    manager.wait_for_build();
    // wait_for_build повертається, коли building_ скинуто; callback може ще виконуватися.
    for (int i = 0; i < 500 && !threw.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(threw.load());
    std::filesystem::remove_all(dir);
}

// TCK-039 (OQ-32): зупинка до старту побудови — побудова скасовується з кодом build_cancelled,
// активний індекс не створюється, стан лишається Empty.
TEST(IndexManagerShutdown, CancelledBuildDoesNotActivateIndex) {
    TempDataset data;
    data.write("a.txt", "alpha beta\n");
    IndexManager manager;
    manager.request_cancel();

    std::promise<search::index::BuildResult> done;
    auto future = done.get_future();
    ASSERT_TRUE(manager.start_build(
        data.path(), 2, [&done](const search::index::BuildResult &r) { done.set_value(r); }));
    const auto result = future.get();
    manager.wait_for_build();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "build_cancelled");
    EXPECT_EQ(manager.state(), IndexState::Empty);
    EXPECT_EQ(manager.snapshot(), nullptr);
}

// TCK-039: скасування під час першої побудови не лишає стан Building.
TEST(IndexManagerShutdown, CancelDuringBuildReturnsToEmptyAndUnblocksWaiters) {
    TempDataset data;
    for (int i = 0; i < 2000; ++i) {
        data.write("d" + std::to_string(i) + ".txt", "alpha beta gamma delta\n");
    }
    IndexManager manager;
    ASSERT_TRUE(manager.start_build(data.path(), 2));
    manager.request_cancel();
    manager.wait_for_build();

    EXPECT_NE(manager.state(), IndexState::Building);
    EXPECT_EQ(manager.state(), IndexState::Empty);
    EXPECT_EQ(manager.snapshot(), nullptr);
}
