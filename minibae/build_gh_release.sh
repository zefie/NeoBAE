#!/bin/bash
RDIR="$(realpath $(pwd))"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

SILENT=1

if [ ! -z "$1" ]; then
	SILENT=0
fi

function runcmd() {
	if [ ${SILENT} -eq 1 ]; then
		${@} 2>/dev/null > /dev/null;
	else
		${@}
	fi
}

function install_file() {
	if [ -f "${1}" ]; then
		mv "${1}" "${2}"
	else
		exit 1
	fi
}

rm -f "${ODIR}/"*
rmdir "${ODIR}" 2>/dev/null
mkdir "${ODIR}"

#echo "Building Linux 32bit..."
#runcmd make -f Makefile clean pack
#install_file "${BDIR}/playbae_linux32_static.gz" "${ODIR}/playbae_linux32_static.gz"
#runcmd make -f Makefile clean

#echo "Building Linux 32bit clang..."
#runcmd make -f Makefile.clang clean pack
#install_file "${BDIR}/playbae_linux32_clang_static.gz" "${ODIR}/playbae_linux32_clang_static.gz"
#runcmd make -f Makefile.clang clean

echo "Building MingW32 DirectSound..."
runcmd make -f Makefile.mingw clean all pack
install_file "${BDIR}/playbae.exe.gz" "${ODIR}/playbae_dsound.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_dsound.zip" *.dll *.lib
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 SDL2..."
runcmd make -f Makefile.mingw clean all pack
install_file "${BDIR}/playbae.exe.gz" "${ODIR}/playbae_sdl2.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_sdl2.zip" *.dll *.lib
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw USE_SDL=1 clean

echo "Building Enscripten WASM32..."
runcmd make -f Makefile.emcc clean pack
install_file "${BDIR}/playbae_wasm32.tar.gz" "${ODIR}/playbae_wasm32.tar.gz"
runcmd make -f Makefile.emcc clean

cd "${RDIR}"
ls -l "${ODIR}"

