#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="${1:-status}"

case "${ACTION}" in
  setup|build|run|status) ;;
  *)
    echo "Usage: $0 {setup|build|run|status}" >&2
    exit 2
    ;;
esac

is_windows() {
  case "$(uname -s 2>/dev/null || true)" in
    MINGW*|MSYS*|CYGWIN*) return 0 ;;
  esac

  [[ "${OS:-}" == "Windows_NT" ]]
}

if ! is_windows; then
  exec bash "${ROOT_DIR}/scripts/px4/${ACTION}.sh"
fi

if ! wsl.exe --status >/dev/null 2>&1; then
  echo "WSL2 is not installed." >&2
  echo "Open PowerShell as Administrator and run:" >&2
  echo "  wsl --install -d Ubuntu-24.04" >&2
  echo "Restart Windows, launch Ubuntu once, then run: make px4-setup" >&2
  exit 1
fi

if ! command -v cygpath >/dev/null 2>&1; then
  echo "cygpath is required to translate the repository path for WSL2." >&2
  exit 1
fi

wsl_args=()
if [[ -n "${PX4_WSL_DISTRO:-}" ]]; then
  wsl_args+=(--distribution "${PX4_WSL_DISTRO}")
fi

windows_root="$(cygpath -w "${ROOT_DIR}")"
wsl_root="$(MSYS_NO_PATHCONV=1 wsl.exe "${wsl_args[@]}" --exec \
  wslpath -a "${windows_root}" | tr -d '\r')"

wsl_env=()
for variable in \
  PX4_VERSION \
  JSBSIM_VERSION \
  PX4_WORKSPACE \
  PX4_JSBSIM_MODEL \
  PX4_HEADLESS
do
  if [[ -n "${!variable+x}" ]]; then
    wsl_env+=("${variable}=${!variable}")
  fi
done

exec env MSYS_NO_PATHCONV=1 wsl.exe "${wsl_args[@]}" --exec \
  env "${wsl_env[@]}" bash "${wsl_root}/scripts/px4/${ACTION}.sh"
