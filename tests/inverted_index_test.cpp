#include "index/inverted_index.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using search::index::InvertedIndex;
using search::index::Posting;
using search::index::TermTable;

std::vector<std::string> words(std::initializer_list<const char *> list) {
    return {list.begin(), list.end()};
}

} // namespace

// TEST-UT-02: вставка і пошук терма; кілька термів одного документа.
TEST(InvertedIndex, InsertAndFindSeveralTermsOfOneDocument) {
    InvertedIndex index;
    index.add_document("a.txt", words({"parallel", "mutex", "parallel"}));

    ASSERT_NE(index.postings("parallel"), nullptr);
    EXPECT_EQ(*index.postings("parallel"), (std::vector<Posting>{{0, 2}}));
    EXPECT_EQ(*index.postings("mutex"), (std::vector<Posting>{{0, 1}}));
    EXPECT_EQ(index.postings("absent"), nullptr);
    EXPECT_EQ(index.term_count(), 2U);
}

// TEST-UT-03: term_frequency; терм у кількох документах; немає дублів doc_id у списку.
TEST(InvertedIndex, TermFrequencyAndNoDuplicateDocIds) {
    InvertedIndex index;
    index.add_document("a.txt", words({"thread", "thread", "thread"}));
    index.add_document("b.txt", words({"thread", "pool"}));

    EXPECT_EQ(index.term_frequency("thread", "a.txt"), 3U);
    EXPECT_EQ(index.term_frequency("thread", "b.txt"), 1U);
    EXPECT_EQ(index.term_frequency("pool", "a.txt"), 0U);

    const auto *list = index.postings("thread");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(*list, (std::vector<Posting>{{0, 3}, {1, 1}}));
    EXPECT_EQ(index.doc_id(1), "b.txt");
}

// TEST-UT-04: порожній документ рахується, постингів немає.
TEST(InvertedIndex, EmptyDocumentIsCountedWithoutPostings) {
    InvertedIndex index;
    index.add_document("empty.txt", {});
    index.add_document("b.txt", words({"x"}));

    EXPECT_EQ(index.document_count(), 2U);
    EXPECT_EQ(index.term_count(), 1U);
    EXPECT_EQ(index.term_frequency("x", "empty.txt"), 0U);
}

// TEST-UT-16 (однопотокова частина): колізії і розширення без втрат.
TEST(TermTable, GrowsAndKeepsEveryKeyAcrossCollisions) {
    TermTable table;
    constexpr std::size_t kTerms = 5000;
    for (std::size_t i = 0; i < kTerms; ++i) {
        const std::string key = "t" + std::to_string(i);
        EXPECT_EQ(table.find_or_insert(key, i), i);
    }
    EXPECT_EQ(table.size(), kTerms);
    EXPECT_GE(table.capacity(), kTerms * 2);
    for (std::size_t i = 0; i < kTerms; ++i) {
        EXPECT_EQ(table.find("t" + std::to_string(i)), i);
    }
    EXPECT_EQ(table.find("missing"), TermTable::kNotFound);
    // Повторна вставка не змінює наявне значення.
    EXPECT_EQ(table.find_or_insert("t7", 999), 7U);
    EXPECT_EQ(table.size(), kTerms);
}

TEST(InvertedIndex, ManyTermsRemainSearchable) {
    InvertedIndex index;
    std::vector<std::string> tokens;
    for (int i = 0; i < 2000; ++i) {
        tokens.push_back("w" + std::to_string(i));
    }
    index.add_document("big.txt", tokens);
    index.add_document("other.txt", words({"w1999", "w0"}));

    EXPECT_EQ(index.term_frequency("w1999", "big.txt"), 1U);
    EXPECT_EQ(index.term_frequency("w1999", "other.txt"), 1U);
    EXPECT_EQ(index.term_frequency("w0", "other.txt"), 1U);
    EXPECT_EQ(index.term_count(), 2000U);
}
