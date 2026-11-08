#pragma once

#include "index/shared_inverted_index.hpp"

#include <string>
#include <vector>

namespace search::ranking {

struct ScoredDocument {
    std::string doc_id;
    double score = 0.0;
};

// Ранжування TF-IDF без нормалізації (пропозиція P для OQ-04; ПЛЕЙСХОЛДЕР до рішення власника).
//   score(d) = Σ_t tf(t, d) · ln(N / df(t)) за унікальними термами запиту.
// Терми без входжень ігноруються; терм з df == N дає внесок 0. Результат: score спадно,
// при рівності — doc_id лексично (детермінований tie-break, TCK-015).
// Thread-safe, якщо index thread-safe (читає через SharedInvertedIndex).
std::vector<ScoredDocument> rank_tfidf(const index::SharedInvertedIndex &index,
                                       std::vector<std::string> terms);

} // namespace search::ranking
