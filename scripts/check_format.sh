#!/bin/sh
# TEST-DOC-02 (NFR-015): форматування C++ за clang-format (Google). Потрібен clang-format.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
files=$(git ls-files '*.cpp' '*.hpp' '*.cc' '*.h' | grep -v '^build/')
if [ -z "$files" ]; then
    echo 'FAIL: немає C++-файлів для перевірки'
    exit 1
fi
# shellcheck disable=SC2086
clang-format --dry-run --Werror $files
echo "OK: форматування C++ відповідає clang-format"
