#include "index/index_builder.hpp"

#include "pool/thread_pool.hpp"

#include <algorithm>
#include <exception>
#include <future>
#include <utility>

namespace search::index {
namespace {

// Скільки документів в одній задачі. Дрібні задачі дають кращу балансировку, великі —
// менше накладних витрат пулу; значення тимчасове, його уточнить вимірювання (TCK-029).
constexpr std::size_t kChunkSize = 32;

} // namespace

std::unique_ptr<SharedInvertedIndex> build_index(const std::vector<Document> &documents,
                                                 std::size_t threads,
                                                 const DocumentTokenizer &tokenizer) {
    if (threads == 0) {
        throw std::invalid_argument("build_index: потрібен принаймні один потік");
    }
    auto index = std::make_unique<SharedInvertedIndex>();
    const std::size_t queue_capacity = std::max<std::size_t>(threads * 4, 16);
    pool::ThreadPool workers(threads, queue_capacity);

    std::vector<std::future<void>> chunks;
    for (std::size_t begin = 0; begin < documents.size(); begin += kChunkSize) {
        const std::size_t end = std::min(documents.size(), begin + kChunkSize);
        chunks.push_back(workers.submit([&documents, &tokenizer, &index, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                const Document &doc = documents[i];
                index->add_document(doc.doc_id, tokenizer(doc.text));
            }
        }));
    }

    // Чекаємо всі порції, навіть після першого збою: задачі мають завершитися до повернення.
    // Прапорець окремо від тексту: виняток із порожнім what() теж є збоєм.
    bool failed = false;
    std::string first_error;
    for (std::future<void> &chunk : chunks) {
        try {
            chunk.get();
        } catch (const std::exception &e) {
            if (!failed) {
                failed = true;
                first_error = e.what();
            }
        } catch (...) {
            if (!failed) {
                failed = true;
                first_error = "невідомий виняток";
            }
        }
    }
    if (failed) {
        throw IndexBuildError("index_build_failed: " + first_error);
    }
    return index;
}

} // namespace search::index
