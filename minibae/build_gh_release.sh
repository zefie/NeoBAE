#!/bin/bash
RDIR="$(realpath $(pwd))"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

export USE_BASSMIDI=1
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
		echo "Could not find file ${1}"
		exit 1
	fi
}

rm -f "${ODIR}/"*
rmdir "${ODIR}" 2>/dev/null
mkdir "${ODIR}"

if [ "${USE_BASSMIDI}" -eq 1 ]; then
	if [ ! -d "${RDIR}/src/thirdparty/bassmidi/" ]; then
		# Custom for zefie's Jenkins build system
		cp -r /opt/bassmidi "${RDIR}/src/thirdparty"
	fi
fi

export BITS=32
echo "Building MingW32 DirectSound x32..."
runcmd make clean
runcmd make -f Makefile.mingw -j$(nproc) all
runcmd make -f Makefile.mingw pack
install_file "${BDIR}/playbae_dsound_x32.exe.zip" "${ODIR}/playbae_dsound_x32.zip"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_dsound_x32.zip" *.dll *.lib *.a
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

export USE_SDL2=1
echo "Building MingW32 SDL2 x32..."
runcmd make clean
runcmd make -f Makefile.mingw -j$(nproc) all
runcmd make -f Makefile.mingw pack
install_file "${BDIR}/playbae_sdl2_x32.exe.zip" "${ODIR}/playbae_sdl2_x32.zip"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_sdl2_x32.zip" *.dll *.lib *.a
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

export BITS=64
export USE_SDL2=
echo "Building MingW32 DirectSound x64..."
runcmd make clean
runcmd make -f Makefile.mingw -j$(nproc) all 
runcmd make -f Makefile.mingw pack
install_file "${BDIR}/playbae_dsound_x64.exe.zip" "${ODIR}/playbae_dsound_x64.zip"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_dsound_x64.zip" *.dll *.lib *.a
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

export USE_SDL2=1
echo "Building MingW32 SDL2 x64..."
runcmd make clean
runcmd make -f Makefile.mingw -j$(nproc) all
runcmd make -f Makefile.mingw pack
install_file "${BDIR}/playbae_sdl2_x64.exe.zip" "${ODIR}/playbae_sdl2_x64.zip"
runcmd cd "${BDIR}" && runcmd zip -u "${ODIR}/libMiniBAE_win_sdl2_x64.zip" *.dll *.lib *.a
runcmd cd "${RDIR}"
runcmd make -f Makefile.mingw clean

echo "Building MingW32 SDL2 GUI x64..."
runcmd make clean
runcmd make -f Makefile.gui-mingw -j$(nproc) all
runcmd make -f Makefile.gui-mingw pack
install_file "${BDIR}/zefidi.zip" "${ODIR}/zefidi_sdl2_x64.zip"
runcmd cd "${RDIR}"
runcmd make -f Makefile.gui-mingw clean

export USE_SDL=0
export NOAUTO=1
export SF2_SUPPORT=0
export MP3_DEC=1
export WASM=1
echo "Building Enscripten WASM..."
runcmd make clean
runcmd make -f Makefile.emcc -j$(nproc) all
runcmd make -f Makefile.emcc pack
install_file "${BDIR}/playbae_wasm.tar.gz" "${ODIR}/playbae_wasm.tar.gz"
runcmd make -f Makefile.emcc clean

cd "${RDIR}"
ls -l "${ODIR}"

