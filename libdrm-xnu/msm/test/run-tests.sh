#!/bin/bash
# Host tests of the Adreno libdrm entry points against a mock of the kernel's SweetGPUClient.
#   MALI=path/to/mapple_gpu_mali  KERNEL=path/to/xnu_kernel_qcom_common  MESA=path/to/unpacked mesa-26.2.6  test/run-tests.sh
set -euo pipefail
: "${MALI:?}" "${KERNEL:?}" "${MESA:?}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="${TMPDIR:-/tmp}/msm-test.$$"; mkdir -p "$T/shim/linux"; trap 'rm -rf "$T"' EXIT
KDIR="$KERNEL/iokit/Kernel/arm"
DRM="$MALI/libdrm-xnu/include"
# Mesa's uapi headers want Linux's integer types
cat > "$T/shim/linux/types.h" <<'EOT'
#include <stdint.h>
typedef uint8_t __u8; typedef uint16_t __u16; typedef uint32_t __u32; typedef uint64_t __u64;
typedef int8_t __s8; typedef int16_t __s16; typedef int32_t __s32; typedef int64_t __s64;
EOT
CC="${CC:-cc} -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-four-char-constants"

echo "== the library's constants and structures against the kernel's header, and against Mesa's"
$CC -I"$HERE" -I"$KDIR" "$HERE/test/check_kernel.c" -o "$T/check_kernel"
$CC -I"$T/shim" -I"$MESA/include/drm-uapi" "$HERE/test/check_mesa.c" -o "$T/check_mesa"
"$T/check_kernel" > "$T/kernel.txt"; "$T/check_mesa" > "$T/mesa.txt"
if ! diff "$T/kernel.txt" "$T/mesa.txt"; then echo "MISMATCH: the kernel's table differs from Mesa's header"; exit 1; fi
echo "kernel table == Mesa's header ($(wc -l < "$T/kernel.txt") entries)"

echo "== the mock serves the selectors the kernel file serves"
labels() { grep -oE 'case (SWEET_MSM_BASE \+ DRM_MSM_[A-Z_]+|SWEET_GPU_SEL_[A-Z_]+):' "$1" | sort -u; }
labels "$KDIR/SweetGPUClient.cpp" > "$T/k.cases"; labels "$HERE/msm_mock.c" > "$T/m.cases"
if ! diff "$T/k.cases" "$T/m.cases"; then echo "MISMATCH: the mock does not serve the kernel's selectors"; exit 1; fi
echo "same $(wc -l < "$T/k.cases") selector labels"

echo "== libdrm entry points over the mock"
$CC -I"$HERE" -I"$KDIR" -I"$DRM" -I"$DRM/libdrm" "$HERE/test/test_msm.c" "$HERE/xf86drm_msm.c" "$HERE/msm_gpu.c" "$HERE/msm_mock.c" \
    -framework IOKit -framework CoreFoundation -o "$T/test_msm"
"$T/test_msm"
