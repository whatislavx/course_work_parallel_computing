#!/bin/sh
# TEST-DOC-10 (CON-015, AD-10): модулі індексу не підключають заборонені контейнери і std::hash.
# Черга пулу потоків (TCK-009) додасться до перевірки разом з каталогом src/pool.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
pattern='#include[[:space:]]*<(unordered_map|map|set|unordered_set)>|std::(unordered_map|unordered_set|map|multimap|set|multiset|hash)[^a-z_]'

found=0
for dir in "$root/src/index"; do
    if grep -rEn "$pattern" "$dir"; then
        found=1
    fi
done

if [ "$found" -ne 0 ]; then
    echo "FAIL: заборонені контейнери знайдено"
    exit 1
fi
echo "OK: заборонених контейнерів у модулях індексу не знайдено"
