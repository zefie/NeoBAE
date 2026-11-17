#!/bin/bash
# Build the NeoBAEDroid non-SAF release APK ("apk" flavor, com.zefie.NeoBAEDroid.NoSAF,
# uses MANAGE_EXTERNAL_STORAGE) for direct download / sideloading.
#
#   .zefie/build_android_apk.sh [--clean] [extra gradle args...]
#
# Output: out/android/NeoBAE-<versionName>-NoSAF.apk
# See .zefie/android_common.sh for the required environment (ANDROID_HOME, JAVA_HOME, signing).
source "$(dirname "${0}")/android_common.sh"

TASKS=()
if [ "${1:-}" = "--clean" ]; then TASKS+=(clean); shift; fi
TASKS+=(:app:assembleApkRelease)

setup_sdk
setup_ndk
setup_signing
run_gradle "${TASKS[@]}" "$@"

VER="$(android_version_name)"
APK_DIR="${DROID_DIR}/app/build/outputs/apk/apk/release"
SRC="$(ls -t "${APK_DIR}"/*.apk 2>/dev/null | head -n1 || true)"
[ -n "${SRC}" ] || die "no APK produced in ${APK_DIR}"

mkdir -p "${OUT_DIR}"
DEST="${OUT_DIR}/NeoBAE-${VER}-NoSAF.apk"
[[ "${SRC}" == *-unsigned.apk ]] && DEST="${OUT_DIR}/NeoBAE-${VER}-NoSAF-unsigned.apk"
cp -f "${SRC}" "${DEST}"
echo "=== APK: ${DEST}"
