#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_linux

printf 'Workspace: %s\n' "${PX4_WORKSPACE}"
printf 'PX4 requested: %s\n' "${PX4_VERSION}"
printf 'JSBSim requested: %s\n' "${JSBSIM_VERSION}"

if [[ -d "${PX4_SOURCE_DIR}/.git" ]]; then
  printf 'PX4 checkout: %s\n' "$(git -C "${PX4_SOURCE_DIR}" describe --tags --always --dirty)"
else
  echo "PX4 checkout: missing"
fi

if [[ -d "${PX4_JSBSIM_SOURCE_DIR}/.git" ]]; then
  printf 'JSBSim checkout: %s\n' \
    "$(git -C "${PX4_JSBSIM_SOURCE_DIR}" describe --tags --always --dirty)"
else
  echo "JSBSim checkout: missing"
fi

px4_binary="${PX4_SOURCE_DIR}/build/px4_sitl_default/bin/px4"
bridge_binary="${PX4_SOURCE_DIR}/build/px4_sitl_default/build_jsbsim_bridge/jsbsim_bridge"
[[ -x "${px4_binary}" ]] && echo "PX4 SITL binary: ready" \
  || echo "PX4 SITL binary: missing"
[[ -x "${bridge_binary}" ]] && echo "JSBSim bridge: ready" \
  || echo "JSBSim bridge: missing"
