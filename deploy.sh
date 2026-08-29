#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

usage() {
  printf 'Usage: %s <branch> [--revision <commit-or-ref>] | --status | --remove <branch> [--force]\n' "$0" >&2
  exit 2
}

[[ $# -ge 1 ]] || usage
case "$1" in
  --status)
    [[ $# -eq 1 ]] || usage
    exec "$SCRIPT_DIR/list-deployments.sh"
    ;;
  --remove)
    shift
    [[ $# -ge 1 ]] || usage
    exec "$SCRIPT_DIR/undeploy.sh" "$@"
    ;;
esac
branch="$1"
shift
revision=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --revision)
      [[ $# -ge 2 ]] || usage
      revision="$2"
      shift 2
      ;;
    *) usage ;;
  esac
done

need_command docker
need_command git
need_command python3
need_command curl
need_command openssl
need_command cloudflared
validate_branch_name "$branch"
init_state
validate_runtime_config
resolve_cloudflare_tunnel
slug="$(slug_for_branch "$branch")"
acquire_lock "$slug"
validate_tls_files

printf 'Fetching origin/%s...\n' "$branch"
git -C "$REPO_ROOT" ls-remote --exit-code --heads origin "refs/heads/$branch" >/dev/null \
  || die "remote branch does not exist: origin/$branch"
git -C "$REPO_ROOT" fetch --prune origin "+refs/heads/$branch:refs/remotes/origin/$branch"

if [[ -z "$revision" ]]; then
  revision="refs/remotes/origin/$branch"
fi
commit="$(git -C "$REPO_ROOT" rev-parse --verify "$revision^{commit}" 2>/dev/null)" \
  || die "revision does not resolve to a commit: $revision"
short_commit="${commit:0:12}"
worktree="$WORKTREES_DIR/$slug/$short_commit"
unit_dir="$UNITS_DIR/$slug"
project="$(project_for_slug "$slug")"
hostname="$(hostname_for "$branch" "$slug")"
github_environment="$(github_environment_for_slug "$slug")"
previous_hostname="$(unit_value "$unit_dir" previous-hostname 2>/dev/null || unit_value "$unit_dir" hostname 2>/dev/null || true)"
recorded_commit="$(unit_value "$unit_dir" commit 2>/dev/null || true)"
recorded_status="$(unit_value "$unit_dir" status 2>/dev/null || true)"
successful_commit="$(unit_value "$unit_dir" successful-commit 2>/dev/null || true)"
deployment_stage="preparing deployment"

deployment_cleanup() {
  local result=$?
  if (( result != 0 )) && [[ -d "$unit_dir" ]]; then
    atomic_value "$unit_dir" status failed || true
    if [[ "$GITHUB_DEPLOYMENT_CREATED" == true ]]; then
      github_update_deployment_status \
        "$unit_dir" failure "Deployment failed: $deployment_stage" || true
    fi
  fi
  release_lock
  return "$result"
}
trap deployment_cleanup EXIT

mkdir -p "$unit_dir/data" "$unit_dir/scenarios" "$(dirname "$worktree")"
if [[ -z "$successful_commit" && "$recorded_status" == running && "$recorded_commit" =~ ^[0-9a-f]{40}$ ]]; then
  successful_commit="$recorded_commit"
  atomic_value "$unit_dir" successful-commit "$successful_commit"
fi
if [[ -n "$previous_hostname" && "$previous_hostname" != "$hostname" ]]; then
  atomic_value "$unit_dir" previous-hostname "$previous_hostname"
fi
atomic_value "$unit_dir" branch "$branch"
atomic_value "$unit_dir" commit "$commit"
atomic_value "$unit_dir" hostname "$hostname"
atomic_value "$unit_dir" status starting
deployment_stage="GitHub deployment initialization"
github_create_deployment \
  "$unit_dir" "$commit" "$github_environment" "$branch" "$hostname" \
  || die "GitHub deployment reporting is required but initialization failed"

deployment_stage="preparing worktree"
if [[ ! -e "$worktree/.git" ]]; then
  git -C "$REPO_ROOT" worktree add --detach "$worktree" "$commit"
fi
scenario_dir="$unit_dir/scenarios"
if [[ -d "$worktree/scenarios" ]]; then
  scenario_dir="$worktree/scenarios"
fi
deployment_stage="allocating runtime port"
port="$(allocate_port "$unit_dir" "$project")"

atomic_value "$unit_dir" port "$port"
atomic_value "$unit_dir" project "$project"
atomic_value "$unit_dir" worktree "$worktree"
atomic_value "$unit_dir" scenario-path "$scenario_dir"
built_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
atomic_value "$unit_dir" built-at "$built_at"
export_compose_values "$unit_dir"

printf 'Building and starting %s at commit %s...\n' "$branch" "$short_commit"
deployment_stage="Compose configuration validation"
docker compose -p "$project" -f "$DEPLOY_COMPOSE" config --quiet
deployment_stage="container build"
if [[ -n "$DEPLOY_BUILDER" ]]; then
  docker compose -p "$project" -f "$DEPLOY_COMPOSE" build --builder "$DEPLOY_BUILDER"
else
  docker compose -p "$project" -f "$DEPLOY_COMPOSE" build
fi
deployment_stage="container startup"
docker compose -p "$project" -f "$DEPLOY_COMPOSE" up -d --force-recreate --remove-orphans --wait
deployment_stage="application health check"
wait_for_http "$port"
deployment_stage="version metadata verification"
version_fields="$(read_version_metadata "http://127.0.0.1:$port/api/version" 5)" \
  || die "application /api/version did not return valid build metadata"
actual_branch="${version_fields%%$'\t'*}"
actual_commit="${version_fields#*$'\t'}"
[[ "$actual_branch" == "$branch" ]] \
  || die "application branch metadata mismatch: expected $branch, got $actual_branch"
[[ "$actual_commit" == "$commit" ]] \
  || die "application commit metadata mismatch: expected $commit, got $actual_commit"

deployment_stage="edge startup"
ensure_edge
route_path="$ROUTES_DIR/$slug.caddy"
route_backup=""
if [[ -f "$route_path" ]]; then
  route_backup="$unit_dir/route.backup.$$"
  cp "$route_path" "$route_backup"
fi
deployment_stage="Caddy route configuration"
write_route "$slug" "$hostname" "$port"
if ! reload_edge; then
  if [[ -n "$route_backup" ]]; then
    mv "$route_backup" "$route_path"
  else
    rm -f "$route_path"
  fi
  reload_edge || true
  die "Caddy route validation/reload failed; previous route was restored"
fi
rm -f "$route_backup"
deployment_stage="local edge HTTPS verification"
verify_https_route "$hostname"
printf 'Ensuring Cloudflare DNS route for %s...\n' "$hostname"
deployment_stage="Cloudflare DNS configuration"
ensure_cloudflare_dns_route "$unit_dir" "$hostname"
printf 'Waiting for public HTTPS...\n'
deployment_stage="public HTTPS verification"
verify_public_https_route "$hostname"
if [[ -n "$previous_hostname" && "$previous_hostname" != "$hostname" ]]; then
  printf 'Removing previous Cloudflare DNS route for %s...\n' "$previous_hostname"
  deployment_stage="previous DNS route cleanup"
  delete_cloudflare_dns_route "$unit_dir" "$previous_hostname" false
  rm -f "$unit_dir/previous-hostname"
fi
deployment_stage="GitHub success reporting"
if [[ "$GITHUB_DEPLOYMENT_CREATED" == true ]]; then
  github_update_deployment_status \
    "$unit_dir" success "Deployment completed successfully" "https://$hostname" \
    || die "GitHub deployment reporting is required but success status failed"
fi
deployment_stage="recording successful deployment"
record_successful_commit "$unit_dir" "$commit"
atomic_value "$unit_dir" status running
remove_old_worktrees "$slug" "$worktree" "$(unit_value "$unit_dir" previous-successful-commit 2>/dev/null || true)"

printf '\nDeployment successful: https://%s\n' "$hostname"
printf '  branch:  %s\n' "$branch"
printf '  commit:  %s\n' "$commit"
printf '  built:   %s\n' "$built_at"
printf '  project: %s\n' "$project"
printf '  port:    127.0.0.1:%s\n' "$port"
printf '  tunnel:  %s (%s)\n' "$CLOUDFLARE_TUNNEL" "$TUNNEL_ID"
