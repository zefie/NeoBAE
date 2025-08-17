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

echo "Building MingW32 DirectSound x86..."
runcmd make -f Makefile.mingw BITS=32 clean all pack
install_file "${BDIR}/playbae_dsound_x86.exe.gz" "${ODIR}/playbae_dsound_x86.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_dsound_x86.zip" *.dll *.lib
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 SDL2 x86..."
runcmd make -f Makefile.mingw USE_SDL=1 BITS=32 clean all pack
install_file "${BDIR}/playbae_sdl2_x86.exe.gz" "${ODIR}/playbae_sdl2_x86.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_sdl2_x86.zip" *.dll *.lib

runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 DirectSound x64..."
runcmd make -f Makefile.mingw BITS=64 clean all pack
install_file "${BDIR}/playbae_dsound_x64.exe.gz" "${ODIR}/playbae_dsound_x64.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_dsound_x64.zip" *.dll *.lib
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 SDL2 x64..."
runcmd make -f Makefile.mingw USE_SDL=1 BITS=64 clean all pack
install_file "${BDIR}/playbae_sdl2_x64.exe.gz" "${ODIR}/playbae_sdl2_x64.exe.gz"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_sdl2_x64.zip" *.dll *.lib
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 SDL2 GUI x64..."
runcmd make -f Makefile.gui-mingw clean all pack
install_file "${BDIR}/minibae_gui.exe.gz" "${ODIR}/minibae_gui_sdl2_x64.exe.gz"
runcmd cd "${RDIR}"
runcmd make -f Makefile.gui-mingw clean

echo "Building Enscripten WASM32..."
runcmd make -f Makefile.emcc clean pack
install_file "${BDIR}/playbae_wasm32.tar.gz" "${ODIR}/playbae_wasm32.tar.gz"
runcmd make -f Makefile.emcc clean

cd "${RDIR}"
ls -l "${ODIR}"

