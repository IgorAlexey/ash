#!/usr/bin/env bash
set -euo pipefail
ROOT=${1:-.}

raw=$(grep -rnP '\x1b' "$ROOT/src" --include='*.c' --include='*.h' 2>/dev/null \
      | grep -v '/src/term/' || true)
txt=$(grep -rnE '\\(033|x1[bB]|e)' "$ROOT/src" --include='*.c' --include='*.h' 2>/dev/null \
      | grep -v '/src/term/' || true)

if [ -n "$raw" ] || [ -n "$txt" ]; then
    echo "FAIL escape bytes outside src/term/:"
    [ -n "$raw" ] && echo "$raw"
    [ -n "$txt" ] && echo "$txt"
    exit 1
fi
echo "OK   no escape emission outside src/term/"
