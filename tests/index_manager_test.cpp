#include "index/index_manager.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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

    ASSERT_TRUE(manager.start_build(data.path() / "missing", 2));
    manager.wait_for_build();

    EXPECT_EQ(manager.state(), IndexState::Ready);
    ASSERT_NE(manager.snapshot(), nullptr);
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
