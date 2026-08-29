#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

usage() {
  printf 'Usage: %s [branch]\n' "$0" >&2
  exit 2
}

[[ $# -le 1 ]] || usage
requested_branch="${1:-}"
failures=0
docker_ready=false

ok() {
  printf '[OK] %s\n' "$*"
}

fail() {
  printf '[FAIL] %s\n' "$*"
  failures=$((failures + 1))
}

container_id_for_service() {
  local project="$1"
  local service="$2"
  docker ps -a --filter "label=com.docker.compose.project=$project" \
    --filter "label=com.docker.compose.service=$service" -q 2>/dev/null | sed -n '1p'
}

container_health() {
  local container_id="$1"
  docker inspect "$container_id" \
    --format '{{.State.Status}}|{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' \
    2>/dev/null
}

printf 'Global checks\n'
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  docker_ready=true
  ok 'Docker daemon'
else
  fail 'Docker daemon is unavailable'
fi

if command -v openssl >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1 \
  && (validate_tls_files) >/dev/null 2>&1; then
  ok 'TLS certificate/key readable, external, and matching'
else
  fail 'TLS certificate/key validation'
fi

if [[ "$docker_ready" == true ]]; then
  edge_id="$(container_id_for_service "$EDGE_PROJECT" edge)"
  tunnel_id="$(container_id_for_service "$EDGE_PROJECT" tunnel)"
  if [[ -n "$edge_id" ]]; then
    edge_health="$(container_health "$edge_id" || true)"
    if [[ "$edge_health" == running\|healthy ]]; then
      ok 'edge container running/healthy'
    else
      fail "edge container state: ${edge_health:-unknown}"
    fi
    if docker exec "$edge_id" caddy validate --config "$CADDY_CONFIG_PATH" --adapter caddyfile \
      >/dev/null 2>&1; then
      ok 'Caddy configuration valid'
    else
      fail 'Caddy configuration validation'
    fi
  else
    fail 'edge container missing'
    fail 'Caddy configuration validation unavailable'
  fi
  if [[ -n "$tunnel_id" ]]; then
    tunnel_health="$(container_health "$tunnel_id" || true)"
    if [[ "$tunnel_health" == running\|* ]]; then
      ok 'cloudflared tunnel running'
    else
      fail "cloudflared tunnel state: ${tunnel_health:-unknown}"
    fi
  else
    fail 'cloudflared tunnel container missing'
  fi
else
  fail 'edge check unavailable without Docker'
  fail 'Caddy configuration check unavailable without Docker'
  fail 'cloudflared check unavailable without Docker'
fi

verify_branch() {
  local branch="$1"
  local unit_dir project port hostname route_path backend_id web_id details
  local expected_commit version_fields actual_branch actual_commit
  printf '\nBranch checks: %s\n' "$branch"
  unit_dir="$(unit_dir_for_branch "$branch" 2>/dev/null || true)"
  if [[ -z "$unit_dir" ]]; then
    fail "$branch unit state missing"
    return
  fi
  ok "$branch unit state"
  project="$(unit_value "$unit_dir" project 2>/dev/null || true)"
  port="$(unit_value "$unit_dir" port 2>/dev/null || true)"
  hostname="$(unit_value "$unit_dir" hostname 2>/dev/null || true)"
  expected_commit="$(unit_value "$unit_dir" commit 2>/dev/null || true)"

  if [[ "$docker_ready" == true && -n "$project" ]]; then
    backend_id="$(container_id_for_service "$project" backend)"
    web_id="$(container_id_for_service "$project" web)"
    if [[ -n "$backend_id" ]]; then
      details="$(container_health "$backend_id" || true)"
      if [[ "$details" == running\|healthy ]]; then
        ok "$branch backend container running/healthy"
      else
        fail "$branch backend container state: ${details:-unknown}"
      fi
    else
      fail "$branch backend container missing"
    fi
    if [[ -n "$web_id" ]]; then
      details="$(container_health "$web_id" || true)"
      if [[ "$details" == running\|healthy ]]; then
        ok "$branch web container running/healthy"
      else
        fail "$branch web container state: ${details:-unknown}"
      fi
    else
      fail "$branch web container missing"
    fi
  else
    fail "$branch container checks unavailable"
  fi

  if [[ "$port" =~ ^[0-9]+$ ]] && http_2xx "http://127.0.0.1:$port/api/health" 5; then
    ok "localhost:$port/api/health"
  else
    fail "localhost:${port:-unknown}/api/health"
  fi

  route_path="$ROUTES_DIR/$(basename "$unit_dir").caddy"
  if [[ -f "$route_path" ]] \
    && grep -Fq "https://$hostname" "$route_path" \
    && grep -Fq "host.docker.internal:$port" "$route_path"; then
    ok "$branch Caddy route"
  else
    fail "$branch Caddy route"
  fi

  if [[ -n "$hostname" ]] && http_2xx "https://$hostname/api/health" 10; then
    ok "https://$hostname"
  else
    fail "https://${hostname:-unknown}"
  fi

  if [[ -n "$hostname" ]] \
    && version_fields="$(read_version_metadata "https://$hostname/api/version" 10)"; then
    ok '/api/version reachable with valid JSON'
    actual_branch="${version_fields%%$'\t'*}"
    actual_commit="${version_fields#*$'\t'}"
    if [[ "$actual_branch" == "$branch" ]]; then
      ok "deployed branch matches: $branch"
    else
      fail 'deployed branch mismatch'
      printf '  expected: %s\n  actual:   %s\n' "$branch" "$actual_branch"
    fi
    if [[ -n "$expected_commit" && "$actual_commit" == "$expected_commit" ]]; then
      ok "deployed commit matches: ${actual_commit:0:12}"
    else
      fail 'deployed commit mismatch'
      printf '  expected: %s\n  actual:   %s\n' "${expected_commit:-unknown}" "$actual_commit"
    fi
  else
    fail "https://${hostname:-unknown}/api/version reachable with valid JSON"
  fi
}

if [[ -n "$requested_branch" ]]; then
  validate_branch_name "$requested_branch"
  verify_branch "$requested_branch"
else
  checked=0
  for unit_dir in "$UNITS_DIR"/*; do
    [[ -d "$unit_dir" && -f "$unit_dir/branch" ]] || continue
    [[ "$(unit_value "$unit_dir" status 2>/dev/null || true)" != stopped ]] || continue
    verify_branch "$(unit_value "$unit_dir" branch)"
    checked=$((checked + 1))
  done
  [[ "$checked" -gt 0 ]] || fail 'no active deployment state found'
fi

printf '\nResult: %s failure(s)\n' "$failures"
(( failures == 0 ))
