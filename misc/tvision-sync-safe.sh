#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

STASH_MSG="tvision-sync-safe-$(date +%Y%m%d-%H%M%S)"
STASHED=0

echo "[tvision-sync] repository: $ROOT_DIR"

if ! git remote | grep -qx "tvision-upstream"; then
  echo "[tvision-sync] adding remote: tvision-upstream"
  git remote add tvision-upstream https://github.com/magiblot/tvision.git
fi

if ! git diff --quiet || ! git diff --cached --quiet || [ -n "$(git ls-files --others --exclude-standard)" ]; then
  echo "[tvision-sync] saving current workspace to stash"
  git stash push -u -m "$STASH_MSG" >/dev/null
  STASHED=1
fi

echo "[tvision-sync] fetch upstream"
git fetch --prune tvision-upstream

echo "[tvision-sync] subtree pull"
git subtree pull --prefix=tvision tvision-upstream master --squash

echo "[tvision-sync] apply local patch queue"
rm -f tvision/.mr-patches-applied
make -j1 tvision-apply-patches

if [ "$STASHED" -eq 1 ]; then
  echo "[tvision-sync] restoring saved workspace"
  if git stash apply --index stash@{0}; then
    git stash drop stash@{0} >/dev/null
  else
    echo "[tvision-sync] stash apply had conflicts. Resolve manually." >&2
    exit 1
  fi
fi

echo "[tvision-sync] done"
