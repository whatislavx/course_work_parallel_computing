#include "search/snippet.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

using search::snippet::make_snippet;

} // namespace

TEST(Snippet, StartsAtFirstWordContainingAQueryTerm) {
    EXPECT_EQ(make_snippet("the quick brown fox jumps", {"brown"}, 100), "brown fox jumps");
}

TEST(Snippet, MatchingIsCaseInsensitiveAndIgnoresPunctuation) {
    EXPECT_EQ(make_snippet("Hello, World! again", {"world"}, 100), "World! again");
}

TEST(Snippet, NoMatchReturnsEmptyString) {
    EXPECT_EQ(make_snippet("alpha beta gamma", {"delta"}, 100), "");
    EXPECT_EQ(make_snippet("", {"alpha"}, 100), "");
    EXPECT_EQ(make_snippet("alpha beta", {}, 100), "");
}

TEST(Snippet, ZeroLimitReturnsEmptyString) {
    EXPECT_EQ(make_snippet("alpha beta", {"alpha"}, 0), "");
}

TEST(Snippet, LengthNeverExceedsLimitAndCutsAtWordBoundary) {
    const std::string text = "alpha bravo charlie delta";
    const std::string snippet = make_snippet(text, {"alpha"}, 13);
    EXPECT_LE(snippet.size(), 13u);
    EXPECT_EQ(snippet, "alpha bravo");
}

TEST(Snippet, MatchedWordLongerThanLimitGivesEmptyString) {
    EXPECT_EQ(make_snippet("supercalifragilistic", {"supercalifragilistic"}, 5), "");
}

TEST(Snippet, DoesNotSplitUtf8Characters) {
    // «привіт» займає 12 байтів; обрізання на межі символу не дає невалідного UTF-8.
    const std::string text = "слово привіт світ";
    const std::string snippet = make_snippet(text, {"привіт"}, 14);
    EXPECT_LE(snippet.size(), 14u);
    EXPECT_FALSE(snippet.empty());
    // Межі фрагмента мають збігатися з межами символів: перший байт — початок символу,
    // а байт після фрагмента в тексті — теж не продовження.
    ASSERT_FALSE(snippet.empty());
    EXPECT_NE(static_cast<unsigned char>(snippet.front()) & 0xC0, 0x80);
    const std::size_t after = text.find(snippet) + snippet.size();
    if (after < text.size()) {
        EXPECT_NE(static_cast<unsigned char>(text[after]) & 0xC0, 0x80);
    }
}
