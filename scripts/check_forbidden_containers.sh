#!/bin/sh
# TEST-DOC-10 (CON-015, AD-10): модулі індексу і пулу потоків не підключають заборонені
# контейнери і std::hash. Коментарі (рядки, що починаються з //) ігноруються.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
# Директива з довільними пробілами після '#'; ім'я типу за межею ідентифікатора (у т.ч. кінець рядка).
pattern='#[[:space:]]*include[[:space:]]*<(unordered_map|map|set|unordered_set)>|std::(unordered_map|unordered_set|map|multimap|set|multiset|hash)([^A-Za-z0-9_]|$)'

found=0
for dir in "$root/src/index" "$root/src/pool"; do
    hits=$(grep -rEn "$pattern" "$dir" | grep -vE ':[0-9]+:[[:space:]]*//' || true)
    if [ -n "$hits" ]; then
        printf '%s\n' "$hits"
        found=1
    fi
done

if [ "$found" -ne 0 ]; then
    echo "FAIL: заборонені контейнери знайдено"
    exit 1
fi
echo "OK: заборонених контейнерів у модулях індексу і пулу не знайдено"
