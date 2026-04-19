#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
	echo "usage: $0 <input-markdown> <output-header>" >&2
	exit 2
fi

input="$1"
output="$2"

mkdir -p "$(dirname "$output")"

cat >"$output" <<'EOF'
#ifndef MR_HELP_GENERATED_HPP
#define MR_HELP_GENERATED_HPP

static const char kMrEmbeddedHelpMarkdown[] = R"MRHELP(
EOF

cat "$input" >>"$output"

cat >>"$output" <<'EOF'
)MRHELP";

#endif
EOF
