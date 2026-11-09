#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace search::title {

// Назва документа: перший непорожній рядок тексту з нормалізованими пробілами.
// Довжина не перевищує max_chars байтів; якщо рядок довший, він обрізається на межі слова
// (або на межі UTF-8 символу, якщо слова немає). Якщо змістовного рядка немає, повертається
// порожній рядок — викликач підставляє doc_id.
//
// Детерміновано: та сама вхідна строка завжди дає ту саму назву.
std::string make_title(std::string_view text, std::size_t max_chars);

// Назва з імені файлу виду "doc<цифри>.txt": "Документ <N>", N — число з імені без провідних нулів.
// Для будь-якого іншого імені повертає порожній рядок.
std::string make_title_from_doc_id(std::string_view doc_id);

} // namespace search::title
