#!/bin/bash
function checkexit() {
	if [ "$1" != 0 ]; then
		exit "$1"
	fi
}

REPO_ROOT="$(pwd)"

# Build all MinGW arches. --noinstall: packaging is CPack into out/, not
# cmake --install into /opt/llvm-mingw/<triple> (not writable; fails once
# NeoBAEInstall.cmake defines real install rules).
/opt/llvm-mingw/wrapper.sh --noinstall --cmake \
	-DNEOBAE_STATIC=1 -DBAE_PLATFORM=SDL3 -DNEOBAE_BUILD_VCLIB=1 \
	-DNEOBAE_EXTERNAL_CODECS=1 -DBAE_ENABLE_MIDI_HARDWARE=1 "${@}"
QUEUE_EXIT=$?

# we now have build-aarch64-w64-mingw32, build-armv7-w64-mingw32, build-x86_64-w64-mingw32, and build-i686-w64-mingw32
rm -rf out/
mkdir -p out/

if [ "${QUEUE_EXIT}" != 0 ]; then
	exit "${QUEUE_EXIT}"
fi

for t in aarch64 armv7 x86_64 i686; do
	echo " *** Packaging suite + sdk ZIPs for ${t} with CPack..."
	cpack --config "${REPO_ROOT}/build-${t}-w64-mingw32/CPackConfig.cmake" -G ZIP -B "${REPO_ROOT}/out"
	checkexit "$?"
done

if [ "${1}" == "--clean" ]; then
    echo " *** Cleaning build directories..."
    rm -rf build-*-w64-mingw32
fi
