#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
JOBS="${JOBS:-0}"
TIMEOUT="${TIMEOUT:-600}"
RUN_TARGET="${RUN_TARGET:-rund}"
PROGRESS="${PROGRESS:-auto}"
COLOR="${COLOR:-auto}"

cd "${REPO_ROOT}"
export CCACHE_DIR="${CCACHE_DIR:-${REPO_ROOT}/.ccache}"

if command -v conda >/dev/null 2>&1; then
    eval "$(conda shell.bash hook)" || true
elif [ -x /usr/local/anaconda3/bin/conda ]; then
    eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)" || true
fi

if [ -f init.sh ]; then
    # shellcheck disable=SC1091
    set +e
    set +u
    source init.sh
    init_status=$?
    set -e
    set -u
    if [ "${init_status}" -ne 0 ]; then
        exit "${init_status}"
    fi
fi

python3 "${SCRIPT_DIR}/run_matrix.py" --mode full --jobs "${JOBS}" --timeout "${TIMEOUT}" --run-target "${RUN_TARGET}" --progress "${PROGRESS}" --color "${COLOR}"
