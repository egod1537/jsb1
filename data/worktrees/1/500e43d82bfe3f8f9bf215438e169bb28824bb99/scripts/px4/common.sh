#!/usr/bin/env bash

set -euo pipefail

PX4_VERSION="${PX4_VERSION:-v1.17.0}"
JSBSIM_VERSION="${JSBSIM_VERSION:-v1.3.0}"
PX4_WORKSPACE="${PX4_WORKSPACE:-${HOME}/.local/share/jsb-test/px4}"
PX4_SOURCE_DIR="${PX4_SOURCE_DIR:-${PX4_WORKSPACE}/PX4-Autopilot}"
PX4_VENV_DIR="${PX4_VENV_DIR:-${PX4_WORKSPACE}/venv}"
PX4_JSBSIM_SOURCE_DIR="${PX4_JSBSIM_SOURCE_DIR:-${PX4_WORKSPACE}/jsbsim}"
PX4_JSBSIM_BUILD_DIR="${PX4_JSBSIM_BUILD_DIR:-${PX4_WORKSPACE}/build-jsbsim}"
PX4_INSTALL_PREFIX="${PX4_INSTALL_PREFIX:-${PX4_WORKSPACE}/install}"

require_linux() {
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "PX4 SITL must run in Linux. On Windows, use the WSL wrapper." >&2
    exit 1
  fi
}

require_px4_workspace() {
  if [[ ! -d "${PX4_SOURCE_DIR}/.git" ]]; then
    echo "PX4 source is not installed: ${PX4_SOURCE_DIR}" >&2
    echo "Run: make px4-setup" >&2
    exit 1
  fi

  if [[ ! -x "${PX4_VENV_DIR}/bin/python" ]]; then
    echo "PX4 Python environment is not installed: ${PX4_VENV_DIR}" >&2
    echo "Run: make px4-setup" >&2
    exit 1
  fi

  if [[ ! -f "${PX4_INSTALL_PREFIX}/lib/libJSBSim.so" ]]; then
    echo "JSBSim library is not installed: ${PX4_INSTALL_PREFIX}" >&2
    echo "Run: make px4-setup" >&2
    exit 1
  fi
}

activate_px4_environment() {
  # shellcheck disable=SC1091
  source "${PX4_VENV_DIR}/bin/activate"
  export JSBSIM_ROOT_DIR="${PX4_INSTALL_PREFIX}"
  export CMAKE_PREFIX_PATH="${PX4_INSTALL_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
  export LD_LIBRARY_PATH="${PX4_INSTALL_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  export PATH="${PX4_INSTALL_PREFIX}/bin:${PATH}"
}

px4_jsbsim_target() {
  case "${PX4_JSBSIM_MODEL:-rascal}" in
    rascal|malolo|quadrotor_x|hexarotor_x)
      printf 'jsbsim_%s\n' "${PX4_JSBSIM_MODEL:-rascal}"
      ;;
    *)
      echo "Unsupported PX4 JSBSim model: ${PX4_JSBSIM_MODEL}" >&2
      echo "Supported models: rascal, malolo, quadrotor_x, hexarotor_x" >&2
      exit 1
      ;;
  esac
}
