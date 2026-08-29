#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

AUTO_LOG_DIR="$STATE_ROOT/logs"
AUTO_LOG_FILE="$AUTO_LOG_DIR/auto-deploy.log"
AUTO_CRON_TAG="# jsb1-auto-deploy"
AUTO_HEADS_FILE=""
AUTO_WATCHER_LOCK=""

usage() {
  cat >&2 <<EOF
Usage:
  $0 --once
  $0 --install
  $0 --status
  $0 --uninstall
EOF
  exit 2
}

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S%z')" "$*"
}

cleanup_crontab_files() {
  local current_crontab="${1:-}"
  local next_crontab="${2:-}"
  [[ -z "$current_crontab" ]] || rm -f -- "$current_crontab"
  [[ -z "$next_crontab" ]] || rm -f -- "$next_crontab"
}

list_remote_heads() {
  git -C "$REPO_ROOT" ls-remote --heads origin
}

deploy_remote_revision() {
  "$SCRIPT_DIR/deploy.sh" "$1" --revision "$2"
}

undeploy_stale_branch() {
  "$SCRIPT_DIR/undeploy.sh" "$1"
}

remote_has_branch() {
  local branch="$1"
  awk -v expected="refs/heads/$branch" '$2 == expected { found = 1 } END { exit !found }' \
    "$AUTO_HEADS_FILE"
}

run_once() {
  need_command git
  need_command docker
  need_command cloudflared
  init_state

  AUTO_WATCHER_LOCK="$LOCKS_DIR/auto-deploy.lock"
  if ! mkdir "$AUTO_WATCHER_LOCK" 2>/dev/null; then
    local owner_pid
    owner_pid="$(sed -n '1p' "$AUTO_WATCHER_LOCK/pid" 2>/dev/null || true)"
    if [[ "$owner_pid" =~ ^[0-9]+$ ]] && kill -0 "$owner_pid" 2>/dev/null; then
      log 'another automatic deployment scan is already running; skipping'
      return 0
    fi
    rm -f "$AUTO_WATCHER_LOCK/pid"
    rmdir "$AUTO_WATCHER_LOCK" 2>/dev/null \
      || die "could not recover stale automatic deployment lock"
    mkdir "$AUTO_WATCHER_LOCK" \
      || die "could not acquire automatic deployment lock"
  fi
  printf '%s\n' "$$" >"$AUTO_WATCHER_LOCK/pid"

  AUTO_HEADS_FILE="$(mktemp "${TMPDIR:-/tmp}/jsb1-auto-deploy.XXXXXX")"
  auto_cleanup() {
    [[ -z "$AUTO_HEADS_FILE" ]] || rm -f "$AUTO_HEADS_FILE"
    if [[ -n "$AUTO_WATCHER_LOCK" ]]; then
      rm -f "$AUTO_WATCHER_LOCK/pid"
      rmdir "$AUTO_WATCHER_LOCK" 2>/dev/null || true
    fi
  }
  trap auto_cleanup EXIT

  if ! list_remote_heads >"$AUTO_HEADS_FILE"; then
    log 'could not read remote branches from origin'
    return 1
  fi

  local now retry_seconds stale_grace auto_main
  now="$(date +%s)"
  retry_seconds="${JSB1_AUTO_DEPLOY_RETRY_SEC:-300}"
  stale_grace="${JSB1_STALE_BRANCH_GRACE_SEC:-86400}"
  auto_main="${JSB1_AUTO_DEPLOY_MAIN:-false}"
  [[ "$retry_seconds" =~ ^[0-9]+$ ]] || die "JSB1_AUTO_DEPLOY_RETRY_SEC must be numeric"
  [[ "$stale_grace" =~ ^[0-9]+$ ]] || die "JSB1_STALE_BRANCH_GRACE_SEC must be numeric"
  [[ "$auto_main" == true || "$auto_main" == false ]] \
    || die "JSB1_AUTO_DEPLOY_MAIN must be true or false"

  local sha ref branch slug unit_dir current status attempted retry_at skipped stale_removed
  while IFS=$'\t' read -r sha ref; do
    [[ "$sha" =~ ^[0-9a-f]{40}$ && "$ref" == refs/heads/* ]] || continue
    branch="${ref#refs/heads/}"
    validate_branch_name "$branch"
    slug="$(slug_for_branch "$branch")"
    unit_dir="$UNITS_DIR/$slug"
    current="$(unit_value "$unit_dir" commit 2>/dev/null || true)"
    status="$(unit_value "$unit_dir" status 2>/dev/null || true)"
    stale_removed="$(unit_value "$unit_dir" auto-stale-removed 2>/dev/null || true)"

    if [[ -f "$unit_dir/stale-since" ]]; then
      rm -f "$unit_dir/stale-since"
      log "remote branch restored: $branch; cleared stale state"
    fi

    if [[ "$branch" == "$MAIN_BRANCH" && "$auto_main" != true ]]; then
      mkdir -p "$unit_dir"
      skipped="$(unit_value "$unit_dir" auto-skipped-commit 2>/dev/null || true)"
      if [[ "$skipped" != "$sha" ]]; then
        log "detected origin/$branch at ${sha:0:12}; automatic main deployment disabled"
        atomic_value "$unit_dir" auto-skipped-commit "$sha"
      fi
      continue
    fi
    rm -f "$unit_dir/auto-skipped-commit"

    if [[ "$current" == "$sha" && "$status" != failed && "$status" != starting \
      && ! ( "$status" == stopped && "$stale_removed" == true ) ]]; then
      continue
    fi

    attempted="$(unit_value "$unit_dir" auto-attempted-commit 2>/dev/null || true)"
    retry_at="$(unit_value "$unit_dir" auto-next-retry-at 2>/dev/null || true)"
    if [[ "$current" == "$sha" && "$attempted" == "$sha" && "$retry_at" =~ ^[0-9]+$ && "$now" -lt "$retry_at" ]]; then
      log "waiting to retry $branch at ${sha:0:12}"
      continue
    fi

    mkdir -p "$unit_dir"
    atomic_value "$unit_dir" auto-attempted-commit "$sha"
    atomic_value "$unit_dir" auto-next-retry-at "$((now + retry_seconds))"
    log "detected origin/$branch at ${sha:0:12}; deploying"
    if deploy_remote_revision "$branch" "$sha"; then
      rm -f "$unit_dir/auto-attempted-commit" "$unit_dir/auto-next-retry-at" \
        "$unit_dir/auto-stale-removed"
      log "automatic deployment completed: $branch"
    else
      log "automatic deployment failed: $branch (retry in ${retry_seconds}s)"
    fi
  done <"$AUTO_HEADS_FILE"

  local stale_since elapsed
  for unit_dir in "$UNITS_DIR"/*; do
    [[ -d "$unit_dir" && -f "$unit_dir/branch" ]] || continue
    branch="$(unit_value "$unit_dir" branch)"
    [[ "$branch" != "$MAIN_BRANCH" ]] || {
      rm -f "$unit_dir/stale-since"
      continue
    }
    if remote_has_branch "$branch"; then
      continue
    fi
    status="$(unit_value "$unit_dir" status 2>/dev/null || true)"
    [[ "$status" != stopped ]] || continue

    stale_since="$(unit_value "$unit_dir" stale-since 2>/dev/null || true)"
    if [[ ! "$stale_since" =~ ^[0-9]+$ ]]; then
      stale_since="$now"
      atomic_value "$unit_dir" stale-since "$stale_since"
      log "remote branch removed: $branch"
      log "marked stale: $branch (grace ${stale_grace}s)"
    fi
    elapsed=$((now - stale_since))
    (( elapsed >= stale_grace )) || continue

    if undeploy_stale_branch "$branch"; then
      rm -f "$unit_dir/stale-since" "$unit_dir/auto-attempted-commit" \
        "$unit_dir/auto-next-retry-at"
      atomic_value "$unit_dir" auto-stale-removed true
      log "stale deployment removed after grace period: $branch"
    else
      log "stale deployment removal failed; will retry: $branch"
    fi
  done
  auto_cleanup
  trap - EXIT
}

install_watcher() {
  need_command crontab
  init_state
  mkdir -p "$AUTO_LOG_DIR"

  local current_crontab="" next_crontab="" cron_command auto_main retry_seconds stale_grace builder_assignment grep_status
  auto_main="${JSB1_AUTO_DEPLOY_MAIN:-false}"
  retry_seconds="${JSB1_AUTO_DEPLOY_RETRY_SEC:-300}"
  stale_grace="${JSB1_STALE_BRANCH_GRACE_SEC:-86400}"
  [[ "$auto_main" == true || "$auto_main" == false ]] \
    || die "JSB1_AUTO_DEPLOY_MAIN must be true or false"
  [[ "$retry_seconds" =~ ^[0-9]+$ ]] || die "JSB1_AUTO_DEPLOY_RETRY_SEC must be numeric"
  [[ "$stale_grace" =~ ^[0-9]+$ ]] || die "JSB1_STALE_BRANCH_GRACE_SEC must be numeric"
  builder_assignment=""
  if [[ -n "$DEPLOY_BUILDER" ]]; then
    [[ "$DEPLOY_BUILDER" =~ ^[A-Za-z0-9_.-]+$ ]] || die "invalid JSB1_DEPLOY_BUILDER name"
    builder_assignment=" JSB1_DEPLOY_BUILDER=$DEPLOY_BUILDER"
  fi
  current_crontab="$(mktemp "${TMPDIR:-/tmp}/jsb1-current-crontab.XXXXXX")" \
    || die "could not create temporary current crontab file"
  if ! next_crontab="$(mktemp "${TMPDIR:-/tmp}/jsb1-next-crontab.XXXXXX")"; then
    cleanup_crontab_files "$current_crontab" "$next_crontab"
    die "could not create temporary next crontab file"
  fi
  crontab -l >"$current_crontab" 2>/dev/null || true
  grep_status=0
  grep -Fv "$AUTO_CRON_TAG" "$current_crontab" >"$next_crontab" || grep_status=$?
  if (( grep_status > 1 )); then
    cleanup_crontab_files "$current_crontab" "$next_crontab"
    die "could not filter the existing crontab"
  fi
  cron_command="* * * * * HOME=$HOME PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin JSB1_AUTO_DEPLOY_MAIN=$auto_main JSB1_AUTO_DEPLOY_RETRY_SEC=$retry_seconds JSB1_STALE_BRANCH_GRACE_SEC=$stale_grace$builder_assignment /bin/bash $SCRIPT_DIR/auto-deploy.sh --once >> $AUTO_LOG_FILE 2>&1 $AUTO_CRON_TAG"
  if ! printf '%s\n' "$cron_command" >>"$next_crontab"; then
    cleanup_crontab_files "$current_crontab" "$next_crontab"
    die "could not write the automatic deployment cron entry"
  fi
  if ! crontab "$next_crontab"; then
    cleanup_crontab_files "$current_crontab" "$next_crontab"
    die "could not install the automatic deployment cron entry"
  fi
  cleanup_crontab_files "$current_crontab" "$next_crontab"

  # Remove a plist left by the older LaunchAgent installer. It cannot run in a
  # headless login session and is superseded by the cron entry above.
  rm -f "$HOME/Library/LaunchAgents/net.mangagaki.jsb1.auto-deploy.plist"
  run_once
  printf 'Automatic deployment installed (remote branches checked every minute, main=%s, stale-grace=%ss).\n' \
    "$auto_main" "$stale_grace"
  printf 'Log: %s\n' "$AUTO_LOG_FILE"
}

show_status() {
  local cron_entry
  cron_entry="$(crontab -l 2>/dev/null | grep -F "$AUTO_CRON_TAG" || true)"
  if [[ -z "$cron_entry" ]]; then
    printf 'Automatic deployment is not installed.\n'
    return 1
  fi
  printf 'Automatic deployment watcher: installed (every minute)\n'
  if [[ "$cron_entry" == *"JSB1_AUTO_DEPLOY_MAIN=true"* ]]; then
    printf 'Automatic main deployment: enabled\n'
  else
    printf 'Automatic main deployment: disabled\n'
  fi
  printf 'Log: %s\n' "$AUTO_LOG_FILE"
  if [[ -f "$AUTO_LOG_FILE" ]]; then
    printf '\nRecent activity:\n'
    tail -n 20 "$AUTO_LOG_FILE"
  fi
}

uninstall_watcher() {
  local current_crontab next_crontab
  current_crontab="$(mktemp "${TMPDIR:-/tmp}/jsb1-current-crontab.XXXXXX")"
  next_crontab="$(mktemp "${TMPDIR:-/tmp}/jsb1-next-crontab.XXXXXX")"
  crontab -l >"$current_crontab" 2>/dev/null || true
  grep -Fv "$AUTO_CRON_TAG" "$current_crontab" >"$next_crontab" || true
  crontab "$next_crontab"
  rm -f "$current_crontab" "$next_crontab"
  printf 'Automatic deployment watcher removed. Existing deployments were left running.\n'
}

main() {
  case "${1:-}" in
    --once)
      [[ $# -eq 1 ]] || usage
      run_once
      ;;
    --install)
      [[ $# -eq 1 ]] || usage
      install_watcher
      ;;
    --status)
      [[ $# -eq 1 ]] || usage
      show_status
      ;;
    --uninstall)
      [[ $# -eq 1 ]] || usage
      uninstall_watcher
      ;;
    *) usage ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
