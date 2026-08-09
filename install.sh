#!/bin/sh

set -eu

release_version="0.2.2"
release_epoch="@MR_RELEASE_EPOCH@"
prefix="/usr/local"

if [ "$(id -u)" -eq 0 ]; then
	echo "Run this installer as the target user, without sudo." >&2
	echo "It requests sudo only for the system-wide installation step." >&2
	exit 1
fi

while [ "$#" -gt 0 ]; do
	case "$1" in
		--prefix)
			if [ "$#" -lt 2 ]; then
				echo "Missing path after --prefix." >&2
				exit 2
			fi
			prefix="$2"
			shift 2
			;;
		--prefix=*)
			prefix=${1#--prefix=}
			shift
			;;
		--help)
			echo "Usage: ./install.sh [--prefix PATH]"
			echo "Default prefix: /usr/local"
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			exit 2
			;;
	esac
done

case "$prefix" in
	/*) ;;
	*)
		echo "Installation prefix must be an absolute path." >&2
		exit 2
		;;
esac

script_directory=$(CDPATH= cd "$(dirname "$0")" && pwd)
macro_source="$script_directory/share/mr/macros"

for required_file in \
	"$script_directory/bin/mr" \
	"$script_directory/bin/mr.hlp" \
	"$script_directory/share/doc/mr/mr-users-manual.pdf" \
	"$script_directory/share/doc/mr/mr-macro-reference.pdf" \
	"$script_directory/share/doc/mr/mr-technical-manual.pdf" \
	"$script_directory/share/licenses/mr/TVISION-COPYRIGHT"; do
	if [ ! -f "$required_file" ]; then
		echo "Incomplete release package: missing $required_file" >&2
		exit 1
	fi
done
if [ ! -d "$macro_source" ]; then
	echo "Incomplete release package: missing $macro_source" >&2
	exit 1
fi

if command -v ldd >/dev/null 2>&1; then
	ldd_report=$(ldd "$script_directory/bin/mr" 2>&1 || true)
	if printf '%s\n' "$ldd_report" | grep -E "not found|version .* required by" >/dev/null 2>&1; then
		echo "This build cannot run on the current system:" >&2
		printf '%s\n' "$ldd_report" >&2
		exit 1
	fi
fi

system_install_required=0
for packaged_file in \
	"bin/mr" \
	"bin/mr.hlp" \
	"share/doc/mr/mr-users-manual.pdf" \
	"share/doc/mr/mr-macro-reference.pdf" \
	"share/doc/mr/mr-technical-manual.pdf" \
	"share/licenses/mr/TVISION-COPYRIGHT"; do
	if ! cmp -s "$script_directory/$packaged_file" "$prefix/$packaged_file"; then
		system_install_required=1
		break
	fi
done

if [ "$system_install_required" -eq 0 ]; then
	echo "System files already match this build."
elif { [ -d "$prefix" ] && [ -w "$prefix" ]; } ||
	{ [ ! -e "$prefix" ] && [ -w "$(dirname "$prefix")" ]; }; then
	install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr"
	install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
else
	if ! command -v sudo >/dev/null 2>&1; then
		echo "Installing under $prefix requires write access or sudo." >&2
		exit 1
	fi
	sudo install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	sudo install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr"
	sudo install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	sudo install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	sudo install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
fi

config_directory=${XDG_CONFIG_HOME:-"$HOME/.config"}
macro_target="$config_directory/mr/macros"
install -d -m 0755 "$macro_target"

find "$macro_source" -type f -name '*.mrmac' -print | sort |
	while IFS= read -r source_file; do
		relative_path=${source_file#"$macro_source"/}
		target_file="$macro_target/$relative_path"
		install -d -m 0755 "$(dirname "$target_file")"
		if [ -e "$target_file" ]; then
			echo "Preserved user macro: $target_file"
		else
			install -m 0644 "$source_file" "$target_file"
			echo "Installed user macro: $target_file"
		fi
	done

echo "Installed MR executable: $prefix/bin/mr"
echo "Installed MR help:       $prefix/bin/mr.hlp"
echo "Installed MR manuals:    $prefix/share/doc/mr"
echo "Initialized user macros: $macro_target"

release_notes=$(cat <<EOF
MR ${release_version} (build ${release_epoch})

This is the first downloadable Linux x86-64 build.
Run the installer again to update the system files for this build.
Existing files below ${macro_target} are deliberately preserved.
Each additional user runs this same installer once for an independent macro set.
EOF
)

if [ -t 1 ] && command -v less >/dev/null 2>&1; then
	printf '%s\n' "$release_notes" | LESSSECURE=1 less -R
else
	printf '%s\n' "$release_notes"
fi
