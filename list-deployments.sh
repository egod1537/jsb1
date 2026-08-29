#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

init_state
printf '%-24s %-18s %-9s %-7s %-12s %s\n' BRANCH SLUG STATUS PORT COMMIT URL
found=false
for unit_dir in "$UNITS_DIR"/*; do
  [[ -d "$unit_dir" && -f "$unit_dir/branch" ]] || continue
  found=true
  branch="$(unit_value "$unit_dir" branch)"
  slug="$(basename "$unit_dir")"
  project="$(unit_value "$unit_dir" project 2>/dev/null || true)"
  status="$(unit_value "$unit_dir" status 2>/dev/null || printf unknown)"
  if [[ -n "$project" ]] && command -v docker >/dev/null 2>&1; then
    if docker ps --filter "label=com.docker.compose.project=$project" --filter "label=com.docker.compose.service=web" -q 2>/dev/null | grep -q .; then
      status=running
    elif [[ "$status" == running || "$status" == starting ]]; then
      status=stopped
    fi
  fi
  port="$(unit_value "$unit_dir" port 2>/dev/null || printf -- '-')"
  commit="$(unit_value "$unit_dir" commit 2>/dev/null || printf unknown)"
  hostname="$(unit_value "$unit_dir" hostname 2>/dev/null || printf unknown)"
  printf '%-24s %-18s %-9s %-7s %-12s https://%s\n' \
    "$branch" "$slug" "$status" "$port" "${commit:0:12}" "$hostname"
done
if [[ "$found" == false ]]; then
  printf '(no deployments)\n'
fi
