#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_linux
require_px4_workspace
activate_px4_environment

target="$(px4_jsbsim_target)"
echo "Starting PX4 SITL with ${target} on simulator TCP port 4560..."
cd "${PX4_SOURCE_DIR}"
if [[ "${PX4_HEADLESS:-1}" == "1" ]]; then
  HEADLESS=1 make px4_sitl "${target}"
else
  env -u HEADLESS make px4_sitl "${target}"
fi
