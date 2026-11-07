#include "index/document_source.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace search::index {
namespace fs = std::filesystem;
namespace {

std::optional<std::string> read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string content;
    std::array<char, 4096> buffer{};
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
        content.append(buffer.data(), static_cast<std::size_t>(in.gcount()));
    }
    if (in.bad()) { // помилка вводу-виводу посеред файлу: не віддаємо обрізаний вміст
        return std::nullopt;
    }
    return content;
}

} // namespace

bool is_valid_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t min = 0;
        char32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            length = 2;
            cp = c & 0x1F;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            length = 3;
            cp = c & 0x0F;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            length = 4;
            cp = c & 0x07;
            min = 0x10000;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
}

DiscoveryResult load_documents(const fs::path &dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) {
        throw DatasetError("dataset directory is missing or not a directory: " + dir.string());
    }

    // Корінь має бути читабельним: інакше обхід мовчки дав би 0 документів (AC-003.3).
    {
        fs::directory_iterator probe(dir, ec);
        if (ec) {
            throw DatasetError("dataset directory is not readable: " + dir.string());
        }
    }

    DiscoveryResult result;
    // skip_permission_denied лишається як страховка; недоступні підкаталоги перевіряються
    // явно нижче, щоб запис про пропуск потрапив у skipped (FR-003, OQ-19).
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        throw DatasetError("dataset traversal failed: " + ec.message());
    }
    const fs::recursive_directory_iterator end;
    while (it != end) {
        const fs::directory_entry entry = *it;
        // lexically_relative не розкриває симлінки, на відміну від relative (OQ-18).
        std::string doc_id = entry.path().lexically_relative(dir).generic_string();
        std::error_code entry_ec;
        if (!entry.is_symlink(entry_ec) && entry.is_directory(entry_ec) && !entry_ec) {
            // Звичайний підкаталог: якщо його не можна відкрити, пропускаємо з причиною і не
            // заходимо.
            std::error_code open_ec;
            fs::directory_iterator probe(entry.path(), open_ec);
            if (open_ec) {
                result.skipped.push_back({std::move(doc_id), "unreadable directory"});
                it.disable_recursion_pending();
                it.increment(ec);
                if (ec) {
                    throw DatasetError("dataset traversal failed: " + ec.message());
                }
                continue;
            }
        }
        entry_ec.clear();
        const bool regular = entry.is_regular_file(entry_ec);
        if (entry_ec) {
            result.skipped.push_back({std::move(doc_id), "unreadable"});
        } else if (regular) {
            auto content = read_file(entry.path());
            if (!content) {
                result.skipped.push_back({std::move(doc_id), "unreadable"});
            } else if (!is_valid_utf8(*content)) {
                result.skipped.push_back({std::move(doc_id), "invalid UTF-8"});
            } else {
                result.documents.push_back({std::move(doc_id), std::move(*content)});
            }
        }
        it.increment(ec);
        if (ec) {
            throw DatasetError("dataset traversal failed: " + ec.message());
        }
    }

    std::sort(result.documents.begin(), result.documents.end(),
              [](const Document &a, const Document &b) { return a.doc_id < b.doc_id; });
    std::sort(result.skipped.begin(), result.skipped.end(),
              [](const SkippedFile &a, const SkippedFile &b) { return a.doc_id < b.doc_id; });
    return result;
}

} // namespace search::index
