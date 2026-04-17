#!/usr/bin/env bash
set -euo pipefail

INPUT_PATH="${1:-README.md}"
OUTPUT_PATH="${2:-app/MRAboutQuotes.generated.hpp}"
FIRST_QUOTE='"I live again." Caleb (Blood)'

tmp_quotes="$(mktemp)"
mkdir -p "$(dirname "$OUTPUT_PATH")"
tmp_output="$(mktemp "$(dirname "$OUTPUT_PATH")/.MRAboutQuotes.generated.hpp.XXXXXX")"
trap 'rm -f "$tmp_quotes" "$tmp_output"' EXIT

awk -v first_quote="$FIRST_QUOTE" '
function trim(s) {
	sub(/^[[:space:]]+/, "", s)
	sub(/[[:space:]]+$/, "", s)
	return s
}
BEGIN {
	seen_first_note = 0
	in_note_block = 0
	count = 0
}
{
	if (!seen_first_note && $0 ~ /^>[[:space:]]*\[!NOTE\][[:space:]]*$/) {
		seen_first_note = 1
		in_note_block = 1
		next
	}
	if (!in_note_block)
		next

	if ($0 ~ /^>/) {
		if ($0 ~ /^>[[:space:]]*-[[:space:]]*/) {
			line = $0
			sub(/^>[[:space:]]*-[[:space:]]*/, "", line)
			line = trim(line)
			if (line != "")
				quotes[++count] = line
		}
		next
	}

	in_note_block = 0
}
END {
	found_first = 0
	for (i = 1; i <= count; ++i)
		if (quotes[i] == first_quote)
			found_first = 1

	print first_quote
	if (found_first) {
		for (i = 1; i <= count; ++i)
			if (quotes[i] != first_quote)
				print quotes[i]
	} else {
		for (i = 1; i <= count; ++i)
			print quotes[i]
	}
}
' "$INPUT_PATH" > "$tmp_quotes"

{
	echo "#ifndef MRABOUTQUOTES_GENERATED_HPP"
	echo "#define MRABOUTQUOTES_GENERATED_HPP"
	echo
	echo "#include <cstddef>"
	echo
	echo "static const char *const kAboutQuotes[] = {"
	while IFS= read -r line; do
		escaped="${line//\\/\\\\}"
		escaped="${escaped//\"/\\\"}"
		printf '    "%s",\n' "$escaped"
	done < "$tmp_quotes"
	echo "};"
	echo
	echo "constexpr std::size_t kAboutQuoteCount = sizeof(kAboutQuotes) / sizeof(kAboutQuotes[0]);"
	echo
	echo "#endif"
} > "$tmp_output"

mv "$tmp_output" "$OUTPUT_PATH"
