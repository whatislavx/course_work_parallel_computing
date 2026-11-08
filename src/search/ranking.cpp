#include "search/ranking.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace search::ranking {

std::vector<ScoredDocument> rank_tfidf(const index::SharedInvertedIndex &index,
                                       std::vector<std::string> terms) {
    std::vector<ScoredDocument> contributions;
    const std::size_t total_documents = index.document_count();
    if (total_documents == 0) {
        return {};
    }

    // Унікальні терми: повторний терм запиту не має подвоювати внесок.
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    for (const std::string &term : terms) {
        const std::vector<std::string> documents = index.documents_containing(term);
        if (documents.empty()) {
            continue;
        }
        const double idf =
            std::log(static_cast<double>(total_documents) / static_cast<double>(documents.size()));
        for (const std::string &doc_id : documents) {
            const double tf = static_cast<double>(index.term_frequency(term, doc_id));
            contributions.push_back({doc_id, tf * idf});
        }
    }

    // Сумуємо внески одного документа: сортування за doc_id групує їх без map.
    std::sort(contributions.begin(), contributions.end(),
              [](const ScoredDocument &a, const ScoredDocument &b) { return a.doc_id < b.doc_id; });
    std::vector<ScoredDocument> ranked;
    for (ScoredDocument &item : contributions) {
        if (!ranked.empty() && ranked.back().doc_id == item.doc_id) {
            ranked.back().score += item.score;
        } else {
            ranked.push_back(std::move(item));
        }
    }

    std::sort(ranked.begin(), ranked.end(), [](const ScoredDocument &a, const ScoredDocument &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.doc_id < b.doc_id;
    });
    return ranked;
}

} // namespace search::ranking
