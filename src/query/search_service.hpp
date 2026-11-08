#pragma once

#include "index/shared_inverted_index.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace search::query {

struct SearchHit {
    std::string doc_id;
    double score = 0.0;
    std::string snippet;
};

struct SearchOptions {
    std::size_t top_k = 10;              // SEARCH_TOP_K (OQ-06, тимчасове)
    std::size_t snippet_max_chars = 200; // SNIPPET_MAX_CHARS (OQ-05, тимчасове)
};

// Немає активного індексу (409 index_not_ready, EP-03).
class IndexNotReadyError : public std::runtime_error {
  public:
    IndexNotReadyError() : std::runtime_error("index is not ready") {}
};

// Пошук за ключовими словами: OR-семантика з ранжуванням TF-IDF (OQ-03, пропозиція).
// Тексти для snippet перечитуються з dataset_dir (рішення BL-02: пам'ять не подвоюємо);
// лише для top-K результатів. Якщо файл недоступний, snippet порожній, а пошук не падає.
//
// Thread-safe: читає знімок індексу через snapshot_provider_ і не змінює стан.
class SearchService {
  public:
    using SnapshotProvider = std::function<std::shared_ptr<const index::SharedInvertedIndex>()>;

    SearchService(SnapshotProvider snapshot_provider, std::filesystem::path dataset_dir,
                  SearchOptions options);

    // Кидає IndexNotReadyError, якщо активного індексу немає. Порожній після токенізації
    // запит дає порожній результат.
    std::vector<SearchHit> search(std::string_view query) const;

  private:
    SnapshotProvider snapshot_provider_;
    std::filesystem::path dataset_dir_;
    SearchOptions options_;
};

} // namespace search::query
