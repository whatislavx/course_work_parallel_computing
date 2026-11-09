#include "search/title.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace search::title {
namespace {

bool is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// Перший рядок, у якому є хоча б один непробільний символ.
std::string_view first_content_line(std::string_view text) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        for (const char c : line) {
            if (!is_blank(c)) {
                return line;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return {};
}

// Стискає довільні пробіли в один пробіл і прибирає пробіли на краях.
std::string normalize_spaces(std::string_view line) {
    std::string result;
    result.reserve(line.size());
    bool pending_space = false;
    for (const char c : line) {
        if (is_blank(c)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
            pending_space = false;
        }
        result.push_back(c);
    }
    return result;
}

// Індекс, до якого можна обрізати рядок, не розриваючи UTF-8 символ.
std::size_t utf8_boundary(std::string_view s, std::size_t limit) {
    if (limit >= s.size()) {
        return s.size();
    }
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return cut;
}

} // namespace

std::string make_title(std::string_view text, std::size_t max_chars) {
    if (max_chars == 0) {
        return {};
    }
    const std::string title = normalize_spaces(first_content_line(text));
    if (title.size() <= max_chars) {
        return title;
    }
    const std::size_t cut = utf8_boundary(title, max_chars);
    // Останній пробіл у межах cut: title[space] не потрапляє до результату, слово залишається
    // цілим.
    const std::size_t space = title.rfind(' ', cut);
    // Обрізаємо на пробілі, якщо він є і слово вміщується повністю; інакше — на межі символу.
    const std::size_t end = (space != std::string::npos && space > 0) ? space : cut;
    std::string result = title.substr(0, end);
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::string make_title_from_doc_id(std::string_view doc_id) {
    constexpr std::string_view kPrefix = "doc";
    constexpr std::string_view kSuffix = ".txt";
    if (doc_id.size() <= kPrefix.size() + kSuffix.size() ||
        doc_id.substr(0, kPrefix.size()) != kPrefix ||
        doc_id.substr(doc_id.size() - kSuffix.size()) != kSuffix) {
        return {};
    }
    const std::string_view digits =
        doc_id.substr(kPrefix.size(), doc_id.size() - kPrefix.size() - kSuffix.size());
    // 18 цифр гарантовано вміщуються в uint64_t.
    if (digits.size() > 18) {
        return {};
    }
    std::uint64_t number = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return {};
        }
        number = number * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return "Документ " + std::to_string(number);
}

} // namespace search::title
