#!/usr/bin/env bash
set -euo pipefail
ROOT=${1:-.}

hits=$(grep -rnE '\bdl(open|sym|close|error|mopen)[[:space:]]*\(' \
       "$ROOT/src" --include='*.c' 2>/dev/null \
       | grep -v '/src/ext/loader\.c:' || true)
if [ -n "$hits" ]; then
    echo "FAIL dynamic loading outside src/ext/loader.c:"
    echo "$hits"
    exit 1
fi
echo "OK   dynamic loading confined to ext/loader"
