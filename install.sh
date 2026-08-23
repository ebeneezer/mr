#!/bin/sh

set -eu

release_version="0.2.33"
release_epoch="@MR_RELEASE_EPOCH@"
prefix="/usr/local"

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

case "$0" in
	*/*) script_parent=${0%/*} ;;
	*) script_parent=. ;;
esac
script_directory=$(CDPATH= cd "$script_parent" && pwd)
macro_source="$script_directory/share/mr/macros"
if [ -z "${XDG_CONFIG_HOME:-}" ] && [ -z "${HOME:-}" ]; then
	printf 'mr cannot be installed.\n\nUser configuration path is unavailable:\n  neither XDG_CONFIG_HOME nor HOME is set\n\nNo files were installed.\n' >&2
	exit 1
fi
config_directory=${XDG_CONFIG_HOME:-"${HOME}/.config"}
macro_target="$config_directory/mr/macros"

missing_commands=
for required_command in cmp dirname find grep install ldd sed sort; do
	if ! command -v "$required_command" >/dev/null 2>&1; then
		if [ -n "$missing_commands" ]; then missing_commands="$missing_commands
$required_command"
		else missing_commands="$required_command"
		fi
	fi
done
if [ -n "$missing_commands" ]; then
	printf 'mr cannot be installed.\n\nRequired preflight commands are missing:\n' >&2
	for missing_command in $missing_commands; do printf '  %s\n' "$missing_command" >&2; done
	printf '\nNo files were installed.\n' >&2
	exit 1
fi

for required_file in \
	"$script_directory/bin/mr" \
	"$script_directory/bin/mr.hlp" \
	"$script_directory/share/doc/mr/mr-users-manual.pdf" \
	"$script_directory/share/doc/mr/mr-macro-reference.pdf" \
	"$script_directory/share/doc/mr/mr-technical-manual.pdf" \
	"$script_directory/share/licenses/mr/TVISION-COPYRIGHT"; do
	if [ ! -f "$required_file" ]; then
		printf 'mr cannot be installed.\n\nRelease package file is missing:\n  %s\n\nNo files were installed.\n' "$required_file" >&2
		exit 1
	fi
done
if [ ! -d "$macro_source" ]; then
	printf 'mr cannot be installed.\n\nRelease package directory is missing:\n  %s\n\nNo files were installed.\n' "$macro_source" >&2
	exit 1
fi

ldd_report=$(ldd -r "$script_directory/bin/mr" 2>&1 || true)
missing_libraries=$(printf '%s\n' "$ldd_report" | sed -n 's/^[[:space:]]*\([^[:space:]]*\)[[:space:]]*=>[[:space:]]*not found.*/\1/p' | sort -u)
if [ -n "$missing_libraries" ]; then
	printf 'mr cannot be installed.\n\nMissing libraries:\n' >&2
	printf '%s\n' "$missing_libraries" | sed 's/^/  /' >&2
	apt_packages=
	pacman_packages=
	for missing_library in $missing_libraries; do
		case "$missing_library" in
			libpcre2-8.so.0) apt_package=libpcre2-8-0; pacman_package=pcre2 ;;
			libncursesw.so.6) apt_package=libncursesw6; pacman_package=ncurses ;;
			libtinfo.so.6) apt_package=libtinfo6; pacman_package=ncurses ;;
			libgpm.so.2) apt_package=libgpm2; pacman_package=gpm ;;
			libpangocairo-1.0.so.0|libpango-1.0.so.0) apt_package=libpango-1.0-0; pacman_package=pango ;;
			libharfbuzz.so.0) apt_package=libharfbuzz0b; pacman_package=harfbuzz ;;
			libgobject-2.0.so.0|libglib-2.0.so.0) apt_package=libglib2.0-0; pacman_package=glib2 ;;
			libcairo.so.2) apt_package=libcairo2; pacman_package=cairo ;;
			libcurl.so.4) apt_package=libcurl4; pacman_package=curl ;;
			libarchive.so.13) apt_package=libarchive13; pacman_package=libarchive ;;
			libssl.so.3|libcrypto.so.3) apt_package=libssl3; pacman_package=openssl ;;
			*) apt_package=; pacman_package= ;;
		esac
		if [ -n "$apt_package" ]; then apt_packages="${apt_packages}${apt_packages:+
}$apt_package"; fi
		if [ -n "$pacman_package" ]; then pacman_packages="${pacman_packages}${pacman_packages:+
}$pacman_package"; fi
	done
	if command -v apt >/dev/null 2>&1 && [ -n "$apt_packages" ]; then
		printf '\nInstall as root, then run this installer again:\n  apt install' >&2
		printf '%s\n' "$apt_packages" | sort -u | while IFS= read -r package; do printf ' %s' "$package"; done >&2
		printf '\n' >&2
	elif command -v pacman >/dev/null 2>&1 && [ -n "$pacman_packages" ]; then
		printf '\nInstall as root, then run this installer again:\n  pacman -S --needed' >&2
		printf '%s\n' "$pacman_packages" | sort -u | while IFS= read -r package; do printf ' %s' "$package"; done >&2
		printf '\n' >&2
	fi
	printf '\nNo files were installed.\n' >&2
	exit 1
fi

missing_runtime_versions=$(printf '%s\n' "$ldd_report" | sed -n 's/.*version .\([^ ]*\). not found.*/\1/p' | sort -u)
if [ -n "$missing_runtime_versions" ]; then
	printf 'mr cannot be installed.\n\nRequired runtime versions are unavailable:\n' >&2
	printf '%s\n' "$missing_runtime_versions" | sed 's/^/  /' >&2
	printf '\nNo files were installed.\n' >&2
	exit 1
fi

missing_runtime_symbols=$(printf '%s\n' "$ldd_report" | sed -n 's/.*undefined symbol:[[:space:]]*\([^[:space:]]*\).*/\1/p' | sort -u)
if [ -n "$missing_runtime_symbols" ]; then
	printf 'mr cannot be installed.\n\nRequired runtime symbols are unavailable:\n' >&2
	printf '%s\n' "$missing_runtime_symbols" | sed 's/^/  /' >&2
	printf '\nNo files were installed.\n' >&2
	exit 1
fi

if startup_error=$("$script_directory/bin/mr" --help 2>&1 >/dev/null); then
	startup_status=0
else
	startup_status=$?
fi
if [ "$startup_status" -ne 0 ]; then
	printf 'mr cannot be installed.\n\nStartup compatibility check failed:\n' >&2
	case "$startup_error" in
		*"CPU ISA level is lower than required"*)
			printf '  The CPU ISA level is lower than required by this mr package.\n' >&2
			;;
		*"error while loading shared libraries:"*)
			startup_library=$(printf '%s\n' "$startup_error" | sed -n 's/.*error while loading shared libraries: \([^:]*\):.*/\1/p' | sed -n '1p')
			if [ -n "$startup_library" ]; then printf '  Missing library: %s\n' "$startup_library" >&2
			else printf '  The dynamic loader rejected the packaged executable.\n' >&2
			fi
			;;
		*)
			startup_reason=$(printf '%s\n' "$startup_error" | sed -n '1p')
			if [ -n "$startup_reason" ]; then printf '  %s\n' "$startup_reason" >&2
			else printf '  The packaged executable exited with status %s.\n' "$startup_status" >&2
			fi
			;;
	esac
	printf '\nNo files were installed.\n' >&2
	exit 1
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

system_install_with_sudo=0
if [ "$system_install_required" -ne 0 ]; then
	for target_directory in "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"; do
		target_probe="$target_directory"
		while [ ! -e "$target_probe" ]; do
			target_parent=$(dirname "$target_probe")
			if [ "$target_parent" = "$target_probe" ]; then break; fi
			target_probe="$target_parent"
		done
		if [ ! -d "$target_probe" ]; then
			printf 'mr cannot be installed.\n\nInstallation path is blocked by a non-directory:\n  %s\n\nNo files were installed.\n' "$target_probe" >&2
			exit 1
		fi
		if [ ! -w "$target_probe" ] || [ ! -x "$target_probe" ]; then system_install_with_sudo=1; fi
	done
	if [ "$system_install_with_sudo" -ne 0 ]; then
		if ! command -v sudo >/dev/null 2>&1; then
			printf 'mr cannot be installed.\n\nInstallation path requires write access or sudo:\n  %s\n\nNo files were installed.\n' "$prefix" >&2
			exit 1
		fi
		if ! sudo -v; then
			printf 'mr cannot be installed.\n\nsudo authorization failed.\n\nNo files were installed.\n' >&2
			exit 1
		fi
	fi
fi

config_probe="$macro_target"
while [ ! -e "$config_probe" ]; do
	config_parent=$(dirname "$config_probe")
	if [ "$config_parent" = "$config_probe" ]; then break; fi
	config_probe="$config_parent"
done
if [ ! -d "$config_probe" ] || [ ! -w "$config_probe" ] || [ ! -x "$config_probe" ]; then
	printf 'mr cannot be installed.\n\nUser macro directory is not writable:\n  %s\n\nNo files were installed.\n' "$macro_target" >&2
	exit 1
fi

find "$macro_source" -type f -name '*.mrmac' -print | sort |
	while IFS= read -r source_file; do
		relative_path=${source_file#"$macro_source"/}
		target_probe=$(dirname "$macro_target/$relative_path")
		while [ ! -e "$target_probe" ]; do
			target_parent=$(dirname "$target_probe")
			if [ "$target_parent" = "$target_probe" ]; then break; fi
			target_probe="$target_parent"
		done
		if [ ! -d "$target_probe" ] || [ ! -w "$target_probe" ] || [ ! -x "$target_probe" ]; then
			printf 'mr cannot be installed.\n\nUser macro path is not writable:\n  %s\n\nNo files were installed.\n' "$macro_target/$relative_path" >&2
			exit 1
		fi
	done

if [ "$system_install_required" -eq 0 ]; then
	:
elif [ "$system_install_with_sudo" -eq 0 ]; then
	install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr"
	install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
else
	sudo install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	sudo install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr"
	sudo install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	sudo install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	sudo install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
fi

install -d -m 0755 "$macro_target"

find "$macro_source" -type f -name '*.mrmac' -print | sort |
	while IFS= read -r source_file; do
		relative_path=${source_file#"$macro_source"/}
		target_file="$macro_target/$relative_path"
		install -d -m 0755 "$(dirname "$target_file")"
		if [ ! -e "$target_file" ]; then install -m 0644 "$source_file" "$target_file"; fi
	done

printf 'mr %s (build %s) installed successfully.\n' "$release_version" "$release_epoch"
printf 'Executable: %s/bin/mr\n' "$prefix"
printf 'User macros: %s (existing files preserved)\n' "$macro_target"
