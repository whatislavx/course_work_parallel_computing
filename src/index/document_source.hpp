#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace search::index {

// Каталог датасету відсутній або недоступний; побудова завершується помилкою (FR-003, AC-003.3).
class DatasetError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct Document {
    std::string doc_id; // відносний шлях з «/» як роздільником (OQ-18)
    std::string text;
};

struct SkippedFile {
    std::string doc_id;
    std::string reason; // «unreadable» або «invalid UTF-8» (OQ-19)
};

struct DiscoveryResult {
    std::vector<Document> documents; // відсортовано за doc_id
    std::vector<SkippedFile> skipped;
};

// Рекурсивно знаходить звичайні файли каталогу, читає їх як UTF-8 і повертає документи.
// Нечитабельні файли і некоректний UTF-8 пропускаються з причиною; порожній файл — документ
// (OQ-22).
DiscoveryResult load_documents(const std::filesystem::path &dir);

bool is_valid_utf8(std::string_view text);

} // namespace search::index
