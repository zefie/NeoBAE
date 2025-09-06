#!/bin/bash
RDIR="$(realpath "$(pwd)")"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

export USE_BASSMIDI=1
SILENT=1

if [ -n "$1" ]; then
	if [[ "$1" =~ ^[0-9]+$ ]]; then
		SKIPTO=$1
		echo "Skipping to build ${1}"
		if [ -n "${2}" ]; then
			SILENT=0
		fi
	else
		SILENT=0
	fi
fi

function signit() {
  # Custom for zefie's Jenkins build system
  osslsigncode sign \
    -certs /opt/signkey/signcert.pem \
    -key /opt/signkey/signkey.pem \
    -n "zefie's miniBAE" \
    -i "https://www.soundmusicsys.com" \
    -t "http://timestamp.digicert.com" \
    -in "${1}" "${2}"
}

function runcmd() {
	if [ ${SILENT} -eq 1 ]; then
		"${@}" 2>/dev/null > /dev/null;
	else
		"${@}"
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

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 1 ]; then
	export BITS=32
	echo "Building MingW32 DirectSound x32..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all	
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_dsound_x32.zip" -- playbae.exe bass*
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_dsound_x32.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 2 ]; then
	export USE_SDL2=1
	echo "Building MingW32 SDL2 x32..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_sdl2_x32.zip" -- playbae.exe bass*
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_sdl2_x32.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 3 ]; then
	export BITS=64
	export USE_SDL2=
	echo "Building MingW32 DirectSound x64..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all 
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_dsound_x64.zip" -- playbae.exe bass*
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_dsound_x64.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 4 ]; then
	export USE_SDL2=1
	echo "Building MingW32 SDL2 x64..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_sdl2_x64.zip" -- playbae.exe bass*
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_sdl2_x64.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 5 ]; then
	echo "Building MingW32 SDL2 GUI x64..."
	runcmd make clean
	runcmd make -f Makefile.gui-mingw "-j$(nproc)" all
    signit "${BDIR}/zefidi.exe" "${BDIR}/zefidi_signed.exe"
    mv "${BDIR}/zefidi_signed.exe" "${BDIR}/zefidi.exe"
	runcmd cd "${BDIR}" || exit 1 && zip -9 "${ODIR}/zefidi_sdl2_x64.zip" -- zefidi.exe bass*
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.gui-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 6 ]; then
	export USE_SDL=0
	export NOAUTO=1
	export SF2_SUPPORT=0
	export MP3_DEC=1
	export WASM=1
	echo "Building Enscripten WASM..."
	runcmd make clean
	runcmd make -f Makefile.emcc "-j$(nproc)" all
	runcmd make -f Makefile.emcc pack
	install_file "${BDIR}/playbae_wasm.tar.gz" "${ODIR}/playbae_wasm.tar.gz"
	runcmd make -f Makefile.emcc clean
fi

cd "${RDIR}" || exit 1
ls -l "${ODIR}"

