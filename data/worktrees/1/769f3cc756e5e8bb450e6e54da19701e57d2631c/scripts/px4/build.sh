#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_linux
require_px4_workspace
activate_px4_environment

target="$(px4_jsbsim_target)"
(
  cd "${PX4_SOURCE_DIR}"
  DONT_RUN=1 HEADLESS=1 make px4_sitl "${target}"
)
