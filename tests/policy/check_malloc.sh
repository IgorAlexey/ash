#!/usr/bin/env bash
set -euo pipefail
ROOT=${1:-.}

fam='malloc|calloc|realloc|reallocarray|aligned_alloc|posix_memalign|memalign'
fam="$fam|valloc|pvalloc|strdup|strndup|asprintf|vasprintf|getline|getdelim|free"

hits=$(grep -rnE "\\b(${fam})[[:space:]]*\\(" "$ROOT/src" --include='*.c' 2>/dev/null \
       | grep -v '/src/base/arena\.c:' || true)
if [ -n "$hits" ]; then
    echo "FAIL heap allocation outside src/base/arena.c:"
    echo "$hits"
    exit 1
fi
echo "OK   allocation confined to arena"
