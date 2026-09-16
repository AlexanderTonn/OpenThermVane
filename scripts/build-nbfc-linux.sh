#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
nbfc_dir="${repo_root}/external/nbfc"

if [[ ! -d "${nbfc_dir}" ]]; then
    echo "NBFC submodule missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

exec bash "${nbfc_dir}/build.sh"
