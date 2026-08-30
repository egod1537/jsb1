#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/jsb1-deploy-env-test.XXXXXX")"
cleanup() {
  rm -rf "$test_root"
}
trap cleanup EXIT

test_home="$test_root/home"
env_file="$test_home/.config/jsb1/deploy.env"
mkdir -p "$(dirname "$env_file")"

read_loaded_token() {
  (
    unset JSB1_GITHUB_TOKEN GITHUB_TOKEN
    HOME="$test_home" JSB1_DEPLOY_ENV_FILE="$env_file" \
      /bin/bash -c 'source "$1/scripts/deploy-lib.sh"; github_auth_token' \
      bash "$REPO_ROOT"
  )
}

missing_result="$(
  unset JSB1_GITHUB_TOKEN GITHUB_TOKEN
  HOME="$test_home" JSB1_DEPLOY_ENV_FILE="$test_root/missing.env" \
    /bin/bash -c 'source "$1/scripts/deploy-lib.sh"; printf "%s:%s\n" "$DEPLOY_ENV_STATUS" "${JSB1_GITHUB_TOKEN-unset}"' \
    bash "$REPO_ROOT"
)"
[[ "$missing_result" == missing:unset ]]

printf "export JSB1_GITHUB_TOKEN='file-token'\n" >"$env_file"
chmod 600 "$env_file"
[[ "$(read_loaded_token)" == file-token ]]
default_path_result="$(
  unset JSB1_GITHUB_TOKEN GITHUB_TOKEN JSB1_DEPLOY_ENV_FILE
  HOME="$test_home" \
    /bin/bash -c 'source "$1/scripts/deploy-lib.sh"; github_auth_token' \
    bash "$REPO_ROOT"
)"
[[ "$default_path_result" == file-token ]]

process_result="$(
  HOME="$test_home" JSB1_DEPLOY_ENV_FILE="$env_file" \
    JSB1_GITHUB_TOKEN=process-token \
    /bin/bash -c 'source "$1/scripts/deploy-lib.sh"; github_auth_token' \
    bash "$REPO_ROOT"
)"
[[ "$process_result" == process-token ]]

chmod 644 "$env_file"
permission_output="$(
  unset JSB1_GITHUB_TOKEN GITHUB_TOKEN
  HOME="$test_home" JSB1_DEPLOY_ENV_FILE="$env_file" \
    /bin/bash -c 'source "$1/scripts/deploy-lib.sh"; printf "%s\n" "$DEPLOY_ENV_STATUS"' \
    bash "$REPO_ROOT" 2>&1
)"
grep -F 'warning: deployment environment file has group/world permissions' \
  <<<"$permission_output" >/dev/null
grep -Fx configured <<<"$permission_output" >/dev/null
if grep -F -- file-token <<<"$permission_output" >/dev/null; then
  printf 'deployment environment warning leaked the token\n' >&2
  exit 1
fi
chmod 600 "$env_file"

mock_bin="$test_root/bin"
mock_crontab_file="$test_root/crontab"
mkdir -p "$mock_bin"
cat >"$mock_bin/crontab" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
if [[ "${1:-}" == -l ]]; then
  [[ ! -f "$MOCK_CRONTAB_FILE" ]] || cat "$MOCK_CRONTAB_FILE"
  exit 0
fi
[[ $# -eq 1 ]]
cp "$1" "$MOCK_CRONTAB_FILE"
SH
chmod +x "$mock_bin/crontab"
export MOCK_CRONTAB_FILE="$mock_crontab_file"

install_output="$(
  HOME="$test_home" PATH="$mock_bin:$PATH" \
    JSB1_DEPLOY_ENV_FILE="$env_file" \
    JSB1_DEPLOY_STATE_DIR="$test_root/state" \
    /bin/bash -c '
      source "$1/auto-deploy.sh"
      run_once() { :; }
      install_watcher
    ' bash "$REPO_ROOT"
)"
grep -F 'Automatic deployment installed' <<<"$install_output" >/dev/null
grep -F 'JSB1_DEPLOY_ENV_FILE=' "$mock_crontab_file" >/dev/null
grep -F -- "$env_file" "$mock_crontab_file" >/dev/null
if grep -F -- file-token "$mock_crontab_file" >/dev/null; then
  printf 'generated crontab contains the GitHub token\n' >&2
  exit 1
fi

status_output="$(
  HOME="$test_home" PATH="$mock_bin:$PATH" \
    JSB1_DEPLOY_ENV_FILE="$env_file" \
    JSB1_DEPLOY_STATE_DIR="$test_root/state" \
    "$REPO_ROOT/auto-deploy.sh" --status
)"
grep -F 'Deployment environment: configured' <<<"$status_output" >/dev/null
grep -F 'GitHub reporting credential: configured' <<<"$status_output" >/dev/null
if grep -F -- file-token <<<"$status_output" >/dev/null; then
  printf 'auto-deploy status leaked the GitHub token\n' >&2
  exit 1
fi

crontab_before="$(cksum "$mock_crontab_file")"
printf "export JSB1_GITHUB_TOKEN='rotated-token'\n" >"$env_file"
chmod 600 "$env_file"
[[ "$(read_loaded_token)" == rotated-token ]]
[[ "$(cksum "$mock_crontab_file")" == "$crontab_before" ]]

mock_helper="$test_root/github_helper.py"
mock_github_log="$test_root/github.log"
cat >"$mock_helper" <<'PY'
import os
import sys

if os.environ.get("JSB1_GITHUB_TOKEN") != os.environ["MOCK_EXPECTED_TOKEN"]:
    raise SystemExit("expected rotated deployment token")
with open(os.environ["MOCK_GITHUB_LOG"], "a", encoding="utf-8") as stream:
    stream.write(" ".join(sys.argv[1:]) + "\n")
if "create" in sys.argv:
    print("2718")
PY

reporting_output="$(
  unset JSB1_GITHUB_TOKEN GITHUB_TOKEN
  HOME="$test_home" JSB1_DEPLOY_ENV_FILE="$env_file" \
    JSB1_DEPLOY_STATE_DIR="$test_root/reporting-state" \
    MOCK_EXPECTED_TOKEN=rotated-token MOCK_GITHUB_LOG="$mock_github_log" \
    /bin/bash -c '
      source "$1/scripts/deploy-lib.sh"
      GITHUB_DEPLOYMENT_HELPER="$2"
      unit_dir="$UNITS_DIR/impl"
      mkdir -p "$unit_dir"
      github_create_deployment \
        "$unit_dir" 8efb0664ab92f2df6155281415fbe33051366868 \
        jsb1/impl impl impl-jsb.mangagaki.net
      github_update_commit_status \
        "$unit_dir" 8efb0664ab92f2df6155281415fbe33051366868 \
        pending jsb1/deploy/impl "Deploying impl" \
        https://impl-jsb.mangagaki.net
    ' bash "$REPO_ROOT" "$mock_helper" 2>&1
)"
grep -F -- '--repository egod1537/jsb1 create --commit' \
  "$mock_github_log" >/dev/null
grep -F -- '--repository egod1537/jsb1 status-commit --commit' \
  "$mock_github_log" >/dev/null
if grep -F -- rotated-token "$mock_github_log" >/dev/null \
  || grep -F -- rotated-token <<<"$reporting_output" >/dev/null; then
  printf 'GitHub reporting output leaked the rotated token\n' >&2
  exit 1
fi

printf 'Deployment environment loader tests passed.\n'
