#!/usr/bin/env bash

set -euo pipefail

operation=${1:-}
if [[ -z "$operation" ]]; then
	echo "Usage: $0 <seed|record|optimize|clean> [-- <mr arguments>]" >&2
	exit 2
fi
shift

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

build_root=${MR_BOLT_BUILD_DIR:-build/bolt}
case "$build_root" in
	/*)
		;;
	*)
		build_root="$repo_root/$build_root"
		;;
esac
build_root=$(realpath -m "$build_root")
case "$build_root" in
	"$repo_root"/build/*)
		;;
	*)
		echo "BOLT build root must be below $repo_root/build." >&2
		exit 2
		;;
esac

active_file="$build_root/active"
comparison_file="$build_root/comparison.info"
cohort_dir=
seed_binary=
merged_profile=
bolt_binary=
stripped_binary=

make_command=${MR_BOLT_MAKE:-make}
perf_command=${MR_BOLT_PERF:-perf}
perf2bolt_command=${MR_BOLT_PERF2BOLT:-perf2bolt}
merge_fdata_command=${MR_BOLT_MERGE_FDATA:-merge-fdata}
llvm_bolt_command=${MR_BOLT_LLVM_BOLT:-llvm-bolt}
strip_command=${MR_BOLT_STRIP:-strip}

require_command() {
	local command_name=$1

	if ! command -v "$command_name" >/dev/null 2>&1; then
		echo "Required command not found: $command_name" >&2
		exit 1
	fi
}

metadata_value() {
	local metadata_file=$1
	local metadata_key=$2

	sed -n "s/^${metadata_key}=//p" "$metadata_file" | head -n 1
}

binary_build_id() {
	readelf -n "$1" | sed -n 's/.*Build ID: //p' | head -n 1
}

set_cohort_paths() {
	seed_binary="$cohort_dir/mr.seed"
	merged_profile="$cohort_dir/profile-merged.fdata"
	bolt_binary="$cohort_dir/mr.bolt"
	stripped_binary="$cohort_dir/mr.bolt.stripped"
}

resolve_cohort() {
	local requested_cohort=${MR_BOLT_COHORT_DIR:-}
	local active_relative

	if [[ -n "$requested_cohort" ]]; then
		case "$requested_cohort" in
			/*)
				cohort_dir=$requested_cohort
				;;
			*)
				cohort_dir="$repo_root/$requested_cohort"
				;;
		esac
	else
		if [[ ! -f "$active_file" ]]; then
			echo "No active BOLT cohort. Run make bolt-seed first." >&2
			exit 1
		fi
		active_relative=$(sed -n '1p' "$active_file")
		cohort_dir="$build_root/$active_relative"
	fi

	cohort_dir=$(realpath -m "$cohort_dir")
	case "$cohort_dir" in
		"$build_root"/gcc/* | "$build_root"/clang/*)
			;;
		*)
			echo "Invalid BOLT cohort path: $cohort_dir" >&2
			exit 2
			;;
	esac
	set_cohort_paths
}

source_commit=
source_dirty=
source_state_sha256=
read_source_state() {
	local git_status

	require_command git
	require_command sha256sum
	source_commit=$(git rev-parse HEAD)
	git_status=$(git status --porcelain=v1 --untracked-files=normal)
	if [[ -n "$git_status" ]]; then
		source_dirty=true
	else
		source_dirty=false
	fi
	source_state_sha256=$(
		{
			git diff --binary HEAD --
			while IFS= read -r -d '' untracked_file; do
				printf '\0%s\0' "$untracked_file"
				sha256sum -- "$untracked_file"
			done < <(git ls-files --others --exclude-standard -z)
		} | sha256sum | awk '{print $1}'
	)
}

comparison_epoch=
select_comparison_epoch() {
	local existing_epoch=
	local existing_commit=
	local existing_state=
	local comparison_temp="$comparison_file.tmp"
	local current_epoch

	read_source_state
	if [[ -f "$comparison_file" ]]; then
		existing_epoch=$(metadata_value "$comparison_file" comparison_epoch)
		existing_commit=$(metadata_value "$comparison_file" source_commit)
		existing_state=$(metadata_value "$comparison_file" source_state_sha256)
	fi
	if [[ "$existing_epoch" =~ ^[0-9]+$ && "$existing_commit" == "$source_commit" && "$existing_state" == "$source_state_sha256" ]]; then
		comparison_epoch=$existing_epoch
		return
	fi

	current_epoch=$(date +%s)
	if [[ "$existing_epoch" =~ ^[0-9]+$ && "$current_epoch" -le "$existing_epoch" ]]; then
		current_epoch=$((existing_epoch + 1))
	fi
	comparison_epoch=$current_epoch
	mkdir -p "$build_root"
	rm -f "$comparison_temp"
	{
		printf 'comparison_epoch=%s\n' "$comparison_epoch"
		printf 'source_commit=%s\n' "$source_commit"
		printf 'source_dirty=%s\n' "$source_dirty"
		printf 'source_state_sha256=%s\n' "$source_state_sha256"
	} > "$comparison_temp"
	mv -f "$comparison_temp" "$comparison_file"
}

write_active_launcher() {
	local cohort_relative=${cohort_dir#"$build_root"/}
	local build_root_relative=${build_root#"$repo_root"/}
	local cohort_repo_relative=${cohort_dir#"$repo_root"/}
	local active_temp="$active_file.tmp"
	local launcher_path="$repo_root/mrbolt"
	local launcher_temp="$repo_root/mrbolt.tmp"

	rm -f "$active_temp" "$launcher_temp"
	printf '%s\n' "$cohort_relative" > "$active_temp"
	mv -f "$active_temp" "$active_file"
	{
		printf '%s\n' '#!/usr/bin/env bash'
		printf '\n'
		printf '%s\n' 'set -euo pipefail'
		printf '\n'
		printf '%s\n' 'mrbolt_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)'
		printf 'mrbolt_build_root=%q\n' "$build_root_relative"
		printf 'mrbolt_cohort_dir=%q\n' "$cohort_repo_relative"
		printf '%s\n' 'exec env MR_BOLT_BUILD_DIR="$mrbolt_build_root" MR_BOLT_COHORT_DIR="$mrbolt_cohort_dir" "$mrbolt_root/misc/mr-bolt-workflow.sh" record -- "$@"'
	} > "$launcher_temp"
	chmod 755 "$launcher_temp"
	mv -f "$launcher_temp" "$launcher_path"
}

validate_seed_identity() {
	local manifest_build_id
	local actual_build_id
	local cohort_build_id=${cohort_dir##*/}

	if [[ ! -x "$seed_binary" || ! -f "$cohort_dir/seed.info" ]]; then
		echo "BOLT seed not found. Run make bolt-seed first." >&2
		exit 1
	fi
	manifest_build_id=$(metadata_value "$cohort_dir/seed.info" build_id)
	actual_build_id=$(binary_build_id "$seed_binary")
	if [[ -z "$actual_build_id" || "$manifest_build_id" != "$actual_build_id" || "$cohort_build_id" != "$actual_build_id" ]]; then
		echo "BOLT cohort identity mismatch: $cohort_dir" >&2
		exit 1
	fi
}

profile_build_id() {
	local profile_file=$1

	"$perf_command" buildid-list -i "$profile_file" 2>/dev/null |
		awk '$2 ~ /(^|\/)mr\.seed$/ { print $1; exit }'
}

restore_required=0
restore_standard_build() {
	local operation_status=$?
	local clean_status=0
	local build_status=0

	trap - EXIT
	if [[ "$restore_required" -eq 0 ]]; then
		exit "$operation_status"
	fi

	set +e
	MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" clean-tvision CXX=clang++
	clean_status=$?
	if [[ "$comparison_epoch" =~ ^[0-9]+$ ]]; then
		MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" clean all CXX=clang++ MR_BUILD_EPOCH="$comparison_epoch"
	else
		MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" clean all CXX=clang++
	fi
	build_status=$?
	set -e

	if [[ "$operation_status" -eq 0 && "$clean_status" -ne 0 ]]; then
		operation_status=$clean_status
	fi
	if [[ "$operation_status" -eq 0 && "$build_status" -ne 0 ]]; then
		operation_status=$build_status
	fi
	exit "$operation_status"
}

build_seed() {
	local cxx=${MR_BOLT_CXX:-clang++}
	local compiler_name=${cxx##*/}
	local compiler_tag
	local cc
	local compiler_extra
	local cxx_flags
	local c_flags
	local link_flags
	local tvision_flags
	local seed_sections
	local seed_build_id
	local seed_temp
	local info_temp

	case "$compiler_name" in
		clang++)
			compiler_tag=clang
			cc=clang
			compiler_extra=
			;;
		g++)
			compiler_tag=gcc
			cc=gcc
			compiler_extra="-fno-reorder-blocks-and-partition"
			;;
		*)
			echo "Unsupported BOLT compiler: $cxx" >&2
			exit 2
			;;
	esac

	require_command "$make_command"
	require_command "$cxx"
	require_command "$cc"
	require_command readelf
	select_comparison_epoch

	restore_required=1
	trap restore_standard_build EXIT
	trap 'exit 129' HUP
	trap 'exit 130' INT
	trap 'exit 143' TERM

	cxx_flags="-Wall -g -O3 -DNDEBUG -DMR_BUILD_EPOCH=$comparison_epoch -march=x86-64 -mtune=generic $compiler_extra -std=\$(CXXSTD) \$(PTHREAD_FLAGS) \$(INCLUDES)"
	c_flags="-Wall -g -O3 -DNDEBUG -march=x86-64 -mtune=generic $compiler_extra \$(INCLUDES)"
	link_flags="\$(PTHREAD_FLAGS) \$(TVISION_LIB) \$(PCRE2_LIB) \$(NCURSESW_LIB) \$(GPM_LIB) \$(TINFO_LIB) \$(PDF_EXPORT_LIBS) -Wl,--emit-relocs -Wl,--build-id=sha1 -Wl,--discard-sframe"
	tvision_flags="-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -DCMAKE_C_COMPILER_LAUNCHER=$repo_root/misc/mr-compiler-temp.sh -DCMAKE_CXX_COMPILER_LAUNCHER=$repo_root/misc/mr-compiler-temp.sh -DCMAKE_C_FLAGS_RELEASE=-O3\\ -DNDEBUG\\ -march=x86-64\\ -mtune=generic\\ $compiler_extra -DCMAKE_CXX_FLAGS_RELEASE=-O3\\ -DNDEBUG\\ -march=x86-64\\ -mtune=generic\\ $compiler_extra -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=ON -DTV_BUILD_EXAMPLES=OFF -DTV_BUILD_TESTS=OFF -DTV_BUILD_AVSCOLOR=OFF -DTV_OPTIMIZE_BUILD=ON"

	MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" clean-tvision CXX="$cxx" CC="$cc"
	MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" tvision-build CXX="$cxx" CC="$cc" TVISION_CMAKE_FLAGS="$tvision_flags"
	MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" clean CXX="$cxx" CC="$cc"
	MAKEFLAGS= MFLAGS= MAKELEVEL=0 "$make_command" all CXX="$cxx" CC="$cc" CXXFLAGS="$cxx_flags" CFLAGS="$c_flags" LDFLAGS="$link_flags"

	seed_sections=$(readelf -SW mr)
	if [[ "$seed_sections" != *".rela.text"* || "$seed_sections" != *".symtab"* ]]; then
		echo "BOLT seed lacks .rela.text or .symtab." >&2
		exit 1
	fi
	if [[ "$seed_sections" == *".rela.sframe"* ]]; then
		echo "BOLT seed contains unsupported SFrame relocations." >&2
		exit 1
	fi
	seed_build_id=$(binary_build_id mr)
	if [[ ! "$seed_build_id" =~ ^[0-9a-f]{40}$ ]]; then
		echo "BOLT seed has no valid SHA-1 build-id." >&2
		exit 1
	fi

	cohort_dir="$build_root/$compiler_tag/$seed_build_id"
	set_cohort_paths
	mkdir -p "$cohort_dir"
	seed_temp="$seed_binary.tmp"
	info_temp="$cohort_dir/seed.info.tmp"
	if [[ -f "$seed_binary" ]] && ! cmp -s mr "$seed_binary"; then
		echo "Build-id collision with existing BOLT seed: $seed_build_id" >&2
		exit 1
	fi
	rm -f "$seed_temp" "$info_temp"
	cp mr "$seed_temp"
	mv -f "$seed_temp" "$seed_binary"
	{
		printf 'compiler=%s\n' "$cxx"
		printf 'compiler_tag=%s\n' "$compiler_tag"
		printf 'compiler_version=%s\n' "$("$cxx" --version | sed -n '1p')"
		printf 'build_id=%s\n' "$seed_build_id"
		printf 'comparison_epoch=%s\n' "$comparison_epoch"
		printf 'source_commit=%s\n' "$source_commit"
		printf 'source_dirty=%s\n' "$source_dirty"
		printf 'source_state_sha256=%s\n' "$source_state_sha256"
		printf 'cxxflags=%s\n' "$cxx_flags"
		printf 'ldflags=%s\n' "$link_flags"
	} > "$info_temp"
	mv -f "$info_temp" "$cohort_dir/seed.info"
	write_active_launcher
	printf 'BOLT seed: %s\n' "$seed_binary"
	printf 'BOLT recorder: %s\n' "$repo_root/mrbolt"
}

record_profile() {
	local record_epoch
	local session_data
	local recording_data
	local seed_build_id
	local recorded_build_id

	resolve_cohort
	require_command "$perf_command"
	require_command readelf
	validate_seed_identity
	if [[ "${1:-}" == "--" ]]; then
		shift
	fi

	record_epoch=$(date +%s)
	session_data="$cohort_dir/perf-$record_epoch.data"
	recording_data="$session_data.recording"
	if [[ -e "$session_data" || -e "$recording_data" ]]; then
		echo "BOLT profile for EPOCH $record_epoch already exists." >&2
		exit 1
	fi
	rm -f "$recording_data"
	"$perf_command" record -e cycles:u -j any,u -o "$recording_data" -- "$seed_binary" "$@"
	seed_build_id=$(binary_build_id "$seed_binary")
	recorded_build_id=$(profile_build_id "$recording_data")
	if [[ "$recorded_build_id" != "$seed_build_id" ]]; then
		echo "Recorded profile does not match active BOLT seed; keeping $recording_data for diagnosis." >&2
		exit 1
	fi
	mv -f "$recording_data" "$session_data"
	printf 'BOLT perf profile: %s\n' "$session_data"
}

optimize_binary() {
	local perf_file
	local perf_name
	local profile_file
	local profile_temp
	local merged_temp
	local bolt_temp
	local stripped_temp
	local seed_help
	local bolt_help
	local seed_build_id
	local recorded_build_id
	local -a perf_files
	local -a profile_files=()

	resolve_cohort
	require_command "$perf2bolt_command"
	require_command "$merge_fdata_command"
	require_command "$llvm_bolt_command"
	require_command "$strip_command"
	require_command "$perf_command"
	require_command readelf
	require_command cmp
	validate_seed_identity
	merged_temp="$merged_profile.tmp"
	bolt_temp="$bolt_binary.tmp"
	stripped_temp="$stripped_binary.tmp"
	seed_help="$cohort_dir/seed-help.tmp"
	bolt_help="$cohort_dir/bolt-help.tmp"
	seed_build_id=$(binary_build_id "$seed_binary")

	shopt -s nullglob
	perf_files=("$cohort_dir"/perf-[0-9]*.data)
	shopt -u nullglob
	if [[ "${#perf_files[@]}" -eq 0 ]]; then
		echo "No EPOCH-marked BOLT profiles found. Run ./mrbolt first." >&2
		exit 1
	fi
	for perf_file in "${perf_files[@]}"; do
		recorded_build_id=$(profile_build_id "$perf_file")
		if [[ "$recorded_build_id" != "$seed_build_id" ]]; then
			echo "Incompatible BOLT profile: $perf_file" >&2
			exit 1
		fi
	done

	rm -f "$merged_temp" "$bolt_temp" "$stripped_temp" "$seed_help" "$bolt_help"
	for perf_file in "${perf_files[@]}"; do
		perf_name=${perf_file##*/}
		profile_file="$cohort_dir/profile-${perf_name#perf-}"
		profile_file=${profile_file%.data}.fdata
		profile_temp="$profile_file.tmp"
		rm -f "$profile_temp"
		"$perf2bolt_command" --perfdata="$perf_file" "$seed_binary" -o "$profile_temp"
		mv -f "$profile_temp" "$profile_file"
		profile_files+=("$profile_file")
	done
	"$merge_fdata_command" -o "$merged_temp" "${profile_files[@]}"
	"$llvm_bolt_command" "$seed_binary" \
		-o "$bolt_temp" \
		--data="$merged_temp" \
		--reorder-blocks=ext-tsp \
		--reorder-functions=cdsort \
		--split-functions \
		--split-all-cold \
		--split-eh \
		--icf=safe \
		--no-huge-pages \
		--update-debug-sections \
		--dyno-stats
	"$seed_binary" --help > "$seed_help"
	"$bolt_temp" --help > "$bolt_help"
	cmp "$seed_help" "$bolt_help"
	"$strip_command" --strip-all -o "$stripped_temp" "$bolt_temp"

	mv -f "$merged_temp" "$merged_profile"
	mv -f "$bolt_temp" "$bolt_binary"
	mv -f "$stripped_temp" "$stripped_binary"
	rm -f "$seed_help" "$bolt_help"
	printf 'Merged %d BOLT profiles: %s\n' "${#profile_files[@]}" "$merged_profile"
	printf 'BOLT binary: %s\n' "$bolt_binary"
	printf 'Stripped BOLT binary: %s\n' "$stripped_binary"
}

clean_outputs() {
	if [[ -d "$build_root" ]]; then
		find "$build_root" -type f \
			\( -name 'mr.seed' -o -name 'mr.seed.tmp' -o -name 'seed.info' -o -name 'seed.info.tmp' \
			-o -name 'perf-*.data' -o -name 'perf-*.data.recording' \
			-o -name 'profile-*.fdata' -o -name 'profile-*.fdata.tmp' \
			-o -name 'profile-merged.fdata' -o -name 'profile-merged.fdata.tmp' \
			-o -name 'mr.bolt' -o -name 'mr.bolt.tmp' \
			-o -name 'mr.bolt.stripped' -o -name 'mr.bolt.stripped.tmp' \
			-o -name 'seed-help.tmp' -o -name 'bolt-help.tmp' \
			-o -name 'active' -o -name 'active.tmp' \
			-o -name 'comparison.info' -o -name 'comparison.info.tmp' \) -delete
		find "$build_root" -depth -type d -empty -delete
	fi
	rm -f "$repo_root/mrbolt" "$repo_root/mrbolt.tmp"
}

case "$operation" in
	seed)
		build_seed
		;;
	record)
		record_profile "$@"
		;;
	optimize)
		optimize_binary
		;;
	clean)
		clean_outputs
		;;
	*)
		echo "Unknown BOLT operation: $operation" >&2
		exit 2
		;;
esac
