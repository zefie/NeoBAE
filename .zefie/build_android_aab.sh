#!/bin/bash
# Build the NeoBAEDroid SAF release App Bundle ("play" flavor, com.zefie.NeoBAEDroid,
# Storage Access Framework only) for upload to the Google Play Console.
#
#   .zefie/build_android_aab.sh [--clean] [extra gradle args...]
#
# Output: out/android/NeoBAE-<versionName>-play.aab
# Play requires a signed bundle (your upload key); set NEOBAE_KEYSTORE & co.
# See .zefie/android_common.sh for the required environment (ANDROID_HOME, JAVA_HOME, signing).
source "$(dirname "${0}")/android_common.sh"

TASKS=()
if [ "${1:-}" = "--clean" ]; then TASKS+=(clean); shift; fi
TASKS+=(:app:bundlePlayRelease)

setup_sdk
setup_ndk
setup_signing
[ "${SIGNING_ENABLED}" = 1 ] || echo "warning: Play Console will reject an unsigned bundle" >&2
run_gradle "${TASKS[@]}" "$@"

VER="$(android_version_name)"
SRC="${DROID_DIR}/app/build/outputs/bundle/playRelease/app-play-release.aab"
[ -f "${SRC}" ] || die "no bundle produced at ${SRC}"

mkdir -p "${OUT_DIR}"
DEST="${OUT_DIR}/NeoBAE-${VER}-play.aab"
[ "${SIGNING_ENABLED}" = 1 ] || DEST="${OUT_DIR}/NeoBAE-${VER}-play-unsigned.aab"
cp -f "${SRC}" "${DEST}"
echo "=== AAB: ${DEST}"
