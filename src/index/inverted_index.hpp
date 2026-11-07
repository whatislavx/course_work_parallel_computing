#pragma once

#include "index/term_table.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace search::index {

// Запис постингу: документ (індекс у списку документів) і частота терма в ньому.
struct Posting {
    std::size_t doc_index = 0;
    std::size_t term_frequency = 0;

    friend bool operator==(const Posting &, const Posting &) = default;
};

// Однопотоковий інвертований індекс: терм → список постингів за документами.
// Потокобезпека — у TCK-010.
class InvertedIndex {
  public:
    // Додає документ з токенами (результат tokenize). Порожній список токенів означає
    // документ без постингів, але він рахується (TEST-UT-04).
    void add_document(std::string doc_id, const std::vector<std::string> &tokens);

    std::size_t document_count() const { return doc_ids_.size(); }
    std::size_t term_count() const { return table_.size(); }

    // Постинги терма або nullptr, якщо терма немає.
    const std::vector<Posting> *postings(std::string_view term) const;

    // Частота терма в документі; 0, якщо терма чи документа немає.
    std::size_t term_frequency(std::string_view term, std::string_view doc_id) const;

    const std::string &doc_id(std::size_t doc_index) const { return doc_ids_.at(doc_index); }

  private:
    TermTable table_;
    std::vector<std::vector<Posting>> lists_;
    std::vector<std::string> doc_ids_;
};

} // namespace search::index
