#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace search::index {

// Власна хеш-таблиця з відкритою адресацією (лінійне зондування) поверх std::vector (AD-02,
// CON-015). Ключ — терм, значення — індекс у зовнішньому сховищі постингів. Коефіцієнт заповнення ≤
// 1/2.
class TermTable {
  public:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    TermTable();

    // Повертає значення для терма або kNotFound.
    std::size_t find(std::string_view term) const;

    // Якщо терма немає, вставляє його зі значенням new_value і повертає його;
    // інакше повертає наявне значення.
    std::size_t find_or_insert(std::string_view term, std::size_t new_value);

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return slots_.size(); }

  private:
    struct Slot {
        std::string key;
        std::size_t value = kNotFound; // kNotFound означає порожній слот
    };

    static std::size_t hash(std::string_view term);
    std::size_t probe(std::string_view term) const;
    void grow();

    std::vector<Slot> slots_;
    std::size_t size_ = 0;
};

} // namespace search::index
