#!/bin/sh
set -eu

# Live smoke test for the deepseek (openai-compat) provider path.
# NOT part of ctest: it spends real credits. Run it by hand:
#
#   DEEPSEEK_API_KEY=... scripts/ai_smoke.sh
#
# It builds the smoke_deepseek target, hits the API for a one-sentence
# completion, and prints the streamed text plus parsed token usage/cost.

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${ASH_BUILD_DIR:-$root/build}

if [ -z "${DEEPSEEK_API_KEY:-}" ]; then
    echo "ai_smoke: set DEEPSEEK_API_KEY" >&2
    exit 2
fi

cmake -S "$root" -B "$build" >/dev/null
cmake --build "$build" --target smoke_deepseek >/dev/null

exec "$build/smoke_deepseek"
