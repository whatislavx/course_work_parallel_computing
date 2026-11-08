#!/bin/sh
# TEST-DOC-08 (CON-004): зовнішні залежності лише з затвердженого списку (CLAUDE.md §5).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
approved='Boost|Threads|GTest|googletest'
# CMake-файли склеюємо в один рядок, щоб ловити виклики, розбиті на кілька рядків.
deps=$(find CMakeLists.txt src tests benchmarks -name 'CMakeLists.txt' -o -name '*.cmake' 2>/dev/null \
    | xargs cat | tr '\n' ' ' \
    | grep -oiE '(find_package|FetchContent_Declare|ExternalProject_Add|CPMAddPackage)[[:space:]]*\([[:space:]]*[A-Za-z_]+' \
    | sed -E 's/.*\([[:space:]]*//' | sort -u)
bad=$(printf '%s\n' "$deps" | grep -Ev "^(${approved})?$" || true)
if [ -n "$bad" ]; then
    printf '%s\n' "$bad"
    echo "FAIL: залежності поза затвердженим списком"
    exit 1
fi
echo "OK: залежності CMake: $(printf '%s ' $deps)"
