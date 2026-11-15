#!/bin/bash
# Mesa's real msm backend over libdrm-xnu/msm and the mock; compiles the test with the flags Mesa used for freedreno_bo.c.
#   MALI=... KERNEL=... BUILD=Mesa's host build dir (mesa/configure-host.sh, then ninja src/freedreno/drm/libfreedreno_drm.a)  test/run-fd-test.sh
set -euo pipefail
: "${MALI:?}" "${KERNEL:?}" "${BUILD:?}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="${TMPDIR:-/tmp}/fd-test.$$"; mkdir -p "$T"; trap 'rm -rf "$T"' EXIT
FLAGS=$(python3 - "$BUILD" <<'EOT'
import json, shlex, sys
for e in json.load(open(sys.argv[1] + '/compile_commands.json')):
    if e['file'].endswith('src/freedreno/drm/freedreno_bo.c'):
        a = shlex.split(e['command']); out = []; i = 1
        while i < len(a):
            x = a[i]
            if x in ('-o', '-c', '-MQ', '-MF'): i += 2; continue
            if x in ('-MD',) or x.startswith('-fdiagnostics') or x.endswith('.c') or x.endswith('.c.o'): i += 1; continue
            out.append(shlex.quote(x)); i += 1
        print(' '.join(out)); break
EOT
)
cd "$BUILD"
eval ${CC:-cc} $FLAGS -O1 -g -I"$HERE" -I"$KERNEL/iokit/Kernel/arm" -Wno-unused-parameter \
  "$HERE/test/test_fd_backend.c" "$HERE/xf86drm_msm.c" "$HERE/msm_gpu.c" "$HERE/msm_mock.c" \
  src/freedreno/drm/libfreedreno_drm.a src/freedreno/common/libfreedreno_common.a src/util/libmesa_util.a \
  src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a \
  -L/opt/homebrew/opt/zstd/lib -lzstd -lz -lc++ -framework IOKit -framework CoreFoundation -o "$T/test_fd"
"$T/test_fd"
