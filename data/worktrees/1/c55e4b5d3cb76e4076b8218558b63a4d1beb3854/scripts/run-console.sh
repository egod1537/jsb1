#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONSOLE_BINARY=""
CONSOLE_BINARY_CANDIDATES=(
  "$ROOT_DIR/build/jsb-flight-console.exe"
  "$ROOT_DIR/build/jsb-flight-console"
  "$ROOT_DIR/build/debug/jsb-flight-console.exe"
  "$ROOT_DIR/build/debug/jsb-flight-console"
)

add_path_if_dir() {
  local dir="$1"

  if [[ -d "$dir" ]]; then
    PATH="$dir:$PATH"
  fi
}

add_msys2_ucrt_path() {
  add_path_if_dir "/ucrt64/bin"
  add_path_if_dir "/c/msys64/ucrt64/bin"
  add_path_if_dir "/mnt/c/msys64/ucrt64/bin"
}

is_windows_console_binary() {
  [[ "$CONSOLE_BINARY" == *.exe || -f "$CONSOLE_BINARY.exe" ]]
}

for candidate in "${CONSOLE_BINARY_CANDIDATES[@]}"; do
  if [[ -f "$candidate" ]]; then
    CONSOLE_BINARY="$candidate"
    break
  fi
done

if [[ -z "$CONSOLE_BINARY" ]]; then
  echo "Console binary not found. Checked:" >&2
  printf '  %s\n' "${CONSOLE_BINARY_CANDIDATES[@]}" >&2
  echo "Build it first with: cmake --build build" >&2
  exit 1
fi

cd "$ROOT_DIR"
if is_windows_console_binary; then
  add_msys2_ucrt_path
fi
exec "$CONSOLE_BINARY"
