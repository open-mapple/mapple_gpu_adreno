#!/bin/bash
# Mesa with freedreno for the Mac host, against libdrm-xnu from the Mali tree. Experiment: host only, no phone.
#   MALI=path/to/mapple_gpu_mali SRC=unpacked mesa-26.2.6 (with the Mali patch 0001 and ours applied) BUILD=build dir
set -euo pipefail
: "${MALI:?}" "${SRC:?}" "${BUILD:?}"
PC=$BUILD.pc
mkdir -p "$PC"
cat > "$PC/libdrm.pc" <<EOT
prefix=$MALI/libdrm-xnu
includedir=\${prefix}/include
libdir=\${prefix}/out

Name: libdrm
Description: libdrm-xnu stub
Version: 2.4.134
Libs: -L\${libdir} -ldrm-xnu
Cflags: -I\${includedir} -I\${includedir}/libdrm
EOT
export PKG_CONFIG_PATH=$PC:${BREW_PREFIX:-/opt/homebrew}/lib/pkgconfig
exec nice -n 19 meson setup "$@" "$BUILD" "$SRC" --buildtype=release -Ddefault_library=static -Db_ndebug=true -Dc_args=-g0 -Dcpp_args=-g0 \
  -Dplatforms= -Dgallium-drivers=freedreno -Dvulkan-drivers= -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled \
  -Dglx=disabled -Dgbm=disabled -Dllvm=disabled -Ddraw-use-llvm=false -Dexpat=disabled -Dxmlconfig=disabled -Dvalgrind=disabled \
  -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dtools= -Dvideo-codecs= -Dgallium-va=disabled -Dperfetto=false \
  -Dhtml-docs=disabled -Dzstd=enabled -Dzlib=enabled -Dshader-cache=enabled
