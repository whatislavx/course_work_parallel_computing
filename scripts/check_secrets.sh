#!/bin/sh
# TEST-DOC-06/07 (NFR-021): у відстежуваних файлах не має бути секретів. Евристика, а не повний аудит.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
pattern='AKIA[0-9A-Z]{16}|-----BEGIN [A-Z ]*PRIVATE KEY-----|(api[_-]?key|secret|token|passwd|password)[[:space:]]*[=:][[:space:]]*["'\'']?[A-Za-z0-9_-]*[0-9][A-Za-z0-9_-]{7,}'
hits=$(git ls-files -z | xargs -0 grep -EniI "$pattern" -- | grep -v '^tests/fixtures/' | grep -v '^\.env\.example:' || true)
if [ -n "$hits" ]; then
    printf '%s\n' "$hits"
    echo "FAIL: можливі секрети у відстежуваних файлах"
    exit 1
fi
echo "OK: відстежувані файли не містять ознак секретів"
