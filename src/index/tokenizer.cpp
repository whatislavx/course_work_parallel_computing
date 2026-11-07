#include "index/tokenizer.hpp"

#include <cstddef>

namespace search::index {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

struct DecodedChar {
    char32_t code_point;
    std::size_t length;
    bool valid;
};

DecodedChar decode_at(std::string_view s, std::size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        return {c, 1, true};
    }
    std::size_t length = 0;
    char32_t cp = 0;
    char32_t min = 0;
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
        return {kReplacement, 1, false};
    }
    if (i + length > s.size()) {
        return {kReplacement, 1, false};
    }
    for (std::size_t k = 1; k < length; ++k) {
        const auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            return {kReplacement, 1, false};
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return {kReplacement, 1, false};
    }
    return {cp, length, true};
}

bool is_token_char(char32_t cp) {
    if (cp < 0x80) {
        return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
    }
    if (cp == kReplacement) {
        return false;
    }
    // Латинська-1: керувальні і пунктуація, крім ª µ º (вони — літери).
    if (cp <= 0xBF) {
        return cp == 0xAA || cp == 0xB5 || cp == 0xBA;
    }
    if (cp == 0xD7 || cp == 0xF7) { // × ÷
        return false;
    }
    if (cp >= 0x2000 && cp <= 0x206F) { // загальна пунктуація, пробіли
        return false;
    }
    if (cp >= 0x3000 && cp <= 0x303F) { // CJK-пунктуація
        return false;
    }
    if ((cp >= 0x2190 && cp <= 0x2BFF) || (cp >= 0xFF00 && cp <= 0xFF0F) ||
        (cp >= 0x1F000 && cp <= 0x1FAFF)) { // стрілки, символи, повноширинна пунктуація, емодзі
        return false;
    }
    if (cp == 0xFEFF) {
        return false;
    }
    return true;
}

char32_t to_lower(char32_t cp) {
    if (cp >= 'A' && cp <= 'Z') {
        return cp + 0x20;
    }
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) { // латинська-1 верхній регістр
        return cp + 0x20;
    }
    // Латинська розширена-A: пари «парна верхня / непарна нижня», з винятками діапазонів.
    if (cp >= 0x100 && cp <= 0x137 && cp != 0x130 && (cp % 2 == 0)) {
        return cp + 1;
    }
    if (cp == 0x130) { // İ → i (спрощено, без комбінувального крапка-над-i)
        return 0x69;
    }
    if (cp >= 0x139 && cp <= 0x148 && (cp % 2 == 1)) {
        return cp + 1;
    }
    if (cp >= 0x14A && cp <= 0x177 && (cp % 2 == 0)) {
        return cp + 1;
    }
    if (cp == 0x178) { // Ÿ → ÿ
        return 0xFF;
    }
    if (cp >= 0x179 && cp <= 0x17E && (cp % 2 == 1)) {
        return cp + 1;
    }
    // Грецька: базові літери, літери з тонами і діерезисом.
    if (cp == 0x386) { // Ά
        return 0x3AC;
    }
    if (cp >= 0x388 && cp <= 0x38A) { // Έ Ή Ί
        return cp + 37;
    }
    if (cp == 0x38C) { // Ό
        return 0x3CC;
    }
    if (cp == 0x38E || cp == 0x38F) { // Ύ Ώ
        return cp + 63;
    }
    if (cp >= 0x391 && cp <= 0x3AB && cp != 0x3A2) { // Α–Ϋ без незайнятої 0x3A2
        return cp + 0x20;
    }
    if (cp == 0x3C2) { // фінальна сигма ς → σ, щоб ΟΔΟΣ і οδος мали однакові терми
        return 0x3C3;
    }
    if (cp >= 0x410 && cp <= 0x42F) { // кирилиця А–Я
        return cp + 0x20;
    }
    if (cp >= 0x400 && cp <= 0x40F) { // кирилиця Ѐ–Џ
        return cp + 0x50;
    }
    if (cp == 0x490) { // Ґ (українська)
        return 0x491;
    }
    return cp;
}

void append_utf8(std::string &out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    std::size_t i = 0;
    while (i < text.size()) {
        const DecodedChar ch = decode_at(text, i);
        if (ch.valid && is_token_char(ch.code_point)) {
            append_utf8(current, to_lower(ch.code_point));
        } else if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
        i += ch.length;
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

} // namespace search::index
