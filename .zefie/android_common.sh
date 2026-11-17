#!/bin/bash
# Shared helpers for the NeoBAEDroid build scripts. Sourced, not run directly.
#
# Environment:
#   ANDROID_HOME / ANDROID_SDK_ROOT  Linux Android SDK (needs NDK 27.0.12077973, platform 36).
#                                    Required when local.properties' sdk.dir is not usable here
#                                    (e.g. it points at a Windows SDK).
#                                    Default: ~/dev/android/sdk if present.
#   NEOBAE_NDK                       Standalone NDK dir (default: ~/dev/android-ndk-r27d if present;
#                                    otherwise the SDK's ndk/27.0.12077973 is used).
#   JAVA_HOME                        Temurin JDK 25 (daemon pin in gradle/gradle-daemon-jvm.properties).
#                                    Default: ~/dev/android/jdk-25 if present, else `java` on PATH.
#   NEOBAE_KEYSTORE                  Path to the signing keystore (.jks / .keystore)
#   NEOBAE_KEYSTORE_PASS             Keystore password (prompted for if unset)
#   NEOBAE_KEY_ALIAS                 Key alias
#   NEOBAE_KEY_PASS                  Key password (prompted for if unset; blank = keystore password)
#   If NEOBAE_KEYSTORE is unset, outputs are left unsigned.
set -euo pipefail

NEOBAE_KEYSTORE="${NEOBAE_KEYSTORE:-/mnt/d/ssl/androidks/zefie}"
NEOBAE_KEY_ALIAS="${NEOBAE_KEY_ALIAS:-app-signing-key}"
THEROOT="$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")"
DROID_DIR="${THEROOT}/neobae/src/NeoBAEDroid"
OUT_DIR="${THEROOT}/out/android"
LOCAL_PROPS="${DROID_DIR}/local.properties"
LOCAL_PROPS_BACKUP=""

die() { echo "error: $*" >&2; exit 1; }

android_version_name() {
	tr -d '\r' < "${DROID_DIR}/app/build.gradle" | sed -n 's/^[[:space:]]*versionName = "\(.*\)"/\1/p' | head -n1
}

restore_local_props() {
	if [ -n "${LOCAL_PROPS_BACKUP}" ] && [ -f "${LOCAL_PROPS_BACKUP}" ]; then
		mv -f "${LOCAL_PROPS_BACKUP}" "${LOCAL_PROPS}"
	fi
}

# Make sure gradle sees a usable SDK. local.properties wins over ANDROID_HOME, so if its
# sdk.dir is missing/unusable on this machine, swap it out for the build and restore it after.
setup_sdk() {
	local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
	if [ -z "${sdk}" ] && [ -d "${HOME}/dev/android/sdk/platforms" ]; then
		sdk="${HOME}/dev/android/sdk"
	fi
	local props_sdk=""
	if [ -f "${LOCAL_PROPS}" ]; then
		props_sdk="$(sed -n 's/^sdk\.dir=//p' "${LOCAL_PROPS}" | tail -n1)"
	fi

	if [ -n "${props_sdk}" ] && [ -d "${props_sdk}" ] && [ -z "${sdk}" ]; then
		return 0 # local.properties already points at a usable SDK
	fi
	[ -n "${sdk}" ] || die "no usable Android SDK: set ANDROID_HOME (local.properties sdk.dir='${props_sdk}')"
	[ -d "${sdk}" ] || die "ANDROID_HOME '${sdk}' does not exist"

	if [ "${props_sdk}" != "${sdk}" ]; then
		if [ -f "${LOCAL_PROPS}" ]; then
			LOCAL_PROPS_BACKUP="$(mktemp "${LOCAL_PROPS}.bak.XXXXXX")"
			cp -f "${LOCAL_PROPS}" "${LOCAL_PROPS_BACKUP}"
			trap restore_local_props EXIT
			grep -v '^sdk\.dir=' "${LOCAL_PROPS_BACKUP}" > "${LOCAL_PROPS}" || true
		fi
		echo "sdk.dir=${sdk}" >> "${LOCAL_PROPS}"
	fi
	export ANDROID_HOME="${sdk}"
}

# Gradle args selecting a standalone NDK (version read from its source.properties).
NDK_ARGS=()
setup_ndk() {
	NDK_ARGS=()
	local ndk="${NEOBAE_NDK:-}"
	if [ -z "${ndk}" ] && [ -d "${HOME}/dev/android-ndk-r27d" ]; then
		ndk="${HOME}/dev/android-ndk-r27d"
	fi
	[ -n "${ndk}" ] || return 0
	[ -f "${ndk}/source.properties" ] || die "NEOBAE_NDK '${ndk}' is not an NDK (no source.properties)"
	local rev
	rev="$(sed -n 's/^Pkg\.Revision *= *//p' "${ndk}/source.properties" | tr -d '\r')"
	[ -n "${rev}" ] || die "could not read Pkg.Revision from ${ndk}/source.properties"
	echo "=== using NDK ${rev} at ${ndk}"
	NDK_ARGS=("-Pneobae.ndkPath=$(realpath "${ndk}")" "-Pneobae.ndkVersion=${rev}")
}

# Release signing: app/build.gradle reads NEOBAE_KEYSTORE / NEOBAE_KEYSTORE_PASS /
# NEOBAE_KEY_ALIAS / NEOBAE_KEY_PASS from the environment, so passwords never go on a
# command line (environment is only readable by the same user).
SIGNING_ENABLED=0
setup_signing() {
	SIGNING_ENABLED=0
	if [ -z "${NEOBAE_KEYSTORE:-}" ]; then
		echo "warning: NEOBAE_KEYSTORE not set; output will be UNSIGNED" >&2
		return 0
	fi
	[ -f "${NEOBAE_KEYSTORE}" ] || die "keystore '${NEOBAE_KEYSTORE}' not found"
	[ -n "${NEOBAE_KEY_ALIAS:-}" ] || die "NEOBAE_KEY_ALIAS not set"

	# Prompt for any password not supplied via the environment.
	if [ -z "${NEOBAE_KEYSTORE_PASS:-}" ]; then
		NEOBAE_KEYSTORE_PASS="$(prompt_secret "Keystore password for ${NEOBAE_KEYSTORE}: ")"
		[ -n "${NEOBAE_KEYSTORE_PASS}" ] || die "no keystore password given"
	fi
	if [ -z "${NEOBAE_KEY_PASS:-}" ]; then
		NEOBAE_KEY_PASS="$(prompt_secret "Key password for '${NEOBAE_KEY_ALIAS}' (blank = same as keystore): ")"
		[ -n "${NEOBAE_KEY_PASS}" ] || NEOBAE_KEY_PASS="${NEOBAE_KEYSTORE_PASS}"
	fi

	NEOBAE_KEYSTORE="$(realpath "${NEOBAE_KEYSTORE}")"
	export NEOBAE_KEYSTORE NEOBAE_KEYSTORE_PASS NEOBAE_KEY_ALIAS NEOBAE_KEY_PASS
	SIGNING_ENABLED=1
}

# Make sure gradle never signs with stale variables (e.g. debug builds).
disable_signing() {
	SIGNING_ENABLED=0
	unset NEOBAE_KEYSTORE NEOBAE_KEYSTORE_PASS NEOBAE_KEY_ALIAS NEOBAE_KEY_PASS
}

# prompt_secret <prompt>: read a line from the terminal without echo; prints it on stdout.
prompt_secret() {
	[ -r /dev/tty ] || die "no terminal to prompt on; set NEOBAE_KEYSTORE_PASS (and NEOBAE_KEY_PASS)"
	local secret
	IFS= read -r -s -p "$1" secret < /dev/tty
	echo >/dev/tty
	printf '%s' "${secret}"
}

# run_gradle <task> [extra gradle args...]
run_gradle() {
	# The project pins the Gradle daemon to Temurin 25 (gradle/gradle-daemon-jvm.properties).
	if [ -z "${JAVA_HOME:-}" ] && [ -x "${HOME}/dev/android/jdk-25/bin/java" ]; then
		export JAVA_HOME="${HOME}/dev/android/jdk-25"
	fi
	if [ -z "${JAVA_HOME:-}" ] && command -v java >/dev/null 2>&1; then
		JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
		export JAVA_HOME
	fi
	[ -n "${JAVA_HOME:-}" ] || die "JAVA_HOME not set (gradle.properties disables toolchain auto-detect)"
	chmod +x "${DROID_DIR}/gradlew"
	(cd "${DROID_DIR}" && ./gradlew --no-daemon "$@" "${NDK_ARGS[@]}")
}
