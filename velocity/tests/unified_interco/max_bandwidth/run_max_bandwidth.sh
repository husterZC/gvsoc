#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
APP_DIR="${SCRIPT_DIR}/sw"
RESULT_DIR="${SCRIPT_DIR}/results"
ARCH_DIR="${RESULT_DIR}/arch"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-1200}"
SIZES_TEXT="${SIZES:-4 16 64 256 1024 4096 16384 65536}"
REPEAT="${REPEAT:-128}"
MAX_INFLIGHT="${MAX_INFLIGHT:-8}"
SLOTS="${SLOTS:-${MAX_INFLIGHT}}"
CFG="${CFG:-}"

ALL_SHAPES=(
    "k4_l1_c4:4:4:1:1:16"
    "k4_l2_c8:8:4:2:1:16"
    "k4_l3_c16:16:4:3:1:16"
    "k4_l4_c16:16:4:4:1:16"
    "k5_l3_c15:15:5:3:2:16"
)

read -r -a SIZE_LIST <<< "${SIZES_TEXT}"
if [ "${#SIZE_LIST[@]}" -eq 0 ]; then
    echo "[max-bandwidth] SIZES must contain at least one size" >&2
    exit 1
fi

if ! [[ "${REPEAT}" =~ ^[0-9]+$ ]] || [ "${REPEAT}" -lt 1 ] || [ "${REPEAT}" -gt 255 ]; then
    echo "[max-bandwidth] REPEAT must be an integer from 1 to 255" >&2
    exit 1
fi

if ! [[ "${MAX_INFLIGHT}" =~ ^[0-9]+$ ]] || [ "${MAX_INFLIGHT}" -lt 1 ]; then
    echo "[max-bandwidth] MAX_INFLIGHT must be a positive integer" >&2
    exit 1
fi

if ! [[ "${SLOTS}" =~ ^[0-9]+$ ]] || [ "${SLOTS}" -lt 1 ]; then
    echo "[max-bandwidth] SLOTS must be a positive integer" >&2
    exit 1
fi

max_size=0
for size in "${SIZE_LIST[@]}"; do
    if ! [[ "${size}" =~ ^[0-9]+$ ]] || [ "${size}" -lt 1 ]; then
        echo "[max-bandwidth] invalid transaction size: ${size}" >&2
        exit 1
    fi
    if [ $((size % 4)) -ne 0 ]; then
        echo "[max-bandwidth] transaction size must be a multiple of 4 bytes: ${size}" >&2
        exit 1
    fi
    if [ "${size}" -gt "${max_size}" ]; then
        max_size="${size}"
    fi
done

if [ -n "${DMA_BUFFER_SIZE:-}" ]; then
    dma_buffer_size="${DMA_BUFFER_SIZE}"
else
    dma_buffer_size="${max_size}"
    if [ "${dma_buffer_size}" -lt 4096 ]; then
        dma_buffer_size=4096
    fi
fi

if [ -n "${PENDING_SIZE:-}" ]; then
    pending_size="${PENDING_SIZE}"
else
    pending_size=$((max_size + 128))
    if [ "${pending_size}" -lt 4096 ]; then
        pending_size=4096
    fi
fi

mkdir -p "${RESULT_DIR}/logs" "${ARCH_DIR}"

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

write_arch() {
    local path="$1"
    local clusters="$2"
    local radix="$3"
    local level="$4"
    local link_latency="$5"
    local link_width="$6"

    cat > "${path}" <<EOF
class VelocityArch:

    def __init__(self):

        self.num_cluster             = ${clusters}

        self.cluster_num_lane        = 32
        self.cluster_lane_width      = 4

        self.dma_reg_offset          = 0x00000100
        self.dma_reg_size            = 0x00000100
        self.dma_bus_width           = 16
        self.dma_read_buffer_size    = ${dma_buffer_size}
        self.dma_write_buffer_size   = ${dma_buffer_size}
        self.dma_max_inflight_txn    = ${MAX_INFLIGHT}
        self.dma_base_latency        = 1
        self.dma_cluster_stride      = 0x00010000

        self.unified_interco                 = "fat_tree"
        self.unified_interco_topology        = "fat_tree"
        self.unified_interco_radix           = ${radix}
        self.unified_interco_level           = ${level}
        self.unified_interco_link_latency    = ${link_latency}
        self.unified_interco_link_width      = ${link_width}
        self.unified_interco_link_pending_size = ${pending_size}
        self.unified_interco_router_pending_size = ${pending_size}

        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_size       = 0x00100000

        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000

        self.cluster_zomem_base      = 0x18000000
        self.cluster_zomem_size      = 0x00020000

        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200

        self.instruction_mem_base    = 0x80000000
        self.instruction_mem_size    = 0x00010000

        self.soc_register_base       = 0x70000000
        self.soc_register_size       = 0x00010000
        self.soc_register_eoc        = 0x70000000
EOF
}

field_value() {
    local line="$1"
    local key="$2"
    echo "${line}" | tr ' ' '\n' | awk -F= -v key="${key}" '$1 == key { print $2 }'
}

selected_shapes=()
if [ -n "${CFG}" ]; then
    selected_shapes=("custom:0:0:0:0:0")
elif [ "$#" -gt 0 ]; then
    for requested in "$@"; do
        found=0
        for shape in "${ALL_SHAPES[@]}"; do
            if [ "${shape%%:*}" = "${requested}" ]; then
                selected_shapes+=("${shape}")
                found=1
                break
            fi
        done

        if [ "${found}" -eq 0 ]; then
            echo "[max-bandwidth] unknown shape: ${requested}" >&2
            exit 1
        fi
    done
else
    selected_shapes=("${ALL_SHAPES[@]}")
fi

SUMMARY="${RESULT_DIR}/summary.csv"
echo "shape,clusters,radix,level,link_latency,link_width,txn_size,repeat,max_inflight,slots,bytes,elapsed_ns,bandwidth_mb_s,status,log" > "${SUMMARY}"

overall_status=0

for shape in "${selected_shapes[@]}"; do
    IFS=: read -r name clusters radix level link_latency link_width <<< "${shape}"
    arch="${ARCH_DIR}/velocity_arch_${name}.py"

    if [ -n "${CFG}" ]; then
        arch="${CFG}"
    else
        write_arch "${arch}" "${clusters}" "${radix}" "${level}" "${link_latency}" "${link_width}"
    fi

    echo "[max-bandwidth] shape=${name} arch=${arch} dma_buffer_size=${dma_buffer_size} pending_size=${pending_size}"
    if ! make hw cfg="${arch}"; then
        overall_status=1
        for txn_size in "${SIZE_LIST[@]}"; do
            log="${RESULT_DIR}/logs/${name}_size_${txn_size}.log"
            echo "[max-bandwidth] hardware build failed for ${name}" > "${log}"
            echo "${name},${clusters},${radix},${level},${link_latency},${link_width},${txn_size},${REPEAT},${MAX_INFLIGHT},${SLOTS},0,0,0,HW_FAIL,${log}" >> "${SUMMARY}"
        done
        continue
    fi

    for txn_size in "${SIZE_LIST[@]}"; do
        log="${RESULT_DIR}/logs/${name}_size_${txn_size}.log"
        echo "[max-bandwidth] shape=${name} txn_size=${txn_size} repeat=${REPEAT} max_inflight=${MAX_INFLIGHT} slots=${SLOTS}"
        echo "[max-bandwidth] arch=${arch}" > "${log}"

        set +e
        {
            make sw app="${APP_DIR}" sw_cmake_arg="-DSRC_DIR=${APP_DIR} -DMAX_BANDWIDTH_TEST_TXN_SIZE=${txn_size} -DMAX_BANDWIDTH_TEST_REPEAT=${REPEAT} -DMAX_BANDWIDTH_TEST_MAX_INFLIGHT=${MAX_INFLIGHT} -DMAX_BANDWIDTH_TEST_SLOTS=${SLOTS}" &&
            timeout "${TIMEOUT_SECONDS}" make run
        } 2>&1 | tee -a "${log}"
        cmd_status=${PIPESTATUS[0]}
        set +e

        result_line="$(grep "MAX_BANDWIDTH_RESULT" "${log}" | tail -n 1 || true)"
        if [ "${cmd_status}" -eq 0 ] && echo "${result_line}" | grep -q "MAX_BANDWIDTH_RESULT PASS"; then
            status="PASS"
        else
            status="FAIL"
            overall_status=1
        fi

        bytes="$(field_value "${result_line}" "bytes")"
        elapsed_ns="$(field_value "${result_line}" "elapsed_ns")"
        bandwidth_mb_s="0"
        if [ -n "${bytes}" ] && [ -n "${elapsed_ns}" ] && [ "${elapsed_ns}" -gt 0 ] 2>/dev/null; then
            bandwidth_mb_s="$(awk -v bytes="${bytes}" -v ns="${elapsed_ns}" 'BEGIN { printf "%.3f", (bytes * 1000.0) / ns }')"
        fi

        echo "${name},${clusters},${radix},${level},${link_latency},${link_width},${txn_size},${REPEAT},${MAX_INFLIGHT},${SLOTS},${bytes},${elapsed_ns},${bandwidth_mb_s},${status},${log}" >> "${SUMMARY}"
    done
done

echo "[max-bandwidth] summary: ${SUMMARY}"
exit "${overall_status}"
