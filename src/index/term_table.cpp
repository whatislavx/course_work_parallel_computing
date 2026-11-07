#include "index/term_table.hpp"

#include <utility>

namespace search::index {
namespace {

constexpr std::size_t kInitialCapacity = 16;

} // namespace

TermTable::TermTable() : slots_(kInitialCapacity) {}

// FNV-1a, 64 біти.
std::size_t TermTable::hash(std::string_view term) {
    std::size_t h = 14695981039346656037ULL;
    for (const char c : term) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

// Індекс слота, де лежить терм, або перший порожній слот, куди його можна вставити.
// Розмір таблиці — степінь двійки, тому маска замість ділення.
std::size_t TermTable::probe(std::string_view term) const {
    const std::size_t mask = slots_.size() - 1;
    std::size_t i = hash(term) & mask;
    while (slots_[i].value != kNotFound && slots_[i].key != term) {
        i = (i + 1) & mask;
    }
    return i;
}

std::size_t TermTable::find(std::string_view term) const {
    const std::size_t i = probe(term);
    return slots_[i].value == kNotFound ? kNotFound : slots_[i].value;
}

std::size_t TermTable::find_or_insert(std::string_view term, std::size_t new_value) {
    if ((size_ + 1) * 2 > slots_.size()) {
        grow();
    }
    const std::size_t i = probe(term);
    if (slots_[i].value == kNotFound) {
        slots_[i].key = std::string(term);
        slots_[i].value = new_value;
        ++size_;
    }
    return slots_[i].value;
}

void TermTable::grow() {
    std::vector<Slot> old = std::move(slots_);
    slots_.assign(old.size() * 2, Slot{});
    for (Slot &slot : old) {
        if (slot.value != kNotFound) {
            const std::size_t i = probe(slot.key);
            slots_[i] = std::move(slot);
        }
    }
}

} // namespace search::index
