#include "search/ranking.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

using search::index::SharedInvertedIndex;
using search::ranking::rank_tfidf;

// Тестові документи: токени вже нормалізовані (токенізатор — окремий модуль).
void fill_index(SharedInvertedIndex &index) {
    index.add_document("b.txt", {"alpha", "beta", "alpha"});
    index.add_document("a.txt", {"alpha", "gamma"});
    index.add_document("c.txt", {"delta"});
}

} // namespace

TEST(Ranking, EmptyIndexReturnsNoResults) {
    SharedInvertedIndex index;
    EXPECT_TRUE(rank_tfidf(index, {"alpha"}).empty());
}

TEST(Ranking, UnknownTermsReturnNoResults) {
    SharedInvertedIndex index;
    fill_index(index);
    EXPECT_TRUE(rank_tfidf(index, {"nope"}).empty());
    EXPECT_TRUE(rank_tfidf(index, {}).empty());
}

TEST(Ranking, ScoreIsTermFrequencyTimesLogInverseDocumentFrequency) {
    SharedInvertedIndex index;
    fill_index(index);
    // alpha: df = 2, N = 3 → idf = ln(3/2); b.txt має tf = 2, a.txt — tf = 1.
    const double idf = std::log(3.0 / 2.0);
    const auto ranked = rank_tfidf(index, {"alpha"});
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked[0].doc_id, "b.txt");
    EXPECT_NEAR(ranked[0].score, 2.0 * idf, 1e-12);
    EXPECT_EQ(ranked[1].doc_id, "a.txt");
    EXPECT_NEAR(ranked[1].score, 1.0 * idf, 1e-12);
}

TEST(Ranking, ScoresFromSeveralTermsAreSummedPerDocument) {
    SharedInvertedIndex index;
    fill_index(index);
    const double idf_alpha = std::log(3.0 / 2.0);
    const double idf_gamma = std::log(3.0 / 1.0);
    const auto ranked = rank_tfidf(index, {"alpha", "gamma"});
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked[0].doc_id, "a.txt");
    EXPECT_NEAR(ranked[0].score, idf_alpha + idf_gamma, 1e-12);
}

TEST(Ranking, DuplicateQueryTermsDoNotInflateScores) {
    SharedInvertedIndex index;
    fill_index(index);
    const auto once = rank_tfidf(index, {"alpha"});
    const auto twice = rank_tfidf(index, {"alpha", "alpha"});
    ASSERT_EQ(once.size(), twice.size());
    for (std::size_t i = 0; i < once.size(); ++i) {
        EXPECT_EQ(once[i].doc_id, twice[i].doc_id);
        EXPECT_DOUBLE_EQ(once[i].score, twice[i].score);
    }
}

TEST(Ranking, TermInEveryDocumentContributesZero) {
    SharedInvertedIndex index;
    index.add_document("x.txt", {"common"});
    index.add_document("y.txt", {"common"});
    const auto ranked = rank_tfidf(index, {"common"});
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_DOUBLE_EQ(ranked[0].score, 0.0);
    EXPECT_DOUBLE_EQ(ranked[1].score, 0.0);
}

TEST(Ranking, EqualScoresAreOrderedByDocIdAscending) {
    SharedInvertedIndex index;
    index.add_document("z.txt", {"term", "other"});
    index.add_document("m.txt", {"term", "other"});
    index.add_document("a.txt", {"term", "other"});
    index.add_document("q.txt", {"unrelated"});
    const auto ranked = rank_tfidf(index, {"term"});
    ASSERT_EQ(ranked.size(), 3u);
    EXPECT_EQ(ranked[0].doc_id, "a.txt");
    EXPECT_EQ(ranked[1].doc_id, "m.txt");
    EXPECT_EQ(ranked[2].doc_id, "z.txt");
}
