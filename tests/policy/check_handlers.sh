#!/usr/bin/env bash
set -euo pipefail
ROOT=${1:-.}

bad='printf|fprintf|sprintf|snprintf|vprintf|vfprintf|vsnprintf'
bad="$bad|malloc|calloc|realloc|free|puts|fputs|fwrite|scanf|abort|exit"

scan() {
    local text="$1" where="$2" hits
    hits=$(grep -nE "\\b(${bad})[[:space:]]*\\(" <<< "$text" || true)
    if [ -n "$hits" ]; then
        echo "FAIL known-unsafe call in $where:"
        echo "$hits"
        exit 1
    fi
}

SIG="$ROOT/src/term/signals.c"
[ -f "$SIG" ] || { echo "OK   no signals.c yet"; exit 0; }
scan "$(cat "$SIG")" "src/term/signals.c"

SCR="$ROOT/src/term/screen.c"
if [ -f "$SCR" ]; then
    body=$(awk '/ash_screen_emergency_restore\(void\)/{f=1} f{print} f && /^}/{exit}' "$SCR")
    scan "$body" "ash_screen_emergency_restore in src/term/screen.c"
fi

echo "OK   handler path free of a blocklist of known-unsafe calls (not a completeness proof)"
