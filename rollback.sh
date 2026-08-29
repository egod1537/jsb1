#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

usage() {
  printf 'Usage: %s <branch> [--dry-run]\n' "$0" >&2
  exit 2
}

[[ $# -ge 1 ]] || usage
branch="$1"
shift
dry_run=false
if [[ ${1:-} == --dry-run ]]; then
  dry_run=true
  shift
fi
[[ $# -eq 0 ]] || usage

validate_branch_name "$branch"
init_state
unit_dir="$(unit_dir_for_branch "$branch" 2>/dev/null)" \
  || die "deployment state not found for branch: $branch"
current="$(unit_value "$unit_dir" commit 2>/dev/null || true)"
status="$(unit_value "$unit_dir" status 2>/dev/null || true)"
successful="$(unit_value "$unit_dir" successful-commit 2>/dev/null || true)"
previous="$(unit_value "$unit_dir" previous-successful-commit 2>/dev/null || true)"
target=""
reason=""

if [[ "$status" == failed && "$successful" =~ ^[0-9a-f]{40}$ && "$successful" != "$current" ]]; then
  target="$successful"
  reason="last successful deployment before the failed attempt"
elif [[ "$previous" =~ ^[0-9a-f]{40}$ ]]; then
  target="$previous"
  reason="previous successful deployment"
fi

if [[ -z "$target" ]]; then
  die "no certain previous successful commit is recorded; use ./deploy.sh $branch --revision <known-good-commit>"
fi
[[ "$target" != "$current" ]] \
  || die "recorded rollback target equals the current commit; use an explicit known-good commit"
git -C "$REPO_ROOT" cat-file -e "$target^{commit}" 2>/dev/null \
  || die "recorded rollback commit is no longer available locally: $target; use an explicit known-good commit"

printf 'Rollback target for %s: %s (%s)\n' "$branch" "$target" "$reason"
if [[ "$dry_run" == true ]]; then
  printf 'Dry run only; no deployment changed.\n'
  exit 0
fi
exec "$SCRIPT_DIR/deploy.sh" "$branch" --revision "$target"
