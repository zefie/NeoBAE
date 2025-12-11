#!/bin/bash
RDIR="$(realpath "$(pwd)")"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

SILENT=1

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
export SF2_SUPPORT=0
export USING_FLUIDSYNTH=0
export MP3_DEC=1
echo "Building Enscripten WebAssembly (miniBAE Only)..."
runcmd make clean
runcmd make -f Makefile.emcc "-j$(nproc)" all
runcmd make -f Makefile.emcc pack
install_file "${BDIR}/miniBAE_WASM.tar.gz" "${ODIR}/miniBAE_WASM.tar.gz"
runcmd make -f Makefile.emcc clean

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

