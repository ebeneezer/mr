#!/usr/bin/env bash
set -euo pipefail

base_dir="${TMP_BASE_DIR:-${TMPDIR:-/tmp}}"
tmpdir="$(mktemp -d "${base_dir%/}/mr-compile.XXXXXX")"

cleanup() {
    rm -rf "$tmpdir"
}

trap cleanup EXIT INT TERM HUP

export TMPDIR="$tmpdir"
export TMP="$tmpdir"
export TEMP="$tmpdir"

use_ccache="${MR_USE_CCACHE:-0}"
ccache_bin="${MR_CCACHE:-ccache}"
compile_step=0

for arg in "$@"; do
    if [ "$arg" = "-c" ]; then
        compile_step=1
        break
    fi
done

if [ "$use_ccache" = "1" ] && [ "$compile_step" = "1" ] && command -v "$ccache_bin" >/dev/null 2>&1; then
    "$ccache_bin" "$@"
    exit $?
fi

"$@"
