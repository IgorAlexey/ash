#!/usr/bin/env bash
set -euo pipefail
B=${1:-build}
CC=${CC:-cc}
STUB="$(cd "$(dirname "$0")" && pwd)/stub_main.c"
EXTRA_LIBS=${EXTRA_LIBS:-}

link_layer() {
    local label=$1; shift
    local wa=()
    local a
    for a in "$@"; do wa+=(-Wl,--whole-archive "$a" -Wl,--no-whole-archive); done
    if "$CC" -o "$B/link_$label" "$STUB" "${wa[@]}" $EXTRA_LIBS \
            2>"$B/link_$label.err"; then
        echo "OK   $label self-contained"
    else
        echo "FAIL $label references a higher layer:"
        grep -i 'undefined reference' "$B/link_$label.err" || cat "$B/link_$label.err"
        exit 1
    fi
}

link_layer ai   "$B/libash-ai.a"
link_layer core "$B/libash.a" "$B/libash-ai.a"
