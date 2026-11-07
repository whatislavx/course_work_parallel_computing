#include "index/document_source.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

namespace fs = std::filesystem;
using search::index::DatasetError;
using search::index::load_documents;

class TempDir {
  public:
    TempDir() {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("search-test-" + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path &path() const { return path_; }

  private:
    fs::path path_;
};

void write_file(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST(DocumentSource, DiscoversNestedFilesWithRelativeIds) {
    TempDir dir;
    write_file(dir.path() / "a.txt", "alpha");
    write_file(dir.path() / "sub" / "b.txt", "beta");

    const auto result = load_documents(dir.path());
    ASSERT_EQ(result.documents.size(), 2U);
    EXPECT_EQ(result.documents[0].doc_id, "a.txt");
    EXPECT_EQ(result.documents[0].text, "alpha");
    EXPECT_EQ(result.documents[1].doc_id, "sub/b.txt");
    EXPECT_TRUE(result.skipped.empty());
}

TEST(DocumentSource, EmptyDirectoryGivesZeroDocuments) {
    TempDir dir;
    const auto result = load_documents(dir.path());
    EXPECT_TRUE(result.documents.empty());
    EXPECT_TRUE(result.skipped.empty());
}

TEST(DocumentSource, EmptyFileIsADocument) {
    TempDir dir;
    write_file(dir.path() / "empty.txt", "");
    const auto result = load_documents(dir.path());
    ASSERT_EQ(result.documents.size(), 1U);
    EXPECT_TRUE(result.documents[0].text.empty());
}

TEST(DocumentSource, InvalidUtf8IsSkippedWithReason) {
    TempDir dir;
    write_file(dir.path() / "good.txt", "ok");
    write_file(dir.path() / "bad.txt", std::string("\xC3\x28", 2));
    const auto result = load_documents(dir.path());
    ASSERT_EQ(result.documents.size(), 1U);
    EXPECT_EQ(result.documents[0].doc_id, "good.txt");
    ASSERT_EQ(result.skipped.size(), 1U);
    EXPECT_EQ(result.skipped[0].doc_id, "bad.txt");
    EXPECT_EQ(result.skipped[0].reason, "invalid UTF-8");
}

TEST(DocumentSource, MissingDirectoryThrows) {
    TempDir dir;
    EXPECT_THROW(load_documents(dir.path() / "absent"), DatasetError);
}

TEST(DocumentSource, FileInsteadOfDirectoryThrows) {
    TempDir dir;
    write_file(dir.path() / "f.txt", "x");
    EXPECT_THROW(load_documents(dir.path() / "f.txt"), DatasetError);
}

TEST(DocumentSource, SymlinkKeepsItsOwnDocId) {
    TempDir dir;
    write_file(dir.path() / "real" / "target.txt", "data");
    std::error_code ec;
    fs::create_directories(dir.path() / "docs", ec);
    fs::create_directory_symlink(dir.path() / "real" / "target.txt",
                                 dir.path() / "docs" / "link.txt", ec);
    if (ec) {
        GTEST_SKIP() << "symlinks not supported";
    }
    const auto result = load_documents(dir.path() / "docs");
    ASSERT_EQ(result.documents.size(), 1U);
    EXPECT_EQ(result.documents[0].doc_id, "link.txt");
}

TEST(Utf8, ValidatesSequences) {
    EXPECT_TRUE(search::index::is_valid_utf8("привіт"));
    EXPECT_FALSE(search::index::is_valid_utf8("\xC0\xAF"));     // overlong
    EXPECT_FALSE(search::index::is_valid_utf8("\xED\xA0\x80")); // сурогат
    EXPECT_FALSE(search::index::is_valid_utf8("\xE2\x82")); // обрізана послідовність
}
