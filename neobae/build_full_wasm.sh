#!/bin/bash
RDIR="$(realpath "$(pwd)")"
BDIR="/src/neobae/bin"
ODIR="/src/neobae/out"


SILENT=1

if [ -n "$1" ]; then
	SILENT=0
	shift
fi

function runcmd() {
	if [ ${SILENT} -eq 1 ]; then
		docker run --rm -it -v "${RDIR}/../:/src" zefie/emsdk:latest "${@}" 2>/dev/null > /dev/null;
	else
		docker run --rm -it -v "${RDIR}/../:/src" zefie/emsdk:latest "${@}"
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

runcmd bash -c 'mkdir -p "${BDIR}" && mkdir -p "${ODIR}"'

echo "Building Emscripten WebAssembly (NeoBAE Only)..."
runcmd bash -c 'cd neobae &&
make clean &&
make -f Makefile.emcc "-j$(nproc)" all &&
make -f Makefile.emcc pack &&
mv -v "${BDIR}/NeoBAE_WASM.tar.gz" "${ODIR}/NeoBAE_WASM.tar.gz" &&
make -f Makefile.emcc clean'


echo "Building Emscripten WebAssembly (NeoBAE with FluidLite)..."
runcmd bash -c 'cd neobae &&
make clean &&
make -f Makefile.emcc-full "-j$(nproc)" all &&
make -f Makefile.emcc-full pack
mv -v "${BDIR}/NeoBAE_WASM_FluidLite.tar.gz" "${ODIR}/NeoBAE_WASM_FluidLite.tar.gz"
make -f Makefile.emcc-full clean'

