#!/bin/bash
# Build an installable APK of the SAF ("play" flavor, com.zefie.NeoBAEDroid) build for
# local testing, instead of the Play Store .aab.
#
#   .zefie/build_android_saf_apk.sh [--clean] [--release] [extra gradle args...]
#
#   (default)  Debug build, signed with the auto-generated debug key: installs directly.
#   --release  Release build (minified, like the Play bundle). Signed with NEOBAE_KEYSTORE
#              if set; otherwise unsigned and NOT installable until signed.
#
# Note: same applicationId as the Play Store install. Android refuses to install over an
# app signed with a different key, so uninstall the Play version first (or vice versa).
#
# Output: out/android/NeoBAE-<versionName>-SAF[-debug|-unsigned].apk
# See .zefie/android_common.sh for the environment (ANDROID_HOME, JAVA_HOME, NDK, signing).
source "$(dirname "${0}")/android_common.sh"

BUILD_TYPE=debug
TASKS=()
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) TASKS+=(clean) ;;
		--release) BUILD_TYPE=release ;;
		*) break ;;
	esac
	shift
done

if [ "${BUILD_TYPE}" = release ]; then
	TASKS+=(:app:assemblePlayRelease)
else
	TASKS+=(:app:assemblePlayDebug)
fi

setup_sdk
setup_ndk
if [ "${BUILD_TYPE}" = release ]; then
	setup_signing
else
	disable_signing # debug builds use the debug keystore
fi
run_gradle "${TASKS[@]}" "$@"

VER="$(android_version_name)"
APK_DIR="${DROID_DIR}/app/build/outputs/apk/play/${BUILD_TYPE}"
SRC="$(ls -t "${APK_DIR}"/*.apk 2>/dev/null | head -n1 || true)"
[ -n "${SRC}" ] || die "no APK produced in ${APK_DIR}"

mkdir -p "${OUT_DIR}"
if [ "${BUILD_TYPE}" = debug ]; then
	DEST="${OUT_DIR}/NeoBAE-${VER}-SAF-debug.apk"
elif [[ "${SRC}" == *-unsigned.apk ]]; then
	DEST="${OUT_DIR}/NeoBAE-${VER}-SAF-unsigned.apk"
else
	DEST="${OUT_DIR}/NeoBAE-${VER}-SAF.apk"
fi
cp -f "${SRC}" "${DEST}"
echo "=== APK: ${DEST}"
