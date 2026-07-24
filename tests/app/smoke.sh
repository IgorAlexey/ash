#!/usr/bin/env bash
set -uo pipefail
ASH=${1:?usage: smoke.sh /path/to/ash}
HERE=$(cd "$(dirname "$0")" && pwd)
fail=0

# Hermetic HOME: a stray ~/.ash/settings.json (provider/model pins) must not
# skew autodetect or startup assertions on a developer machine.
SCRATCH=$(mktemp -d) || exit 1
trap 'rm -rf "$SCRATCH"' EXIT
export HOME="$SCRATCH"

out=$("$ASH" --version)
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: --version exit $rc"; fail=1; }
printf '%s\n' "$out" | grep -q '^ash ' || { echo "FAIL: --version output: $out"; fail=1; }

out=$(env -u ASH_API_KEY -u ANTHROPIC_API_KEY -u DEEPSEEK_API_KEY -u OPENAI_API_KEY \
      "$ASH" </dev/null 2>&1 >/dev/null)
rc=$?
[ $rc -eq 3 ] || { echo "FAIL: keyless startup should not gate, expected the tty guard (3), got $rc"; fail=1; }
printf '%s\n' "$out" | grep -q 'no API key' || { echo "FAIL: keyless startup should warn on stderr"; fail=1; }

out=$(env -u ANTHROPIC_API_KEY -u DEEPSEEK_API_KEY -u OPENAI_API_KEY ASH_API_KEY=x \
      "$ASH" </dev/null 2>&1 >/dev/null)
rc=$?
{ [ $rc -eq 3 ] && printf '%s\n' "$out" | grep -q 'no API key'; } ||
    { echo "FAIL: generic ASH_API_KEY must not satisfy the provider key check"; fail=1; }

out=$(env -u ANTHROPIC_API_KEY -u OPENAI_API_KEY DEEPSEEK_API_KEY=x \
      "$ASH" </dev/null 2>&1 >/dev/null)
rc=$?
[ $rc -eq 3 ] || { echo "FAIL: lone DEEPSEEK_API_KEY should autodetect and reach the tty guard, got $rc"; fail=1; }
printf '%s\n' "$out" | grep -q 'no API key' && { echo "FAIL: autodetect must accept a lone DEEPSEEK_API_KEY"; fail=1; }

ANTHROPIC_API_KEY=x "$ASH" </dev/null >/dev/null 2>&1
[ $? -eq 3 ] || { echo "FAIL: non-tty stdin should exit 3"; fail=1; }

if command -v python3 >/dev/null 2>&1; then
    if command -v strace >/dev/null 2>&1; then
        python3 "$HERE/nocfg.py" "$ASH" strace || fail=1
    else
        python3 "$HERE/nocfg.py" "$ASH" || fail=1
    fi
else
    echo "SKIP: python3 unavailable, cannot run the pty startup check"
fi

[ "$fail" -eq 0 ] || exit 1
echo "OK   ash smoke: version, key guard, tty guard, pty startup, no config"
