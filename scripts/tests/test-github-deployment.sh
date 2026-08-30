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
atomic_value "$unit_dir" branch impl

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
github_update_commit_status \
  "$unit_dir" "$commit" pending "jsb1/deploy/impl" "Deploying impl" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-commit-status)" == disabled ]]
github_verify_commit_status \
  "$unit_dir" "$commit" success "jsb1/deploy/impl"
[[ "$(unit_value "$unit_dir" github-commit-status-verification)" == disabled ]]

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
[[ "$(github_commit_status_context_for_slug feature-foo)" == jsb1/deploy/feature-foo ]]

github_update_commit_status \
  "$unit_dir" "$commit" pending "jsb1/deploy/impl" "Deploying impl" \
  "https://impl-jsb.mangagaki.net"
[[ "$GITHUB_COMMIT_STATUS_STARTED" == true ]]
[[ "$(unit_value "$unit_dir" github-commit-status)" == pending ]]
[[ "$(unit_value "$unit_dir" github-commit-status-verification)" == pending ]]
[[ "$(unit_value "$unit_dir" github-commit-status-context)" == jsb1/deploy/impl ]]
[[ "$(unit_value "$unit_dir" github-commit-status-commit)" == "$commit" ]]

github_update_deployment_status \
  "$unit_dir" success "Deployment completed successfully" "https://impl-jsb.mangagaki.net"
github_update_deployment_status "$unit_dir" failure "application health check failed"
github_mark_environment_inactive "$unit_dir"
[[ "$(unit_value "$unit_dir" github-deployment-status)" == inactive ]]
github_update_commit_status \
  "$unit_dir" "$commit" success "jsb1/deploy/impl" "Deployment successful" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-commit-status)" == success ]]
github_verify_commit_status \
  "$unit_dir" "$commit" success "jsb1/deploy/impl"
[[ "$(unit_value "$unit_dir" github-commit-status)" == success ]]
[[ "$(unit_value "$unit_dir" github-commit-status-verification)" == verified ]]
github_update_commit_status \
  "$unit_dir" "$commit" failure "jsb1/deploy/impl" "Health check failed" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-commit-status)" == failure ]]
grep -F -- "--commit $commit --environment jsb1/impl" "$mock_log" >/dev/null
grep -F -- "--state in_progress" "$mock_log" >/dev/null
grep -F -- "--state success" "$mock_log" >/dev/null
grep -F -- "--state failure" "$mock_log" >/dev/null
grep -F -- "--state inactive" "$mock_log" >/dev/null
grep -F -- "status-commit --commit $commit --state pending --context jsb1/deploy/impl" "$mock_log" >/dev/null
grep -F -- "status-commit --commit $commit --state success --context jsb1/deploy/impl" "$mock_log" >/dev/null
grep -F -- "verify-status --commit $commit --state success --context jsb1/deploy/impl" "$mock_log" >/dev/null
grep -F -- "--target-url https://impl-jsb.mangagaki.net" "$mock_log" >/dev/null
if grep -F -- "$JSB1_GITHUB_TOKEN" "$mock_log" >/dev/null; then
  printf 'GitHub token leaked into helper arguments\n' >&2
  exit 1
fi

new_commit="a20391f7d40fe05fecba69543c40ba1a64598a20"
# A recreated branch keeps its deterministic context but reports against its new SHA.
github_create_deployment "$unit_dir" "$new_commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
github_update_commit_status \
  "$unit_dir" "$new_commit" pending "jsb1/deploy/impl" "Deploying impl" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-commit-status)" == pending ]]
[[ "$(unit_value "$unit_dir" github-commit-status-verification)" == pending ]]
github_update_commit_status \
  "$unit_dir" "$new_commit" success "jsb1/deploy/impl" "Deployment successful" \
  "https://impl-jsb.mangagaki.net"
github_verify_commit_status \
  "$unit_dir" "$new_commit" success "jsb1/deploy/impl"
github_create_deployment "$unit_dir" "$commit" "jsb1/impl" impl "impl-jsb.mangagaki.net"
github_update_commit_status \
  "$unit_dir" "$commit" pending "jsb1/deploy/impl" "Deploying impl" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-deployment-commit)" == "$commit" ]]
[[ "$(grep -c -- '--environment jsb1/impl' "$mock_log")" -ge 3 ]]
grep -F -- "--commit $new_commit --environment jsb1/impl" "$mock_log" >/dev/null
grep -F -- "--commit $commit --environment jsb1/impl" "$mock_log" >/dev/null
grep -F -- "status-commit --commit $new_commit --state pending" "$mock_log" >/dev/null
grep -F -- "status-commit --commit $new_commit --state success" "$mock_log" >/dev/null
grep -F -- "verify-status --commit $new_commit --state success" "$mock_log" >/dev/null
grep -F -- "status-commit --commit $commit --state pending" "$mock_log" >/dev/null

export MOCK_GITHUB_MODE=fail
GITHUB_DEPLOYMENT_REQUIRED=false
github_update_deployment_status "$unit_dir" failure "container build failed"
[[ "$(unit_value "$unit_dir" github-deployment-status)" == error ]]
github_update_commit_status \
  "$unit_dir" "$commit" failure "jsb1/deploy/impl" "Build failed" \
  "https://impl-jsb.mangagaki.net"
[[ "$(unit_value "$unit_dir" github-commit-status)" == error ]]

GITHUB_DEPLOYMENT_REQUIRED=true
if github_update_deployment_status "$unit_dir" failure "container build failed"; then
  printf 'strict mode unexpectedly accepted an API failure\n' >&2
  exit 1
fi
if github_update_commit_status \
  "$unit_dir" "$commit" failure "jsb1/deploy/impl" "Build failed" \
  "https://impl-jsb.mangagaki.net"; then
  printf 'strict mode unexpectedly accepted a commit status API failure\n' >&2
  exit 1
fi
github_verify_commit_status \
  "$unit_dir" "$commit" success "jsb1/deploy/impl"
[[ "$(unit_value "$unit_dir" github-commit-status)" == error ]]
[[ "$(unit_value "$unit_dir" github-commit-status-verification)" == error ]]

status_output="$(
  JSB1_DEPLOY_STATE_DIR="$STATE_ROOT" \
  JSB1_GITHUB_TOKEN=test-token \
  "$REPO_ROOT/list-deployments.sh"
)"
grep -F -- "GITHUB_DEPLOY" <<<"$status_output" >/dev/null
grep -F -- "GITHUB_COMMIT" <<<"$status_output" >/dev/null
grep -F -- "GITHUB_VERIFY" <<<"$status_output" >/dev/null
grep -F -- "jsb1/deploy/impl" <<<"$status_output" >/dev/null
grep -F -- "${commit:0:12}" <<<"$status_output" >/dev/null
grep -E -- 'impl[[:space:]]+unknown[[:space:]]+error[[:space:]]+error' \
  <<<"$status_output" >/dev/null
if grep -F -- "github_update_commit_status" "$REPO_ROOT/undeploy.sh" >/dev/null; then
  printf 'undeploy must not rewrite commit status history\n' >&2
  exit 1
fi

pending_line="$(grep -n -m1 'deployment_stage="GitHub commit status initialization"' \
  "$REPO_ROOT/deploy.sh" | cut -d: -f1)"
deployment_line="$(grep -n -m1 'deployment_stage="GitHub deployment initialization"' \
  "$REPO_ROOT/deploy.sh" | cut -d: -f1)"
build_line="$(grep -n -m1 'deployment_stage="container build"' \
  "$REPO_ROOT/deploy.sh" | cut -d: -f1)"
(( pending_line < deployment_line && deployment_line < build_line ))
grep -F -- '"$SCRIPT_DIR/deploy.sh" "$1" --revision "$2"' \
  "$REPO_ROOT/auto-deploy.sh" >/dev/null
grep -F -- 'JSB1_AUTO_DEPLOY_REQUEST=true' \
  "$REPO_ROOT/auto-deploy.sh" >/dev/null
grep -F -- '"${JSB1_AUTO_DEPLOY_REQUEST:-false}" == true' \
  "$REPO_ROOT/deploy.sh" >/dev/null
grep -F -- 'exec "$SCRIPT_DIR/deploy.sh" "$branch" --revision "$target"' \
  "$REPO_ROOT/rollback.sh" >/dev/null

printf 'GitHub deployment shell policy tests passed.\n'
