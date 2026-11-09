#include "search/snippet.hpp"

#include "index/tokenizer.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace search::snippet {
namespace {

// «…» (U+2026, 3 байти UTF-8): фрагмент починається всередині тексту, а не з його початку.
constexpr std::string_view kLeadingEllipsis = "\xE2\x80\xA6";

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

// Початок слова, що закінчується перед позицією from (пробіли пропускаються).
// npos, якщо перед from слів немає.
std::size_t previous_word_start(std::string_view text, std::size_t from) {
    std::size_t end = from;
    while (end > 0 && is_space(text[end - 1])) {
        --end;
    }
    if (end == 0) {
        return std::string_view::npos;
    }
    std::size_t start = end;
    while (start > 0 && !is_space(text[start - 1])) {
        --start;
    }
    return start;
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
        // Контекст: до чверті ліміту слів перед входженням, цілими словами. Слово запиту має
        // вміститися разом із контекстом і «…», інакше контекст відкидається.
        const std::size_t lead_budget = max_chars / 4;
        const auto needed = [&](std::size_t from) {
            return (start - from) + word.size() + (from > 0 ? kLeadingEllipsis.size() : 0);
        };
        std::size_t window_start = start;
        for (;;) {
            const std::size_t prev = previous_word_start(text, window_start);
            if (prev == std::string_view::npos || start - prev > lead_budget ||
                needed(prev) > max_chars) {
                break;
            }
            window_start = prev;
        }
        const bool with_ellipsis = window_start > 0 && needed(window_start) <= max_chars;
        const std::size_t budget = with_ellipsis ? max_chars - kLeadingEllipsis.size() : max_chars;
        // Ліміт не переповнює позицію: обмежуємо залишком тексту.
        const std::size_t room = std::min(budget, text.size() - window_start);
        const std::size_t end = utf8_safe_end(text, window_start + room);
        std::string_view fragment = text.substr(window_start, end - window_start);
        // Обрізаємо до останнього пробілу лише якщо слово на межі не вміщується цілком.
        if (end < text.size() && !is_space(text[end])) {
            const std::size_t cut = fragment.find_last_of(" \t\n\r\v\f");
            if (cut != std::string_view::npos && cut > 0) {
                fragment = fragment.substr(0, cut);
            }
        }
        // Хвостові пробіли (наприклад, перенос рядка в кінці файлу) не належать фрагменту.
        const std::size_t last = fragment.find_last_not_of(" \t\n\r\v\f");
        fragment =
            last == std::string_view::npos ? std::string_view{} : fragment.substr(0, last + 1);
        std::string result = with_ellipsis ? std::string(kLeadingEllipsis) : std::string{};
        result.append(fragment);
        return result;
    }
    return {};
}

} // namespace search::snippet
