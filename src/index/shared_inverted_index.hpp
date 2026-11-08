#pragma once

#include "index/inverted_index.hpp"

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace search::index {

// Потокобезпечна обгортка над InvertedIndex (NFR-004, NFR-005, CON-004).
//
// Стратегія синхронізації: один std::shared_mutex на весь індекс (див. ADR-003). Читання
// (пошук, статистика) — паралельні, запис (додавання документа) — ексклюзивний. Документ
// додається цілком під одним замком, тож постинги одного документа завжди неперервні.
//
// Guarded by mutex_: усі дані індексу. Методи повертають копії, а не вказівники на внутрішні
// списки: вказівник міг би стати недійсним після запису, що розширює таблицю термів.
// Статистика для ранжування, знята під одним замком (узгоджений знімок N і df).
struct TermPostings {
    std::string term;
    // Пари (doc_id, term_frequency) за постингами терма; порядок не гарантовано.
    std::vector<std::pair<std::string, std::size_t>> docs;
};

struct RankingStatistics {
    std::size_t document_count = 0;
    std::vector<TermPostings> terms; // у порядку вхідних термів
};

class SharedInvertedIndex {
  public:
    // Thread-safe.
    void add_document(std::string doc_id, const std::vector<std::string> &tokens);

    // Thread-safe.
    std::size_t document_count() const;
    std::size_t term_count() const;
    std::size_t term_frequency(std::string_view term, std::string_view doc_id) const;

    // Thread-safe: N і постинги всіх термів читаються під одним shared-замком за O(Σ df).
    // Терми без входжень дають порожній список постингів.
    RankingStatistics ranking_statistics(const std::vector<std::string> &terms) const;

    // doc_id документів, що містять терм, відсортовані лексично.
    std::vector<std::string> documents_containing(std::string_view term) const;

  private:
    mutable std::shared_mutex mutex_;
    InvertedIndex index_;
};

} // namespace search::index
