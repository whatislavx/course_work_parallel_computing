#pragma once

#include "index/document_source.hpp"
#include "index/shared_inverted_index.hpp"
#include "index/tokenizer.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace search::index {

// Аварійне завершення задачі індексації (FR-042). Індекс у такому разі не створюється і не
// активується; попередній індекс лишається недоторканим (BDD-IDX-12).
class IndexBuildError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Побудову скасовано кооперативно (TCK-039, OQ-32): індекс не створюється і не активується.
class BuildCancelledError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Токенізатор для побудови. За замовчуванням — tokenize; тести підставляють двійник для збою.
using DocumentTokenizer = std::function<std::vector<std::string>(std::string_view)>;

// Паралельна побудова індексу з threads робочими потоками (FR-004, FR-006, NFR-005).
// Документи діляться на порції; кожна порція — задача пулу, що токенізує документи і
// додає їх до SharedInvertedIndex. Кидає std::invalid_argument при threads == 0 і
// IndexBuildError, якщо будь-яка задача завершилась винятком, і BuildCancelledError, якщо cancel
// (за наявності) встановлено до завершення. Cancel перевіряється на початку кожної порції, тож
// скасування зупиняє роботу не пізніше ніж після однієї порції. Усі задачі завершуються до
// повернення або кидання, тож жодна задача не звертається до вже звільнених даних.
std::unique_ptr<SharedInvertedIndex> build_index(const std::vector<Document> &documents,
                                                 std::size_t threads,
                                                 const DocumentTokenizer &tokenizer = tokenize,
                                                 const std::atomic<bool> *cancel = nullptr);

} // namespace search::index
