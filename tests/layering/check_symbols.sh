#!/usr/bin/env bash
set -euo pipefail
B=${1:-build}

syms_def() {
    nm -g --defined-only "$@" 2>/dev/null \
        | awk 'NF>=3 && length($2)==1 {print $3}' | sort -u
}
ext_refs() {
    comm -23 \
        <(nm -g --undefined-only "$1" 2>/dev/null | awk '$1=="U" {print $2}' | sort -u) \
        <(syms_def "$1")
}

ai_syms=$(syms_def "$B/libash-ai.a")
[ -n "$ai_syms" ] || {
    echo "FAIL libash-ai.a reports zero symbols (LTO or empty archive)"; exit 1; }

mid_def=$(syms_def "$B/libash.a")
mapfile -t top_obj < <(find "$B/CMakeFiles/ash.dir" -name '*.o')
[ "${#top_obj[@]}" -gt 0 ] || {
    echo "FAIL no top-layer objects under $B/CMakeFiles/ash.dir (build layout changed)"
    exit 1
}
top_def=$(syms_def "${top_obj[@]}")

up=$(comm -12 <(ext_refs "$B/libash-ai.a") <(printf '%s\n' "$mid_def" "$top_def" | sort -u))
[ -z "$up" ] || { echo "FAIL libash-ai references upward:"; echo "$up"; exit 1; }

up=$(comm -12 <(ext_refs "$B/libash.a") <(printf '%s\n' "$top_def" | sort -u))
[ -z "$up" ] || { echo "FAIL libash references top:"; echo "$up"; exit 1; }

echo "OK   symbol layering clean"
