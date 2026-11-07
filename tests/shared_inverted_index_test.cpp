#include "index/shared_inverted_index.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using search::index::InvertedIndex;
using search::index::SharedInvertedIndex;

const std::vector<std::string> kVocabulary = {
    "parallel", "mutex",  "semaphore", "thread", "pool",  "index", "query", "search",
    "document", "worker", "server",    "cache",  "token", "term",  "rank",  "queue",
};

struct Doc {
    std::string id;
    std::vector<std::string> tokens;
};

// Детерміновані документи: однаковий seed — однаковий корпус.
std::vector<Doc> make_corpus(std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, kVocabulary.size() - 1);
    std::uniform_int_distribution<int> length(0, 40);
    std::vector<Doc> docs;
    for (std::size_t i = 0; i < count; ++i) {
        Doc doc{"doc" + std::to_string(i), {}};
        const int n = length(rng);
        for (int k = 0; k < n; ++k) {
            doc.tokens.push_back(kVocabulary[pick(rng)]);
        }
        docs.push_back(std::move(doc));
    }
    return docs;
}

// Еталон: послідовна побудова. Порядок вставки на результат не впливає, якщо порівнювати
// за doc_id (індекси документів залежать від порядку).
InvertedIndex sequential_reference(const std::vector<Doc> &docs) {
    InvertedIndex reference;
    for (const Doc &doc : docs) {
        reference.add_document(doc.id, doc.tokens);
    }
    return reference;
}

void expect_same_as_reference(const SharedInvertedIndex &actual, const std::vector<Doc> &docs) {
    InvertedIndex reference = sequential_reference(docs);
    EXPECT_EQ(actual.document_count(), reference.document_count());
    for (const std::string &term : kVocabulary) {
        std::vector<std::string> expected_docs;
        for (const Doc &doc : docs) {
            const std::size_t tf = reference.term_frequency(term, doc.id);
            if (tf > 0) {
                expected_docs.push_back(doc.id);
            }
            EXPECT_EQ(actual.term_frequency(term, doc.id), tf) << term << " in " << doc.id;
        }
        std::sort(expected_docs.begin(), expected_docs.end()); // порядок результату — лексичний
        EXPECT_EQ(actual.documents_containing(term), expected_docs) << term;
    }
}

} // namespace

TEST(SharedInvertedIndex, SingleThreadMatchesReference) {
    const auto docs = make_corpus(200, 1);
    SharedInvertedIndex index;
    for (const Doc &doc : docs) {
        index.add_document(doc.id, doc.tokens);
    }
    expect_same_as_reference(index, docs);
}

// TEST-UT-16 / TEST-CONC-01: одночасна вставка з 8 потоків дає результат еталона.
TEST(SharedInvertedIndex, EightConcurrentWritersMatchSequentialReference) {
    const auto docs = make_corpus(800, 7);
    SharedInvertedIndex index;
    constexpr std::size_t kWriters = 8;

    std::vector<std::thread> writers;
    for (std::size_t w = 0; w < kWriters; ++w) {
        writers.emplace_back([&index, &docs, w] {
            for (std::size_t i = w; i < docs.size(); i += kWriters) {
                index.add_document(docs[i].id, docs[i].tokens);
            }
        });
    }
    for (auto &t : writers) {
        t.join();
    }
    expect_same_as_reference(index, docs);
}

// TEST-CONC-02 (модульна частина): читання під час запису не падає і не дає неконсистентних
// значень; після завершення запису результат збігається з еталоном.
TEST(SharedInvertedIndex, ReadersDuringWritesSeeConsistentCounts) {
    const auto docs = make_corpus(600, 11);
    SharedInvertedIndex index;
    std::atomic<bool> writing{true};
    std::atomic<bool> violation{false};

    std::thread writer([&index, &docs, &writing] {
        for (const Doc &doc : docs) {
            index.add_document(doc.id, doc.tokens);
        }
        writing.store(false);
    });

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&index, &writing, &violation] {
            // Обмежена кількість ітерацій: std::shared_mutex може віддавати перевагу читачам,
            // і необмежений цикл читання голодив би письменника.
            for (int iteration = 0; iteration < 500 && writing.load(); ++iteration) {
                std::this_thread::yield();
                const std::size_t docs_now = index.document_count();
                const auto containing = index.documents_containing("thread");
                // Кожен документ, що містить терм, уже видимий у лічильнику документів.
                if (containing.size() > index.document_count()) {
                    violation.store(true);
                }
                if (docs_now > 600) {
                    violation.store(true);
                }
            }
        });
    }
    writer.join();
    for (auto &t : readers) {
        t.join();
    }
    EXPECT_FALSE(violation.load());
    expect_same_as_reference(index, docs);
}

TEST(SharedInvertedIndex, EmptyDocumentIsCountedAndTermsAbsent) {
    SharedInvertedIndex index;
    index.add_document("empty.txt", {});
    EXPECT_EQ(index.document_count(), 1U);
    EXPECT_EQ(index.term_count(), 0U);
    EXPECT_TRUE(index.documents_containing("anything").empty());
}
