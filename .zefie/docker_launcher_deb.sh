#!/bin/bash
# Build NeoBAE .deb packages (binary + source) inside the deb-toolchain Docker images.
#
#   .zefie/docker_launcher_deb.sh [options] <target|all> [target...]
#
# Options:
#   --debug         Debug packages: -O0 -g, debug console, binaries NOT stripped
#                   (output goes to out/deb/<target>-debug/)
#   --no-source     Skip building the source package (.dsc/.tar.xz)
#   --rebuild-image Rebuild the Docker image(s) even if they already exist
#   --list          List supported targets and exit
#   -h, --help      Show this help
#
# Targets are <release>-<arch>, same names as the deb_tc_packer docker.sh.
# Output: <repo>/out/deb/<target>[-debug]/
#
# Environment:
#   DEB_TOOLCHAIN_DIR  Path to the deb_tc_packer checkout
#                      (default: <repo>/../../docker/deb_tc_packer)
set -euo pipefail

THEROOT="$(realpath "$(dirname "${0}")/..")"
TOOLCHAIN_DIR="${DEB_TOOLCHAIN_DIR:-$(realpath -m "${THEROOT}/../../docker/deb_tc_packer")}"

# target -> "base-image arch family"
# family selects the extra -dev packages: sdl3 or sdl2 (zefidi and nbeditor support both;
# Ubuntu 22.04/24.04 only ship SDL2).
declare -A TARGETS=(
  [trixie-amd64]="debian:trixie amd64 sdl3"
  [trixie-arm64]="debian:trixie arm64 sdl3"            # Raspberry Pi OS 64-bit (Pi 3/4/5)
  [ubuntu2404-amd64]="ubuntu:24.04 amd64 sdl2"
  [ubuntu2404-arm64]="ubuntu:24.04 arm64 sdl2"
  [ubuntu2204-amd64]="ubuntu:22.04 amd64 sdl2"
)
COMMON_PKGS="zlib1g-dev libasound2-dev libcurl4-openssl-dev"
declare -A FAMILY_PKGS=(
  [sdl3]="libsdl3-dev libsdl3-ttf-dev"
  [sdl2]="libsdl2-dev libsdl2-ttf-dev"
)

usage() { sed -n '2,/^set -e/p' "${0}" | sed '$d' | sed 's/^# \{0,1\}//'; }
list_targets() { printf '%s\n' "${!TARGETS[@]}" | sort; }

DEBUG=0
SOURCE=1
REBUILD=0
REQUESTED=()
while [ $# -gt 0 ]; do
  case "$1" in
    --debug) DEBUG=1 ;;
    --no-source) SOURCE=0 ;;
    --rebuild-image) REBUILD=1 ;;
    --list) list_targets; exit 0 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
    *) REQUESTED+=("$1") ;;
  esac
  shift
done

if [ ${#REQUESTED[@]} -eq 0 ]; then
  echo "error: no target given. Supported targets:" >&2
  list_targets | sed 's/^/  /' >&2
  echo "  all" >&2
  exit 1
fi

RUN_TARGETS=()
for t in "${REQUESTED[@]}"; do
  if [ "$t" = all ]; then
    mapfile -t RUN_TARGETS < <(list_targets)
    break
  fi
  [ -n "${TARGETS[$t]:-}" ] || { echo "error: unknown target '$t'. Supported:" >&2; list_targets | sed 's/^/  /' >&2; exit 1; }
  RUN_TARGETS+=("$t")
done

[ -f "${TOOLCHAIN_DIR}/Dockerfile" ] || { echo "error: no Dockerfile in ${TOOLCHAIN_DIR} (set DEB_TOOLCHAIN_DIR)" >&2; exit 1; }

# Runs inside the container (as the host uid). Args: target debug source
read -r -d '' INNER <<'INNER_EOF' || true
set -euo pipefail
TARGET="$1"; DEBUG="$2"; SOURCE="$3"
SUFFIX=""; [ "$DEBUG" = 1 ] && SUFFIX="-debug"
BUILD="/src/build-deb-${TARGET}${SUFFIX}"
OUT="/out"
HOST_ARCH=$(dpkg --print-architecture)

CMAKE_ARGS=(-DNEOBAE_SHARED_LIBNEOBAE=ON)
if [ "$DEBUG" = 1 ]; then
  # LDEBUG: -g -O0 and DEBUG console; BUILD_DEBUG_INFO also disables stripping in CPack.
  CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Debug -DDEBUG=ON -DLDEBUG=ON -DBUILD_DEBUG_INFO=ON)
else
  CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Release)
fi
[ "$TARGET_ARCH" != "$HOST_ARCH" ] && CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE=/opt/toolchain.cmake)

echo "=== [$TARGET${SUFFIX}] configure"
rm -rf "$BUILD"
cmake -S /src -B "$BUILD" "${CMAKE_ARGS[@]}"
echo "=== [$TARGET${SUFFIX}] build"
cmake --build "$BUILD" -j"$(nproc)"
echo "=== [$TARGET${SUFFIX}] package (binary)"
(cd "$BUILD" && cpack -G DEB)
rm -f "$OUT"/*.deb "$OUT"/*.sha256
cp "$BUILD"/*.deb "$BUILD"/*.deb.sha256 "$OUT"/

if [ "$SOURCE" = 1 ]; then
  echo "=== [$TARGET${SUFFIX}] package (source)"
  cd /src
  # Version: exact tag, else 0~git<commitdate>.<hash>[+dirty] (native versions cannot contain '-').
  if TAG=$(git describe --exact-match --tags HEAD 2>/dev/null); then
    VER="${TAG#v}"
  else
    VER="0~git$(git log -1 --format=%cd --date=format:%Y%m%d).$(git rev-parse --short HEAD)"
  fi
  [ -n "$(git status --porcelain --ignore-submodules=none 2>/dev/null)" ] && VER="${VER}+dirty"
  STAGE=$(mktemp -d)
  SRC="$STAGE/neobae-${VER}"
  mkdir -p "$SRC"
  # Tracked files (incl. submodule contents) + untracked-but-not-ignored files,
  # minus sample content, docs and other-platform trees the Linux build never reads.
  { git ls-files --recurse-submodules; git ls-files -o --exclude-standard; } | sort -u \
    | grep -Ev '^(content|docs|deps/android|neobae/osx|neobae/src/(NeoBAEDroid|iOSPlay|wasm))/' \
    | tar --ignore-failed-read -T - -cf - 2>/dev/null | tar -xf - -C "$SRC"
  cat > "$SRC/debian/changelog" <<CL
neobae (${VER}) unstable; urgency=medium

  * Snapshot build of ${VER}.

 -- zefie <zefie@users.noreply.github.com>  $(date -R)
CL
  (cd "$SRC" && dpkg-buildpackage -S -d -nc -us -uc)
  mv -f "$STAGE"/neobae_* "$OUT"/
  rm -rf "$STAGE"
fi
ls -l "$OUT"
INNER_EOF

for t in "${RUN_TARGETS[@]}"; do
  read -r BASE ARCH FAMILY <<<"${TARGETS[$t]}"
  IMAGE="zefie/neobae-deb:${t}"
  if [ "$REBUILD" = 1 ] || ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "=== building image ${IMAGE}"
    docker build --build-arg BASE="${BASE}" --build-arg TARGET_ARCH="${ARCH}" \
      --build-arg EXTRA_PKGS="${COMMON_PKGS} ${FAMILY_PKGS[$FAMILY]}" \
      -t "${IMAGE}" "${TOOLCHAIN_DIR}"
  fi
  SUFFIX=""; [ "${DEBUG}" = 1 ] && SUFFIX="-debug"
  OUTDIR="${THEROOT}/out/deb/${t}${SUFFIX}"
  mkdir -p "${OUTDIR}"
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${THEROOT}":/src -v "${OUTDIR}":/out -w /src \
    "${IMAGE}" bash -c "${INNER}" _ "${t}" "${DEBUG}" "${SOURCE}"
  echo "=== ${t}${SUFFIX}: packages in ${OUTDIR}"
done
