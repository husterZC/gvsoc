#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
APP_DIR="${SCRIPT_DIR}/sw"
RESULT_DIR="${SCRIPT_DIR}/results"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-900}"

if [ "$#" -gt 0 ]; then
    CLUSTERS=("$@")
else
    CLUSTERS=(2 4 8 16 32 64 128)
fi

mkdir -p "${RESULT_DIR}/logs"

cd "${REPO_ROOT}" || exit 1

export CCACHE_DIR="${CCACHE_DIR:-${REPO_ROOT}/.ccache}"
mkdir -p "${CCACHE_DIR}"

if command -v conda >/dev/null 2>&1; then
    eval "$(conda shell.bash hook)" || true
elif [ -x /usr/local/anaconda3/bin/conda ]; then
    eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)" || true
fi

if [ -f init.sh ]; then
    # shellcheck disable=SC1091
    set +u
    source init.sh
    set -u
fi

SUMMARY="${RESULT_DIR}/summary.csv"
echo "clusters,status,log" > "${SUMMARY}"

overall_status=0

for clusters in "${CLUSTERS[@]}"; do
    arch="${SCRIPT_DIR}/arch/velocity_arch_${clusters}.py"
    log="${RESULT_DIR}/logs/scaling_${clusters}.log"

    if [ ! -f "${arch}" ]; then
        echo "[DMA scaling] missing architecture file: ${arch}" | tee "${log}"
        echo "${clusters},MISSING_ARCH,${log}" >> "${SUMMARY}"
        overall_status=1
        continue
    fi

    echo "[DMA scaling] clusters=${clusters}"
    echo "[DMA scaling] arch=${arch}" | tee "${log}"

    set +e
    {
        make hw cfg="${arch}" &&
        make sw app="${APP_DIR}" &&
        timeout "${TIMEOUT_SECONDS}" make run
    } 2>&1 | tee -a "${log}"
    cmd_status=${PIPESTATUS[0]}
    set +e

    if [ "${cmd_status}" -eq 0 ] && grep -q "DMA_SCALING_RESULT PASS" "${log}"; then
        echo "${clusters},PASS,${log}" >> "${SUMMARY}"
    else
        echo "${clusters},FAIL,${log}" >> "${SUMMARY}"
        overall_status=1
    fi
done

echo "[DMA scaling] summary: ${SUMMARY}"
exit "${overall_status}"
