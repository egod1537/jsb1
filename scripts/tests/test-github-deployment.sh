#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$REPO_ROOT/scripts/deploy-lib.sh"

test_root="$(mktemp -d "${TMPDIR:-/tmp}/jsb1-github-test.XXXXXX")"
cleanup() {
  rm -rf "$test_root"
}
trap cleanup EXIT

STATE_ROOT="$test_root/state"
UNITS_DIR="$STATE_ROOT/units"
WORKTREES_DIR="$STATE_ROOT/worktrees"
ROUTES_DIR="$STATE_ROOT/routes"
LOCKS_DIR="$STATE_ROOT/locks"
unit_dir="$UNITS_DIR/impl"
mkdir -p "$unit_dir"

mock_helper="$test_root/github_helper.py"
mock_log="$test_root/requests.log"
cat >"$mock_helper" <<'PY'
import os
import sys

with open(os.environ["MOCK_GITHUB_LOG"], "a", encoding="utf-8") as stream:
    stream.write(" ".join(sys.argv[1:]) + "\n")
if os.environ.get("MOCK_GITHUB_MODE") == "fail":
    print("error: mock API unavailable", file=sys.stderr)
    raise SystemExit(1)
if "create" in sys.argv:
    print("2718")
PY
GITHUB_DEPLOYMENT_HELPER="$mock_helper"
export MOCK_GITHUB_LOG="$mock_log"
commit="8efb0664ab92f2df6155281415fbe33051366868"

unset JSB1_GITHUB_TOKEN GITHUB_TOKEN
GITHUB_DEPLOYMENT_REQUIRED=false
github_create_deployment "$unit_dir" "$commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-deployment-status)" == disabled ]]
[[ "$GITHUB_DEPLOYMENT_CREATED" == false ]]

GITHUB_DEPLOYMENT_REQUIRED=true
if github_create_deployment "$unit_dir" "$commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"; then
  printf 'strict mode unexpectedly accepted a missing token\n' >&2
  exit 1
fi

export JSB1_GITHUB_TOKEN=test-token
GITHUB_DEPLOYMENT_REQUIRED=false
github_create_deployment "$unit_dir" "$commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
[[ "$GITHUB_DEPLOYMENT_CREATED" == true ]]
[[ "$(unit_value "$unit_dir" github-deployment-id)" == 2718 ]]
[[ "$(unit_value "$unit_dir" github-deployment-status)" == in_progress ]]
[[ "$(unit_value "$unit_dir" github-deployment-commit)" == "$commit" ]]
[[ "$(github_environment_for_slug feature-foo)" == jsb1/feature-foo ]]

github_update_deployment_status \
  "$unit_dir" success "Deployment completed successfully" "https://impl-jsb.mangagaki.net"
github_update_deployment_status "$unit_dir" failure "application health check failed"
github_mark_environment_inactive "$unit_dir"
[[ "$(unit_value "$unit_dir" github-deployment-status)" == inactive ]]
grep -F -- "--commit $commit --environment jsb1/impl" "$mock_log" >/dev/null
grep -F -- "--state in_progress" "$mock_log" >/dev/null
grep -F -- "--state success" "$mock_log" >/dev/null
grep -F -- "--state failure" "$mock_log" >/dev/null
grep -F -- "--state inactive" "$mock_log" >/dev/null
if grep -F -- "$JSB1_GITHUB_TOKEN" "$mock_log" >/dev/null; then
  printf 'GitHub token leaked into helper arguments\n' >&2
  exit 1
fi

new_commit="a20391f7d40fe05fecba69543c40ba1a64598a20"
github_create_deployment "$unit_dir" "$new_commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
github_create_deployment "$unit_dir" "$commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-deployment-commit)" == "$commit" ]]
[[ "$(grep -c -- '--environment jsb1/impl' "$mock_log")" -ge 3 ]]
grep -F -- "--commit $new_commit --environment jsb1/impl" "$mock_log" >/dev/null
grep -F -- "--commit $commit --environment jsb1/impl" "$mock_log" >/dev/null

export MOCK_GITHUB_MODE=fail
GITHUB_DEPLOYMENT_REQUIRED=false
github_update_deployment_status "$unit_dir" failure "container build failed"
[[ "$(unit_value "$unit_dir" github-deployment-status)" == error ]]

GITHUB_DEPLOYMENT_REQUIRED=true
if github_update_deployment_status "$unit_dir" failure "container build failed"; then
  printf 'strict mode unexpectedly accepted an API failure\n' >&2
  exit 1
fi

printf 'GitHub deployment shell policy tests passed.\n'
