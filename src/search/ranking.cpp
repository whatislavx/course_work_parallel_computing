#include "search/ranking.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace search::ranking {

std::vector<ScoredDocument> rank_tfidf(const index::SharedInvertedIndex &index,
                                       std::vector<std::string> terms) {
    // Унікальні терми: повторний терм запиту не має подвоювати внесок.
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    // Один знімок під замком: N і df узгоджені між собою навіть за паралельного запису.
    const index::RankingStatistics stats = index.ranking_statistics(terms);
    if (stats.document_count == 0) {
        return {};
    }
    const auto total_documents = static_cast<double>(stats.document_count);

    std::vector<ScoredDocument> contributions;
    for (const index::TermPostings &term : stats.terms) {
        if (term.docs.empty()) {
            continue;
        }
        const double idf = std::log(total_documents / static_cast<double>(term.docs.size()));
        for (const auto &[doc_id, tf] : term.docs) {
            contributions.push_back({doc_id, static_cast<double>(tf) * idf});
        }
    }

    // stable_sort зберігає порядок внесків одного документа (за порядком термів), тож
    // підсумовування відбувається в детермінованому порядку.
    std::stable_sort(
        contributions.begin(), contributions.end(),
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
