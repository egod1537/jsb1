#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

usage() {
  printf 'Usage: %s <branch> [--force]\n' "$0" >&2
  exit 2
}

[[ $# -ge 1 ]] || usage
branch="$1"
shift
force=false
if [[ ${1:-} == "--force" ]]; then
  force=true
  shift
fi
[[ $# -eq 0 ]] || usage
validate_branch_name "$branch"
if [[ "$branch" == "$MAIN_BRANCH" && "$force" != true ]]; then
  die "refusing to undeploy main without --force"
fi
need_command docker
need_command git
need_command python3
need_command openssl
need_command cloudflared
init_state
validate_runtime_config
resolve_cloudflare_tunnel
slug="$(slug_for_branch "$branch")"
unit_dir="$UNITS_DIR/$slug"
[[ -f "$unit_dir/branch" && "$(unit_value "$unit_dir" branch)" == "$branch" ]] \
  || die "deployment not found for branch: $branch"
acquire_lock "$slug"
validate_tls_files
export_compose_values "$unit_dir"
project="$(unit_value "$unit_dir" project)"
hostname="$(unit_value "$unit_dir" hostname)"
route_path="$ROUTES_DIR/$slug.caddy"
route_backup=""
if [[ -f "$route_path" ]]; then
  route_backup="$unit_dir/route.backup.$$"
  mv "$route_path" "$route_backup"
  if ! reload_edge; then
    mv "$route_backup" "$route_path"
    reload_edge || true
    die "Caddy route removal failed; route was restored"
  fi
fi

docker compose -p "$project" -f "$DEPLOY_COMPOSE" down --remove-orphans
delete_cloudflare_dns_route "$unit_dir" "$hostname"
rm -f "$route_backup"
worktree="$(unit_value "$unit_dir" worktree 2>/dev/null || true)"
if [[ -n "$worktree" && "$worktree" == "$WORKTREES_DIR/$slug/"* && -e "$worktree/.git" ]]; then
  git -C "$REPO_ROOT" worktree remove --force "$worktree"
  git -C "$REPO_ROOT" worktree prune
fi
rm -f "$unit_dir/port"
atomic_value "$unit_dir" status stopped
github_mark_environment_inactive "$unit_dir" \
  || die "GitHub deployment reporting is required but inactive status failed"
printf 'Undeployed %s (data retained in %s).\n' "$branch" "$unit_dir/data"
