#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
APP_DIR="${SCRIPT_DIR}/sw"
RESULT_DIR="${SCRIPT_DIR}/results"
ARCH_DIR="${RESULT_DIR}/arch"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-1200}"
WORDS="${WORDS:-1}"
MAX_INFLIGHT="${MAX_INFLIGHT:-8}"
PENDING_SIZE="${PENDING_SIZE:-4096}"

ALL_SHAPES=(
    "k4_l1_c4:4:4:1:1:16"
    "k4_l2_c8:8:4:2:1:16"
    "k4_l3_c16:16:4:3:1:16"
    "k4_l4_c16:16:4:4:1:16"
    "k5_l3_c15:15:5:3:2:16"
)

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
        self.dma_read_buffer_size    = 4096
        self.dma_write_buffer_size   = 4096
        self.dma_max_inflight_txn    = 16
        self.dma_base_latency        = 1
        self.dma_cluster_stride      = 0x00010000

        self.unified_interco                 = "fat_tree"
        self.unified_interco_topology        = "fat_tree"
        self.unified_interco_radix           = ${radix}
        self.unified_interco_level           = ${level}
        self.unified_interco_link_latency    = ${link_latency}
        self.unified_interco_link_width      = ${link_width}
        self.unified_interco_link_pending_size = ${PENDING_SIZE}
        self.unified_interco_router_pending_size = ${PENDING_SIZE}

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
if [ "$#" -gt 0 ]; then
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
            echo "[fat-tree] unknown shape: ${requested}" >&2
            exit 1
        fi
    done
else
    selected_shapes=("${ALL_SHAPES[@]}")
fi

SUMMARY="${RESULT_DIR}/summary.csv"
echo "shape,clusters,radix,level,link_latency,link_width,words,max_inflight,transfers,bytes,elapsed_ns,bandwidth_mb_s,status,log" > "${SUMMARY}"

overall_status=0

for shape in "${selected_shapes[@]}"; do
    IFS=: read -r name clusters radix level link_latency link_width <<< "${shape}"
    arch="${ARCH_DIR}/velocity_arch_${name}.py"
    log="${RESULT_DIR}/logs/${name}.log"

    write_arch "${arch}" "${clusters}" "${radix}" "${level}" "${link_latency}" "${link_width}"

    echo "[fat-tree] shape=${name} clusters=${clusters} radix=${radix} level=${level} link_latency=${link_latency} link_width=${link_width}"
    echo "[fat-tree] arch=${arch}" | tee "${log}"

    set +e
    {
        make hw cfg="${arch}" &&
        make sw app="${APP_DIR}" sw_cmake_arg="-DSRC_DIR=${APP_DIR} -DFAT_TREE_TEST_WORDS=${WORDS} -DFAT_TREE_TEST_MAX_INFLIGHT=${MAX_INFLIGHT}" &&
        timeout "${TIMEOUT_SECONDS}" make run
    } 2>&1 | tee -a "${log}"
    cmd_status=${PIPESTATUS[0]}
    set +e

    result_line="$(grep "FAT_TREE_ALL_TO_ALL_RESULT" "${log}" | tail -n 1 || true)"
    if [ "${cmd_status}" -eq 0 ] && echo "${result_line}" | grep -q "FAT_TREE_ALL_TO_ALL_RESULT PASS"; then
        status="PASS"
    else
        status="FAIL"
        overall_status=1
    fi

    transfers="$(field_value "${result_line}" "transfers")"
    bytes="$(field_value "${result_line}" "bytes")"
    elapsed_ns="$(field_value "${result_line}" "elapsed_ns")"
    bandwidth_mb_s="0"
    if [ -n "${bytes}" ] && [ -n "${elapsed_ns}" ] && [ "${elapsed_ns}" -gt 0 ] 2>/dev/null; then
        bandwidth_mb_s="$(awk -v bytes="${bytes}" -v ns="${elapsed_ns}" 'BEGIN { printf "%.3f", (bytes * 1000.0) / ns }')"
    fi

    echo "${name},${clusters},${radix},${level},${link_latency},${link_width},${WORDS},${MAX_INFLIGHT},${transfers},${bytes},${elapsed_ns},${bandwidth_mb_s},${status},${log}" >> "${SUMMARY}"
done

echo "[fat-tree] summary: ${SUMMARY}"
exit "${overall_status}"
