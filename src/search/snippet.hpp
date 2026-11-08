#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace search::snippet {

// Фрагмент тексту, що починається з першого слова, яке містить хоча б один терм множини
// (після токенізації, без регістру). Довжина фрагмента не перевищує max_chars байтів і не
// розрізає UTF-8 символ. Якщо такого слова немає або воно не вміщується в max_chars,
// повертається порожній рядок.
//
// Правило вибору фрагмента — ПЛЕЙСХОЛДЕР до рішення OQ-05 (пропозиція P: фрагмент навколо
// першого входження терма). Розмір max_chars — ПЛЕЙСХОЛДЕР для SNIPPET_MAX_CHARS.
std::string make_snippet(std::string_view text, const std::vector<std::string> &terms,
                         std::size_t max_chars);

} // namespace search::snippet
