#include "index/inverted_index.hpp"

#include <utility>

namespace search::index {

void InvertedIndex::add_document(std::string doc_id, const std::vector<std::string> &tokens) {
    const std::size_t doc = doc_ids_.size();
    doc_ids_.push_back(std::move(doc_id));

    for (const std::string &token : tokens) {
        const std::size_t candidate = lists_.size();
        const std::size_t slot = table_.find_or_insert(token, candidate);
        if (slot == candidate) {
            lists_.emplace_back();
        }
        std::vector<Posting> &list = lists_[slot];
        // Документи додаються послідовно, тож повтор терма в поточному документі — останній запис.
        if (!list.empty() && list.back().doc_index == doc) {
            ++list.back().term_frequency;
        } else {
            list.push_back(Posting{doc, 1});
        }
    }
}

const std::vector<Posting> *InvertedIndex::postings(std::string_view term) const {
    const std::size_t slot = table_.find(term);
    return slot == TermTable::kNotFound ? nullptr : &lists_[slot];
}

std::size_t InvertedIndex::term_frequency(std::string_view term, std::string_view doc_id) const {
    const std::vector<Posting> *list = postings(term);
    if (list == nullptr) {
        return 0;
    }
    for (const Posting &posting : *list) {
        if (doc_ids_[posting.doc_index] == doc_id) {
            return posting.term_frequency;
        }
    }
    return 0;
}

} // namespace search::index
