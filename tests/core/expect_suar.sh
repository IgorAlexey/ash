#!/usr/bin/env bash
set -uo pipefail
out=$("$1" 2>&1)
if grep -q 'stack-use-after-return' <<< "$out"; then
    echo "OK   stack-use-after-return detected across a fiber round-trip"
    exit 0
fi
echo "FAIL expected a stack-use-after-return trap; got:"
echo "$out" | tail -8
exit 1
