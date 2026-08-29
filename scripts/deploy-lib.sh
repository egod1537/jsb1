#!/usr/bin/env bash

set -Eeuo pipefail

LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$LIB_DIR/.." && pwd -P)"
STATE_ROOT="${JSB1_DEPLOY_STATE_DIR:-$REPO_ROOT/data/branch-deployments}"
UNITS_DIR="$STATE_ROOT/units"
WORKTREES_DIR="$STATE_ROOT/worktrees"
ROUTES_DIR="$STATE_ROOT/routes"
LOCKS_DIR="$STATE_ROOT/locks"
# shellcheck disable=SC2034 # Used by scripts that source this library.
DEPLOY_COMPOSE="$REPO_ROOT/compose.deploy.yaml"
EDGE_COMPOSE="$REPO_ROOT/compose.edge.yaml"
EDGE_PROJECT="${JSB1_EDGE_PROJECT:-jsb1-edge}"
CADDY_CONFIG_PATH="/etc/caddy/jsb-config/Caddyfile"
BASE_DOMAIN="${JSB1_DEPLOYMENT_BASE_DOMAIN:-mangagaki.net}"
MAIN_HOSTNAME="${JSB1_DEPLOYMENT_MAIN_HOSTNAME:-jsb.mangagaki.net}"
MAIN_BRANCH="${JSB1_DEPLOYMENT_MAIN_BRANCH:-main}"
PORT_START="${JSB1_DEPLOYMENT_PORT_START:-8100}"
PORT_END="${JSB1_DEPLOYMENT_PORT_END:-8999}"
TLS_CERT_PATH="${JSB1_TLS_CERT_PATH:-/Users/yang/.cloudflare/jsb-origin.pem}"
TLS_KEY_PATH="${JSB1_TLS_KEY_PATH:-/Users/yang/.cloudflare/jsb-origin.key}"
EDGE_HTTPS_PORT="${JSB1_EDGE_HTTPS_PORT:-4443}"
CLOUDFLARE_TUNNEL="${JSB1_CLOUDFLARE_TUNNEL:-jsb1}"
CLOUDFLARED_ORIGIN_CERT="${JSB1_CLOUDFLARED_ORIGIN_CERT:-$HOME/.cloudflared/cert.pem}"
CLOUDFLARED_CREDENTIALS="${JSB1_CLOUDFLARED_CREDENTIALS:-}"
CLOUDFLARED_CONFIG="$STATE_ROOT/cloudflared/config.yml"
CLOUDFLARE_API_TOKEN="${CLOUDFLARE_API_TOKEN:-${CF_API_TOKEN:-}}"
# shellcheck disable=SC2034 # Used by deploy and cleanup entrypoints.
DEPLOY_BUILDER="${JSB1_DEPLOY_BUILDER:-}"
GITHUB_REPOSITORY="${JSB1_GITHUB_REPOSITORY:-egod1537/jsb1}"
GITHUB_DEPLOYMENT_REQUIRED="${JSB1_GITHUB_DEPLOYMENT_REQUIRED:-false}"
GITHUB_DEPLOYMENT_HELPER="${JSB1_GITHUB_DEPLOYMENT_HELPER:-$REPO_ROOT/scripts/github_deployment.py}"
GITHUB_DEPLOYMENT_CREATED=false
GITHUB_COMMIT_STATUS_STARTED=false
GITHUB_REPORTING_WARNING_SHOWN=false

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

init_state() {
  mkdir -p "$UNITS_DIR" "$WORKTREES_DIR" "$ROUTES_DIR" "$LOCKS_DIR"
  if [[ ! -e "$ROUTES_DIR/_empty.caddy" ]]; then
    printf '# Keeps the Caddy import valid before the first deployment.\n' >"$ROUTES_DIR/_empty.caddy"
  fi
}

validate_runtime_config() {
  [[ "$BASE_DOMAIN" =~ ^[a-z0-9]([a-z0-9.-]*[a-z0-9])$ && "$BASE_DOMAIN" == *.* ]] \
    || die "invalid deployment base domain: $BASE_DOMAIN"
  [[ "$MAIN_HOSTNAME" =~ ^[a-z0-9]([a-z0-9.-]*[a-z0-9])$ && "$MAIN_HOSTNAME" == *.* ]] \
    || die "invalid main hostname: $MAIN_HOSTNAME"
  [[ "$MAIN_HOSTNAME" == *".$BASE_DOMAIN" && "${MAIN_HOSTNAME%."$BASE_DOMAIN"}" != *.* ]] \
    || die "main hostname must be exactly one label beneath $BASE_DOMAIN"
  [[ "$PORT_START" =~ ^[0-9]+$ && "$PORT_END" =~ ^[0-9]+$ ]] \
    || die "deployment port range must be numeric"
  (( PORT_START >= 1024 && PORT_START < PORT_END && PORT_END <= 65535 )) \
    || die "invalid deployment port range: $PORT_START-$PORT_END"
  if [[ ! "$EDGE_HTTPS_PORT" =~ ^[0-9]+$ ]] \
    || (( EDGE_HTTPS_PORT < 1 || EDGE_HTTPS_PORT > 65535 )); then
    die "invalid edge HTTPS port: $EDGE_HTTPS_PORT"
  fi
  [[ "$GITHUB_DEPLOYMENT_REQUIRED" == true || "$GITHUB_DEPLOYMENT_REQUIRED" == false ]] \
    || die "JSB1_GITHUB_DEPLOYMENT_REQUIRED must be true or false"
  [[ "$GITHUB_REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] \
    || die "JSB1_GITHUB_REPOSITORY must use owner/name format"
}

acquire_lock() {
  local name="$1"
  local lock_dir="$LOCKS_DIR/$name.lock"
  if ! mkdir "$lock_dir" 2>/dev/null; then
    die "another deployment operation is already running for $name"
  fi
  DEPLOY_LOCK_DIR="$lock_dir"
  trap 'release_lock' EXIT
}

release_lock() {
  if [[ -n "${DEPLOY_LOCK_DIR:-}" && -d "$DEPLOY_LOCK_DIR" ]]; then
    rmdir "$DEPLOY_LOCK_DIR" 2>/dev/null || true
  fi
}

validate_branch_name() {
  local branch="$1"
  [[ -n "$branch" ]] || die "branch is required"
  git check-ref-format --branch "$branch" >/dev/null 2>&1 || die "invalid git branch name: $branch"
}

slug_for_branch() {
  local branch="$1"
  python3 - "$branch" "$MAIN_BRANCH" "$MAIN_HOSTNAME" "$BASE_DOMAIN" "$UNITS_DIR" <<'PY'
import hashlib
import re
import sys
from pathlib import Path

branch, main_branch, main_hostname, base_domain, units_value = sys.argv[1:]
units = Path(units_value)
for item in units.iterdir() if units.exists() else ():
    marker = item / "branch"
    if marker.is_file() and marker.read_text(encoding="utf-8").rstrip("\n") == branch:
        print(item.name)
        raise SystemExit
if branch == main_branch:
    print("main")
    raise SystemExit
base = re.sub(r"[^a-z0-9-]+", "-", branch.lower())
base = re.sub(r"-+", "-", base).strip("-")[:48].rstrip("-")
if not base:
    raise SystemExit("branch does not produce a DNS-safe slug")
main_label = main_hostname[: -(len(base_domain) + 1)]
reserved = base in {"main", main_label}
marker = units / base / "branch"
collision = marker.is_file() and marker.read_text(encoding="utf-8").rstrip("\n") != branch
if reserved or collision:
    digest = hashlib.sha256(branch.encode()).hexdigest()[:8]
    base = f"{base[:39].rstrip('-')}-{digest}"
print(base)
PY
}

unit_value() {
  local unit_dir="$1"
  local name="$2"
  [[ -f "$unit_dir/$name" ]] || return 1
  sed -n '1p' "$unit_dir/$name"
}

unit_dir_for_branch() {
  local branch="$1"
  local unit_dir
  for unit_dir in "$UNITS_DIR"/*; do
    [[ -d "$unit_dir" && -f "$unit_dir/branch" ]] || continue
    if [[ "$(unit_value "$unit_dir" branch)" == "$branch" ]]; then
      printf '%s\n' "$unit_dir"
      return 0
    fi
  done
  return 1
}

atomic_value() {
  local unit_dir="$1"
  local name="$2"
  local value="$3"
  local temp_file="$unit_dir/.$name.$$"
  printf '%s\n' "$value" >"$temp_file"
  mv "$temp_file" "$unit_dir/$name"
}

github_environment_for_slug() {
  local slug="$1"
  printf 'jsb1/%s\n' "$slug"
}

github_commit_status_context_for_slug() {
  local slug="$1"
  printf 'jsb1/deploy/%s\n' "$slug"
}

github_auth_token() {
  if [[ -n "${JSB1_GITHUB_TOKEN:-}" ]]; then
    printf '%s\n' "$JSB1_GITHUB_TOKEN"
  elif [[ -n "${GITHUB_TOKEN:-}" ]]; then
    printf '%s\n' "$GITHUB_TOKEN"
  else
    return 1
  fi
}

github_reporting_warning() {
  local message="$1"
  if [[ "$GITHUB_REPORTING_WARNING_SHOWN" != true ]]; then
    printf 'warning: GitHub reporting %s\n' "$message" >&2
    GITHUB_REPORTING_WARNING_SHOWN=true
  fi
  [[ "$GITHUB_DEPLOYMENT_REQUIRED" != true ]]
}

github_helper_error() {
  local error_file="$1"
  local message=""
  if [[ -s "$error_file" ]]; then
    message="$(sed -n '1p' "$error_file")"
  fi
  rm -f "$error_file"
  printf '%s\n' "${message:-GitHub API request failed}"
}

github_update_deployment_status() {
  local unit_dir="$1"
  local state="$2"
  local description="$3"
  local environment_url="${4:-}"
  local token deployment_id environment error_file message
  deployment_id="$(unit_value "$unit_dir" github-deployment-id 2>/dev/null || true)"
  environment="$(unit_value "$unit_dir" github-environment 2>/dev/null || true)"
  if [[ ! "$deployment_id" =~ ^[0-9]+$ || -z "$environment" ]]; then
    atomic_value "$unit_dir" github-deployment-status untracked
    github_reporting_warning "skipped: no GitHub deployment is recorded for this environment"
    return
  fi
  if ! token="$(github_auth_token)"; then
    atomic_value "$unit_dir" github-deployment-status disabled
    github_reporting_warning "skipped: JSB1_GITHUB_TOKEN/GITHUB_TOKEN is not configured"
    return
  fi
  error_file="$unit_dir/.github-error.$$"
  local -a arguments=(
    --repository "$GITHUB_REPOSITORY"
    status
    --deployment-id "$deployment_id"
    --state "$state"
    --environment "$environment"
    --description "$description"
  )
  if [[ -n "$environment_url" ]]; then
    arguments+=(--environment-url "$environment_url")
  fi
  if ! JSB1_GITHUB_TOKEN="$token" python3 "$GITHUB_DEPLOYMENT_HELPER" "${arguments[@]}" \
    >/dev/null 2>"$error_file"; then
    message="$(github_helper_error "$error_file")"
    atomic_value "$unit_dir" github-deployment-status error
    github_reporting_warning "failed: $message"
    return
  fi
  rm -f "$error_file"
  atomic_value "$unit_dir" github-deployment-status "$state"
}

github_update_commit_status() {
  local unit_dir="$1"
  local commit="$2"
  local state="$3"
  local context="$4"
  local description="$5"
  local target_url="${6:-}"
  local token error_file message
  local -a arguments
  atomic_value "$unit_dir" github-commit-status-context "$context"
  atomic_value "$unit_dir" github-commit-status-commit "$commit"
  if ! token="$(github_auth_token)"; then
    atomic_value "$unit_dir" github-commit-status disabled
    github_reporting_warning "skipped: JSB1_GITHUB_TOKEN/GITHUB_TOKEN is not configured"
    return
  fi
  error_file="$unit_dir/.github-commit-error.$$"
  arguments=(
    --repository "$GITHUB_REPOSITORY"
    status-commit
    --commit "$commit"
    --state "$state"
    --context "$context"
    --description "$description"
  )
  if [[ -n "$target_url" ]]; then
    arguments+=(--target-url "$target_url")
  fi
  if ! JSB1_GITHUB_TOKEN="$token" python3 "$GITHUB_DEPLOYMENT_HELPER" "${arguments[@]}" \
    >/dev/null 2>"$error_file"; then
    message="$(github_helper_error "$error_file")"
    atomic_value "$unit_dir" github-commit-status error
    github_reporting_warning "failed: $message"
    return
  fi
  rm -f "$error_file"
  atomic_value "$unit_dir" github-commit-status "$state"
  if [[ "$state" == pending ]]; then
    # shellcheck disable=SC2034 # Read by deploy.sh's EXIT trap.
    GITHUB_COMMIT_STATUS_STARTED=true
  fi
  return 0
}

github_create_deployment() {
  local unit_dir="$1"
  local commit="$2"
  local environment="$3"
  local branch="$4"
  local hostname="$5"
  local token deployment_id error_file message
  local -a arguments
  GITHUB_DEPLOYMENT_CREATED=false
  atomic_value "$unit_dir" github-environment "$environment"
  if ! token="$(github_auth_token)"; then
    atomic_value "$unit_dir" github-deployment-status disabled
    github_reporting_warning "skipped: JSB1_GITHUB_TOKEN/GITHUB_TOKEN is not configured"
    return
  fi
  error_file="$unit_dir/.github-error.$$"
  arguments=(
    --repository "$GITHUB_REPOSITORY"
    create
    --commit "$commit"
    --environment "$environment"
    --description "Deploy $branch to https://$hostname"
  )
  if [[ "$branch" == "$MAIN_BRANCH" ]]; then
    arguments+=(--production-environment)
  fi
  if ! deployment_id="$(JSB1_GITHUB_TOKEN="$token" python3 "$GITHUB_DEPLOYMENT_HELPER" \
    "${arguments[@]}" 2>"$error_file")"; then
    message="$(github_helper_error "$error_file")"
    atomic_value "$unit_dir" github-deployment-status error
    github_reporting_warning "failed: $message"
    return
  fi
  rm -f "$error_file"
  [[ "$deployment_id" =~ ^[0-9]+$ ]] || {
    atomic_value "$unit_dir" github-deployment-status error
    github_reporting_warning "failed: GitHub returned an invalid deployment id"
    return
  }
  atomic_value "$unit_dir" github-deployment-id "$deployment_id"
  atomic_value "$unit_dir" github-deployment-commit "$commit"
  atomic_value "$unit_dir" github-deployment-status created
  # shellcheck disable=SC2034 # Read by deploy.sh after this sourced function returns.
  GITHUB_DEPLOYMENT_CREATED=true
  github_update_deployment_status "$unit_dir" in_progress "Deployment in progress"
}

github_mark_environment_inactive() {
  local unit_dir="$1"
  github_update_deployment_status "$unit_dir" inactive "Environment undeployed"
}

record_successful_commit() {
  local unit_dir="$1"
  local commit="$2"
  local successful
  successful="$(unit_value "$unit_dir" successful-commit 2>/dev/null || true)"
  if [[ -n "$successful" && "$successful" != "$commit" ]]; then
    atomic_value "$unit_dir" previous-successful-commit "$successful"
  elif [[ -z "$successful" ]]; then
    rm -f "$unit_dir/previous-successful-commit"
  fi
  atomic_value "$unit_dir" successful-commit "$commit"
}

project_for_slug() {
  local slug="$1"
  printf 'jsb1-%s\n' "$slug"
}

hostname_for() {
  local branch="$1"
  local slug="$2"
  if [[ "$branch" == "$MAIN_BRANCH" ]]; then
    printf '%s\n' "$MAIN_HOSTNAME"
  else
    printf '%s-jsb.%s\n' "$slug" "$BASE_DOMAIN"
  fi
}

allocate_port() {
  local unit_dir="$1"
  local project="$2"
  local existing=""
  if existing="$(unit_value "$unit_dir" port 2>/dev/null)" && [[ "$existing" =~ ^[0-9]+$ ]]; then
    if python3 - "$existing" <<'PY'
import socket, sys
s = socket.socket()
try:
    s.bind(("127.0.0.1", int(sys.argv[1])))
except OSError:
    raise SystemExit(1)
finally:
    s.close()
PY
    then
      printf '%s\n' "$existing"
      return
    fi
    if docker ps --filter "label=com.docker.compose.project=$project" --filter "label=com.docker.compose.service=web" -q | grep -q .; then
      printf '%s\n' "$existing"
      return
    fi
    die "saved port $existing is occupied by another process"
  fi
  python3 - "$PORT_START" "$PORT_END" "$UNITS_DIR" "$unit_dir" "$LOCKS_DIR/ports.lock" <<'PY'
import fcntl
import os
import socket
import sys
from pathlib import Path

start, end = map(int, sys.argv[1:3])
units = Path(sys.argv[3])
unit = Path(sys.argv[4])
lock_path = Path(sys.argv[5])
with lock_path.open("w") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    used = set()
    for marker in units.glob("*/port"):
        try:
            used.add(int(marker.read_text().strip()))
        except ValueError:
            pass
    for port in range(start, end + 1):
        if port in used:
            continue
        sock = socket.socket()
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            continue
        finally:
            sock.close()
        temporary = unit / f".port.{os.getpid()}"
        temporary.write_text(f"{port}\n", encoding="utf-8")
        os.replace(temporary, unit / "port")
        print(port)
        raise SystemExit
raise SystemExit("no free deployment port")
PY
}

validate_tls_files() {
  [[ -r "$TLS_CERT_PATH" && -f "$TLS_CERT_PATH" ]] || die "TLS certificate is not readable: $TLS_CERT_PATH"
  [[ -r "$TLS_KEY_PATH" && -f "$TLS_KEY_PATH" ]] || die "TLS private key is not readable: $TLS_KEY_PATH"
  local repo_real cert_real key_real key_mode
  repo_real="$(cd "$REPO_ROOT" && pwd -P)"
  cert_real="$(cd "$(dirname "$TLS_CERT_PATH")" && pwd -P)/$(basename "$TLS_CERT_PATH")"
  key_real="$(cd "$(dirname "$TLS_KEY_PATH")" && pwd -P)/$(basename "$TLS_KEY_PATH")"
  case "$cert_real" in "$repo_real"/*) die "TLS certificate must stay outside the repository" ;; esac
  case "$key_real" in "$repo_real"/*) die "TLS private key must stay outside the repository" ;; esac
  key_mode="$(stat -f '%OLp' "$TLS_KEY_PATH" 2>/dev/null || stat -c '%a' "$TLS_KEY_PATH")"
  (( (8#$key_mode & 7) == 0 )) || die "TLS private key must not be world-readable"
  python3 - "$TLS_CERT_PATH" "$MAIN_HOSTNAME" "$BASE_DOMAIN" <<'PY'
import ssl
import sys

cert_path, main_hostname, base = sys.argv[1:]
decoded = ssl._ssl._test_decode_cert(cert_path)
sans = {value.lower().rstrip(".") for kind, value in decoded.get("subjectAltName", ()) if kind == "DNS"}
if main_hostname not in sans and f"*.{base}" not in sans:
    raise SystemExit(f"certificate SAN does not include {main_hostname} or *.{base}")
if f"*.{base}" not in sans:
    raise SystemExit(f"certificate SAN does not include *.{base}")
PY
  local cert_hash key_hash
  cert_hash="$(openssl x509 -in "$TLS_CERT_PATH" -pubkey -noout | openssl sha256)"
  key_hash="$(openssl pkey -in "$TLS_KEY_PATH" -pubout 2>/dev/null | openssl sha256)"
  [[ -n "$cert_hash" && "$cert_hash" == "$key_hash" ]] || die "TLS certificate and private key do not match"
}

export_compose_values() {
  local unit_dir="$1"
  JSB1_BUILD_CONTEXT="$(unit_value "$unit_dir" worktree)"
  JSB1_DEPLOY_BACKEND_DOCKERFILE="$REPO_ROOT/deploy/backend.Dockerfile"
  JSB1_DEPLOY_FRONTEND_DOCKERFILE="$REPO_ROOT/deploy/frontend.Dockerfile"
  JSB1_DEPLOY_CONFIG_CONTEXT="$REPO_ROOT/deploy"
  JSB1_DEPLOY_DATA_DIR="$unit_dir/data"
  JSB1_SCENARIO_DIR="$(unit_value "$unit_dir" scenario-path)"
  JSB1_DEPLOY_SLUG="$(basename "$unit_dir")"
  JSB1_DEPLOY_BRANCH="$(unit_value "$unit_dir" branch)"
  JSB1_DEPLOY_COMMIT="$(unit_value "$unit_dir" commit)"
  JSB1_DEPLOY_BUILT_AT="$(unit_value "$unit_dir" built-at 2>/dev/null || printf unknown)"
  JSB1_DEPLOY_HOSTNAME="$(unit_value "$unit_dir" hostname)"
  JSB1_DEPLOY_PORT="$(unit_value "$unit_dir" port)"
  JSB1_CADDY_ROUTES_DIR="$ROUTES_DIR"
  JSB1_TLS_CERT_PATH="$TLS_CERT_PATH"
  JSB1_TLS_KEY_PATH="$TLS_KEY_PATH"
  JSB1_CLOUDFLARED_CONFIG_PATH="$CLOUDFLARED_CONFIG"
  JSB1_CLOUDFLARED_CREDENTIALS="$CLOUDFLARED_CREDENTIALS"
  export JSB1_BUILD_CONTEXT JSB1_DEPLOY_BACKEND_DOCKERFILE
  export JSB1_DEPLOY_FRONTEND_DOCKERFILE JSB1_DEPLOY_CONFIG_CONTEXT
  export JSB1_DEPLOY_DATA_DIR JSB1_SCENARIO_DIR JSB1_DEPLOY_SLUG JSB1_DEPLOY_BRANCH
  export JSB1_DEPLOY_COMMIT JSB1_DEPLOY_BUILT_AT JSB1_DEPLOY_HOSTNAME
  export JSB1_DEPLOY_PORT JSB1_CADDY_ROUTES_DIR
  export JSB1_TLS_CERT_PATH JSB1_TLS_KEY_PATH JSB1_CLOUDFLARED_CONFIG_PATH
  export JSB1_CLOUDFLARED_CREDENTIALS
}

resolve_cloudflare_tunnel() {
  local credential_mode tunnel_json
  if [[ "$CLOUDFLARE_TUNNEL" =~ ^[0-9a-fA-F-]{36}$ ]]; then
    TUNNEL_ID="${CLOUDFLARE_TUNNEL,,}"
  else
    tunnel_json="$(cloudflared tunnel --origincert "$CLOUDFLARED_ORIGIN_CERT" list --output json)" \
      || die "could not list Cloudflare tunnels with $CLOUDFLARED_ORIGIN_CERT"
    TUNNEL_ID="$(python3 - "$CLOUDFLARE_TUNNEL" "$tunnel_json" <<'PY'
import json
import sys

name, raw = sys.argv[1:]
matches = [item["id"] for item in json.loads(raw) if item.get("name") == name]
if len(matches) != 1:
    raise SystemExit(f"expected one Cloudflare tunnel named {name!r}, found {len(matches)}")
print(matches[0])
PY
)" || die "could not resolve Cloudflare tunnel: $CLOUDFLARE_TUNNEL"
  fi

  if [[ -z "$CLOUDFLARED_CREDENTIALS" ]]; then
    CLOUDFLARED_CREDENTIALS="$HOME/.cloudflared/$TUNNEL_ID.json"
  fi
  [[ -r "$CLOUDFLARED_CREDENTIALS" && -f "$CLOUDFLARED_CREDENTIALS" ]] \
    || die "Cloudflare tunnel credentials are not readable: $CLOUDFLARED_CREDENTIALS"
  case "$(cd "$(dirname "$CLOUDFLARED_CREDENTIALS")" && pwd -P)/$(basename "$CLOUDFLARED_CREDENTIALS")" in
    "$REPO_ROOT"/*) die "Cloudflare tunnel credentials must stay outside the repository" ;;
  esac
  credential_mode="$(stat -f '%OLp' "$CLOUDFLARED_CREDENTIALS" 2>/dev/null || stat -c '%a' "$CLOUDFLARED_CREDENTIALS")"
  (( (8#$credential_mode & 7) == 0 )) \
    || die "Cloudflare tunnel credentials must not be world-readable"
  python3 - "$CLOUDFLARED_CREDENTIALS" "$TUNNEL_ID" <<'PY'
import json
import sys

path, expected = sys.argv[1:]
with open(path, encoding="utf-8") as stream:
    actual = json.load(stream).get("TunnelID", "").lower()
if actual != expected.lower():
    raise SystemExit("Cloudflare credential TunnelID does not match the selected tunnel")
PY
  export TUNNEL_ID JSB1_CLOUDFLARED_CREDENTIALS="$CLOUDFLARED_CREDENTIALS"
}

write_cloudflared_config() {
  local config_dir temp_config
  config_dir="$(dirname "$CLOUDFLARED_CONFIG")"
  temp_config="$config_dir/.config.yml.$$"
  mkdir -p "$config_dir"
  cat >"$temp_config" <<EOF
tunnel: $TUNNEL_ID
credentials-file: /etc/cloudflared/credentials.json

ingress:
  - hostname: $MAIN_HOSTNAME
    service: https://edge:443
    originRequest:
      noTLSVerify: true
  - hostname: "*.$BASE_DOMAIN"
    service: https://edge:443
    originRequest:
      noTLSVerify: true
  - service: http_status:404
EOF
  cloudflared tunnel --config "$temp_config" ingress validate >/dev/null \
    || die "generated cloudflared ingress configuration is invalid"
  mv "$temp_config" "$CLOUDFLARED_CONFIG"
}

edge_compose() {
  JSB1_CADDY_ROUTES_DIR="$ROUTES_DIR" \
  JSB1_TLS_CERT_PATH="$TLS_CERT_PATH" \
  JSB1_TLS_KEY_PATH="$TLS_KEY_PATH" \
  JSB1_EDGE_HTTPS_PORT="$EDGE_HTTPS_PORT" \
  JSB1_CLOUDFLARED_CONFIG_PATH="$CLOUDFLARED_CONFIG" \
  JSB1_CLOUDFLARED_CREDENTIALS="$CLOUDFLARED_CREDENTIALS" \
    docker compose -p "$EDGE_PROJECT" -f "$EDGE_COMPOSE" "$@"
}

ensure_edge() {
  write_cloudflared_config
  edge_compose up -d --wait edge tunnel
}

ensure_cloudflare_dns_route() {
  local unit_dir="$1"
  local hostname="$2"
  local saved_tunnel=""
  local overwrite=false
  saved_tunnel="$(unit_value "$unit_dir" tunnel-id 2>/dev/null || true)"
  if [[ "$saved_tunnel" == "$TUNNEL_ID" || "$hostname" == "$MAIN_HOSTNAME" || "${JSB1_CLOUDFLARE_OVERWRITE_DNS:-false}" == true ]]; then
    overwrite=true
  fi

  if [[ -n "$CLOUDFLARE_API_TOKEN" ]]; then
    JSB1_CF_TOKEN="$CLOUDFLARE_API_TOKEN" python3 - "$BASE_DOMAIN" "$hostname" "$TUNNEL_ID" "$overwrite" <<'PY'
import json
import os
import sys
import urllib.parse
import urllib.request

zone_name, hostname, tunnel_id, overwrite_value = sys.argv[1:]
target = f"{tunnel_id}.cfargotunnel.com"
headers = {"Authorization": f"Bearer {os.environ['JSB1_CF_TOKEN']}", "Content-Type": "application/json"}

def request(method, path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(f"https://api.cloudflare.com/client/v4{path}", data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=20) as response:
        body = json.load(response)
    if not body.get("success"):
        raise SystemExit("Cloudflare API request failed")
    return body["result"]

zones = request("GET", "/zones?" + urllib.parse.urlencode({"name": zone_name, "status": "active"}))
if len(zones) != 1:
    raise SystemExit(f"expected one active Cloudflare zone for {zone_name}")
zone_id = zones[0]["id"]
records = request("GET", f"/zones/{zone_id}/dns_records?" + urllib.parse.urlencode({"name": hostname}))
payload = {"type": "CNAME", "name": hostname, "content": target, "proxied": True, "ttl": 1}
if not records:
    request("POST", f"/zones/{zone_id}/dns_records", payload)
elif len(records) == 1 and records[0]["type"] == "CNAME" and records[0]["content"].rstrip(".").lower() == target:
    if not records[0].get("proxied"):
        request("PUT", f"/zones/{zone_id}/dns_records/{records[0]['id']}", payload)
elif overwrite_value == "true":
    if len(records) != 1:
        raise SystemExit(f"refusing to overwrite multiple DNS records for {hostname}")
    request("PUT", f"/zones/{zone_id}/dns_records/{records[0]['id']}", payload)
else:
    raise SystemExit(f"DNS name {hostname} is already used by a record not managed by this deployment")
print(f"Cloudflare DNS ready: {hostname} -> {target}")
PY
  else
    local -a route_args=(tunnel --origincert "$CLOUDFLARED_ORIGIN_CERT" route dns)
    if [[ "$overwrite" == true ]]; then
      route_args+=(--overwrite-dns)
    fi
    cloudflared "${route_args[@]}" "$TUNNEL_ID" "$hostname" \
      || die "could not create Cloudflare DNS route; set CLOUDFLARE_API_TOKEN for record-aware idempotency"
  fi
  atomic_value "$unit_dir" tunnel-id "$TUNNEL_ID"
  atomic_value "$unit_dir" dns-managed true
}

delete_cloudflare_dns_route() {
  local unit_dir="$1"
  local hostname="$2"
  local update_state="${3:-true}"
  local tunnel_id
  tunnel_id="$(unit_value "$unit_dir" tunnel-id 2>/dev/null || true)"
  [[ -n "$tunnel_id" && "$(unit_value "$unit_dir" dns-managed 2>/dev/null || true)" == true ]] || return
  JSB1_CF_TOKEN="$CLOUDFLARE_API_TOKEN" JSB1_CF_ORIGIN_CERT="$CLOUDFLARED_ORIGIN_CERT" \
    python3 - "$BASE_DOMAIN" "$hostname" "$tunnel_id" <<'PY'
import base64
import binascii
import json
import os
import sys
import urllib.parse
import urllib.request

zone_name, hostname, tunnel_id = sys.argv[1:]
target = f"{tunnel_id}.cfargotunnel.com"
token = os.environ.get("JSB1_CF_TOKEN", "")
zone_id = ""
if not token:
    with open(os.environ["JSB1_CF_ORIGIN_CERT"], encoding="utf-8") as stream:
        lines = stream.read().splitlines()
    inside = False
    encoded = []
    for line in lines:
        if line == "-----BEGIN ARGO TUNNEL TOKEN-----":
            inside = True
        elif line == "-----END ARGO TUNNEL TOKEN-----":
            break
        elif inside:
            encoded.append(line.strip())
    try:
        credential = json.loads(base64.b64decode("".join(encoded)))
        token = credential["apiToken"]
        zone_id = credential["zoneID"]
    except (KeyError, ValueError, binascii.Error, json.JSONDecodeError) as exc:
        raise SystemExit("could not decode Cloudflare account credential") from exc
headers = {"Authorization": f"Bearer {token}", "Content-Type": "application/json"}

def request(method, path):
    req = urllib.request.Request(f"https://api.cloudflare.com/client/v4{path}", headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=20) as response:
        body = json.load(response)
    if not body.get("success"):
        raise SystemExit("Cloudflare API request failed")
    return body["result"]

if not zone_id:
    zones = request("GET", "/zones?" + urllib.parse.urlencode({"name": zone_name, "status": "active"}))
    if len(zones) != 1:
        raise SystemExit(f"expected one active Cloudflare zone for {zone_name}")
    zone_id = zones[0]["id"]
else:
    zone = request("GET", f"/zones/{zone_id}")
    if zone.get("name", "").rstrip(".").lower() != zone_name:
        raise SystemExit("Cloudflare account credential belongs to a different zone")
records = request("GET", f"/zones/{zone_id}/dns_records?" + urllib.parse.urlencode({"name": hostname, "type": "CNAME"}))
for record in records:
    if record["content"].rstrip(".").lower() == target:
        request("DELETE", f"/zones/{zone_id}/dns_records/{record['id']}")
        print(f"Removed Cloudflare DNS route: {hostname}")
PY
  if [[ "$update_state" == true ]]; then
    atomic_value "$unit_dir" dns-managed false
  fi
}

reload_edge() {
  edge_compose exec -T edge caddy validate --config "$CADDY_CONFIG_PATH" --adapter caddyfile >/dev/null
  edge_compose exec -T edge caddy reload --config "$CADDY_CONFIG_PATH" --adapter caddyfile >/dev/null
}

write_route() {
  local slug="$1"
  local hostname="$2"
  local port="$3"
  local route="$ROUTES_DIR/$slug.caddy"
  local temp_route="$ROUTES_DIR/.$slug.caddy.$$"
  cat >"$temp_route" <<EOF
https://$hostname {
	import jsb_tls
	reverse_proxy host.docker.internal:$port
}
EOF
  mv "$temp_route" "$route"
}

wait_for_http() {
  local port="$1"
  local deadline=$((SECONDS + ${JSB1_DEPLOYMENT_HEALTH_TIMEOUT_SEC:-180}))
  while (( SECONDS < deadline )); do
    if http_2xx "http://127.0.0.1:$port/api/health" 3; then
      return
    fi
    sleep 2
  done
  die "application health check timed out on port $port"
}

http_2xx() {
  local url="$1"
  local timeout="${2:-10}"
  shift 2
  local status
  status="$(curl --noproxy '*' --silent --show-error --output /dev/null \
    --write-out '%{http_code}' --max-time "$timeout" "$@" "$url" 2>/dev/null)" || return 1
  [[ "$status" =~ ^2[0-9][0-9]$ ]]
}

read_version_metadata() {
  local url="$1"
  local timeout="${2:-10}"
  local response
  response="$(curl --noproxy '*' --fail --silent --show-error --max-time "$timeout" "$url" 2>/dev/null)" \
    || return 1
  python3 - "$response" <<'PY'
import json
import sys

try:
    payload = json.loads(sys.argv[1])
except (IndexError, json.JSONDecodeError):
    raise SystemExit(1)
required = ("branch", "commit", "short_commit", "built_at", "hostname")
if not isinstance(payload, dict) or any(key not in payload for key in required):
    raise SystemExit(1)
if not all(isinstance(payload[key], str) for key in required[:-1]):
    raise SystemExit(1)
if payload["hostname"] is not None and not isinstance(payload["hostname"], str):
    raise SystemExit(1)
print(f'{payload["branch"]}\t{payload["commit"]}')
PY
}

verify_https_route() {
  local hostname="$1"
  local expected_fingerprint served_fingerprint
  http_2xx "https://$hostname:$EDGE_HTTPS_PORT/api/health" 5 \
    --insecure --resolve "$hostname:$EDGE_HTTPS_PORT:127.0.0.1" \
    || die "local edge HTTPS health check failed for $hostname"
  expected_fingerprint="$(openssl x509 -in "$TLS_CERT_PATH" -noout -fingerprint -sha256 | tr '[:lower:]' '[:upper:]')"
  served_fingerprint="$(printf '\n' | openssl s_client -connect "127.0.0.1:$EDGE_HTTPS_PORT" -servername "$hostname" 2>/dev/null | openssl x509 -noout -fingerprint -sha256 | tr '[:lower:]' '[:upper:]')"
  [[ "$served_fingerprint" == "$expected_fingerprint" ]] || die "edge did not serve the configured Origin Certificate"
}

verify_public_https_route() {
  local hostname="$1"
  local deadline=$((SECONDS + ${JSB1_DEPLOYMENT_PUBLIC_TIMEOUT_SEC:-180}))
  while (( SECONDS < deadline )); do
    if http_2xx "https://$hostname/api/health" 10; then
      return
    fi
    sleep "${JSB1_DEPLOYMENT_HEALTH_INTERVAL_SEC:-2}"
  done
  die "public Cloudflare HTTPS check timed out for https://$hostname"
}

remove_old_worktrees() {
  local slug="$1"
  local current="$2"
  local previous_commit="${3:-}"
  local previous_tree=""
  if [[ "$previous_commit" =~ ^[0-9a-f]{40}$ ]]; then
    previous_tree="$WORKTREES_DIR/$slug/${previous_commit:0:12}"
  fi
  local old_tree
  for old_tree in "$WORKTREES_DIR/$slug"/*; do
    [[ -e "$old_tree/.git" && "$old_tree" != "$current" && "$old_tree" != "$previous_tree" ]] || continue
    git -C "$REPO_ROOT" worktree remove --force "$old_tree"
  done
  git -C "$REPO_ROOT" worktree prune
}
