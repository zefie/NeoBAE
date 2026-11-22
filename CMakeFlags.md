# NeoBAE CMake Flags

This document describes all optional CMake flags available when building NeoBAE.

## Usage

Pass flags at configure time with `-D<FLAG>=<VALUE>`:

```bash
cmake -B build -DDEBUG=ON -DBAE_DISABLE_MP3_DECODER=ON
cmake --build build
```

Run configure from the repository root (where the top-level `CMakeLists.txt` lives).

## Global Build Options

| Flag | Default | Description |
|------|---------|-------------|
| `DEBUG` | `OFF` | Enable debug build (zefidi debug console). Enables debug output from the NeoBAE engine. |
| `LDEBUG` | `OFF` | Enable full debug build (zefidi debug console, gdb debugging). Disables compile-time optimizations, retains debug symbols, and implies `DEBUG`. |
| `BUILD_DEBUG_INFO` | `OFF` | Emit debug symbols without changing optimization. On MSVC and llvm-mingw this writes `.pdb` files next to each exe and shared `libneobae`. On GCC/Clang Unix this adds DWARF (`-g`) and keeps frame pointers. Independent of `LDEBUG` (which is `-O0`). Used so nbeditor crash logs can resolve backtraces. |
| `NEOBAE_STATIC` | `OFF` | Enable static linking for NeoBAE apps and dependencies (best support on MinGW). When enabled and `NEOBAE_EXTERNAL_CODECS` is not already set, it is forced `ON`. When enabled and `NEOBAE_SHARED_LIBNEOBAE` is not already set, it is forced `ON`. On MinGW, sets static suffixes and link options. |
| `NEOBAE_BUILD_VCLIB` | `OFF` | Build an additional MSVC-style import library for `libneobae.dll` using `dlltool`. Only meaningful for MinGW shared `libneobae.dll` builds. |
| `USE_SDL2` | `OFF` | Prefer SDL2 as the platform backend when both SDL2 and SDL3 are available. Default auto-select prefers SDL3 when present. |
| `CMAKE_BUILD_TYPE` | (empty) | Build type: `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`. |

## Static/Coupling Options

These are controlled automatically when `NEOBAE_STATIC=ON` unless explicitly pre-set:

| Flag | Default (when static) | Description |
|------|------------------------|-------------|
| `NEOBAE_EXTERNAL_CODECS` | `ON` | Use external system libraries for codecs when available instead of bundled sources (FLAC, LAME, Vorbis, Opus, Ogg). |
| `NEOBAE_SHARED_LIBNEOBAE` | `ON` | Build `libneobae` as shared (DLL/.so) while keeping static dependency preference. |

## Platform Backend

| Flag | Default | Description |
|------|---------|-------------|
| `BAE_PLATFORM` | auto | Backend platform. Auto-detect prefers SDL3, then SDL2, then `WinOS` on Windows / `Dummy` elsewhere. Supported values: `Dummy`, `SDL2`, `SDL3`, `Raylib`, `WinOS`, `Ansi`, `WASM`, `Android`, `IOS`, `MacOSX`, `foobar2000`. |

## Feature Flags (BAE_DISABLE_*)

All feature flags default to `OFF` (feature enabled). Set to `ON` to disable.

| Flag | Description |
|------|-------------|
| `BAE_DISABLE_ADP_SUPPORT` | Disable ADP support. |
| `BAE_DISABLE_ADX_SUPPORT` | Disable ADX codec support. |
| `BAE_DISABLE_BAESCRIPT` | Disable BAEScript support. |
| `BAE_DISABLE_BEATNIK_SF2_NRPN` | Disable Beatnik SF2 NRPN behavior. |
| `BAE_DISABLE_BUILTIN_PATCHES_WITH_SF2` | Disable loading built-in patches in the background for SF2. |
| `BAE_DISABLE_BUILTIN_PATCHES_WITH_DLS` | Disable loading built-in patches in the background for DLS. |
| `BAE_DISABLE_CLASSIC_CHORUS` | Disable classic chorus path. |
| `BAE_DISABLE_WEBTV_MODE` | Disable the runtime WebTV Mode (WebTV velocity curve, LFO shapes, default channel pans, filter glide). |
| `BAE_DISABLE_CREATION_API` | Disable Creation API support (alias for RMF editor APIs). |
| `BAE_DISABLE_EMBED_FONT` | Disable embedded GUI font. |
| `BAE_DISABLE_EMBED_PATCHES` | Disable embedded built-in patches. |
| `BAE_DISABLE_FIX_SPAN_DC` | Disable Stereo Pan DC fix. |
| `BAE_DISABLE_FLAC_DECODER` | Disable FLAC decoder support. |
| `BAE_DISABLE_FLAC_ENCODER` | Disable FLAC encoder support. |
| `BAE_DISABLE_FLUIDLITE` | Disable NeoBAE's FluidLite SoundFont backend. Also disables SF2 support if it was auto-enabled. |
| `BAE_DISABLE_J2ME_PATCH` | Disable J2ME percussion bank patch (maps bank 120 for percussion intent). |
| `BAE_DISABLE_KARAOKE` | Disable karaoke support. |
| `BAE_DISABLE_LIB_HYPHEN` | Disable hyphen support (for karaoke lyric editor in NeoBAE Editor). If enabled, it is only built into NeoBAE Editor, not into libneobae. |
| `BAE_DISABLE_LZMA` | Disable LZMA compression support. Also disables ZMF support. |
| `BAE_DISABLE_MIDI_HARDWARE` | Disable hardware MIDI I/O in zefidi via RtMidi. |
| `BAE_DISABLE_MP3_DECODER` | Disable MP3 decoder support. |
| `BAE_DISABLE_MP3_ENCODER` | Disable MP3 encoder support. |
| `BAE_DISABLE_MTHC_SUPPORT` | Disable MTHC (Nokia Compressed MIDI) support. |
| `BAE_DISABLE_NATIVE_DLS` | Disable native DLS system. Also disables XMF support. |
| `BAE_DISABLE_NOKIA_PATCH` | Disable Nokia patch handling. |
| `BAE_DISABLE_OPUS_DECODER` | Disable Opus decoder support. Also disables ZMF support. |
| `BAE_DISABLE_OPUS_ENCODER` | Disable Opus encoder support. |
| `BAE_DISABLE_PLAYLIST` | Disable playlist support. |
| `BAE_DISABLE_QOA_SUPPORT` | Disable QOA codec support. Also disables ZMF support. |
| `BAE_DISABLE_RETRO_RINGTONE_SUPPORT` | Disable retro mobile ringtone format support (IMY/EMY/RNG/RTX/RTTTL). |
| `BAE_DISABLE_RMF_EDITOR` | Disable support for the BAE Editor API (required by NeoBAE Editor). |
| `BAE_DISABLE_RMI_SUPPORT` | Disable RMI support. |
| `BAE_DISABLE_ROLLED_MIDI_UNROLLING` | Disable unrolling of rolled MIDI files. |
| `BAE_DISABLE_SF2_CONVERTER` | Disable SF2->HSB converter support. |
| `BAE_DISABLE_SF2_SUPPORT` | Disable SF2 support. |
| `BAE_DISABLE_VORBIS_DECODER` | Disable Vorbis decoder support. Also disables ZMF support. |
| `BAE_DISABLE_VORBIS_ENCODER` | Disable Vorbis encoder support. |
| `BAE_DISABLE_WMA_SUPPORT` | Disable WMA (MSAUDIO) codec support. |
| `BAE_DISABLE_XMF_SUPPORT` | Disable XMF support. |
| `BAE_DISABLE_ZMF_SUPPORT` | Disable ZMF support. |


## Dependency Constraints

Several features are automatically disabled if their required dependencies are not available:

- **ZMF** requires: LZMA, Vorbis decoder, Opus decoder, FLAC decoder, QOA, Classic Chorus, Fix Span DC.
- **SF2** requires NeoBAE's FluidLite. If FluidLite is disabled, SF2 is disabled.
- **XMF** requires Native DLS. If Native DLS is disabled, XMF is disabled.
- **`BUILD_NBEDITOR`** requires `BAE_ENABLE_RMF_EDITOR`. If RMF editor / Creation API support is disabled (`BAE_DISABLE_RMF_EDITOR` or `BAE_DISABLE_CREATION_API`), `BUILD_NBEDITOR` is forced off.
- **MP3 encoder** requires bundled LAME sources (`neobae/src/thirdparty/lame-3.100-slim/`).
- **FLAC encoder/decoder** requires bundled libFLAC sources (`neobae/src/thirdparty/flac/`).
- **Vorbis encoder/decoder** requires bundled Vorbis/Ogg sources (`neobae/src/thirdparty/libvorbis/`, `neobae/src/thirdparty/libogg/`).
- **Opus encoder/decoder** requires bundled Opus/Opusfile/Ogg sources (`neobae/src/thirdparty/opus/`, `neobae/src/thirdparty/opusfile/`, `neobae/src/thirdparty/libogg/`).

Run `git submodule update --init --recursive` if bundled third-party sources are missing.

## Embedded Resources

| Flag | Default | Description |
|------|---------|-------------|
| `BAE_EMBED_PATCH_FILE` | `neobae/src/banks/patches111/patches111.hsb` | HSB bank file to embed as built-in patches. Set to empty string to disable embedding. Requires Python 3. |
| `BAE_EMBED_TTF_FILE` | `neobae/src/thirdparty/fonts/LiberationSans-Regular.ttf` | TTF font file to embed for zefidi GUI use (Liberation Sans). Set to empty string to disable embedding. Requires Python 3. |
| `BAE_NBEDITOR_FONT_REGULAR` | `neobae/src/thirdparty/fonts/LiberationSans-Regular.ttf` | Regular TTF embedded for nbeditor. |
| `BAE_NBEDITOR_FONT_ITALIC` | `neobae/src/thirdparty/fonts/LiberationSans-Italic.ttf` | Italic TTF embedded for nbeditor (aliases). |

Embedding is only effective when:
1. The corresponding `BAE_DISABLE_EMBED_*` flag is `OFF` (for patch/font embed options).
2. The file path is non-empty and the file exists.
3. Python 3 interpreter is found.

nbeditor embeds Regular + Italic Liberation Sans automatically when those files exist and Python 3 is available.

## Build Target Options

| Flag | Default | Description |
|------|---------|-------------|
| `BUILD_PLAYBAE` | `ON` | Build `playbae` when a non-Dummy/non-Ansi backend is selected. |
| `BUILD_ZEFIDI` | `ON` | Build `zefidi` when SDL2/SDL3 backend and matching SDL_ttf are available. |
| `BUILD_CLITOOLS` | `ON` | Build CLI tools: `songtool`, `rmfutil`, `rmiutil`, `xmfutil`, `sf2-hsb`, `mod2rmf`, `bankrecomp`, `ringtone2mid`, plus codec helpers (`adp2wav`, `adx2wav`, `mthc_decomp`) when enabled. |
| `BUILD_NBEDITOR` | `ON` | Build NeoBAE Editor (`nbeditor`, Dear ImGui + SDL3). Requires `BAE_ENABLE_RMF_EDITOR`, SDL3 headers/library, and `neobae/src/nbeditor` + `neobae/src/thirdparty/imgui` sources. |

## Internal Compatibility Variables

These are derived internally and generally should not be set directly:

| Variable | Source |
|----------|--------|
| `BAE_ENABLE_*` | Each is set `ON` by default, then flipped `OFF` if the corresponding `BAE_DISABLE_*` flag is set. |

## Example Configurations

Minimal build (miniBAE (w/ mobileBAE) style, no codecs, no extras):
```bash
cmake -B build \
  -DBAE_DISABLE_MP3_ENCODER=ON \
  -DBAE_DISABLE_FLAC_DECODER=ON \
  -DBAE_DISABLE_FLAC_ENCODER=ON \
  -DBAE_DISABLE_VORBIS_DECODER=ON \
  -DBAE_DISABLE_VORBIS_ENCODER=ON \
  -DBAE_DISABLE_OPUS_DECODER=ON \
  -DBAE_DISABLE_OPUS_ENCODER=ON \
  -DBAE_DISABLE_ZMF_SUPPORT=ON \
  -DBAE_DISABLE_SF2_SUPPORT=ON \
  -DBAE_DISABLE_FLUIDLITE=ON \
  -DBAE_DISABLE_PLAYLIST=ON \
  -DBAE_DISABLE_BAESCRIPT=ON \
  -DBAE_DISABLE_ADP_SUPPORT=ON \
  -DBAE_DISABLE_ADX_SUPPORT=ON \
  -DBAE_DISABLE_QOA_SUPPORT=ON \
  -DBAE_DISABLE_WMA_SUPPORT=ON \
  -DBAE_DISABLE_RMI_SUPPORT=ON \
  -DBUILD_CLITOOLS=OFF \
  -DBUILD_NBEDITOR=OFF
```

SDL2 GUI build with codecs:
```bash
cmake -B build \
  -DBAE_PLATFORM=SDL2 \
  -DDEBUG=ON
```

Static MinGW build:
```bash
cmake -B build \
  -G "MinGW Makefiles" \
  -DNEOBAE_STATIC=ON \
  -DBAE_PLATFORM=WinOS
```

Visual Studio Build (with CMake)
```bash
cmake -B build \
  -G "Visual Studio 18 2026" \
  -DBAE_PLATFORM=WinOS
```
