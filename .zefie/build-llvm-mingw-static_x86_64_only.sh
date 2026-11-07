#!/bin/bash
function checkexit() {
	if [ "$1" != 0 ]; then
		exit "$1"
	fi
}

# Build the project
export TARGET=x86_64-w64-mingw32
export TOOLCHAIN_PREFIX="/opt/llvm-mingw"
export PATH="${TOOLCHAIN_PREFIX}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export CC="${TARGET}-clang"
export CXX="${TARGET}-clang++"
export RC="${TARGET}-windres"
export AR="${TARGET}-ar"
export RANLIB="${TARGET}-ranlib"
export STRIP="${TARGET}-strip"
export OBJCOPY="${TARGET}-objcopy"
export OBJDUMP="${TARGET}-objdump"
export PKG_CONFIG="/usr/bin/pkgconf"
export PKG_CONFIG_LIBDIR="${TOOLCHAIN_PREFIX}/${TARGET}/lib/pkgconfig:${TOOLCHAIN_PREFIX}/${TARGET}/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${TOOLCHAIN_PREFIX}/${TARGET}"
export CFLAGS="-march=x86-64 -mtune=znver4"
export CXXFLAGS="$CFLAGS"

REPO_ROOT="$(pwd)"
# Staging prefix only (writable). Do not install into the toolchain tree;
# CPack packages from the build tree via cmake_install.cmake.
INSTALL_PREFIX="${REPO_ROOT}/out/cmake-install-${TARGET}"
mkdir -p "build-${TARGET}" "${INSTALL_PREFIX}"
pushd "build-${TARGET}"

cmake .. \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_RC_COMPILER="${RC}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_FIND_ROOT_PATH="${TOOLCHAIN_PREFIX}/${TARGET}" \
    -DCMAKE_PREFIX_PATH="${TOOLCHAIN_PREFIX}/${TARGET}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEOBAE_EXTERNAL_CODECS=1 \
    -DNEOBAE_STATIC=1 -DBAE_PLATFORM=SDL3 -DNEOBAE_BUILD_VCLIB=1 -DBAE_ENABLE_MIDI_HARDWARE=1 -DBUILD_NBEDITOR=1 "${@}"
checkexit "$?"

cmake --build . --config Release -- -j$(nproc)
checkexit "$?"

popd

# Package suite + sdk ZIPs via CPack (names come from BAE_VERSION in CMake).
rm -rf out/
mkdir -p out/
echo " *** Packaging suite + sdk ZIPs with CPack..."
cpack --config "${REPO_ROOT}/build-${TARGET}/CPackConfig.cmake" -G ZIP -B "${REPO_ROOT}/out"
checkexit "$?"

if [ "${1}" == "--clean" ]; then
    echo " *** Cleaning build directories..."
    rm -rf build-*-w64-mingw32
fi
