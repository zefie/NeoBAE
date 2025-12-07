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
	SILENT=0
	shift
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

export USE_SDL=0
export NOAUTO=1
export SF2_SUPPORT=1
export USING_FLUIDSYNTH=1
export MP3_DEC=1
echo "Building Enscripten WebAssembly (miniBAE & FluidSynth)..."
runcmd make clean
runcmd make -f Makefile.emcc-full "-j$(nproc)" all
runcmd make -f Makefile.emcc-full pack
install_file "${BDIR}/miniBAE_WASM_FluidSynth.tar.gz" "${ODIR}/miniBAE_WASM_FluidSynth.tar.gz"
runcmd make -f Makefile.emcc-full clean

