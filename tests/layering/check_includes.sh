#!/usr/bin/env bash
set -euo pipefail
ROOT=${1:-.}
fail=0

forbid() {
    local dir=$1; shift
    local target hits
    for target in "$@"; do
        hits=$(grep -rEl "include[[:space:]]*[\"<]ash/${target}[./]" \
               "$ROOT/src/$dir" "$ROOT/include/ash/$dir" 2>/dev/null || true)
        if [ -n "$hits" ]; then
            echo "FAIL $dir includes ash/$target:"
            echo "$hits"
            fail=1
        fi
    done
}

forbid base ai core term app ext
forbid ai   core term app ext
forbid core      term app ext
forbid ext  ai app

[ "$fail" -eq 0 ] || exit 1
echo "OK   include layering clean"
