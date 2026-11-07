#include "index/shared_inverted_index.hpp"

#include <algorithm>
#include <mutex>

namespace search::index {

void SharedInvertedIndex::add_document(std::string doc_id, const std::vector<std::string> &tokens) {
    std::unique_lock lock(mutex_);
    index_.add_document(std::move(doc_id), tokens);
}

std::size_t SharedInvertedIndex::document_count() const {
    std::shared_lock lock(mutex_);
    return index_.document_count();
}

std::size_t SharedInvertedIndex::term_count() const {
    std::shared_lock lock(mutex_);
    return index_.term_count();
}

std::size_t SharedInvertedIndex::term_frequency(std::string_view term,
                                                std::string_view doc_id) const {
    std::shared_lock lock(mutex_);
    return index_.term_frequency(term, doc_id);
}

std::vector<std::string> SharedInvertedIndex::documents_containing(std::string_view term) const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    const std::vector<Posting> *list = index_.postings(term);
    if (list == nullptr) {
        return result;
    }
    result.reserve(list->size());
    for (const Posting &posting : *list) {
        result.push_back(index_.doc_id(posting.doc_index));
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace search::index
