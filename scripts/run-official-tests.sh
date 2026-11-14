#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Fetch the official assignment, replace only its implementation, and run its checker.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="$ROOT/.upstream-tests"
TASK="$UPSTREAM/content/assignments/memory-allocator"

for command in git gcc make python3 ltrace; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Missing dependency: %s\n' "$command" >&2
        printf 'Install with: sudo apt install -y git build-essential python3 ltrace valgrind\n' >&2
        exit 1
    fi
done

if [[ ! -d "$UPSTREAM/.git" ]]; then
    git clone --quiet --depth 1 --filter=blob:none --sparse \
        https://github.com/open-education-hub/operating-systems.git "$UPSTREAM"
    git -C "$UPSTREAM" sparse-checkout set content/assignments/memory-allocator
fi

if [[ ! -f "$TASK/src/Makefile" ]]; then
    echo 'Unable to find the official assignment in the upstream checkout' >&2
    exit 1
fi

cp "$ROOT/src/osmem.c" "$TASK/src/osmem.c"
make -C "$TASK/src" clean all
make -C "$TASK/tests" clean_snippets snippets

printf '\n===== Official Open Education Hub checker =====\n'
cd "$TASK/tests"
if [[ $# -eq 0 ]]; then
    set -- -d
fi
python3 run_tests.py "$@" | tee "$ROOT/official-tests.log"

if [[ "$*" == *'-v'* ]] || [[ "$*" == *'test-'* ]]; then
    printf '\nTest results saved in %s\n' "$ROOT/official-tests.log"
    exit 0
fi

if ! grep -Eq 'Total:[[:space:]]+100/100' "$ROOT/official-tests.log"; then
    echo 'The official checker did not report 100/100. Inspect official-tests.log.' >&2
    exit 1
fi
printf '\nThe official checker reported 100/100.\n'
