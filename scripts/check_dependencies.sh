#!/bin/sh
# TEST-DOC-08 (CON-004): зовнішні залежності лише з затвердженого списку (CLAUDE.md §5).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
approved='Boost|Threads|GTest|googletest'
deps=$(grep -rhoE '(find_package|FetchContent_Declare)\([A-Za-z_]+' CMakeLists.txt src tests benchmarks 2>/dev/null \
    | sed -E 's/.*\(//' | sort -u)
bad=$(printf '%s\n' "$deps" | grep -Ev "^(${approved})?$" || true)
if [ -n "$bad" ]; then
    printf '%s\n' "$bad"
    echo "FAIL: залежності поза затвердженим списком"
    exit 1
fi
echo "OK: залежності CMake: $(printf '%s ' $deps)"
