#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <path-to-protoc> [output-directory]" >&2
  exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(dirname "$script_dir")
output=${2:-"$repo_root/build/generated/contract/validation"}
python "$script_dir/contract_tool.py" validate \
  --root "$repo_root" \
  --protoc "$1" \
  --output "$output"
