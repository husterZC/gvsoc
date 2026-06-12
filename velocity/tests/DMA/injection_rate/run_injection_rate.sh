#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
APP_DIR="${SCRIPT_DIR}/sw"
CONFIG_HEADER="${APP_DIR}/include/dma_test_config.h"
RESULT_DIR="${SCRIPT_DIR}/results"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-900}"
MAX_INFLIGHT="${MAX_INFLIGHT:-8}"

if [ "$#" -gt 0 ]; then
    GAPS=("$@")
else
    GAPS=(0 4 16 64 256 1024)
fi

mkdir -p "${RESULT_DIR}/logs"
export MPLCONFIGDIR="${MPLCONFIGDIR:-${RESULT_DIR}/.matplotlib}"
mkdir -p "${MPLCONFIGDIR}"

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

echo "[DMA injection] building hardware"
if [ -n "${CFG:-}" ]; then
    make hw cfg="${CFG}" || exit 1
else
    make hw || exit 1
fi

overall_status=0

for gap in "${GAPS[@]}"; do
    log="${RESULT_DIR}/logs/injection_gap_${gap}.log"

    cat > "${CONFIG_HEADER}" <<EOF
#ifndef DMA_TEST_CONFIG_H
#define DMA_TEST_CONFIG_H

#define DMA_TEST_INJECT_GAP ${gap}u
#define DMA_TEST_MAX_INFLIGHT ${MAX_INFLIGHT}u

#endif
EOF

    echo "[DMA injection] gap=${gap} max_inflight=${MAX_INFLIGHT}"
    echo "[DMA injection] gap=${gap} max_inflight=${MAX_INFLIGHT}" > "${log}"

    set +e
    {
        make sw app="${APP_DIR}" &&
        timeout "${TIMEOUT_SECONDS}" make run
    } 2>&1 | tee -a "${log}"
    cmd_status=${PIPESTATUS[0]}
    set +e

    if [ "${cmd_status}" -ne 0 ] || ! grep -q "DMA_INJECTION_RESULT PASS" "${log}"; then
        overall_status=1
    fi
done

python3 "${SCRIPT_DIR}/tools/plot_latency.py" \
    --logs "${RESULT_DIR}/logs" \
    --csv "${RESULT_DIR}/latency_vs_injection_rate.csv" \
    --figure "${RESULT_DIR}/latency_vs_injection_rate.png"

echo "[DMA injection] csv: ${RESULT_DIR}/latency_vs_injection_rate.csv"
echo "[DMA injection] figure: ${RESULT_DIR}/latency_vs_injection_rate.png"
exit "${overall_status}"
