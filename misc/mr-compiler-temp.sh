#!/usr/bin/env bash
set -euo pipefail

base_dir="${TMP_BASE_DIR:-/dev/shm}"
tmpdir="$(mktemp -d "${base_dir%/}/mr-compile.XXXXXX")"

cleanup() {
    rm -rf "$tmpdir"
}

trap cleanup EXIT INT TERM HUP

export TMPDIR="$tmpdir"
export TMP="$tmpdir"
export TEMP="$tmpdir"

"$@"
