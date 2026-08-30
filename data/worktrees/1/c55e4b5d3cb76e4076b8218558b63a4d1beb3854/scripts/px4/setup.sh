#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_linux

if [[ ! -r /etc/os-release ]]; then
  echo "Cannot identify the Linux distribution." >&2
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" ]]; then
  echo "PX4 setup currently supports Ubuntu 22.04 or 24.04 under WSL2." >&2
  exit 1
fi

case "${VERSION_ID:-}" in
  22.04|24.04) ;;
  *)
    echo "Unsupported Ubuntu version: ${VERSION_ID:-unknown}" >&2
    echo "Install Ubuntu 22.04 or 24.04 for PX4 SITL." >&2
    exit 1
    ;;
esac

echo "Installing PX4 SITL and JSBSim bridge build dependencies..."
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  astyle \
  bc \
  build-essential \
  ccache \
  cmake \
  cppcheck \
  file \
  git \
  lcov \
  libboost-filesystem-dev \
  libboost-system-dev \
  libboost-thread-dev \
  libeigen3-dev \
  libssl-dev \
  libtinyxml-dev \
  libxml2-dev \
  libxml2-utils \
  ninja-build \
  pkg-config \
  python3-dev \
  python3-pip \
  python3-setuptools \
  python3-venv \
  python3-wheel \
  rsync \
  shellcheck \
  unzip \
  zip

mkdir -p "${PX4_WORKSPACE}"

checkout_repository() {
  local repository_url="$1"
  local version="$2"
  local destination="$3"

  if [[ ! -d "${destination}/.git" ]]; then
    git clone --branch "${version}" --depth 1 "${repository_url}" "${destination}"
    return
  fi

  if [[ -n "$(git -C "${destination}" status --porcelain)" ]]; then
    echo "Refusing to update a modified dependency checkout: ${destination}" >&2
    exit 1
  fi

  git -C "${destination}" fetch --depth 1 origin "refs/tags/${version}:refs/tags/${version}"
  git -C "${destination}" checkout --detach "${version}"
}

checkout_repository \
  "https://github.com/PX4/PX4-Autopilot.git" \
  "${PX4_VERSION}" \
  "${PX4_SOURCE_DIR}"
git -C "${PX4_SOURCE_DIR}" submodule sync --recursive
git -C "${PX4_SOURCE_DIR}" submodule update --init --recursive --depth 1

if [[ ! -x "${PX4_VENV_DIR}/bin/python" ]]; then
  python3 -m venv "${PX4_VENV_DIR}"
fi
"${PX4_VENV_DIR}/bin/python" -m pip install --upgrade pip setuptools wheel
"${PX4_VENV_DIR}/bin/python" -m pip install \
  -r "${PX4_SOURCE_DIR}/Tools/setup/requirements.txt"

checkout_repository \
  "https://github.com/JSBSim-Team/jsbsim.git" \
  "${JSBSIM_VERSION}" \
  "${PX4_JSBSIM_SOURCE_DIR}"

cmake -S "${PX4_JSBSIM_SOURCE_DIR}" -B "${PX4_JSBSIM_BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${PX4_INSTALL_PREFIX}" \
  -DBUILD_DOCS=OFF \
  -DBUILD_JULIA_PACKAGE=OFF \
  -DBUILD_MATLAB_SFUNCTION=OFF \
  -DBUILD_PYTHON_MODULE=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build "${PX4_JSBSIM_BUILD_DIR}"
cmake --install "${PX4_JSBSIM_BUILD_DIR}"

require_px4_workspace
activate_px4_environment

target="$(px4_jsbsim_target)"
echo "Building PX4 SITL target: ${target}"
(
  cd "${PX4_SOURCE_DIR}"
  DONT_RUN=1 HEADLESS=1 make px4_sitl "${target}"
)

echo
echo "PX4/JSBSim environment is ready."
echo "PX4: ${PX4_SOURCE_DIR} (${PX4_VERSION})"
echo "JSBSim: ${PX4_INSTALL_PREFIX} (${JSBSIM_VERSION})"
echo "Run: make px4-run"
