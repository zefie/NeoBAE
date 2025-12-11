#!/bin/bash
RDIR="$(realpath "$(pwd)")"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

export USE_FLUIDSYNTH=1
SILENT=1

if  [ "${1}" == "testing" ]; then
	shift;
	function signit() {
  		echo "Skipping signing for testing build..."
		mv "${1}" "${2}"
	}
else
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
fi

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

if [ "${USE_BASSMIDI}" == 1 ]; then
	if [ ! -d "${RDIR}/src/thirdparty/bassmidi/" ]; then
		# Custom for zefie's Jenkins build system
		cp -r /opt/bassmidi "${RDIR}/src/thirdparty"
	fi
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 2 ]; then
	export USE_SDL3=1
	export BITS=32
	echo "Building MingW32 SDL3 x32..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_win_sdl3_x32.zip" -- playbae.exe libfluid*.dll SDL*.dll sndfile.dll
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_sdl3_x32.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 3 ]; then
	export USE_SDL3=1
	export BITS=64
	echo "Building MingW32 SDL3 x64..."
	runcmd make clean
	runcmd make -f Makefile.mingw "-j$(nproc)" all
    signit "${BDIR}/playbae.exe" "${BDIR}/playbae_signed.exe"
    mv "${BDIR}/playbae_signed.exe" "${BDIR}/playbae.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/playbae_win_sdl3_x64.zip" -- playbae.exe libfluid*.dll SDL*.dll sndfile.dll
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/libMiniBAE_win_sdl3_x64.zip" -- *.dll *.lib *.a
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 4 ]; then
	export USE_SDL3=1
	export BITS=32
	echo "Building MingW32 SDL3 GUI x32..."
	runcmd make clean
	runcmd make -f Makefile.gui-mingw "-j$(nproc)" all
    signit "${BDIR}/zefidi.exe" "${BDIR}/zefidi_signed.exe"
    mv "${BDIR}/zefidi_signed.exe" "${BDIR}/zefidi.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/zefidi_win_sdl3_x32.zip" -- zefidi.exe libfluid*.dll SDL*.dll sndfile.dll
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.gui-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 5 ]; then
	export USE_SDL3=1
	export BITS=64
	echo "Building MingW32 SDL3 GUI x64..."
	runcmd make clean
	runcmd make -f Makefile.gui-mingw "-j$(nproc)" all
    signit "${BDIR}/zefidi.exe" "${BDIR}/zefidi_signed.exe"
    mv "${BDIR}/zefidi_signed.exe" "${BDIR}/zefidi.exe"
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/zefidi_win_sdl3_x64.zip" -- zefidi.exe libfluid*.dll SDL*.dll sndfile.dll
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.gui-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 6 ]; then
	export BITS=32
	echo "Building RMFInfo (x32)..."
	runcmd make clean
	runcmd make -f Makefile.rmfinfo-mingw "-j$(nproc)" all
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/rmfinfo_win_x32.zip" -- rmfinfo.exe
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.rmfinfo-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 7 ]; then
	export BITS=64
	echo "Building RMFInfo (x64)..."
	runcmd make clean
	runcmd make -f Makefile.rmfinfo-mingw "-j$(nproc)" all
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/rmfinfo_win_x64.zip" -- rmfinfo.exe
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.rmfinfo-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 8 ]; then
	export BITS=32
	echo "Building RMF2MID (x32)..."
	runcmd make clean
	runcmd make -f Makefile.rmf2mid-mingw "-j$(nproc)" all
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/rmf2mid_win_x32.zip" -- rmf2mid.exe
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.rmf2mid-mingw clean
fi

if [ -z "${SKIPTO}" ] || [ "${SKIPTO}" -le 9 ]; then
	export BITS=64
	echo "Building RMF2MID (x64)..."
	runcmd make clean
	runcmd make -f Makefile.rmf2mid-mingw "-j$(nproc)" all
	runcmd cd "${BDIR}" || exit 1 && runcmd zip -9 "${ODIR}/rmf2mid_win_x64.zip" -- rmf2mid.exe
	runcmd cd "${RDIR}" || exit 1
	runcmd make -f Makefile.rmf2mid-mingw clean
fi

cd "${RDIR}" || exit 1
ls -l "${ODIR}"

