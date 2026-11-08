#include "index/index_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using search::index::build_index;
using search::index::BuildCancelledError;
using search::index::Document;
using search::index::IndexBuildError;
using search::index::InvertedIndex;
using search::index::tokenize;

const std::vector<std::string> kVocabulary = {
    "parallel", "mutex",  "semaphore", "thread", "pool",  "index", "query", "search",
    "document", "worker", "server",    "cache",  "token", "term",  "rank",  "queue",
};

std::vector<Document> make_corpus(std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, kVocabulary.size() - 1);
    std::uniform_int_distribution<int> length(0, 30);
    std::vector<Document> docs;
    for (std::size_t i = 0; i < count; ++i) {
        std::string text;
        const int n = length(rng);
        for (int k = 0; k < n; ++k) {
            text += kVocabulary[pick(rng)] + " ";
        }
        docs.push_back(Document{"doc" + std::to_string(i), text});
    }
    return docs;
}

InvertedIndex sequential_reference(const std::vector<Document> &docs) {
    InvertedIndex reference;
    for (const Document &doc : docs) {
        reference.add_document(doc.doc_id, tokenize(doc.text));
    }
    return reference;
}

// Порівняння за doc_id: індекси документів залежать від порядку, зміст — ні.
void expect_matches_reference(const search::index::SharedInvertedIndex &actual,
                              const std::vector<Document> &docs) {
    const InvertedIndex reference = sequential_reference(docs);
    EXPECT_EQ(actual.document_count(), reference.document_count());
    for (const std::string &term : kVocabulary) {
        std::vector<std::string> expected_docs;
        for (const Document &doc : docs) {
            const std::size_t tf = reference.term_frequency(term, doc.doc_id);
            if (tf > 0) {
                expected_docs.push_back(doc.doc_id);
            }
            EXPECT_EQ(actual.term_frequency(term, doc.doc_id), tf) << term << " in " << doc.doc_id;
        }
        std::sort(expected_docs.begin(), expected_docs.end());
        EXPECT_EQ(actual.documents_containing(term), expected_docs) << term;
    }
}

} // namespace

// TEST-IT-01 / TEST-CONC-01: результат паралельної побудови збігається з еталоном для всіх p.
TEST(IndexBuilder, ParallelBuildMatchesSequentialReferenceForAllThreadCounts) {
    const auto docs = make_corpus(300, 3);
    const std::size_t logical_cpus = std::max(1U, std::thread::hardware_concurrency());
    for (std::size_t threads : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4},
                                std::size_t{8}, logical_cpus}) {
        const auto index = build_index(docs, threads);
        expect_matches_reference(*index, docs);
    }
}

// TEST-CONC-05: повторні побудови з різною кількістю потоків не зависають.
TEST(IndexBuilder, HundredBuildsCompleteWithoutHanging) {
    const auto docs = make_corpus(40, 9);
    std::mt19937 rng(5);
    std::uniform_int_distribution<std::size_t> threads(1, 4);
    for (int round = 0; round < 100; ++round) {
        const auto index = build_index(docs, threads(rng));
        ASSERT_EQ(index->document_count(), docs.size());
    }
}

TEST(IndexBuilder, EmptyDocumentListProducesEmptyIndex) {
    const auto index = build_index({}, 4);
    EXPECT_EQ(index->document_count(), 0U);
    EXPECT_EQ(index->term_count(), 0U);
}

TEST(IndexBuilder, ZeroThreadsIsRejected) {
    EXPECT_THROW(build_index({}, 0), std::invalid_argument);
}

// TEST-IT-06 / BDD-IDX-12: внутрішній виняток однієї задачі дає помилку побудови.
TEST(IndexBuilder, TaskExceptionFailsTheBuild) {
    auto docs = make_corpus(100, 2);
    docs[50].text = "boom";
    const search::index::DocumentTokenizer failing = [](std::string_view text) {
        if (text == "boom") {
            throw std::runtime_error("внутрішній збій");
        }
        return tokenize(text);
    };
    EXPECT_THROW(build_index(docs, 4, failing), IndexBuildError);
}

// Виняток із порожнім повідомленням також є збоєм побудови (раніше тихо проходив).
TEST(IndexBuilder, ExceptionWithEmptyMessageStillFailsTheBuild) {
    const auto docs = make_corpus(10, 4);
    const search::index::DocumentTokenizer silent_failure =
        [](std::string_view) -> std::vector<std::string> { throw std::runtime_error(""); };
    EXPECT_THROW(build_index(docs, 2, silent_failure), IndexBuildError);
}

TEST(IndexBuilder, FailureMessageCarriesErrorCodePrefix) {
    const auto docs = make_corpus(5, 6);
    const search::index::DocumentTokenizer failing =
        [](std::string_view) -> std::vector<std::string> { throw std::runtime_error("disk gone"); };
    try {
        build_index(docs, 2, failing);
        FAIL() << "expected IndexBuildError";
    } catch (const IndexBuildError &e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("index_build_failed"), std::string::npos);
        EXPECT_NE(message.find("disk gone"), std::string::npos);
    }
}

// TCK-039 (OQ-32): встановлений до початку сигнал скасування перериває побудову і не повертає
// індекс; це не збій, тож виняток інший.
TEST(IndexBuilder, PreSetCancelFlagAbortsBuildWithCancelledError) {
    const auto docs = make_corpus(100, 2);
    std::atomic<bool> cancel{true};
    EXPECT_THROW(build_index(docs, 4, tokenize, &cancel), BuildCancelledError);
}

// Без встановленого сигналу той самий виклик дає повний індекс (регресія від TCK-039).
TEST(IndexBuilder, UnsetCancelFlagKeepsBuildComplete) {
    const auto docs = make_corpus(100, 2);
    std::atomic<bool> cancel{false};
    const auto index = build_index(docs, 4, tokenize, &cancel);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->document_count(), docs.size());
}
