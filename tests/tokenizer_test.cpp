#include "index/tokenizer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using search::index::tokenize;
using Tokens = std::vector<std::string>;

} // namespace

TEST(Tokenizer, LowercasesAsciiAndSplitsOnPunctuation) {
    EXPECT_EQ(tokenize("Hello, World! 42"), (Tokens{"hello", "world", "42"}));
}

TEST(Tokenizer, UnderscoreAndHyphenAreSeparators) {
    EXPECT_EQ(tokenize("a-b_c"), (Tokens{"a", "b", "c"}));
}

TEST(Tokenizer, LowercasesCyrillic) {
    EXPECT_EQ(tokenize("Привіт, Світ"), (Tokens{"привіт", "світ"}));
    EXPECT_EQ(tokenize("МОСКВА2026"), (Tokens{"москва2026"}));
}

TEST(Tokenizer, LowercasesUkrainianGe) {
    EXPECT_EQ(tokenize("Ґанок ґанок"), (Tokens{"ґанок", "ґанок"}));
}

TEST(Tokenizer, EmojiAndArrowsAreSeparators) {
    EXPECT_EQ(tokenize("кіт\U0001F600собака→дім"), (Tokens{"кіт", "собака", "дім"}));
}

TEST(Tokenizer, LowercasesLatinExtended) { EXPECT_EQ(tokenize("ÄÖÜ"), (Tokens{"äöü"})); }

TEST(Tokenizer, InvalidBytesActAsSeparators) {
    EXPECT_EQ(tokenize("ab\xFF"
                       "cd"),
              (Tokens{"ab", "cd"}));
}

TEST(Tokenizer, EmptyAndPunctuationOnlyYieldNoTokens) {
    EXPECT_TRUE(tokenize("").empty());
    EXPECT_TRUE(tokenize(" ,.!? \n\t").empty());
}
