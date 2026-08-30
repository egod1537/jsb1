#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLIGHTGEAR_APPIMAGE="$ROOT_DIR/.deps/flightgear/flightgear.AppImage"
FLIGHTGEAR_ARGS=(
  --fdm=null
  --native-fdm=socket,in,120,,5500,udp
)

is_windows() {
  case "$(uname -s 2>/dev/null || true)" in
    MINGW*|MSYS*|CYGWIN*) return 0 ;;
  esac

  [[ "${OS:-}" == "Windows_NT" ]]
}

normalize_windows_path() {
  local path="$1"

  if command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$path" 2>/dev/null || printf '%s\n' "$path"
  else
    printf '%s\n' "$path"
  fi
}

if is_windows; then
  candidates=()

  if [[ -n "${FLIGHTGEAR_BIN:-}" ]]; then
    candidates+=("$(normalize_windows_path "$FLIGHTGEAR_BIN")")
  fi

  if command -v fgfs.exe >/dev/null 2>&1; then
    candidates+=("$(command -v fgfs.exe)")
  fi

  if command -v fgfs >/dev/null 2>&1; then
    candidates+=("$(command -v fgfs)")
  fi

  for candidate in \
    /c/Program\ Files/FlightGear*/bin/fgfs.exe \
    /c/Program\ Files\ \(x86\)/FlightGear*/bin/fgfs.exe
  do
    [[ -f "$candidate" ]] && candidates+=("$candidate")
  done

  for candidate in "${candidates[@]}"; do
    if [[ -x "$candidate" ]]; then
      exec "$candidate" "${FLIGHTGEAR_ARGS[@]}"
    fi
  done

  echo "FlightGear executable not found." >&2
  echo "Install it with: winget install FlightGear.FlightGear" >&2
  echo "Or set FLIGHTGEAR_BIN to the full path of fgfs.exe." >&2
  exit 1
fi

if [[ ! -x "$FLIGHTGEAR_APPIMAGE" ]]; then
  echo "FlightGear AppImage not found: $FLIGHTGEAR_APPIMAGE" >&2
  exit 1
fi

exec "$FLIGHTGEAR_APPIMAGE" "${FLIGHTGEAR_ARGS[@]}"
