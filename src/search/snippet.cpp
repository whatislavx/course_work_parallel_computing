#include "search/snippet.hpp"

#include "index/tokenizer.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace search::snippet {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Межа символу UTF-8: продовження має вигляд 10xxxxxx.
std::size_t utf8_safe_end(std::string_view text, std::size_t end) {
    while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return end;
}

bool word_matches(std::string_view word, const std::vector<std::string> &terms) {
    for (const std::string &token : index::tokenize(word)) {
        if (std::find(terms.begin(), terms.end(), token) != terms.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string make_snippet(std::string_view text, const std::vector<std::string> &terms,
                         std::size_t max_chars) {
    if (terms.empty() || max_chars == 0) {
        return {};
    }
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && is_space(text[pos])) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && !is_space(text[pos])) {
            ++pos;
        }
        if (start == pos) {
            break;
        }
        const std::string_view word = text.substr(start, pos - start);
        if (!word_matches(word, terms)) {
            continue;
        }
        // Фрагмент має містити саме слово з входженням: слово довше за ліміт пропускаємо,
        // а пошук продовжуємо з наступних слів.
        if (word.size() > max_chars) {
            continue;
        }
        // Ліміт не переповнює позицію: обмежуємо залишком тексту.
        const std::size_t room = std::min(max_chars, text.size() - start);
        const std::size_t end = utf8_safe_end(text, start + room);
        std::string_view fragment = text.substr(start, end - start);
        // Обрізаємо до останнього пробілу лише якщо слово на межі не вміщується цілком.
        if (end < text.size() && !is_space(text[end])) {
            const std::size_t cut = fragment.find_last_of(" \t\n\r\v\f");
            if (cut != std::string_view::npos && cut > 0) {
                fragment = fragment.substr(0, cut);
            }
        }
        return std::string(fragment);
    }
    return {};
}

} // namespace search::snippet
