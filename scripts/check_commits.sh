#!/bin/sh
# TEST-DOC-03 (NFR-016): теми комітів відповідають Conventional Commits (CLAUDE.md, git.md §4).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
types='feat|fix|docs|test|refactor|perf|build|ci|chore|style'
pattern="^(${types})(\([a-z-]+\))?!?: .{1,72}$"
bad=$(git log --no-merges --format='%s' | grep -Ev "$pattern" || true)
if [ -n "$bad" ]; then
    printf '%s\n' "$bad"
    echo "FAIL: коміти не відповідають Conventional Commits"
    exit 1
fi
echo "OK: усі коміти (крім merge) відповідають Conventional Commits"
