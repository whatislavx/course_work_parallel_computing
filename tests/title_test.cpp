#include "search/title.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

using search::title::make_title;
using search::title::make_title_from_doc_id;

} // namespace

TEST(Title, UsesFirstNonBlankLine) {
    EXPECT_EQ(make_title("Solar energy\nbody text", 120), "Solar energy");
}

TEST(Title, SkipsLeadingBlankLinesAndWhitespaceOnlyLines) {
    EXPECT_EQ(make_title("\n   \n\t\nGarden notes\nmore", 120), "Garden notes");
}

TEST(Title, CollapsesInternalAndEdgeWhitespace) {
    EXPECT_EQ(make_title("   the   big \t  tree  \r\nnext", 120), "the big tree");
}

TEST(Title, EmptyOrBlankTextGivesEmptyTitle) {
    EXPECT_EQ(make_title("", 120), "");
    EXPECT_EQ(make_title("  \n \t \n", 120), "");
}

TEST(Title, ZeroLimitGivesEmptyTitle) { EXPECT_EQ(make_title("Solar energy", 0), ""); }

TEST(Title, LongLineIsCutAtWordBoundaryWithinLimit) {
    const std::string title = make_title("alpha bravo charlie delta", 13);
    EXPECT_LE(title.size(), 13u);
    EXPECT_EQ(title, "alpha bravo");
}

TEST(Title, SingleLongWordIsCutAtUtf8Boundary) {
    // "ї" — два байти (0xD1 0x97); ліміт 3 не повинен розрізати символ.
    const std::string title = make_title("їїї", 3);
    EXPECT_LE(title.size(), 3u);
    EXPECT_EQ(title, "ї");
}

TEST(Title, LineWithExactlyLimitBytesIsKept) {
    EXPECT_EQ(make_title("alpha bravo", 11), "alpha bravo");
}

TEST(Title, OnlyTheFirstContentLineIsUsed) {
    EXPECT_EQ(make_title("first\nsecond line\nthird", 120), "first");
}

TEST(TitleFromDocId, NumberedFileNameGivesDocumentNumber) {
    EXPECT_EQ(make_title_from_doc_id("doc001175.txt"), "Документ 1175");
}

TEST(TitleFromDocId, LeadingZerosAreDropped) {
    EXPECT_EQ(make_title_from_doc_id("doc000000.txt"), "Документ 0");
    EXPECT_EQ(make_title_from_doc_id("doc000042.txt"), "Документ 42");
}

TEST(TitleFromDocId, NonMatchingNameGivesEmptyTitle) {
    EXPECT_EQ(make_title_from_doc_id("b.txt"), "");
    EXPECT_EQ(make_title_from_doc_id("doc.txt"), "");
    EXPECT_EQ(make_title_from_doc_id("doc12.md"), "");
    EXPECT_EQ(make_title_from_doc_id("doc12a.txt"), "");
    EXPECT_EQ(make_title_from_doc_id("docx12.txt"), "");
    EXPECT_EQ(make_title_from_doc_id("../doc1.txt"), "");
}

TEST(TitleFromDocId, TooLongNumberGivesEmptyTitle) {
    EXPECT_EQ(make_title_from_doc_id("doc1234567890123456789.txt"), "");
}
