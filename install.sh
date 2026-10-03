#!/bin/sh

set -eu

release_version="0.3.4"
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
printf 'mr %s installer\n' "$release_version"
printf 'Checking the release package and runtime...\n'
if [ -z "${XDG_CONFIG_HOME:-}" ] && [ -z "${HOME:-}" ]; then
	printf 'mr cannot be installed.\n\nUser configuration path is unavailable:\n  neither XDG_CONFIG_HOME nor HOME is set\n\nNo files were installed.\n' >&2
	exit 1
fi
config_directory=${XDG_CONFIG_HOME:-"${HOME}/.config"}
macro_target="$config_directory/mr/macros"

missing_commands=
for required_command in cat cmp dirname find grep id install ldd locale mktemp rm sed sort; do
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

mr_collision_path=
mr_search_path="${PATH:-}:"
while [ -n "$mr_search_path" ]; do
	mr_search_directory=${mr_search_path%%:*}
	mr_search_path=${mr_search_path#*:}
	if [ -z "$mr_search_directory" ]; then mr_search_directory=.; fi
	mr_candidate="$mr_search_directory/mr"
	if [ ! -f "$mr_candidate" ] || [ ! -x "$mr_candidate" ]; then continue; fi
	mr_candidate_help=$(PERLDOC_PAGER=cat PERLDOC=-otext "$mr_candidate" --help 2>/dev/null | sed -n '1,40p' || true)
	case "$mr_candidate_help" in
		*"terminal-based programmer's editor"*) ;;
		*) mr_collision_path="$mr_candidate"; break ;;
	esac
done
if [ -n "$mr_collision_path" ]; then
	printf '\nAnother mr command is present: %s\n' "$mr_collision_path" >&2
	printf 'Install the MR editor as mr and use editor filename completion? [y/N] ' >&2
	if ! IFS= read -r mr_collision_answer </dev/tty; then mr_collision_answer=; fi
	case "$mr_collision_answer" in
		y|Y|yes|YES) ;;
		*) printf 'Installation cancelled. No files were installed.\n' >&2; exit 1 ;;
	esac
fi

character_map=$(locale charmap 2>/dev/null || true)
missing_utf8_locale=
configure_utf8_locale=
if [ "$character_map" != "UTF-8" ]; then
	configure_utf8_locale=1
	if [ "$(LC_ALL=C.UTF-8 locale charmap 2>/dev/null || true)" = "UTF-8" ]; then
		LC_ALL=C.UTF-8
		export LC_ALL
		printf 'Using C.UTF-8 for mr (current character map: %s).\n' "${character_map:-unknown}"
	else
		missing_utf8_locale=1
	fi
fi

missing_emoji_codepoints=
if command -v fc-list >/dev/null 2>&1; then
	for required_emoji_codepoint in 1F4FC 1F50E 1F550 1F551 1F552 1F553 1F554 1F555 1F556 1F557 1F558 1F559 1F55A 1F55B; do
		if [ -z "$(fc-list ":charset=$required_emoji_codepoint" file 2>/dev/null | sed -n '1p')" ]; then
			missing_emoji_codepoints="${missing_emoji_codepoints}${missing_emoji_codepoints:+
}$required_emoji_codepoint"
		fi
	done
else
	missing_emoji_codepoints=unknown
fi

ldd_report=$(ldd -r "$script_directory/bin/mr" 2>&1 || true)
missing_libraries=$(printf '%s\n' "$ldd_report" | sed -n 's/^[[:space:]]*\([^[:space:]]*\)[[:space:]]*=>[[:space:]]*not found.*/\1/p' | sort -u)
missing_terminal_probe=
if [ ! -x /usr/bin/tput ]; then missing_terminal_probe=1; fi
terminal_colors=$(/usr/bin/tput colors 2>/dev/null || true)
terminal_needs_color=
case "$terminal_colors" in
	''|*[!0-9]*) terminal_needs_color=1 ;;
	*) if [ "$terminal_colors" -lt 8 ]; then terminal_needs_color=1; fi ;;
esac
required_changes=
if [ -n "$missing_libraries$missing_emoji_codepoints$missing_terminal_probe$configure_utf8_locale" ]; then required_changes=1; fi
terminal_shell_requested=
if [ -n "$required_changes" ] || [ -n "$terminal_needs_color" ]; then
	printf '\nInstaller setup\n' >&2
	if [ -n "$configure_utf8_locale" ]; then printf '  System locale must be set to C.UTF-8.\n' >&2; fi
	if [ -n "$missing_utf8_locale" ]; then printf '  C.UTF-8 locale must be generated.\n' >&2; fi
	if [ -n "$missing_libraries" ]; then
		printf '  Missing libraries:\n' >&2
		printf '%s\n' "$missing_libraries" | sed 's/^/    /' >&2
	fi
	if [ -n "$missing_emoji_codepoints" ]; then
		printf '  Emoji font coverage is incomplete.\n' >&2
		if [ "$missing_emoji_codepoints" = unknown ]; then
			printf '    Missing command: fc-list\n' >&2
		else
			printf '    Missing Unicode codepoints:\n' >&2
			printf '%s\n' "$missing_emoji_codepoints" | sed 's/^/      U+/' >&2
		fi
	fi
	if [ -n "$missing_terminal_probe" ]; then printf '  Terminal capability tool is missing: /usr/bin/tput\n' >&2; fi
	if [ -n "$terminal_needs_color" ]; then
		printf '  Optional shell TERM fallback: xterm-256color (current colors: %s).\n' "${terminal_colors:-unknown}" >&2
	fi

	package_manager=
	if command -v apt-get >/dev/null 2>&1; then package_manager=apt
	elif command -v pacman >/dev/null 2>&1; then package_manager=pacman
	fi
	apt_packages=
	pacman_packages=
	unmapped_libraries=
	locale_support_package=
	if [ -n "$missing_utf8_locale" ] && { ! command -v localedef >/dev/null 2>&1 || [ ! -f /usr/share/i18n/locales/C ] || [ ! -f /usr/share/i18n/charmaps/UTF-8.gz ]; }; then
		locale_support_package=1
	fi
	if [ -n "$configure_utf8_locale" ] && [ "$package_manager" = apt ] && ! command -v update-locale >/dev/null 2>&1; then
		locale_support_package=1
	fi
	if [ -n "$locale_support_package" ]; then
		apt_packages=locales
		pacman_packages=glibc
	fi
	for missing_library in $missing_libraries; do
		case "$missing_library" in
			libpcre2-8.so.0) apt_package=libpcre2-8-0; pacman_package=pcre2 ;;
			libncursesw.so.6) apt_package=libncursesw6; pacman_package=ncurses ;;
			libtinfo.so.6) apt_package=libtinfo6; pacman_package=ncurses ;;
			libgpm.so.2) apt_package=libgpm2; pacman_package=gpm ;;
			libpangocairo-1.0.so.0) apt_package=libpangocairo-1.0-0; pacman_package=pango ;;
			libpangoft2-1.0.so.0) apt_package=libpangoft2-1.0-0; pacman_package=pango ;;
			libpango-1.0.so.0) apt_package=libpango-1.0-0; pacman_package=pango ;;
			libharfbuzz.so.0) apt_package=libharfbuzz0b; pacman_package=harfbuzz ;;
			libgobject-2.0.so.0|libglib-2.0.so.0) apt_package=libglib2.0-0; pacman_package=glib2 ;;
			libcairo.so.2) apt_package=libcairo2; pacman_package=cairo ;;
			libcurl.so.4) apt_package=libcurl4; pacman_package=curl ;;
			libarchive.so.13) apt_package=libarchive13; pacman_package=libarchive ;;
			libssl.so.3|libcrypto.so.3) apt_package=libssl3; pacman_package=openssl ;;
			*) apt_package=; pacman_package= ;;
		esac
		if [ "$package_manager" = apt ] && command -v apt-cache >/dev/null 2>&1; then
			case "$apt_package" in
				libglib2.0-0|libcurl4|libarchive13|libssl3)
					if apt-cache show "${apt_package}t64" >/dev/null 2>&1; then apt_package="${apt_package}t64"; fi
					;;
			esac
		fi
		if [ "$package_manager" = apt ] && [ -z "$apt_package" ]; then unmapped_libraries="${unmapped_libraries}${unmapped_libraries:+
}$missing_library"; fi
		if [ "$package_manager" = pacman ] && [ -z "$pacman_package" ]; then unmapped_libraries="${unmapped_libraries}${unmapped_libraries:+
}$missing_library"; fi
		if [ -n "$apt_package" ]; then apt_packages="${apt_packages}${apt_packages:+
}$apt_package"; fi
		if [ -n "$pacman_package" ]; then pacman_packages="${pacman_packages}${pacman_packages:+
}$pacman_package"; fi
	done
	if [ -n "$missing_emoji_codepoints" ]; then
		apt_packages="${apt_packages}${apt_packages:+
}fontconfig
fonts-noto-color-emoji"
		pacman_packages="${pacman_packages}${pacman_packages:+
}fontconfig
noto-fonts-emoji"
	fi
	if [ -n "$missing_terminal_probe" ]; then
		apt_packages="${apt_packages}${apt_packages:+
}ncurses-bin"
		pacman_packages="${pacman_packages}${pacman_packages:+
}ncurses"
	fi
	if [ -n "$unmapped_libraries" ] || { [ -z "$package_manager" ] && [ -n "$missing_libraries$missing_emoji_codepoints$missing_terminal_probe$locale_support_package" ]; }; then
		printf '\nCannot install all missing dependencies automatically.\n' >&2
		if [ -n "$unmapped_libraries" ]; then printf '%s\n' "$unmapped_libraries" | sed 's/^/  /' >&2; fi
		printf 'No mr files were installed.\n' >&2
		exit 1
	fi
	if [ -n "$configure_utf8_locale" ] && [ "$package_manager" != apt ] && ! command -v localectl >/dev/null 2>&1; then
		printf 'mr cannot be installed. localectl is required to save the system locale. No mr files were installed.\n' >&2
		exit 1
	fi
	if [ "$package_manager" = apt ]; then packages=$(printf '%s\n' "$apt_packages" | sort -u)
	elif [ "$package_manager" = pacman ]; then packages=$(printf '%s\n' "$pacman_packages" | sort -u)
	else packages=
	fi
	set -- $packages
	if [ "$#" -gt 0 ]; then
		printf '\nPackages to install\n' >&2
		for package do printf '  %s\n' "$package" >&2; done
	fi
	printf 'Apply these changes now (sudo if needed)? [y/N] ' >&2
	if ! IFS= read -r install_answer </dev/tty; then install_answer=; fi
	case "$install_answer" in
		y|Y|yes|YES) if [ -n "$terminal_needs_color" ]; then terminal_shell_requested=1; fi ;;
		*)
			if [ -n "$required_changes" ]; then
				printf 'Installation cancelled. No mr files were installed.\n' >&2
				exit 1
			fi
			;;
	esac
	if { [ "$#" -gt 0 ] || [ -n "$configure_utf8_locale" ]; } && [ "$(id -u)" -ne 0 ] && ! command -v sudo >/dev/null 2>&1; then
		printf 'sudo is required to prepare the missing dependencies. No mr files were installed.\n' >&2
		exit 1
	fi
	if [ "$#" -gt 0 ]; then
		printf '\nInstalling dependencies...\n' >&2
		if [ "$(id -u)" -eq 0 ]; then
			if [ "$package_manager" = apt ]; then apt-get install -y -- "$@"
			else pacman -S --needed --noconfirm -- "$@"
			fi
		else
			if [ "$package_manager" = apt ]; then sudo apt-get install -y -- "$@"
			else sudo pacman -S --needed --noconfirm -- "$@"
			fi
		fi
	fi
	if [ -n "$missing_utf8_locale" ]; then
		printf '\nGenerating C.UTF-8...\n' >&2
		if [ "$(id -u)" -eq 0 ]; then
			if ! localedef -i C -f UTF-8 C.UTF-8; then
				printf 'mr cannot be installed. C.UTF-8 generation failed. No mr files were installed.\n' >&2
				exit 1
			fi
		elif ! sudo localedef -i C -f UTF-8 C.UTF-8; then
			printf 'mr cannot be installed. C.UTF-8 generation failed. No mr files were installed.\n' >&2
			exit 1
		fi
		if [ "$(LC_ALL=C.UTF-8 locale charmap 2>/dev/null || true)" != "UTF-8" ]; then
			printf 'mr cannot be installed. C.UTF-8 remains unavailable. No mr files were installed.\n' >&2
			exit 1
		fi
		LC_ALL=C.UTF-8
		export LC_ALL
	fi
	if [ -n "$configure_utf8_locale" ]; then
		printf '\nSaving C.UTF-8 as the system locale...\n' >&2
		if [ "$package_manager" = apt ]; then
			if ! command -v update-locale >/dev/null 2>&1; then
				printf 'mr cannot be installed. update-locale is unavailable. No mr files were installed.\n' >&2
				exit 1
			fi
			set -- update-locale LANG=C.UTF-8 LC_ALL=C.UTF-8
		else
			set -- localectl set-locale LANG=C.UTF-8
		fi
		if [ "$(id -u)" -eq 0 ]; then
			if ! "$@"; then
				printf 'mr cannot be installed. System locale update failed. No mr files were installed.\n' >&2
				exit 1
			fi
		elif ! sudo "$@"; then
			printf 'mr cannot be installed. System locale update failed. No mr files were installed.\n' >&2
			exit 1
		fi
	fi
	ldd_report=$(ldd -r "$script_directory/bin/mr" 2>&1 || true)
	missing_libraries=$(printf '%s\n' "$ldd_report" | sed -n 's/^[[:space:]]*\([^[:space:]]*\)[[:space:]]*=>[[:space:]]*not found.*/\1/p' | sort -u)
	if [ -n "$missing_libraries" ]; then
		printf 'mr cannot be installed. Libraries are still missing:\n' >&2
		printf '%s\n' "$missing_libraries" | sed 's/^/  /' >&2
		printf 'No mr files were installed.\n' >&2
		exit 1
	fi
	if ! command -v fc-list >/dev/null 2>&1; then
		printf 'mr cannot be installed. Fontconfig is still unavailable. No mr files were installed.\n' >&2
		exit 1
	fi
	for required_emoji_codepoint in 1F4FC 1F50E 1F550 1F551 1F552 1F553 1F554 1F555 1F556 1F557 1F558 1F559 1F55A 1F55B; do
		if [ -z "$(fc-list ":charset=$required_emoji_codepoint" file 2>/dev/null | sed -n '1p')" ]; then
			printf 'mr cannot be installed. Emoji font coverage is still incomplete (U+%s). No mr files were installed.\n' "$required_emoji_codepoint" >&2
			exit 1
		fi
	done
	if [ ! -x /usr/bin/tput ]; then
		printf 'mr cannot be installed. /usr/bin/tput is still unavailable. No mr files were installed.\n' >&2
		exit 1
	fi
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

if [ "$(/usr/bin/tput -T xterm-256color colors 2>/dev/null || true)" != 256 ]; then
	printf 'mr cannot be installed. The xterm-256color terminal definition is unavailable.\nNo mr files were installed.\n' >&2
	exit 1
fi

terminal_shell_startup=
terminal_colors=$(/usr/bin/tput colors 2>/dev/null || true)
terminal_needs_color=
case "$terminal_colors" in
	''|*[!0-9]*) terminal_needs_color=1 ;;
	*) if [ "$terminal_colors" -lt 8 ]; then terminal_needs_color=1; fi ;;
esac
if [ -n "$terminal_shell_requested" ] && [ -n "$terminal_needs_color" ]; then
	if [ -z "${HOME:-}" ]; then
		printf 'HOME is required to update the shell startup file. No mr files were installed.\n' >&2
		exit 1
	fi
	case "${SHELL:-}" in
		*/bash) terminal_shell_startup="$HOME/.bashrc" ;;
		*/zsh) terminal_shell_startup="$HOME/.zshrc" ;;
		*) printf 'This shell has no supported startup file. No mr files were installed.\n' >&2; exit 1 ;;
	esac
	if [ -e "$terminal_shell_startup" ]; then
		if [ ! -f "$terminal_shell_startup" ] || [ ! -w "$terminal_shell_startup" ]; then
			printf 'Shell startup file is not writable: %s\nNo mr files were installed.\n' "$terminal_shell_startup" >&2
			exit 1
		fi
	elif [ ! -w "$HOME" ]; then
		printf 'Home directory is not writable: %s\nNo mr files were installed.\n' "$HOME" >&2
		exit 1
	fi
fi

bash_completion_startup=
zsh_completion_startup=
if [ -n "${HOME:-}" ]; then
	if command -v bash >/dev/null 2>&1 && ! grep -Fqx '# MR editor filename completion' "$HOME/.bashrc" 2>/dev/null && ! grep -Fqx 'complete -o default mr' "$HOME/.bashrc" 2>/dev/null; then
		bash_completion_startup="$HOME/.bashrc"
	fi
	if command -v zsh >/dev/null 2>&1 && ! grep -Fqx '# MR editor filename completion' "$HOME/.zshrc" 2>/dev/null && ! grep -Fqx 'compdef _files mr' "$HOME/.zshrc" 2>/dev/null; then
		zsh_completion_startup="$HOME/.zshrc"
	fi
	for shell_startup in "$bash_completion_startup" "$zsh_completion_startup"; do
		if [ -z "$shell_startup" ]; then continue; fi
		if [ -e "$shell_startup" ]; then
			if [ ! -f "$shell_startup" ] || [ ! -w "$shell_startup" ]; then
				printf 'Shell startup file is not writable: %s\nNo mr files were installed.\n' "$shell_startup" >&2
				exit 1
			fi
		elif [ ! -w "$HOME" ]; then
			printf 'Home directory is not writable: %s\nNo mr files were installed.\n' "$HOME" >&2
			exit 1
		fi
	done
fi

launcher_file=$(mktemp /tmp/mr-launcher.XXXXXX)
trap 'rm -f -- "$launcher_file"' 0
cat > "$launcher_file" <<'EOF'
#!/bin/sh
case "$0" in
	*/*) launcher_path=$0 ;;
	*) launcher_path=$(command -v "$0") ;;
esac
case "$launcher_path" in
	*/*) binary_path=${launcher_path%/*}/mr.real ;;
	*) printf 'mr: cannot locate the installed executable.\n' >&2; exit 1 ;;
esac
if [ "$(locale charmap 2>/dev/null || true)" != "UTF-8" ]; then
	if [ "$(LC_ALL=C.UTF-8 locale charmap 2>/dev/null || true)" != "UTF-8" ]; then
		printf 'mr: C.UTF-8 locale is unavailable.\n' >&2
		exit 1
	fi
	LC_ALL=C.UTF-8
	export LC_ALL
fi
if [ "${1:-}" != --internal-apply-update ]; then
	terminal_colors=$(/usr/bin/tput colors 2>/dev/null || true)
	case "$terminal_colors" in
		''|*[!0-9]*) TERM=xterm-256color; export TERM ;;
		*) if [ "$terminal_colors" -lt 8 ]; then TERM=xterm-256color; export TERM; fi ;;
	esac
fi
exec "$binary_path" "$@"
EOF

system_install_required=0
if ! cmp -s "$script_directory/bin/mr" "$prefix/bin/mr.real" || ! cmp -s "$launcher_file" "$prefix/bin/mr"; then system_install_required=1; fi
for packaged_file in \
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
	printf '\nProgram files are already current.\n' >&2
elif [ "$system_install_with_sudo" -eq 0 ]; then
	printf '\nInstalling mr...\n' >&2
	install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr.real"
	install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
	install -m 0755 "$launcher_file" "$prefix/bin/mr"
else
	printf '\nInstalling mr...\n' >&2
	sudo install -d -m 0755 "$prefix/bin" "$prefix/share/doc/mr" "$prefix/share/licenses/mr"
	sudo install -m 0755 "$script_directory/bin/mr" "$prefix/bin/mr.real"
	sudo install -m 0644 "$script_directory/bin/mr.hlp" "$prefix/bin/mr.hlp"
	sudo install -m 0644 "$script_directory/share/doc/mr/"*.pdf "$prefix/share/doc/mr/"
	sudo install -m 0644 "$script_directory/share/licenses/mr/TVISION-COPYRIGHT" "$prefix/share/licenses/mr/TVISION-COPYRIGHT"
	sudo install -m 0755 "$launcher_file" "$prefix/bin/mr"
fi

install -d -m 0755 "$macro_target"

find "$macro_source" -type f -name '*.mrmac' -print | sort |
	while IFS= read -r source_file; do
		relative_path=${source_file#"$macro_source"/}
		target_file="$macro_target/$relative_path"
		install -d -m 0755 "$(dirname "$target_file")"
		if [ ! -e "$target_file" ]; then install -m 0644 "$source_file" "$target_file"; fi
	done

if [ -n "$terminal_shell_startup" ] && ! grep -Fqx '# mr terminal color fallback' "$terminal_shell_startup" 2>/dev/null; then
	cat >> "$terminal_shell_startup" <<'EOF'

# mr terminal color fallback
if [ -x /usr/bin/tput ]; then
	mr_terminal_colors=$(/usr/bin/tput colors 2>/dev/null || true)
	case "$mr_terminal_colors" in
		''|*[!0-9]*) TERM=xterm-256color; export TERM ;;
		*) if [ "$mr_terminal_colors" -lt 8 ]; then TERM=xterm-256color; export TERM; fi ;;
	esac
	unset mr_terminal_colors
fi
EOF
fi

if [ -n "$bash_completion_startup" ]; then
	cat >> "$bash_completion_startup" <<'EOF'

# MR editor filename completion
complete -o default mr
EOF
fi
if [ -n "$zsh_completion_startup" ]; then
	cat >> "$zsh_completion_startup" <<'EOF'

# MR editor filename completion
if ! (( ${+functions[compdef]} )); then
	autoload -Uz compinit
	compinit
fi
compdef _files mr
EOF
fi
if [ -z "${HOME:-}" ]; then
	printf 'Shell completion was not configured because HOME is unavailable.\n' >&2
fi

printf '\nInstalled mr %s (build %s).\n' "$release_version" "$release_epoch"
printf '  Command: %s/bin/mr\n' "$prefix"
printf '  User macros: %s (existing files preserved)\n\n' "$macro_target"
printf "Enter 'mr' to begin.\n"
