#include "query/search_service.hpp"

#include "index/tokenizer.hpp"
#include "search/ranking.hpp"
#include "search/snippet.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace search::query {
namespace {

// doc_id — відносний шлях у датасеті (OQ-18). Абсолютні шляхи та «..» відхиляємо, щоб
// читання не виходило за межі DATASET_DIR.
std::string read_document(const std::filesystem::path &dataset_dir, const std::string &doc_id) {
    const std::filesystem::path relative(doc_id);
    if (relative.empty() || relative.is_absolute()) {
        return {};
    }
    for (const auto &part : relative) {
        if (part == "..") {
            return {};
        }
    }
    // Симлінк або пристрій може вивести файл за межі датасету: перевіряємо канонічний шлях.
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(dataset_dir, ec);
    if (ec) {
        return {};
    }
    const std::filesystem::path full = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec) {
        return {};
    }
    const auto mismatch = std::mismatch(root.begin(), root.end(), full.begin(), full.end());
    if (mismatch.first != root.end() || !std::filesystem::is_regular_file(full, ec) || ec) {
        return {};
    }
    std::ifstream in(full, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

} // namespace

SearchService::SearchService(SnapshotProvider snapshot_provider, std::filesystem::path dataset_dir,
                             SearchOptions options)
    : snapshot_provider_(std::move(snapshot_provider)), dataset_dir_(std::move(dataset_dir)),
      options_(options) {}

std::vector<SearchHit> SearchService::search(std::string_view query) const {
    const std::shared_ptr<const index::SharedInvertedIndex> snapshot =
        snapshot_provider_ ? snapshot_provider_() : nullptr;
    if (!snapshot) {
        throw IndexNotReadyError();
    }
    const std::vector<std::string> terms = index::tokenize(query);
    if (terms.empty()) {
        return {};
    }

    std::vector<ranking::ScoredDocument> ranked = ranking::rank_tfidf(*snapshot, terms);
    if (ranked.size() > options_.top_k) {
        ranked.resize(options_.top_k);
    }

    std::vector<SearchHit> hits;
    hits.reserve(ranked.size());
    for (ranking::ScoredDocument &item : ranked) {
        const std::string text = read_document(dataset_dir_, item.doc_id);
        hits.push_back({std::move(item.doc_id), item.score,
                        snippet::make_snippet(text, terms, options_.snippet_max_chars)});
    }
    return hits;
}

} // namespace search::query
