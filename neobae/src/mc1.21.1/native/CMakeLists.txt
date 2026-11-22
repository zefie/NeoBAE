#
# NeoBAE for Minecraft - native CMake build
#
# Builds libNeoBAE.{so,dll,dylib} containing:
#   * BAE_Source/Common/*  (engine)
#   * BAE_API_Dummy.c       (pull-mode platform backend - no native audio thread)
#   * neobaemc_jni.c        (JNI shim for com.zefie.neobaemc.audio.*)
#   * bundled thirdparty codecs (libogg, libvorbis, libflac, lzma, libg722, minimp3)
#
# The resulting shared library is dropped into build/<os>-<arch>/<libname>
# where the Gradle `processResources` task picks it up.
#
cmake_minimum_required(VERSION 3.20)
project(NeoBAEMC C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Locate sources --------------------------------------------------------
set(MC_DIR  ${CMAKE_CURRENT_SOURCE_DIR})
set(NB_ROOT ${MC_DIR}/../../..)               # neobae/src/mc1.21.1/native/ -> neobae/
get_filename_component(NB_ROOT "${NB_ROOT}" ABSOLUTE)
set(BAE     ${NB_ROOT}/src/BAE_Source)
set(TP      ${NB_ROOT}/src/thirdparty)

if(NOT EXISTS "${BAE}/Common/NeoBAE.h")
    message(FATAL_ERROR "Cannot find BAE_Source at ${BAE}. Did you move this directory?")
endif()

# --- Generate the embedded patches header (BAEPatches.h) -------------------
set(PATCHES_HSB    ${NB_ROOT}/src/banks/patches111/patches111.hsb)
set(PATCHES_SCRIPT ${NB_ROOT}/../scripts/create_embedded_patches_h.py)
set(PATCHES_OUT    ${CMAKE_CURRENT_BINARY_DIR}/generated/BAEPatches.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT ${PATCHES_OUT}
    COMMAND ${CMAKE_COMMAND} -E echo "Generating BAEPatches.h from ${PATCHES_HSB}"
    COMMAND python3 ${PATCHES_SCRIPT} ${PATCHES_HSB} ${PATCHES_OUT}
    DEPENDS ${PATCHES_HSB} ${PATCHES_SCRIPT}
    COMMENT "Generating embedded patches header"
    VERBATIM
)
add_custom_target(neobaemc_gen DEPENDS ${PATCHES_OUT})

# --- Source list (mirrors NeoBAEDroid/jni/Android.mk) ----------------------
set(ENGINE_SRC
    ${BAE}/Common/BAE_Override.c
    ${BAE}/Common/DriverTools.c
    ${BAE}/Common/GenAudioStreams.c
    ${BAE}/Common/GenCache.c
    ${BAE}/Common/GenChorus.c
    ${BAE}/Common/GenFiltersReverbU3232.c
    ${BAE}/Common/GenInterp2ReverbU3232.c
    ${BAE}/Common/GenOutput.c
    ${BAE}/Common/GenPatch.c
    ${BAE}/Common/GenReverb.c
    ${BAE}/Common/GenReverbNew.c
    ${BAE}/Common/GenReverbNeo.c
    ${BAE}/Common/GenSample.c
    ${BAE}/Common/GenSeq.c
    ${BAE}/Common/GenSeqTools.c
    ${BAE}/Common/GenBankBalance.c
    ${BAE}/Common/GenSetup.c
    ${BAE}/Common/GenSong.c
    ${BAE}/Common/GenSoundFiles.c
    ${BAE}/Common/GenSynth.c
    ${BAE}/Common/GenSynthFiltersSimple.c
    ${BAE}/Common/GenSynthFiltersU3232.c
    ${BAE}/Common/GenSynthInterp2Simple.c
    ${BAE}/Common/GenSynthInterp2U3232.c
    ${BAE}/Common/GenSF2_FluidLite.c
    ${BAE}/Common/GenRMI.c
    ${BAE}/Common/GenXMF.c
    ${BAE}/Common/GenRingtone.c
    ${BAE}/Common/NeoBAE.c
    ${BAE}/Common/NewNewLZSS.c
    ${BAE}/Common/SampleTools.c
    ${BAE}/Common/X_API.c
    ${BAE}/Common/X_Instruments.c
    ${BAE}/Common/X_EditorTools.c
    ${BAE}/Common/X_Decompress.c
    ${BAE}/Common/X_IMA.c
    ${TP}/adpcm-xq/adpcm-lib.c
    ${TP}/adpcm-xq/adpcm-dns.c
    ${BAE}/Common/X_LZMA.c
    ${BAE}/Common/g711.c
    ${BAE}/Common/g721.c
    ${BAE}/Common/g723_24.c
    ${BAE}/Common/g723_40.c
    ${BAE}/Common/g72x.c
    ${BAE}/Common/sha1mini.c
    ${BAE}/Common/cast5mini.c
    ${BAE}/Common/XFileTypes.c
    ${BAE}/Common/XVorbisFiles.c
    ${BAE}/Common/XOpusFiles.c
    ${BAE}/Common/XQOAFiles.c
    ${BAE}/Common/X_DebugCallback.c
    ${NB_ROOT}/src/script/baescript.c
    ${NB_ROOT}/src/script/baescript_lexer.c
    ${NB_ROOT}/src/script/baescript_parser.c
    ${NB_ROOT}/src/script/baescript_vm.c
    ${NB_ROOT}/src/mthc/mthc_decomp.c
    ${NB_ROOT}/src/adp2wav/adp2wav_decode.c
    ${TP}/libg722/g722_decode.c
    ${NB_ROOT}/src/BAE_MPEG_Source_II/XMPEG_minimp3_wrapper.c
    ${NB_ROOT}/src/BAE_MPEG_Source_II/XMPEGFilesSun.c
    ${BAE}/Platform/BAE_API_Dummy.c
)

# libogg
file(GLOB OGG_SRC      ${TP}/libogg/src/*.c)
# libvorbis (exclude standalone utility programs that have main())
file(GLOB VORBIS_SRC   ${TP}/libvorbis/lib/*.c)
list(FILTER VORBIS_SRC EXCLUDE REGEX ".*/(psytune|barkmel|tone)\\.c$")
# FLAC
file(GLOB FLAC_SRC     ${TP}/flac/src/libFLAC/*.c)
list(FILTER FLAC_SRC EXCLUDE REGEX ".*nasm.*")
# LZMA
file(GLOB LZMA_SRC     ${TP}/lzma-26.00/C/*.c)
# Opus (libopus core + opusfile for high-level decode). Glob covers the
# ~150 .c files across src/, celt/, silk/, silk/float/ and opusfile/src/.
file(GLOB OPUS_CORE_SRC   ${TP}/opus/src/*.c)
file(GLOB OPUS_CELT_SRC   ${TP}/opus/celt/*.c)
file(GLOB OPUS_SILK_SRC   ${TP}/opus/silk/*.c)
file(GLOB OPUS_SILK_F_SRC ${TP}/opus/silk/float/*.c)
file(GLOB OPUSFILE_SRC    ${TP}/opusfile/src/*.c)
# opus has demo / test mains and standalone helpers we don't want
list(FILTER OPUS_CORE_SRC EXCLUDE REGEX ".*/(opus_demo|opus_compare|repacketizer_demo|qext_compare|test_opus_.*)\\.c$")
list(FILTER OPUS_CELT_SRC EXCLUDE REGEX ".*/(opus_custom_demo|mini_kfft)\\.c$")
list(FILTER OPUSFILE_SRC  EXCLUDE REGEX ".*/(http|wincerts)\\.c$")
set(OPUS_SRC ${OPUS_CORE_SRC} ${OPUS_CELT_SRC} ${OPUS_SILK_SRC} ${OPUS_SILK_F_SRC} ${OPUSFILE_SRC})

# --- Build -----------------------------------------------------------------
add_library(NeoBAE SHARED
    neobaemc_jni.c
    ${ENGINE_SRC}
    ${OGG_SRC}
    ${VORBIS_SRC}
    ${FLAC_SRC}
    ${LZMA_SRC}
    ${OPUS_SRC}
)
add_dependencies(NeoBAE neobaemc_gen)

# Find JNI (must be Java 21 for NeoForge 1.21.1)
# When cross-compiling to Windows we can't find a Windows JDK on the Linux host,
# so reuse the host JDK's portable jni.h and supply our own Win32 jni_md.h.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_HOST_WIN32)
    find_path(HOST_JNI_H jni.h
        HINTS $ENV{JAVA_HOME}/include
              /usr/lib/jvm/java-21-openjdk-amd64/include
              /usr/lib/jvm/default-java/include
        NO_CMAKE_FIND_ROOT_PATH
    )
    if(NOT HOST_JNI_H)
        message(FATAL_ERROR "Could not find host JDK jni.h; set JAVA_HOME.")
    endif()
    set(JNI_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/jni-win32   # our Win32 jni_md.h shim, must come first
        ${HOST_JNI_H}
    )
else()
    find_package(JNI REQUIRED)
endif()

target_include_directories(NeoBAE PRIVATE
    ${BAE}/Common
    ${BAE}/Platform
    ${NB_ROOT}/src/script
    ${NB_ROOT}/src/mthc
    ${NB_ROOT}/src/adp2wav
    ${NB_ROOT}/src/BAE_MPEG_Source_II
    ${TP}/config
    ${TP}/minimp3
    ${TP}/libogg/include
    # Opus must come before libvorbis: both ship a top-level mdct.h /
    # analysis.h / info.h and opus's internal sources include them by
    # bare name. (GCC still finds vorbis's via the implicit "source dir
    # first" rule when compiling vorbis files.)
    ${TP}/opus/include
    ${TP}/opus/celt
    ${TP}/opus/silk
    ${TP}/opus/silk/float
    ${TP}/opus/src
    ${TP}/opusfile/include
    ${TP}/opusfile/src
    ${TP}/libvorbis/include
    ${TP}/libvorbis/lib
    ${TP}/flac/include
    ${TP}/flac/src/libFLAC/include
    ${TP}/flac/src
    ${TP}/lzma-26.00/C
    ${TP}/libg722
    ${TP}/qoa
    ${TP}/adpcm-xq
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${JNI_INCLUDE_DIRS}
)

target_compile_definitions(NeoBAE PRIVATE
    X_PLATFORM=X_DUMMY
    _BUILT_IN_PATCHES=1
    HAVE_CONFIG_H=1
    HAVE_STDINT_H=1
    HAVE_INTTYPES_H=1
    SIZE_T_MAX=SIZE_MAX
    USE_FLAC_DECODER=1
    USE_FLAC_ENCODER=1
    USE_VORBIS_DECODER=1
    USE_VORBIS_ENCODER=1
    USE_MPEG_DECODER=1
    USE_OPUS_DECODER=1
    USE_QOA_SUPPORT=1
    OPUS_BUILD=1
    VAR_ARRAYS=1
    USE_SF2_SUPPORT=0
    USE_RMI_SUPPORT=1
    USE_XMF_SUPPORT=0
    USE_ZMF_SUPPORT=1
    USE_LZMA_COMPRESSION=1
    Z7_ST=1
    USE_RETRO_RINGTONE_SUPPORT=1
    BAE_FIX_SPAN_DC=1
    BAE_CLASSIC_CHORUS=1
    USE_WEBTV_MODE=1
    NEOBAE_MC=1
)

if(WIN32)
    target_link_libraries(NeoBAE PRIVATE winmm pthread)
    set_target_properties(NeoBAE PROPERTIES PREFIX "")
    # FLAC's headers default to dllimport on Windows; we link it statically.
    target_compile_definitions(NeoBAE PRIVATE FLAC__NO_DLL OGG_STATIC)
    # FLAC on Windows uses UTF-8 wrappers for fopen/chmod/utime; the source
    # lives outside libFLAC/ and must be added explicitly.
    target_sources(NeoBAE PRIVATE ${TP}/flac/src/share/win_utf8_io/win_utf8_io.c)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(NeoBAE PRIVATE m pthread dl)
endif()
if(APPLE)
    set_target_properties(NeoBAE PROPERTIES SUFFIX ".dylib")
endif()

# --- Install into the gradle-discoverable layout ---------------------------
if(WIN32)
    set(_OS windows)
elseif(APPLE)
    set(_OS macos)
else()
    set(_OS linux)
endif()
set(_ARCH ${CMAKE_SYSTEM_PROCESSOR})
string(TOLOWER "${_ARCH}" _ARCH)
if(_ARCH STREQUAL "amd64" OR _ARCH STREQUAL "x86_64")
    set(_ARCH x86_64)
elseif(_ARCH STREQUAL "aarch64" OR _ARCH STREQUAL "arm64")
    set(_ARCH aarch64)
endif()

set_target_properties(NeoBAE PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/${_OS}/${_ARCH}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/${_OS}/${_ARCH}"
)
