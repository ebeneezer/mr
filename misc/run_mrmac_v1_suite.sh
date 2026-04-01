#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

declare -a staged_macros=(
  "mrmac/macros/test_block_state_staged.mrmac"
  "mrmac/macros/test_editor_state_vars_staged.mrmac"
  "mrmac/macros/test_erase_window_staged.mrmac"
  "mrmac/macros/test_file_state_vars_staged.mrmac"
  "mrmac/macros/test_global_runtime_affinity.mrmac"
  "mrmac/macros/test_global_state_staged.mrmac"
  "mrmac/macros/test_macro_registry_state_staged.mrmac"
  "mrmac/macros/test_mark_stack_staged.mrmac"
  "mrmac/macros/test_replace_last_search_staged.mrmac"
  "mrmac/macros/test_run_macro_session_state_staged.mrmac"
  "mrmac/macros/test_runtime_options_staged.mrmac"
  "mrmac/macros/test_search_replace_staged.mrmac"
  "mrmac/macros/test_window_state_vars_staged.mrmac"
  "mrmac/macros/test_window_ui_commands_staged.mrmac"
)

echo "[MRMAC v1] Build binaries"
make mr stage-profile-probe regression-probe

echo "[MRMAC v1] Staged eligibility"
staged_output="$(./misc/mr_stage_profile_probe "${staged_macros[@]}")"
printf '%s\n' "${staged_output}"

staged_fail=0
while IFS= read -r line; do
  if [[ "${line}" != *"canStage=1"* ]] || [[ "${line}" != *"unsupported=<none>"* ]]; then
    staged_fail=1
  fi
done <<< "${staged_output}"
if [[ "${staged_fail}" -ne 0 ]]; then
  echo "[MRMAC v1] FAILED: staged eligibility mismatch"
  exit 1
fi

echo "[MRMAC v1] Macro compile sweep"
compile_output="$(./misc/mr_stage_profile_probe mrmac/macros/test*.mrmac)"
printf '%s\n' "${compile_output}" >/dev/null
if printf '%s\n' "${compile_output}" | rg -q "compile_error=|read_error"; then
  printf '%s\n' "${compile_output}" | rg "compile_error=|read_error"
  echo "[MRMAC v1] FAILED: macro compile sweep"
  exit 1
fi

echo "[MRMAC v1] Background staged probes"
./regression/mr-regression-checks --probe staged-nav
./regression/mr-regression-checks --probe staged-mark-page

echo "[MRMAC v1] PASS"
