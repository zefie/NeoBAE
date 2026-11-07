#!/bin/bash
RDIR="$(realpath "$(pwd)")"
BDIR="${RDIR}/bin"
ODIR="${RDIR}/out"

mkdir -p "${BDIR}"
mkdir -p "${ODIR}"

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

echo "Building Emscripten WebAssembly (NeoBAE Only)..."
runcmd make clean
runcmd make -f Makefile.emcc "-j$(nproc)" all
runcmd make -f Makefile.emcc pack
install_file "${BDIR}/NeoBAE_WASM.tar.gz" "${ODIR}/NeoBAE_WASM.tar.gz"
runcmd make -f Makefile.emcc clean


echo "Building Emscripten WebAssembly (NeoBAE with FluidLite)..."
runcmd make clean
runcmd make -f Makefile.emcc-full "-j$(nproc)" all
runcmd make -f Makefile.emcc-full pack
install_file "${BDIR}/NeoBAE_WASM_FluidLite.tar.gz" "${ODIR}/NeoBAE_WASM_FluidLite.tar.gz"
runcmd make -f Makefile.emcc-full clean

