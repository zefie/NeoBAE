miniBAE DroidPlay Android module

This module is a small Android wrapper that builds the native miniBAE library (using the existing `jni/Android.mk`) and exposes a minimal UI to load/play a MIDI file from assets.

How to use

1. Copy a MIDI file from the repo (for example `../../content/midi/1barloop.mid`) into `app/src/main/assets/1barloop.mid`.
2. Open `minibae/src/DroidPlay` in Android Studio.
3. Build and run on device or emulator (NDK required).

Notes

- The project uses `ndk-build` via Gradle's externalNativeBuild. The existing `jni/Android.mk` compiles the miniBAE sources.
- If you prefer CMake, we can add a CMakeLists that mirrors `Android.mk` instead.

Build notes for 64-bit (arm64/aarch64):

- To produce a 64-bit `libminiBAE.so` for Android (arm64-v8a), ensure `jni/Application.mk` and `app/build.gradle` include `arm64-v8a` (and `x86_64` if desired). This repo adds `arm64-v8a` to `Application.mk` and the Gradle `abiFilters`.
- You can build from Android Studio (it will invoke ndk-build via Gradle) or run `ndk-build` manually in `minibae/src/DroidPlay`.

Quick manual build example (PowerShell):

```powershell
cd d:\git\miniBAE\minibae\src\DroidPlay
ndk-build -j 8
```

After build, the shared libraries will be in `app/src/main/libs/<ABI>/libminiBAE.so` (or `obj/local/<ABI>` for intermediate objects).
