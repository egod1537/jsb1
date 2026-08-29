#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=scripts/deploy-lib.sh
source "$SCRIPT_DIR/scripts/deploy-lib.sh"

usage() {
  printf 'Usage: %s [--dry-run]\n' "$0" >&2
  exit 2
}

dry_run=false
if [[ ${1:-} == --dry-run ]]; then
  dry_run=true
  shift
fi
[[ $# -eq 0 ]] || usage

need_command docker
docker info >/dev/null 2>&1 || die "Docker daemon is unavailable"

candidate_images="$(mktemp "${TMPDIR:-/tmp}/jsb1-cleanup-candidates.XXXXXX")"
protected_images="$(mktemp "${TMPDIR:-/tmp}/jsb1-cleanup-protected.XXXXXX")"
cleanup_files() {
  rm -f "$candidate_images" "$protected_images"
}
trap cleanup_files EXIT

container_ids="$(docker ps -aq)"
if [[ -n "$container_ids" ]]; then
  # Protect images referenced by every container, including stopped containers
  # and containers owned by other projects.
  while IFS= read -r container_id; do
    [[ -n "$container_id" ]] || continue
    docker inspect "$container_id" --format '{{.Image}}'
  done <<<"$container_ids" | sort -u >"$protected_images"
fi

{
  docker image ls --filter label=net.mangagaki.jsb1.deployment-image=true -q
  docker image ls --format '{{.Repository}}|{{.ID}}' \
    | awk -F'|' '$1 ~ /^jsb1-.*-(backend|web)$/ { print $2 }'
} | sed '/^$/d' | sort -u >"$candidate_images"

printf 'JSB1 unused deployment images:\n'
image_count=0
while IFS= read -r image_id; do
  [[ -n "$image_id" ]] || continue
  full_id="$(docker image inspect "$image_id" --format '{{.Id}}' 2>/dev/null || true)"
  [[ -n "$full_id" ]] || continue
  if grep -Fxq "$full_id" "$protected_images"; then
    continue
  fi
  references="$(docker image inspect "$image_id" --format '{{join .RepoTags ","}}' 2>/dev/null || true)"
  if [[ -n "$references" ]]; then
    safe_references=true
    old_ifs="$IFS"
    IFS=','
    for reference in $references; do
      repository="${reference%:*}"
      if [[ ! "$repository" =~ ^jsb1-.*-(backend|web)$ ]]; then
        safe_references=false
        break
      fi
    done
    IFS="$old_ifs"
    if [[ "$safe_references" != true ]]; then
      printf '  protected shared-tag image %s (%s)\n' "$image_id" "$references"
      continue
    fi
  else
    references='<dangling JSB1-labeled image>'
  fi
  image_count=$((image_count + 1))
  if [[ "$dry_run" == true ]]; then
    printf '  [DRY-RUN] remove image %s (%s)\n' "$image_id" "$references"
  else
    printf '  removing image %s (%s)\n' "$image_id" "$references"
    docker image rm "$image_id"
  fi
done <"$candidate_images"
[[ "$image_count" -gt 0 ]] || printf '  none\n'

printf '\nUnused JSB1 Compose networks:\n'
network_count=0
while IFS= read -r network_id; do
  [[ -n "$network_id" ]] || continue
  details="$(docker network inspect "$network_id" \
    --format '{{index .Labels "com.docker.compose.project"}}|{{len .Containers}}|{{.Name}}' 2>/dev/null || true)"
  project="${details%%|*}"
  remainder="${details#*|}"
  attached="${remainder%%|*}"
  network_name="${remainder#*|}"
  [[ "$project" == jsb1-* && "$attached" == 0 ]] || continue
  network_count=$((network_count + 1))
  if [[ "$dry_run" == true ]]; then
    printf '  [DRY-RUN] remove network %s (project %s)\n' "$network_name" "$project"
  else
    printf '  removing network %s (project %s)\n' "$network_name" "$project"
    docker network rm "$network_id"
  fi
done < <(docker network ls --filter label=com.docker.compose.project -q)
[[ "$network_count" -gt 0 ]] || printf '  none\n'

printf '\nBuild cache:\n'
builder="$DEPLOY_BUILDER"
if [[ -n "$builder" ]] && docker buildx inspect "$builder" >/dev/null 2>&1; then
  if [[ "$dry_run" == true ]]; then
    printf '  [DRY-RUN] unused cache in dedicated builder %s would be pruned\n' "$builder"
    docker buildx du --builder "$builder" 2>/dev/null || true
  else
    docker buildx prune --builder "$builder" --force
  fi
else
  printf '  skipped: the current Docker builder is shared with other projects.\n'
  printf '  Set JSB1_DEPLOY_BUILDER to a dedicated JSB1 builder to enable safe cache pruning.\n'
fi

printf '\nActive containers, images, and volumes were not removed.\n'
