#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

init_state
docker_available=false
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  docker_available=true
fi

service_state() {
  local project="$1"
  local service="$2"
  local container_id details state health
  container_id="$(docker ps -a --filter "label=com.docker.compose.project=$project" \
    --filter "label=com.docker.compose.service=$service" -q 2>/dev/null | sed -n '1p')"
  if [[ -z "$container_id" ]]; then
    printf '%s:missing\n' "$service"
    return
  fi
  details="$(docker inspect "$container_id" \
    --format '{{.State.Status}}|{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' 2>/dev/null || true)"
  state="${details%%|*}"
  health="${details#*|}"
  if [[ "$health" == none || -z "$health" ]]; then
    printf '%s:%s\n' "$service" "${state:-unknown}"
  else
    printf '%s:%s/%s\n' "$service" "${state:-unknown}" "$health"
  fi
}

printf '%-28s %-10s %-12s %-12s %-49s %-7s %-12s %-20s %-46s %s\n' \
  BRANCH STATUS GITHUB STALE_FOR CONTAINERS PORT COMMIT BUILT_AT HOSTNAME WORKTREE
found=false
active=0
now="$(date +%s)"
for unit_dir in "$UNITS_DIR"/*; do
  [[ -d "$unit_dir" && -f "$unit_dir/branch" ]] || continue
  found=true
  branch="$(unit_value "$unit_dir" branch)"
  project="$(unit_value "$unit_dir" project 2>/dev/null || true)"
  status="$(unit_value "$unit_dir" status 2>/dev/null || printf unknown)"
  github="$(unit_value "$unit_dir" github-deployment-status 2>/dev/null || true)"
  if [[ -z "$github" ]]; then
    if github_auth_token >/dev/null 2>&1; then
      github=untracked
    else
      github=disabled
    fi
  fi
  containers='state-only (Docker unavailable)'
  if [[ "$docker_available" == true && -n "$project" ]]; then
    backend_state="$(service_state "$project" backend)"
    web_state="$(service_state "$project" web)"
    containers="$backend_state,$web_state"
    if [[ "$web_state" == web:running* ]]; then
      active=$((active + 1))
    fi
  fi
  port="$(unit_value "$unit_dir" port 2>/dev/null || printf -- '-')"
  commit="$(unit_value "$unit_dir" commit 2>/dev/null || printf unknown)"
  built_at="$(unit_value "$unit_dir" built-at 2>/dev/null || printf unknown)"
  hostname="$(unit_value "$unit_dir" hostname 2>/dev/null || printf unknown)"
  worktree="$(unit_value "$unit_dir" worktree 2>/dev/null || printf -- '-')"
  stale_since="$(unit_value "$unit_dir" stale-since 2>/dev/null || true)"
  stale_for='-'
  if [[ "$stale_since" =~ ^[0-9]+$ && "$now" -ge "$stale_since" ]]; then
    stale_for="$((now - stale_since))s"
  fi
  printf '%-28s %-10s %-12s %-12s %-49s %-7s %-12s %-20s %-46s %s\n' \
    "$branch" "$status" "$github" "$stale_for" "$containers" "$port" "${commit:0:12}" "$built_at" "$hostname" "$worktree"
done
if [[ "$found" == false ]]; then
  printf '(no deployments)\n'
fi

state_usage="$(du -sh "$STATE_ROOT" 2>/dev/null | awk '{print $1}' || printf unknown)"
worktree_usage="$(du -sh "$WORKTREES_DIR" 2>/dev/null | awk '{print $1}' || printf unknown)"
printf '\nActive deployments: %s\n' "$active"
printf 'Deployment state disk usage: %s\n' "$state_usage"
printf 'Worktree disk usage: %s\n' "$worktree_usage"
if [[ "$docker_available" == true ]]; then
  printf '\nDocker disk usage:\n'
  docker system df --format '  {{.Type}}: size={{.Size}}, active={{.Active}}, reclaimable={{.Reclaimable}}' \
    2>/dev/null || printf '  unavailable\n'
else
  printf 'Docker daemon: unavailable (state-file status shown above)\n'
fi
