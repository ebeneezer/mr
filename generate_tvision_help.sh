#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
	echo "usage: $0 <tvhc> <input-text> <output-help> <output-header>" >&2
	exit 2
fi

compiler="$1"
input="$2"
help_output="$3"
header_output="$4"
temporary_directory="$(mktemp -d ./.mr-help.XXXXXX)"

trap 'rm -rf "$temporary_directory"' EXIT INT TERM HUP

temporary_help="$temporary_directory/mr.hlp"
temporary_header="$temporary_directory/MRHelpTopics.generated.hpp"

"$compiler" "$input" "$temporary_help" "$temporary_header"

mkdir -p "$(dirname "$help_output")" "$(dirname "$header_output")"

if [ ! -f "$help_output" ] || ! cmp -s "$temporary_help" "$help_output"; then
	cp "$temporary_help" "$help_output"
fi

if [ ! -f "$header_output" ] || ! cmp -s "$temporary_header" "$header_output"; then
	cp "$temporary_header" "$header_output"
fi

touch "$help_output" "$header_output"
