cmake_minimum_required(VERSION 3.10)
project(NeoBAE LANGUAGES C CXX)

set(NEOBAE_RC_ENABLED OFF)
include(CheckLanguage)
check_language(RC)
if(CMAKE_RC_COMPILER)
  enable_language(RC)
  set(NEOBAE_RC_ENABLED ON)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(POLICY CMP0003)
  cmake_policy(SET CMP0003 NEW)
endif()

# Version string generation equivalent to neobae/inc/Makefile.versioning.
set(BAE_VERSION "")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BAE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BAE_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --abbrev-commit --tags --max-count=1
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BAE_GIT_TAG_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  set(BAE_GIT_TAG "")
  if(NOT BAE_GIT_TAG_COMMIT STREQUAL "")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --abbrev=0 --tags ${BAE_GIT_TAG_COMMIT}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE BAE_GIT_TAG
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=format:%Y%m%d
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BAE_GIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

if(BAE_GIT_COMMIT STREQUAL "")
  if(BAE_GIT_DATE STREQUAL "")
    string(TIMESTAMP BAE_GIT_DATE "%Y%m%d")
  endif()
  set(BAE_VERSION "${BAE_GIT_DATE}")
  set(BAE_GIT_DIRTY ON)
else()
  if(NOT BAE_GIT_TAG STREQUAL "")
    if(BAE_GIT_COMMIT STREQUAL BAE_GIT_TAG_COMMIT)
      string(REGEX REPLACE "^v" "" BAE_VERSION "${BAE_GIT_TAG}")
    else()
      set(BAE_VERSION "git-${BAE_GIT_COMMIT}")
    endif()
  else()
    set(BAE_VERSION "git-${BAE_GIT_COMMIT}")
  endif()
  if(NOT BAE_GIT_STATUS STREQUAL "")
    set(BAE_VERSION "${BAE_VERSION}-dirty")
    set(BAE_GIT_DIRTY ON)
  endif()
endif()

add_compile_definitions(_VERSION="${BAE_VERSION}")
message(STATUS "NeoBAE build version: ${BAE_VERSION}")

string(TIMESTAMP BAE_BUILD_YEAR "%Y")
string(TIMESTAMP _month "%m")
string(TIMESTAMP _day "%d")

math(EXPR BAE_BUILD_MONTH "${_month}")
math(EXPR BAE_BUILD_DAY "${_day}")

if((MINGW OR MSVC) AND NEOBAE_RC_ENABLED)
  set(VERSION_MAJOR 2)
  set(VERSION_MINOR "${BAE_BUILD_YEAR}")
  set(VERSION_PATCH "${BAE_BUILD_MONTH}")
  set(VERSION_TWEAK "${BAE_BUILD_DAY}")
  set(PROJECT_VERSION "${BAE_VERSION}")
  set(PROJECT_COMPANY "zefie")
  set(FILE_TYPE "1")
  set(GIT_COMMIT "${BAE_GIT_COMMIT}")
  set(GIT_BRANCH "${BAE_GIT_BRANCH}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE BAE_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  set(VERSION_RC_IN "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/version.rc.in")
  set(VERSION_ICO_RC_IN "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/version_ico.rc.in")
  set(VERSION_ICO_WX_RC_IN "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/version_ico_wx.rc.in")
  set(VERSION_ZEFIDI_RC_IN "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/version_zefidi.rc.in")


  function(neobae_configure_rc TARGET OUTPUT_VAR IN_FILE PROJECT_DESCRIPTION ORIGINAL_FILENAME FILETYPE PROJECT_ICON)
    set(ICONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/neobae/icons")

    set(OUT_RC "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-version.rc")

    configure_file("${IN_FILE}" "${OUT_RC}" @ONLY)

    set(${OUTPUT_VAR} "${OUT_RC}" PARENT_SCOPE)
  endfunction()
endif()

option(DEBUG "Enable debuggable build (zefidi debug console, gdb debugging)" OFF)
option(NEOBAE_STATIC "Enable static linking for NeoBAE apps and dependencies (best support on MinGW)" OFF)
option(NEOBAE_BUILD_VCLIB "Build an additional MSVC-style import library for libneobae.dll using dlltool" OFF)
option(NEOBAE_EXTERNAL_CODECS "Use external system libraries for codecs when available instead of bundled sources (FLAC, LAME, Vorbis, Opus, Ogg)" OFF)
if(NEOBAE_STATIC AND NOT DEFINED CACHE{NEOBAE_EXTERNAL_CODECS})
  set(NEOBAE_EXTERNAL_CODECS ON CACHE BOOL "Use external system libraries for codecs when available instead of bundled sources (FLAC, LAME, Vorbis, Opus, Ogg)" FORCE)
endif()
if(NEOBAE_STATIC AND NOT DEFINED CACHE{NEOBAE_SHARED_LIBNEOBAE})
  set(NEOBAE_SHARED_LIBNEOBAE ON CACHE BOOL "Build libneobae as shared (DLL/.so) while keeping static dependency preference" FORCE)
endif()

if (NOT DEBUG AND NOT LDEBUG)
  # Do not force -O2 into every config. On Visual Studio (multi-config), Debug
  # already gets /RTC1 from CMake, which is incompatible with /O2 (cl D8016).
  get_property(_neobae_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_neobae_is_multi_config)
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-O2>)
  else()
    add_compile_options(-O2)
  endif()
else()
  message(STATUS "Debug build: enabling debug symbols and disabling optimizations")
  if (LDEBUG)
    set(DEBUG ON)
    add_compile_options(-g -O0 -ggdb3 -fPIC -D_DEBUG=1 -D_FULL_DEBUG=1)
  else()
    add_compile_options(-D_DEBUG=1)
  endif()
endif()

if(NEOBAE_STATIC)
  message(STATUS "NEOBAE_STATIC enabled: preferring static linkage for apps and dependencies")
  if(MINGW)
    # Prefer static archives first when resolving libraries in MinGW static mode.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    # Prevent executable import libraries like libzefidi.dll.a in static app builds.
    set(CMAKE_EXECUTABLE_ENABLE_EXPORTS OFF)
    if(NEOBAE_SHARED_LIBNEOBAE)
      # Keep static runtime libs but avoid global -static so shared libneobae can link.
      add_link_options(-static-libgcc $<$<LINK_LANGUAGE:CXX>:-static-libstdc++>)
    else()
      add_link_options(-static -static-libgcc $<$<LINK_LANGUAGE:CXX>:-static-libstdc++>)
    endif()
  endif()
endif()


option(BAE_DISABLE_RMF_EDITOR "Disable BAERmfEditor (Creation API) support" OFF)
option(BAE_DISABLE_SF2_CONVERTER "Disable SF2->HSB converter support" OFF)
option(BAE_DISABLE_ZMF_SUPPORT "Disable ZMF support" OFF)
option(BAE_DISABLE_LZMA "Disable LZMA compression support" OFF)
option(BAE_DISABLE_MP3_ENCODER "Disable MP3 encoder support" OFF)
option(BAE_DISABLE_MP3_DECODER "Disable MP3 decoder support" OFF)
option(BAE_DISABLE_FLAC_ENCODER "Disable FLAC encoder support" OFF)
option(BAE_DISABLE_FLAC_DECODER "Disable FLAC decoder support" OFF)
option(BAE_DISABLE_VORBIS_ENCODER "Disable Vorbis encoder support" OFF)
option(BAE_DISABLE_VORBIS_DECODER "Disable Vorbis decoder support" OFF)
option(BAE_DISABLE_OPUS_ENCODER "Disable Opus encoder support" OFF)
option(BAE_DISABLE_OPUS_DECODER "Disable Opus decoder support" OFF)
option(BAE_DISABLE_KARAOKE "Disable karaoke support" OFF)
option(BAE_DISABLE_BAESCRIPT "Disable BAEScript support" OFF)
option(BAE_DISABLE_PLAYLIST "Disable playlist support" OFF)
option(BAE_DISABLE_SF2_SUPPORT "Disable SF2 support" OFF)
option(BAE_DISABLE_FLUIDLITE "Disable FluidLite integration" OFF)
option(BAE_DISABLE_NATIVE_DLS "Disable native DLS system" OFF)
option(BAE_DISABLE_XMF_SUPPORT "Disable XMF support" OFF)
option(BAE_DISABLE_NOKIA_PATCH "Disable Nokia patch handling" OFF)
option(BAE_DISABLE_BEATNIK_SF2_NRPN "Disable Beatnik SF2 NRPN behavior" OFF)
option(BAE_DISABLE_BUILTIN_PATCHES_WITH_SF2 "Disable loading built-in patches for SF2" OFF)
option(BAE_DISABLE_BUILTIN_PATCHES_WITH_DLS "Disable loading built-in patches for DLS" OFF)
option(BAE_DISABLE_FIX_SPAN_DC "Disable span DC fix" OFF)
option(BAE_DISABLE_CLASSIC_CHORUS "Disable classic chorus path" OFF)
option(BAE_DISABLE_MTHC_SUPPORT "Disable MTHC support" OFF)
option(BAE_DISABLE_ADP_SUPPORT "Disable ADP support" OFF)
option(BAE_DISABLE_CREATION_API "Disable Creation API support (alias for RMF editor APIs)" OFF)
option(BAE_DISABLE_EMBED_PATCHES "Disable embedded built-in patches" OFF)
option(BAE_DISABLE_EMBED_FONT "Disable embedded GUI font" OFF)
option(BAE_DISABLE_RETRO_RINGTONE_SUPPORT "Disable retro mobile ringtone format support (IMY/EMY/RNG/RTX/RTTTL)" OFF)
option(BAE_DISABLE_QOA_SUPPORT "Disable QOA codec support" OFF)
option(BAE_DISABLE_WMA_SUPPORT "Disable WMA (MSAUDIO) codec support" OFF)
option(BAE_DISABLE_RMI_SUPPORT "Disable RMI support" OFF)
option(BAE_DISABLE_J2ME_PATCH "Disable J2ME percussion bank patch (maps bank 120 for percussion intent)" OFF)
option(BAE_DISABLE_ROLLED_MIDI_UNROLLING "Disable unrolling of rolled MIDI files" OFF)
option(BAE_DISABLE_MIDI_HARDWARE "Disable hardware MIDI I/O (zefidi / nbeditor) via RtMidi" OFF)
set(BAE_EMBED_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/banks/patches111/patches111.hsb" CACHE FILEPATH "HSB bank file to embed as built-in patches (empty disables embedding)")
set(BAE_EMBED_TTF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/fonts/LiberationSans-Regular.ttf" CACHE FILEPATH "TTF font file to embed for zefidi GUI use (Liberation Sans; empty disables embedding)")
set(BAE_NBEDITOR_FONT_REGULAR "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/fonts/LiberationSans-Regular.ttf" CACHE FILEPATH "Regular TTF embedded for nbeditor")
set(BAE_NBEDITOR_FONT_ITALIC "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/fonts/LiberationSans-Italic.ttf" CACHE FILEPATH "Italic TTF embedded for nbeditor (aliases)")
option(USE_SDL2 "Prefer SDL2 as the platform backend (default if SDL2 is found and SDL3 is not found)" OFF)
option(BUILD_PLAYBAE "Build playbae when a non-Dummy/non-Ansi backend is selected" ON)
option(BUILD_ZEFIDI "Build zefidi when SDL2/SDL3 backend and matching SDL_ttf are available" ON)
option(BUILD_CLITOOLS "Build CLI tools (songtool, rmfutil, rmiutil, xmfutil, sf2-hsb, mod2rmf, bankrecomp, ringtone2mid, …)" ON)
option(BUILD_NBEDITOR "Build NeoBAE Editor (Dear ImGui + SDL3 or SDL2); requires BAE_ENABLE_RMF_EDITOR" ON)
option(BUILD_DEBUG_INFO "Emit debug symbols / PDBs for executables and libneobae" OFF)
option(BAE_DISABLE_LIB_HYPHEN "nbeditor: syllable Apply-text via vendored libhyphen" OFF)
set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)

# BUILD_DEBUG_INFO is independent of LDEBUG: keep existing -O2 (or LDEBUG's -O0)
# and only add symbols / PDBs so crash backtraces can resolve.
if(BUILD_DEBUG_INFO)
  add_compile_definitions(BUILD_DEBUG_INFO=1)
  if(MSVC)
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
    message(STATUS "BUILD_DEBUG_INFO: MSVC /Zi /DEBUG (PDBs next to exes/DLLs)")
  elseif(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-g -gcodeview -fno-omit-frame-pointer)
    message(STATUS "BUILD_DEBUG_INFO: CodeView + PDBs (llvm-mingw/Clang)")
  else()
    add_compile_options(-g -fno-omit-frame-pointer)
    if(WIN32)
      message(STATUS "BUILD_DEBUG_INFO: DWARF symbols (PDB requires MSVC or llvm-mingw)")
    else()
      message(STATUS "BUILD_DEBUG_INFO: DWARF symbols")
    endif()
  endif()
endif()

# Internal compatibility booleans: keep existing logic in terms of ENABLE vars.
set(BAE_ENABLE_RMF_EDITOR ON)
set(BAE_ENABLE_SF2_CONVERTER ON)
set(BAE_ENABLE_ZMF_SUPPORT ON)
set(BAE_ENABLE_LZMA ON)
set(BAE_ENABLE_MP3_ENCODER ON)
set(BAE_ENABLE_MP3_DECODER ON)
set(BAE_ENABLE_FLAC_ENCODER ON)
set(BAE_ENABLE_FLAC_DECODER ON)
set(BAE_ENABLE_VORBIS_ENCODER ON)
set(BAE_ENABLE_VORBIS_DECODER ON)
set(BAE_ENABLE_OPUS_ENCODER ON)
set(BAE_ENABLE_OPUS_DECODER ON)
set(BAE_ENABLE_KARAOKE ON)
set(BAE_ENABLE_BAESCRIPT ON)
set(BAE_ENABLE_PLAYLIST ON)
set(BAE_ENABLE_SF2_SUPPORT ON)
set(BAE_ENABLE_FLUIDLITE ON)
set(BAE_ENABLE_NATIVE_DLS ON)
set(BAE_ENABLE_XMF_SUPPORT ON)
set(BAE_ENABLE_NOKIA_PATCH ON)
set(BAE_ENABLE_BEATNIK_SF2_NRPN ON)
set(BAE_ENABLE_BUILTIN_PATCHES_WITH_SF2 ON)
set(BAE_ENABLE_BUILTIN_PATCHES_WITH_DLS ON)
set(BAE_ENABLE_FIX_SPAN_DC ON)
set(BAE_ENABLE_CLASSIC_CHORUS ON)
set(BAE_ENABLE_MTHC_SUPPORT ON)
set(BAE_ENABLE_ADP_SUPPORT ON)
set(BAE_ENABLE_EMBED_PATCHES ON)
set(BAE_ENABLE_EMBED_FONT ON)
set(BAE_ENABLE_RETRO_RINGTONE_SUPPORT ON)
set(BAE_ENABLE_QOA_SUPPORT ON)
set(BAE_ENABLE_WMA_SUPPORT ON)
set(BAE_ENABLE_RMI_SUPPORT ON)
set(BAE_ENABLE_J2ME_PATCH ON)
set(BAE_ENABLE_ROLLED_MIDI_UNROLL ON)
set(BAE_ENABLE_ADX_SUPPORT ON)
set(BAE_ENABLE_MIDI_HARDWARE ON)
set(BAE_ENABLE_LIB_HYPHEN ON)
set(BAE_USING_SDL2 OFF)

if(BAE_DISABLE_LIB_HYPHEN)
  set(BAE_ENABLE_LIB_HYPHEN OFF)
  message(STATUS "Disabling hyphen support in NeoBAE Editor")
else()
  message(STATUS "Enabling hyphen support in NeoBAE Editor")
endif()

if(BAE_DISABLE_J2ME_PATCH)
  set(BAE_ENABLE_J2ME_PATCH OFF)
  message(STATUS "Disabling J2ME percussion bank patch (maps bank 120 for percussion intent)")
else()
  message(STATUS "Enabling J2ME percussion bank patch (maps bank 120 for percussion intent)")
endif()

if(BAE_DISABLE_RMF_EDITOR)
  set(BAE_ENABLE_RMF_EDITOR OFF)
  message(STATUS "Disabling RMF/ZMF Creation API Support")
else()
  message(STATUS "Enabling RMF/ZMF Creation API Support")
endif()

if(BAE_DISABLE_SF2_CONVERTER)
  set(BAE_ENABLE_SF2_CONVERTER OFF)
  message(STATUS "Disabling SF2->HSB Converter Support")
else()
  message(STATUS "Enabling SF2->HSB Converter Support")
endif()

if(BAE_DISABLE_ZMF_SUPPORT)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF Support")
else()
  message(STATUS "Enabling ZMF Support")    
endif()

if(BAE_DISABLE_LZMA)
  set(BAE_ENABLE_LZMA OFF)
  message(STATUS "Disabling LZMA Support")
else()
  message(STATUS "Enabling LZMA Support")
endif()

if(BAE_DISABLE_MP3_ENCODER)
  set(BAE_ENABLE_MP3_ENCODER OFF)
  message(STATUS "Disabling MP3 encoder support")
endif()

if(BAE_DISABLE_MP3_DECODER)
  set(BAE_ENABLE_MP3_DECODER OFF)
  message(STATUS "Disabling MP3 decoder support")
endif()

if(BAE_DISABLE_FLAC_ENCODER)
  set(BAE_ENABLE_FLAC_ENCODER OFF)
  message(STATUS "Disabling FLAC encoder support")
endif()

if(BAE_DISABLE_FLAC_DECODER)
  set(BAE_ENABLE_FLAC_DECODER OFF)
  message(STATUS "Disabling FLAC decoder support")
endif()

if(BAE_DISABLE_VORBIS_ENCODER)
  set(BAE_ENABLE_VORBIS_ENCODER OFF)
  message(STATUS "Disabling Vorbis encoder support")
endif()

if(BAE_DISABLE_VORBIS_DECODER)
  set(BAE_ENABLE_VORBIS_DECODER OFF)
  message(STATUS "Disabling Vorbis decoder support")
endif()

if(BAE_DISABLE_OPUS_ENCODER)
  set(BAE_ENABLE_OPUS_ENCODER OFF)
  message(STATUS "Disabling Opus encoder support")
endif()

if(BAE_DISABLE_OPUS_DECODER)
  set(BAE_ENABLE_OPUS_DECODER OFF)
  message(STATUS "Disabling Opus decoder support")
endif()

if(BAE_DISABLE_RETRO_RINGTONE_SUPPORT)
  set(BAE_ENABLE_RETRO_RINGTONE_SUPPORT OFF)
  message(STATUS "Disabling retro mobile ringtone format support (IMY/EMY/RNG/RTX/RTTTL)")
else()
  message(STATUS "Enabling retro mobile ringtone format support (IMY/EMY/RNG/RTX/RTTTL)")
endif()

if (BAE_DISABLE_QOA_SUPPORT)
  set(BAE_ENABLE_QOA_SUPPORT OFF)
  message(STATUS "Disabling QOA codec support")
else()
  message(STATUS "Enabling QOA codec support")
endif()

if (BAE_DISABLE_WMA_SUPPORT)
  set(BAE_ENABLE_WMA_SUPPORT OFF)
  message(STATUS "Disabling WMA (MSAUDIO) codec support")
else()
  message(STATUS "Enabling WMA (MSAUDIO) codec support")
endif()

if(BAE_DISABLE_KARAOKE)
  set(BAE_ENABLE_KARAOKE OFF)
endif()

if(BAE_DISABLE_BAESCRIPT)
  set(BAE_ENABLE_BAESCRIPT OFF)
endif()

if(BAE_DISABLE_PLAYLIST)
  set(BAE_ENABLE_PLAYLIST OFF)
endif()

if(BAE_DISABLE_SF2_SUPPORT)
  set(BAE_ENABLE_SF2_SUPPORT OFF)
endif()

if(BAE_DISABLE_FLUIDLITE)
  set(BAE_ENABLE_FLUIDLITE OFF)
  message(STATUS "Disabling FluidLite")
else()
  message(STATUS "Enabling FluidLite")
endif()

if (BAE_DISABLE_NATIVE_DLS)
  set(BAE_ENABLE_NATIVE_DLS OFF)
  message(STATUS "Disabling Native DLS System")
else()
  message(STATUS "Enabling Native DLS System")
endif()

if(BAE_DISABLE_XMF_SUPPORT)
  set(BAE_ENABLE_XMF_SUPPORT OFF)
endif()

if(BAE_DISABLE_NOKIA_PATCH)
  set(BAE_ENABLE_NOKIA_PATCH OFF)
endif()

if(BAE_DISABLE_BEATNIK_SF2_NRPN)
  set(BAE_ENABLE_BEATNIK_SF2_NRPN OFF)
endif()

if(BAE_DISABLE_BUILTIN_PATCHES_WITH_SF2)
  set(BAE_ENABLE_BUILTIN_PATCHES_WITH_SF2 OFF)
endif()

if(BAE_DISABLE_BUILTIN_PATCHES_WITH_DLS)
  set(BAE_ENABLE_BUILTIN_PATCHES_WITH_DLS OFF)
endif()

if(BAE_DISABLE_FIX_SPAN_DC)
  set(BAE_ENABLE_FIX_SPAN_DC OFF)
endif()

if(BAE_DISABLE_CLASSIC_CHORUS)
  set(BAE_ENABLE_CLASSIC_CHORUS OFF)
endif()

if(BAE_DISABLE_MTHC_SUPPORT)
  set(BAE_ENABLE_MTHC_SUPPORT OFF)
endif()

if(BAE_DISABLE_ADP_SUPPORT)
  set(BAE_ENABLE_ADP_SUPPORT OFF)
endif()

if(BAE_DISABLE_CREATION_API)
  set(BAE_ENABLE_RMF_EDITOR OFF)
endif()

# nbeditor needs the BAE Editor / Creation API (BAE_ENABLE_RMF_EDITOR).
if(BUILD_NBEDITOR AND NOT BAE_ENABLE_RMF_EDITOR)
  set(BUILD_NBEDITOR OFF)
  message(STATUS "Disabling BUILD_NBEDITOR (requires BAE_ENABLE_RMF_EDITOR; disabled via BAE_DISABLE_RMF_EDITOR or BAE_DISABLE_CREATION_API)")
endif()

if(BAE_DISABLE_EMBED_PATCHES)
  set(BAE_ENABLE_EMBED_PATCHES OFF)
endif()

if(BAE_DISABLE_EMBED_FONT)
  set(BAE_ENABLE_EMBED_FONT OFF)
endif()

if(BAE_DISABLE_RMI_SUPPORT)
  set(BAE_ENABLE_RMI_SUPPORT OFF)
endif()

if(BAE_DISABLE_ROLLED_MIDI_UNROLL)
  set(BAE_ENABLE_ROLLED_MIDI_UNROLL OFF)
endif()

if(BAE_DISABLE_ADX_SUPPORT)
  set(BAE_ENABLE_ADX_SUPPORT OFF)
  message(STATUS "Disabling ADX support")
else()
  message(STATUS "Enabling ADX support")
endif()

# RtMidi hardware I/O for zefidi and/or nbeditor (shared input helper).
set(BAE_ENABLE_MIDI_HARDWARE OFF)
set(BAE_MIDI_HW_SOURCES_OK OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/rtmidi/rtmidi_c.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/rtmidi/rtmidi_c.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/rtmidi/RtMidi.cpp")
  set(BAE_MIDI_HW_SOURCES_OK ON)
endif()

if(BAE_DISABLE_MIDI_HARDWARE)
  message(STATUS "Disabling hardware MIDI I/O via RtMidi (BAE_DISABLE_MIDI_HARDWARE)")
elseif(NOT BUILD_ZEFIDI AND NOT BUILD_NBEDITOR)
  message(STATUS "Disabling hardware MIDI I/O via RtMidi (no zefidi/nbeditor)")
elseif(NOT BAE_MIDI_HW_SOURCES_OK)
  message(STATUS "Disabling hardware MIDI I/O: RtMidi sources missing")
else()
  if(WIN32 OR APPLE)
    set(BAE_ENABLE_MIDI_HARDWARE ON)
  elseif(UNIX)
    find_library(BAE_ALSA_LIBRARY NAMES asound)
    if(BAE_ALSA_LIBRARY)
      set(BAE_ENABLE_MIDI_HARDWARE ON)
    else()
      message(STATUS "Disabling hardware MIDI I/O: ALSA library not found")
    endif()
  endif()
  if(BAE_ENABLE_MIDI_HARDWARE)
    message(STATUS "Enabling hardware MIDI I/O via RtMidi (zefidi/nbeditor)")
  endif()
endif()

set(BAE_EMBED_PATCHES_EFFECTIVE OFF)
if(BAE_ENABLE_EMBED_PATCHES)
  if(BAE_EMBED_PATCH_FILE STREQUAL "")
    message(STATUS "Disabling built-in embedded patches: BAE_EMBED_PATCH_FILE is empty")
  elseif(NOT EXISTS "${BAE_EMBED_PATCH_FILE}")
    message(STATUS "Disabling built-in embedded patches: bank file not found at '${BAE_EMBED_PATCH_FILE}'")
  else()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
      message(STATUS "Disabling built-in embedded patches: Python3 interpreter not found")
    else()
      set(BAE_EMBED_PATCHES_EFFECTIVE ON)
      message(STATUS "Enabling built-in embedded patches from '${BAE_EMBED_PATCH_FILE}'")
    endif()
  endif()
endif()

if(BAE_ENABLE_SF2_SUPPORT AND NOT BAE_ENABLE_FLUIDLITE)
  # SF2 is only supported via FluidLite at this moment
  set(BAE_ENABLE_SF2_SUPPORT OFF)
  message(STATUS "Disabling SF2 support: FluidLite is disabled")
endif()

if(BAE_ENABLE_FLUIDLITE AND NOT BAE_ENABLE_SF2_SUPPORT)
  # Due to the codebase, these both need to be on
  set(BAE_ENABLE_SF2_SUPPORT ON)
  message(STATUS "Enabling SF2 support: FluidLite was not disabled. (Try -DBAE_DISABLE_FLUIDLITE=1?)")
endif()

if(BAE_ENABLE_XMF_SUPPORT AND NOT BAE_ENABLE_NATIVE_DLS)
  set(BAE_ENABLE_XMF_SUPPORT OFF)
  message(STATUS "Disabling XMF support: Native DLS is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_LZMA)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: LZMA is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_VORBIS_DECODER)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: Vorbis is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_OPUS_DECODER)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: Opus is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_FLAC_DECODER)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: FLAC is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_QOA_SUPPORT)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: QOA is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_CLASSIC_CHORUS)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: Classic Chorus is disabled")
endif()

if (BAE_ENABLE_ZMF_SUPPORT AND NOT BAE_ENABLE_FIX_SPAN_DC)
  set(BAE_ENABLE_ZMF_SUPPORT OFF)
  message(STATUS "Disabling ZMF support: PAN Fix is disabled")
endif()

# External codec dependency probes (system libs).
set(BAE_LAME_WRAPPER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_MPEG_Source_II/XMPEG_lame_encoder.cpp")
set(BAE_LAME_WRAPPER_AVAILABLE OFF)
if(EXISTS "${BAE_LAME_WRAPPER_SOURCE}")
  set(BAE_LAME_WRAPPER_AVAILABLE ON)
endif()

if(NEOBAE_EXTERNAL_CODECS)
  # Cross toolchains (e.g. llvm-mingw) keep target libs in <toolchain>/<triple>/
  # {include,lib}, next to the compiler's bin/ directory. Add that sysroot to the
  # search prefixes so both it and the host system paths are considered.
  set(_bae_ext_prefixes "")
  if(CMAKE_FIND_ROOT_PATH)
    list(APPEND _bae_ext_prefixes ${CMAKE_FIND_ROOT_PATH})
  endif()
  set(_bae_triple "${CMAKE_C_COMPILER_TARGET}")
  if(NOT _bae_triple AND CMAKE_C_COMPILER)
    get_filename_component(_bae_cc_name "${CMAKE_C_COMPILER}" NAME)
    if(_bae_cc_name MATCHES "^(.+-(mingw32|mingw32uwp|linux-gnu[a-z0-9]*))-")
      set(_bae_triple "${CMAKE_MATCH_1}")
    endif()
  endif()
  if(CMAKE_C_COMPILER)
    get_filename_component(_bae_cc_real "${CMAKE_C_COMPILER}" REALPATH)
    get_filename_component(_bae_cc_bin "${_bae_cc_real}" DIRECTORY)
    get_filename_component(_bae_cc_root "${_bae_cc_bin}" DIRECTORY)
    if(_bae_triple AND IS_DIRECTORY "${_bae_cc_root}/${_bae_triple}")
      list(APPEND _bae_ext_prefixes "${_bae_cc_root}/${_bae_triple}")
    endif()
  endif()
  list(REMOVE_DUPLICATES _bae_ext_prefixes)
  foreach(_pfx ${_bae_ext_prefixes})
    if(IS_DIRECTORY "${_pfx}")
      list(APPEND CMAKE_PREFIX_PATH "${_pfx}")
      list(APPEND CMAKE_LIBRARY_PATH "${_pfx}/lib")
      set(ENV{PKG_CONFIG_PATH} "${_pfx}/lib/pkgconfig:${_pfx}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
  endforeach()
  if(_bae_ext_prefixes)
    message(STATUS "External codec search prefixes: ${_bae_ext_prefixes} (plus system paths)")
  endif()
endif()

find_path(BAE_LAME_INCLUDE_DIR lame/lame.h)
find_library(BAE_LAME_LIBRARY NAMES mp3lame lame)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LAME QUIET libmp3lame)
  if(NOT BAE_LAME_INCLUDE_DIR AND PC_LAME_FOUND)
    set(BAE_LAME_INCLUDE_DIR ${PC_LAME_INCLUDE_DIRS})
  endif()
  if(NOT BAE_LAME_LIBRARY AND PC_LAME_FOUND)
    find_library(BAE_LAME_LIBRARY NAMES mp3lame lame HINTS ${PC_LAME_LIBRARY_DIRS})
  endif()
endif()

set(BAE_MP3_ENCODER_AVAILABLE OFF)
if(BAE_LAME_WRAPPER_AVAILABLE AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lame-3.100-slim/include/lame.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lame-3.100-slim/libmp3lame/lame.c")
  set(BAE_MP3_ENCODER_AVAILABLE ON)
endif()

set(BAE_FLAC_AVAILABLE OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/flac/include/FLAC/all.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/flac/src/libFLAC/stream_decoder.c")
  set(BAE_FLAC_AVAILABLE ON)
endif()

find_path(BAE_VORBIS_INCLUDE_DIR vorbis/codec.h)
find_library(BAE_VORBIS_LIBRARY NAMES vorbis)
find_library(BAE_VORBISFILE_LIBRARY NAMES vorbisfile)
find_library(BAE_VORBISENC_LIBRARY NAMES vorbisenc)
set(BAE_VORBIS_DECODER_AVAILABLE OFF)
set(BAE_VORBIS_ENCODER_AVAILABLE OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libvorbis/lib/info.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libvorbis/lib/vorbisfile.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libogg/src/framing.c")
  set(BAE_VORBIS_DECODER_AVAILABLE ON)
  set(BAE_VORBIS_ENCODER_AVAILABLE ON)
endif()

set(BAE_OPUS_ENCODER_AVAILABLE OFF)
set(BAE_OPUS_DECODER_AVAILABLE OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/include/opus.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/src/opus.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/celt/celt_encoder.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/silk/enc_API.c")
  set(BAE_OPUS_ENCODER_AVAILABLE ON)
endif()
if(BAE_OPUS_ENCODER_AVAILABLE AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opusfile/include/opusfile.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opusfile/src/opusfile.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opusfile/src/info.c")
  set(BAE_OPUS_DECODER_AVAILABLE ON)
endif()

find_path(BAE_OGG_INCLUDE_DIR ogg/ogg.h)
find_library(BAE_OGG_LIBRARY NAMES ogg)

# System codec header lookup (only used when NEOBAE_EXTERNAL_CODECS is ON).
# Sources include <opus.h>, <opusfile.h>, <lame.h> directly, so the directory
# containing those headers (e.g. /usr/include/opus) must be on the include path.
set(BAE_EXTERNAL_CODEC_INCLUDE_DIRS "")
if(NEOBAE_EXTERNAL_CODECS)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_FLAC QUIET flac)
    pkg_check_modules(PC_OPUS QUIET opus)
    pkg_check_modules(PC_OPUSFILE QUIET opusfile)
  endif()
  find_path(BAE_FLAC_INCLUDE_DIR FLAC/stream_decoder.h HINTS ${PC_FLAC_INCLUDE_DIRS})
  find_path(BAE_OPUS_INCLUDE_DIR NAMES opus.h PATH_SUFFIXES opus HINTS ${PC_OPUS_INCLUDE_DIRS})
  find_path(BAE_OPUSFILE_INCLUDE_DIR NAMES opusfile.h PATH_SUFFIXES opus HINTS ${PC_OPUSFILE_INCLUDE_DIRS})
  find_path(BAE_LAME_HEADER_DIR NAMES lame.h PATH_SUFFIXES lame HINTS ${PC_LAME_INCLUDE_DIRS})
  foreach(_d BAE_FLAC_INCLUDE_DIR BAE_VORBIS_INCLUDE_DIR BAE_OGG_INCLUDE_DIR BAE_OPUS_INCLUDE_DIR BAE_OPUSFILE_INCLUDE_DIR BAE_LAME_HEADER_DIR)
    if(${_d})
      list(APPEND BAE_EXTERNAL_CODEC_INCLUDE_DIRS ${${_d}})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES BAE_EXTERNAL_CODEC_INCLUDE_DIRS)
  message(STATUS "External codec include dirs: ${BAE_EXTERNAL_CODEC_INCLUDE_DIRS}")
endif()
set(BAE_OGG_AVAILABLE OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libogg/src/framing.c" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libogg/src/bitwise.c")
  set(BAE_OGG_AVAILABLE ON)
endif()

if(NEOBAE_EXTERNAL_CODECS)
  set(BAE_CODEC_SRC_MODE "external system libraries")
else()
  set(BAE_CODEC_SRC_MODE "bundled sources")
endif()
# Auto-disable codec toggles when required external dependencies are missing.
if(BAE_ENABLE_FLAC_DECODER AND NOT BAE_FLAC_AVAILABLE)
  set(BAE_ENABLE_FLAC_DECODER OFF)
  message(STATUS "Disabling FLAC decoder: bundled libFLAC sources not found (did you forget to submodule update --init --recursive?)")
else()
  if(BAE_ENABLE_FLAC_DECODER)
    message(STATUS "Enabling FLAC decoder: using ${BAE_CODEC_SRC_MODE} (libFLAC)")
  endif()
endif()
if(BAE_ENABLE_MP3_ENCODER AND NOT BAE_MP3_ENCODER_AVAILABLE)
  set(BAE_ENABLE_MP3_ENCODER OFF)
  message(STATUS "Disabling MP3 encoder: bundled LAME sources not found")
else()
  if(BAE_ENABLE_MP3_ENCODER)
    message(STATUS "Enabling MP3 encoder: using ${BAE_CODEC_SRC_MODE} (LAME)")
  endif()
endif()
if(BAE_ENABLE_FLAC_ENCODER AND NOT BAE_FLAC_AVAILABLE)
  set(BAE_ENABLE_FLAC_ENCODER OFF)
  message(STATUS "Disabling FLAC encoder: bundled libFLAC sources not found (did you forget to submodule update --init --recursive?)")
else()
  if(BAE_ENABLE_FLAC_ENCODER)
    message(STATUS "Enabling FLAC encoder: using ${BAE_CODEC_SRC_MODE} (libFLAC)")
  endif()
endif()

if(BAE_ENABLE_VORBIS_DECODER AND (NOT BAE_VORBIS_DECODER_AVAILABLE OR NOT BAE_OGG_AVAILABLE))
  set(BAE_ENABLE_VORBIS_DECODER OFF)
  message(STATUS "Disabling Vorbis decoder: bundled Vorbis/Ogg sources not found (did you forget to submodule update --init --recursive?)")
else()
  if(BAE_ENABLE_VORBIS_DECODER)
    message(STATUS "Enabling Vorbis decoder: using ${BAE_CODEC_SRC_MODE} (Vorbis/Ogg)")
  endif()
endif()
if(BAE_ENABLE_VORBIS_ENCODER AND (NOT BAE_VORBIS_ENCODER_AVAILABLE OR NOT BAE_OGG_AVAILABLE))
  set(BAE_ENABLE_VORBIS_ENCODER OFF)
  message(STATUS "Disabling Vorbis encoder: bundled Vorbis/Ogg sources not found (did you forget to submodule update --init --recursive?)")
else()
  if(BAE_ENABLE_VORBIS_ENCODER)
    message(STATUS "Enabling Vorbis encoder: using ${BAE_CODEC_SRC_MODE} (Vorbis/Ogg)")
  endif()
endif()

if(BAE_ENABLE_OPUS_DECODER AND (NOT BAE_OPUS_DECODER_AVAILABLE OR NOT BAE_OGG_AVAILABLE))
  set(BAE_ENABLE_OPUS_DECODER OFF)
  message(STATUS "Disabling Opus decoder: bundled Opus/Opusfile/Ogg sources not found (did you forget to submodule update --init --recursive?)")
else()
  if(BAE_ENABLE_OPUS_DECODER)
    message(STATUS "Enabling Opus decoder: using ${BAE_CODEC_SRC_MODE} (Opus/Opusfile/Ogg)")
  endif()
endif()
if(BAE_ENABLE_OPUS_ENCODER AND (NOT BAE_OPUS_ENCODER_AVAILABLE OR NOT BAE_OGG_AVAILABLE))
  set(BAE_ENABLE_OPUS_ENCODER OFF)
  message(STATUS "Disabling Opus encoder: bundled Opus/Ogg sources not found")
else()
  if(BAE_ENABLE_OPUS_ENCODER)
    message(STATUS "Enabling Opus encoder: using ${BAE_CODEC_SRC_MODE} (Opus/Ogg)")
  endif()
endif()

find_path(BAE_SDL3_INCLUDE_DIR SDL3/SDL.h)
find_library(BAE_SDL3_LIBRARY NAMES SDL3)
# Cross toolchains (llvm-mingw) often set FIND_ROOT_PATH_MODE_*=ONLY without a
# usable FIND_ROOT_PATH, so find_* misses sysroot SDL. Fall back to pkg-config
# when PKG_CONFIG_LIBDIR points at the target (docker mingw scripts do this).
if(NOT BAE_SDL3_INCLUDE_DIR OR NOT BAE_SDL3_LIBRARY)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_SDL3 QUIET sdl3)
    if(PC_SDL3_FOUND)
      if(NOT BAE_SDL3_INCLUDE_DIR AND PC_SDL3_INCLUDEDIR)
        set(BAE_SDL3_INCLUDE_DIR "${PC_SDL3_INCLUDEDIR}" CACHE PATH "SDL3 include dir" FORCE)
      endif()
      if(NOT BAE_SDL3_LIBRARY AND PC_SDL3_LIBDIR AND EXISTS "${PC_SDL3_LIBDIR}/libSDL3.a")
        set(BAE_SDL3_LIBRARY "${PC_SDL3_LIBDIR}/libSDL3.a" CACHE FILEPATH "SDL3 library" FORCE)
      elseif(NOT BAE_SDL3_LIBRARY AND PC_SDL3_LIBDIR AND EXISTS "${PC_SDL3_LIBDIR}/libSDL3.dll.a")
        set(BAE_SDL3_LIBRARY "${PC_SDL3_LIBDIR}/libSDL3.dll.a" CACHE FILEPATH "SDL3 library" FORCE)
      elseif(NOT BAE_SDL3_LIBRARY)
        find_library(BAE_SDL3_LIBRARY NAMES SDL3 HINTS ${PC_SDL3_LIBDIR} ${PC_SDL3_LIBRARY_DIRS})
      endif()
      if(BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY)
        message(STATUS "SDL3 found via pkg-config: ${BAE_SDL3_LIBRARY}")
      endif()
    endif()
  endif()
endif()
if(BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY)
  set(_BAE_PLATFORM_SDL3_AVAILABLE ON)
else()
  set(_BAE_PLATFORM_SDL3_AVAILABLE OFF)
endif()

find_path(BAE_SDL2_INCLUDE_DIR SDL2/SDL.h)
find_library(BAE_SDL2_LIBRARY NAMES SDL2)
if(NOT BAE_SDL2_INCLUDE_DIR OR NOT BAE_SDL2_LIBRARY)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_SDL2 QUIET sdl2)
    if(PC_SDL2_FOUND)
      if(NOT BAE_SDL2_INCLUDE_DIR AND PC_SDL2_INCLUDEDIR)
        set(BAE_SDL2_INCLUDE_DIR "${PC_SDL2_INCLUDEDIR}" CACHE PATH "SDL2 include dir" FORCE)
      endif()
      if(NOT BAE_SDL2_LIBRARY AND PC_SDL2_LIBDIR AND EXISTS "${PC_SDL2_LIBDIR}/libSDL2.a")
        set(BAE_SDL2_LIBRARY "${PC_SDL2_LIBDIR}/libSDL2.a" CACHE FILEPATH "SDL2 library" FORCE)
      elseif(NOT BAE_SDL2_LIBRARY AND PC_SDL2_LIBDIR AND EXISTS "${PC_SDL2_LIBDIR}/libSDL2.dll.a")
        set(BAE_SDL2_LIBRARY "${PC_SDL2_LIBDIR}/libSDL2.dll.a" CACHE FILEPATH "SDL2 library" FORCE)
      elseif(NOT BAE_SDL2_LIBRARY)
        find_library(BAE_SDL2_LIBRARY NAMES SDL2 HINTS ${PC_SDL2_LIBDIR} ${PC_SDL2_LIBRARY_DIRS})
      endif()
      if(BAE_SDL2_INCLUDE_DIR AND BAE_SDL2_LIBRARY)
        message(STATUS "SDL2 found via pkg-config: ${BAE_SDL2_LIBRARY}")
      endif()
    endif()
  endif()
endif()
if(BAE_SDL2_INCLUDE_DIR AND BAE_SDL2_LIBRARY)
  set(_BAE_PLATFORM_SDL2_AVAILABLE ON)
else()
  set(_BAE_PLATFORM_SDL2_AVAILABLE OFF)
endif()


if(NOT DEFINED BAE_PLATFORM OR BAE_PLATFORM STREQUAL "")
  if(MSVC)
    set(_BAE_PLATFORM_DEFAULT "WinOS")
  else()
    set(_BAE_PLATFORM_DEFAULT "Dummy")
  endif()
  if(_BAE_PLATFORM_SDL3_AVAILABLE AND (NOT _BAE_PLATFORM_SDL2_AVAILABLE OR NOT USE_SDL2))
    set(_BAE_PLATFORM_DEFAULT "SDL3")
    message(STATUS "Using SDL3 platform backend (found SDL3 headers and library).")
  elseif(_BAE_PLATFORM_SDL2_AVAILABLE OR USE_SDL2 STREQUAL "ON")
    set(_BAE_PLATFORM_DEFAULT "SDL2")
    set(BAE_USING_SDL2 ON)
    message(STATUS "Using SDL2 platform backend (found SDL2 headers and library).")
  endif()
  set(BAE_PLATFORM "${_BAE_PLATFORM_DEFAULT}" CACHE STRING "Backend platform (Dummy, SDL2, SDL3, Raylib, WinOS, Ansi, WASM, Android, IOS, MacOSX, foobar2000)")
else()
  set(BAE_PLATFORM "${BAE_PLATFORM}" CACHE STRING "Backend platform (Dummy, SDL2, SDL3, Raylib, WinOS, Ansi, WASM, Android, IOS, MacOSX, foobar2000)")
endif()
message(STATUS "Selected BAE_PLATFORM='${BAE_PLATFORM}'")
set_property(CACHE BAE_PLATFORM PROPERTY STRINGS Dummy SDL2 SDL3 Raylib WinOS Ansi WASM Android IOS MacOSX foobar2000)

set(BAE_PLATFORM_SOURCE "")
set(BAE_X_PLATFORM_DEFINE "")
if(BAE_PLATFORM STREQUAL "Dummy")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_Dummy.c)
  set(BAE_X_PLATFORM_DEFINE X_DUMMY)
elseif(BAE_PLATFORM STREQUAL "SDL2")
  set(BAE_USING_SDL2 ON)
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_SDL2.c)
  set(BAE_X_PLATFORM_DEFINE X_SDL2)
elseif(BAE_PLATFORM STREQUAL "SDL3")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_SDL3.c)
  set(BAE_X_PLATFORM_DEFINE X_SDL3)
elseif(BAE_PLATFORM STREQUAL "Raylib")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_Raylib.c)
  set(BAE_X_PLATFORM_DEFINE X_RAYLIB)
elseif(BAE_PLATFORM STREQUAL "WinOS")
  set(BAE_PLATFORM_SOURCE 
    neobae/src/BAE_Source/Platform/BAE_API_WinOS.c
    neobae/src/BAE_Source/Platform/BAE_API_WinOS_Capture.c
    neobae/src/BAE_Source/Platform/BAE_API_WinOS_Thread.c
  )
  set(BAE_X_PLATFORM_DEFINE X_WIN95)
elseif(BAE_PLATFORM STREQUAL "Ansi")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_Ansi.c)
  set(BAE_X_PLATFORM_DEFINE X_ANSI)
elseif(BAE_PLATFORM STREQUAL "WASM")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_WASM.c)
  set(BAE_X_PLATFORM_DEFINE X_WASM)
elseif(BAE_PLATFORM STREQUAL "Android")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_Android.c)
  set(BAE_X_PLATFORM_DEFINE X_ANDROID)
elseif(BAE_PLATFORM STREQUAL "IOS")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_IOS.c)
  set(BAE_X_PLATFORM_DEFINE X_IOS)
elseif(BAE_PLATFORM STREQUAL "MacOSX")
  set(BAE_PLATFORM_SOURCE neobae/src/BAE_Source/Platform/BAE_API_MacOSX.c)
  set(BAE_X_PLATFORM_DEFINE X_MACINTOSH)
elseif(BAE_PLATFORM STREQUAL "foobar2000")
  set(BAE_PLATFORM_SOURCE
    neobae/src/BAE_Source/Platform/BAE_API_foobar2000.c
    neobae/foobar2000/BAE_ProbeSongLength.c
  )
  set(BAE_X_PLATFORM_DEFINE X_FOOBAR2000_PLUGIN)
  message(STATUS "foobar2000 backend selected. Build libneobae, copy libneobae.dll/.lib into neobae/foobar2000/lib/, then build neobae/foobar2000/foo_neobae.sln with MSVC.")
else()
  message(FATAL_ERROR "Unsupported BAE_PLATFORM='${BAE_PLATFORM}'.")
endif()

set(BAE_SDL_BACKEND_AVAILABLE OFF)
if(BAE_PLATFORM STREQUAL "SDL3")
  if(_BAE_PLATFORM_SDL3_AVAILABLE)
    set(BAE_SDL_BACKEND_AVAILABLE ON)
  else()
    message(FATAL_ERROR
      "BAE_PLATFORM=SDL3 selected, but SDL3 headers/library were not found.\n"
      "For llvm-mingw cross builds, ensure PKG_CONFIG_LIBDIR points at the "
      "target pkgconfig dir, or pass -DCMAKE_FIND_ROOT_PATH=/path/to/<triple> "
      "and/or -DCMAKE_PREFIX_PATH=/path/to/<triple>.")
  endif()
elseif(BAE_PLATFORM STREQUAL "SDL2")
  if(_BAE_PLATFORM_SDL2_AVAILABLE)
    set(BAE_SDL_BACKEND_AVAILABLE ON)
    set(BAE_USING_SDL2 OFF)
  else()
    message(FATAL_ERROR
      "BAE_PLATFORM=SDL2 selected, but SDL2 headers/library were not found.\n"
      "For llvm-mingw cross builds, ensure PKG_CONFIG_LIBDIR points at the "
      "target pkgconfig dir, or pass -DCMAKE_FIND_ROOT_PATH=/path/to/<triple> "
      "and/or -DCMAKE_PREFIX_PATH=/path/to/<triple>.")
  endif()
endif()

set(BAE_SDL_TTF_AVAILABLE OFF)
set(ZEFIDI_SDL_BACKEND_AVAILABLE OFF)
set(ZEFIDI_SDL_VERSION "")
set(ZEFIDI_SDL_INCLUDE_DIR "")
set(ZEFIDI_SDL_LIBRARY "")
set(ZEFIDI_SDL_TTF_INCLUDE_DIR "")
set(ZEFIDI_SDL_TTF_LIBRARY "")
if(BUILD_ZEFIDI)
  message(STATUS "Checking for SDL support for zefidi...")
  if(NOT BAE_SDL3_INCLUDE_DIR)
    find_path(BAE_SDL3_INCLUDE_DIR SDL3/SDL.h)
  endif()
  if(NOT BAE_SDL3_LIBRARY)
    find_library(BAE_SDL3_LIBRARY NAMES SDL3)
  endif()
  if(NOT BAE_SDL3_TTF_INCLUDE_DIR)
    find_path(BAE_SDL3_TTF_INCLUDE_DIR SDL3_ttf/SDL_ttf.h)
  endif()
  if(NOT BAE_SDL3_TTF_LIBRARY)
    find_library(BAE_SDL3_TTF_LIBRARY NAMES SDL3_ttf)
  endif()
  if(NOT BAE_SDL3_TTF_INCLUDE_DIR OR NOT BAE_SDL3_TTF_LIBRARY)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(PC_SDL3_TTF QUIET sdl3-ttf)
      if(PC_SDL3_TTF_FOUND)
        if(NOT BAE_SDL3_TTF_INCLUDE_DIR AND PC_SDL3_TTF_INCLUDEDIR)
          set(BAE_SDL3_TTF_INCLUDE_DIR "${PC_SDL3_TTF_INCLUDEDIR}" CACHE PATH "SDL3_ttf include dir" FORCE)
        endif()
        if(NOT BAE_SDL3_TTF_LIBRARY AND PC_SDL3_TTF_LIBDIR AND EXISTS "${PC_SDL3_TTF_LIBDIR}/libSDL3_ttf.a")
          set(BAE_SDL3_TTF_LIBRARY "${PC_SDL3_TTF_LIBDIR}/libSDL3_ttf.a" CACHE FILEPATH "SDL3_ttf library" FORCE)
        elseif(NOT BAE_SDL3_TTF_LIBRARY AND PC_SDL3_TTF_LIBDIR AND EXISTS "${PC_SDL3_TTF_LIBDIR}/libSDL3_ttf.dll.a")
          set(BAE_SDL3_TTF_LIBRARY "${PC_SDL3_TTF_LIBDIR}/libSDL3_ttf.dll.a" CACHE FILEPATH "SDL3_ttf library" FORCE)
        endif()
      endif()
    endif()
  endif()

  if(NOT BAE_SDL2_INCLUDE_DIR)
    find_path(BAE_SDL2_INCLUDE_DIR SDL2/SDL.h)
  endif()
  if(NOT BAE_SDL2_LIBRARY)
    find_library(BAE_SDL2_LIBRARY NAMES SDL2)
  endif()
  if(NOT BAE_SDL2_TTF_INCLUDE_DIR)
    find_path(BAE_SDL2_TTF_INCLUDE_DIR SDL2/SDL_ttf.h)
  endif()
  if(NOT BAE_SDL2_TTF_LIBRARY)
    find_library(BAE_SDL2_TTF_LIBRARY NAMES SDL2_ttf)
  endif()
  
  if(BAE_PLATFORM STREQUAL "SDL2")
    if(_BAE_PLATFORM_SDL2_AVAILABLE AND BAE_SDL2_TTF_INCLUDE_DIR AND BAE_SDL2_TTF_LIBRARY)
      set(BAE_SDL_TTF_AVAILABLE ON)
      message(STATUS "SDL_ttf support found for SDL2 backend.")
    else()
      message(STATUS "BAE_PLATFORM=SDL2 selected, but SDL2_ttf headers/library were not found.")
    endif()
  elseif(BAE_PLATFORM STREQUAL "SDL3")
    if(_BAE_PLATFORM_SDL3_AVAILABLE AND BAE_SDL3_TTF_INCLUDE_DIR AND BAE_SDL3_TTF_LIBRARY)
      set(BAE_SDL_TTF_AVAILABLE ON)
      message(STATUS "SDL_ttf support found for SDL3 backend.")
    else()
      message(STATUS "BAE_PLATFORM=SDL3 selected, but SDL3_ttf headers/library were not found.")
    endif()
  endif()

  if(BAE_PLATFORM STREQUAL "SDL3"
      AND BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY
      AND BAE_SDL3_TTF_INCLUDE_DIR AND BAE_SDL3_TTF_LIBRARY AND NOT USE_SDL2)
    set(ZEFIDI_SDL_BACKEND_AVAILABLE ON)
    set(ZEFIDI_SDL_VERSION "SDL3")
    set(ZEFIDI_SDL_INCLUDE_DIR ${BAE_SDL3_INCLUDE_DIR})
    set(ZEFIDI_SDL_LIBRARY ${BAE_SDL3_LIBRARY})
    set(ZEFIDI_SDL_TTF_INCLUDE_DIR ${BAE_SDL3_TTF_INCLUDE_DIR})
    set(ZEFIDI_SDL_TTF_LIBRARY ${BAE_SDL3_TTF_LIBRARY})
  elseif(BAE_PLATFORM STREQUAL "SDL2"
      AND BAE_SDL2_INCLUDE_DIR AND BAE_SDL2_LIBRARY
      AND BAE_SDL2_TTF_INCLUDE_DIR AND BAE_SDL2_TTF_LIBRARY)
    set(BAE_USING_SDL2 ON)
    set(ZEFIDI_SDL_BACKEND_AVAILABLE ON)
    set(ZEFIDI_SDL_VERSION "SDL2")
    set(ZEFIDI_SDL_INCLUDE_DIR ${BAE_SDL2_INCLUDE_DIR})
    set(ZEFIDI_SDL_LIBRARY ${BAE_SDL2_LIBRARY})
    set(ZEFIDI_SDL_TTF_INCLUDE_DIR ${BAE_SDL2_TTF_INCLUDE_DIR})
    set(ZEFIDI_SDL_TTF_LIBRARY ${BAE_SDL2_TTF_LIBRARY})
  elseif(BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY
      AND BAE_SDL3_TTF_INCLUDE_DIR AND BAE_SDL3_TTF_LIBRARY AND NOT USE_SDL2)
    set(ZEFIDI_SDL_BACKEND_AVAILABLE ON)
    set(ZEFIDI_SDL_VERSION "SDL3")
    set(ZEFIDI_SDL_INCLUDE_DIR ${BAE_SDL3_INCLUDE_DIR})
    set(ZEFIDI_SDL_LIBRARY ${BAE_SDL3_LIBRARY})
    set(ZEFIDI_SDL_TTF_INCLUDE_DIR ${BAE_SDL3_TTF_INCLUDE_DIR})
    set(ZEFIDI_SDL_TTF_LIBRARY ${BAE_SDL3_TTF_LIBRARY})
  elseif(BAE_SDL2_INCLUDE_DIR AND BAE_SDL2_LIBRARY
      AND BAE_SDL2_TTF_INCLUDE_DIR AND BAE_SDL2_TTF_LIBRARY)
    set(BAE_USING_SDL2 ON)     
    set(ZEFIDI_SDL_BACKEND_AVAILABLE ON)
    set(ZEFIDI_SDL_VERSION "SDL2")
    set(ZEFIDI_SDL_INCLUDE_DIR ${BAE_SDL2_INCLUDE_DIR})
    set(ZEFIDI_SDL_LIBRARY ${BAE_SDL2_LIBRARY})
    set(ZEFIDI_SDL_TTF_INCLUDE_DIR ${BAE_SDL2_TTF_INCLUDE_DIR})
    set(ZEFIDI_SDL_TTF_LIBRARY ${BAE_SDL2_TTF_LIBRARY})
  endif()

  if(ZEFIDI_SDL_BACKEND_AVAILABLE)
    message(STATUS "zefidi will use ${ZEFIDI_SDL_VERSION} independently of BAE_PLATFORM=${BAE_PLATFORM}.")
  else()
    message(STATUS "No usable SDL2/SDL3 + SDL_ttf combination was found for zefidi.")
  endif()
endif()

set(BAE_ZEFIDI_AVAILABLE OFF)
if(ZEFIDI_SDL_BACKEND_AVAILABLE)
  set(BAE_ZEFIDI_AVAILABLE ON)
endif()

set(BAE_PLAYBAE_PLATFORM_AVAILABLE ON)
if(BAE_PLATFORM STREQUAL "Dummy" OR BAE_PLATFORM STREQUAL "Ansi")
  set(BAE_PLAYBAE_PLATFORM_AVAILABLE OFF)
endif()

set(NEOBAE_SOURCES
  ${BAE_PLATFORM_SOURCE}
  neobae/src/BAE_Source/Common/BAE_Override.c
  neobae/src/BAE_Source/Common/DriverTools.c
  neobae/src/BAE_Source/Common/GenAudioStreams.c
  neobae/src/BAE_Source/Common/GenCache.c
  neobae/src/BAE_Source/Common/GenChorus.c
  neobae/src/BAE_Source/Common/GenFiltersReverbU3232.c
  neobae/src/BAE_Source/Common/GenInterp2ReverbU3232.c
  neobae/src/BAE_Source/Common/GenOutput.c
  neobae/src/BAE_Source/Common/GenPatch.c
  neobae/src/BAE_Source/Common/GenReverb.c
  neobae/src/BAE_Source/Common/GenReverbNew.c
  neobae/src/BAE_Source/Common/GenReverbNeo.c
  neobae/src/BAE_Source/Common/GenRingtone.c
  neobae/src/BAE_Source/Common/GenRMI.c
  neobae/src/BAE_Source/Common/GenSample.c
  neobae/src/BAE_Source/Common/GenSeq.c
  neobae/src/BAE_Source/Common/GenSeqTools.c
  neobae/src/BAE_Source/Common/GenBankBalance.c
  neobae/src/BAE_Source/Common/GenSetup.c
  neobae/src/BAE_Source/Common/GenSong.c
  neobae/src/BAE_Source/Common/GenSoundFiles.c
  neobae/src/BAE_Source/Common/GenSynth.c
  neobae/src/BAE_Source/Common/GenSynthFiltersSimple.c
  neobae/src/BAE_Source/Common/GenSynthFiltersU3232.c
  neobae/src/BAE_Source/Common/GenSynthInterp2Simple.c
  neobae/src/BAE_Source/Common/GenSynthInterp2U3232.c
  neobae/src/BAE_Source/Common/NeoBAE.c
  neobae/src/BAE_Source/Common/NeoBAEConfigPath.c
  neobae/src/BAE_Source/Common/NeoBAEBankLoad.c
  neobae/src/BAE_Source/Common/NewNewLZSS.c
  neobae/src/BAE_Source/Common/SampleTools.c
  neobae/src/BAE_Source/Common/X_API.c
  neobae/src/BAE_Source/Common/X_Instruments.c
  neobae/src/BAE_Source/Common/X_EditorTools.c
  neobae/src/BAE_Source/Common/X_Decompress.c
  neobae/src/BAE_Source/Common/X_IMA.c
  neobae/src/thirdparty/adpcm-xq/adpcm-lib.c
  neobae/src/thirdparty/adpcm-xq/adpcm-dns.c
  neobae/src/BAE_Source/Common/g711.c
  neobae/src/BAE_Source/Common/g721.c
  neobae/src/BAE_Source/Common/g723_24.c
  neobae/src/BAE_Source/Common/g723_40.c
  neobae/src/BAE_Source/Common/g72x.c
  neobae/src/BAE_Source/Common/sha1mini.c
  neobae/src/BAE_Source/Common/XFileTypes.c
  neobae/src/BAE_Source/Common/X_DebugCallback.c
)

if (BAE_ENABLE_ADX_SUPPORT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/XADXFiles.c
    neobae/src/adx2wav/adx2wav_decode.c
  )
endif()

if(BAE_ENABLE_LZMA)
  list(APPEND NEOBAE_SOURCES 
  neobae/src/BAE_Source/Common/X_LZMA.c
  neobae/src/thirdparty/lzma-26.00/C/Alloc.c
	neobae/src/thirdparty/lzma-26.00/C/7zAlloc.c
	neobae/src/thirdparty/lzma-26.00/C/7zCrc.c
	neobae/src/thirdparty/lzma-26.00/C/7zCrcOpt.c
	neobae/src/thirdparty/lzma-26.00/C/7zStream.c
	neobae/src/thirdparty/lzma-26.00/C/XzCrc64.c
	neobae/src/thirdparty/lzma-26.00/C/XzCrc64Opt.c
	neobae/src/thirdparty/lzma-26.00/C/Sha256.c
	neobae/src/thirdparty/lzma-26.00/C/Sha256Opt.c
	neobae/src/thirdparty/lzma-26.00/C/CpuArch.c
	neobae/src/thirdparty/lzma-26.00/C/Bra.c
	neobae/src/thirdparty/lzma-26.00/C/Bra86.c
	neobae/src/thirdparty/lzma-26.00/C/BraIA64.c
	neobae/src/thirdparty/lzma-26.00/C/Delta.c
	neobae/src/thirdparty/lzma-26.00/C/LzmaDec.c
	neobae/src/thirdparty/lzma-26.00/C/Lzma2Dec.c
	neobae/src/thirdparty/lzma-26.00/C/LzFind.c
	neobae/src/thirdparty/lzma-26.00/C/LzmaEnc.c
	neobae/src/thirdparty/lzma-26.00/C/Lzma2Enc.c
	neobae/src/thirdparty/lzma-26.00/C/Xz.c
	neobae/src/thirdparty/lzma-26.00/C/XzDec.c
)
endif()

if(BAE_ENABLE_RMF_EDITOR)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/BAE_EditorAPI.c
    neobae/src/BAE_Source/Common/BAE_EditorAPI_codec.c
    neobae/src/BAE_Source/Common/BAE_EditorAPI_midi.c
    neobae/src/BAE_Source/Common/BAE_EditorAPI_rmf_io.c
    neobae/src/BAE_Source/Common/BAE_EditorAPI_document.c
    neobae/src/BAE_Source/Common/BAE_EditorAPI_bank.c
  )
endif()

if(BAE_ENABLE_NATIVE_DLS)
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_Source/Common/GenDLS_MobileBAE.c)
endif()

if(BAE_ENABLE_SF2_CONVERTER)
  list(APPEND NEOBAE_SOURCES
    neobae/src/mod2rmf/mod2rmf_encoder.c
    neobae/src/sf2-hsb/hsb_writer.c
    neobae/src/sf2-hsb/sf2_hsb_converter.c
    neobae/src/sf2-hsb/sf2_parser.c
  )
endif()

if(BAE_ENABLE_FLUIDLITE)
  list(APPEND NEOBAE_SOURCES
    neobae/src/fluidlite/src/fluid_chan.c
    neobae/src/fluidlite/src/fluid_chorus.c
    neobae/src/fluidlite/src/fluid_conv.c
    neobae/src/fluidlite/src/fluid_defsfont.c
    neobae/src/fluidlite/src/fluid_dsp_float.c
    neobae/src/fluidlite/src/fluid_gen.c
    neobae/src/fluidlite/src/fluid_hash.c
    neobae/src/fluidlite/src/fluid_init.c
    neobae/src/fluidlite/src/fluid_list.c
    neobae/src/fluidlite/src/fluid_mod.c
    neobae/src/fluidlite/src/fluid_ramsfont.c
    neobae/src/fluidlite/src/fluid_rev.c
    neobae/src/fluidlite/src/fluid_settings.c
    neobae/src/fluidlite/src/fluid_synth.c
    neobae/src/fluidlite/src/fluid_sys.c
    neobae/src/fluidlite/src/fluid_tuning.c
    neobae/src/fluidlite/src/fluid_voice.c
  )
endif()

if(BAE_ENABLE_SF2_SUPPORT AND BAE_ENABLE_FLUIDLITE)
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_Source/Common/GenSF2_FluidLite.c)
endif()

if(BAE_ENABLE_XMF_SUPPORT)
  list(APPEND NEOBAE_SOURCES 
      neobae/src/BAE_Source/Common/GenXMF.c
      neobae/src/BAE_Source/Common/cast5mini.c
  )
endif()

if(BAE_ENABLE_MTHC_SUPPORT)
  list(APPEND NEOBAE_SOURCES neobae/src/mthc/mthc_decomp.c)
endif()

if(BAE_ENABLE_ADP_SUPPORT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/adp2wav/adp2wav_decode.c
    neobae/src/thirdparty/libg722/g722_decode.c
  )
endif()

if(BAE_ENABLE_RETRO_RINGTONE_SUPPORT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/GenRingtone.c
  )
endif()

if(BAE_ENABLE_QOA_SUPPORT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/XQOAFiles.c
  )
endif()

if(BAE_ENABLE_WMA_SUPPORT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/XWMAFiles.c
    neobae/src/BAE_Source/Common/libwma/wma_decoder.c
  )
endif()

if(BAE_ENABLE_BAESCRIPT)
  list(APPEND NEOBAE_SOURCES
    neobae/src/script/baescript.c
    neobae/src/script/baescript_lexer.c
    neobae/src/script/baescript_parser.c
    neobae/src/script/baescript_vm.c
  )
endif()

if(BAE_ENABLE_MP3_DECODER)
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_MPEG_Source_II/XMPEG_minimp3_wrapper.c)
endif()

if(BAE_ENABLE_MP3_ENCODER OR BAE_ENABLE_MP3_DECODER)
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_MPEG_Source_II/XMPEGFilesSun.c)
endif()

if(BAE_ENABLE_MP3_ENCODER)
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_MPEG_Source_II/XMPEG_lame_encoder.cpp)
  if (NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/VbrTag.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/bitstream.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/encoder.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/fft.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/gain_analysis.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/id3tag.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/lame.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/newmdct.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/presets.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/psymodel.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/quantize.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/quantize_pvt.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/reservoir.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/set_get.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/tables.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/takehiro.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/util.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/vbrquantize.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/version.c
      neobae/src/thirdparty/lame-3.100-slim/libmp3lame/mpglib_interface.c
    )
  endif()
endif()

if(BAE_ENABLE_FLAC_ENCODER OR BAE_ENABLE_FLAC_DECODER OR BAE_ENABLE_VORBIS_ENCODER OR BAE_ENABLE_VORBIS_DECODER OR BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER)
  if (NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/libogg/src/bitwise.c
      neobae/src/thirdparty/libogg/src/framing.c
    )
  endif()
endif()

if(BAE_ENABLE_FLAC_ENCODER OR BAE_ENABLE_FLAC_DECODER)
  if (NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/flac/src/libFLAC/bitmath.c
      neobae/src/thirdparty/flac/src/libFLAC/bitreader.c
      neobae/src/thirdparty/flac/src/libFLAC/bitwriter.c
      neobae/src/thirdparty/flac/src/libFLAC/cpu.c
      neobae/src/thirdparty/flac/src/libFLAC/crc.c
      neobae/src/thirdparty/flac/src/libFLAC/fixed.c
      neobae/src/thirdparty/flac/src/libFLAC/float.c
      neobae/src/thirdparty/flac/src/libFLAC/format.c
      neobae/src/thirdparty/flac/src/libFLAC/lpc.c
      neobae/src/thirdparty/flac/src/libFLAC/md5.c
      neobae/src/thirdparty/flac/src/libFLAC/memory.c
      neobae/src/thirdparty/flac/src/libFLAC/metadata_iterators.c
      neobae/src/thirdparty/flac/src/libFLAC/metadata_object.c
      neobae/src/thirdparty/flac/src/libFLAC/stream_decoder.c
      neobae/src/thirdparty/flac/src/libFLAC/stream_encoder.c
      neobae/src/thirdparty/flac/src/libFLAC/stream_encoder_framing.c
      neobae/src/thirdparty/flac/src/libFLAC/window.c
      neobae/src/thirdparty/flac/src/libFLAC/ogg_decoder_aspect.c
      neobae/src/thirdparty/flac/src/libFLAC/ogg_encoder_aspect.c
      neobae/src/thirdparty/flac/src/libFLAC/ogg_helper.c
      neobae/src/thirdparty/flac/src/libFLAC/ogg_mapping.c
    )
    if(WIN32)
      list(APPEND NEOBAE_SOURCES neobae/src/thirdparty/flac/src/share/win_utf8_io/win_utf8_io.c)
    endif()
  endif()
endif()

if(BAE_ENABLE_VORBIS_ENCODER OR BAE_ENABLE_VORBIS_DECODER)
  if (NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/libvorbis/lib/mdct.c
      neobae/src/thirdparty/libvorbis/lib/smallft.c
      neobae/src/thirdparty/libvorbis/lib/block.c
      neobae/src/thirdparty/libvorbis/lib/envelope.c
      neobae/src/thirdparty/libvorbis/lib/window.c
      neobae/src/thirdparty/libvorbis/lib/lsp.c
      neobae/src/thirdparty/libvorbis/lib/lpc.c
      neobae/src/thirdparty/libvorbis/lib/analysis.c
      neobae/src/thirdparty/libvorbis/lib/synthesis.c
      neobae/src/thirdparty/libvorbis/lib/psy.c
      neobae/src/thirdparty/libvorbis/lib/info.c
      neobae/src/thirdparty/libvorbis/lib/floor1.c
      neobae/src/thirdparty/libvorbis/lib/floor0.c
      neobae/src/thirdparty/libvorbis/lib/res0.c
      neobae/src/thirdparty/libvorbis/lib/mapping0.c
      neobae/src/thirdparty/libvorbis/lib/registry.c
      neobae/src/thirdparty/libvorbis/lib/codebook.c
      neobae/src/thirdparty/libvorbis/lib/sharedbook.c
      neobae/src/thirdparty/libvorbis/lib/lookup.c
      neobae/src/thirdparty/libvorbis/lib/bitrate.c
      neobae/src/thirdparty/libvorbis/lib/vorbisfile.c
    )
  endif()
  if(BAE_ENABLE_VORBIS_ENCODER AND NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES neobae/src/thirdparty/libvorbis/lib/vorbisenc.c)
  endif()
  list(APPEND NEOBAE_SOURCES neobae/src/BAE_Source/Common/XVorbisFiles.c)
endif()

if(BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER)
  list(APPEND NEOBAE_SOURCES
    neobae/src/BAE_Source/Common/XOpusFiles.c
  )
  if (NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/opus/src/opus.c
      neobae/src/thirdparty/opus/src/opus_decoder.c
      neobae/src/thirdparty/opus/src/opus_encoder.c
      neobae/src/thirdparty/opus/src/extensions.c
      neobae/src/thirdparty/opus/src/opus_multistream.c
      neobae/src/thirdparty/opus/src/opus_multistream_encoder.c
      neobae/src/thirdparty/opus/src/opus_multistream_decoder.c
      neobae/src/thirdparty/opus/src/repacketizer.c
      neobae/src/thirdparty/opus/src/opus_projection_encoder.c
      neobae/src/thirdparty/opus/src/opus_projection_decoder.c
      neobae/src/thirdparty/opus/src/mapping_matrix.c
      neobae/src/thirdparty/opus/src/analysis.c
      neobae/src/thirdparty/opus/src/mlp.c
      neobae/src/thirdparty/opus/src/mlp_data.c
      neobae/src/thirdparty/opus/celt/bands.c
      neobae/src/thirdparty/opus/celt/celt.c
      neobae/src/thirdparty/opus/celt/celt_encoder.c
      neobae/src/thirdparty/opus/celt/celt_decoder.c
      neobae/src/thirdparty/opus/celt/cwrs.c
      neobae/src/thirdparty/opus/celt/entcode.c
      neobae/src/thirdparty/opus/celt/entdec.c
      neobae/src/thirdparty/opus/celt/entenc.c
      neobae/src/thirdparty/opus/celt/kiss_fft.c
      neobae/src/thirdparty/opus/celt/laplace.c
      neobae/src/thirdparty/opus/celt/mathops.c
      neobae/src/thirdparty/opus/celt/mdct.c
      neobae/src/thirdparty/opus/celt/modes.c
      neobae/src/thirdparty/opus/celt/pitch.c
      neobae/src/thirdparty/opus/celt/celt_lpc.c
      neobae/src/thirdparty/opus/celt/quant_bands.c
      neobae/src/thirdparty/opus/celt/rate.c
      neobae/src/thirdparty/opus/celt/vq.c
      neobae/src/thirdparty/opus/silk/CNG.c
      neobae/src/thirdparty/opus/silk/code_signs.c
      neobae/src/thirdparty/opus/silk/init_decoder.c
      neobae/src/thirdparty/opus/silk/decode_core.c
      neobae/src/thirdparty/opus/silk/decode_frame.c
      neobae/src/thirdparty/opus/silk/decode_parameters.c
      neobae/src/thirdparty/opus/silk/decode_indices.c
      neobae/src/thirdparty/opus/silk/decode_pulses.c
      neobae/src/thirdparty/opus/silk/decoder_set_fs.c
      neobae/src/thirdparty/opus/silk/dec_API.c
      neobae/src/thirdparty/opus/silk/enc_API.c
      neobae/src/thirdparty/opus/silk/encode_indices.c
      neobae/src/thirdparty/opus/silk/encode_pulses.c
      neobae/src/thirdparty/opus/silk/gain_quant.c
      neobae/src/thirdparty/opus/silk/interpolate.c
      neobae/src/thirdparty/opus/silk/LP_variable_cutoff.c
      neobae/src/thirdparty/opus/silk/NLSF_decode.c
      neobae/src/thirdparty/opus/silk/NSQ.c
      neobae/src/thirdparty/opus/silk/NSQ_del_dec.c
      neobae/src/thirdparty/opus/silk/PLC.c
      neobae/src/thirdparty/opus/silk/shell_coder.c
      neobae/src/thirdparty/opus/silk/tables_gain.c
      neobae/src/thirdparty/opus/silk/tables_LTP.c
      neobae/src/thirdparty/opus/silk/tables_NLSF_CB_NB_MB.c
      neobae/src/thirdparty/opus/silk/tables_NLSF_CB_WB.c
      neobae/src/thirdparty/opus/silk/tables_other.c
      neobae/src/thirdparty/opus/silk/tables_pitch_lag.c
      neobae/src/thirdparty/opus/silk/tables_pulses_per_block.c
      neobae/src/thirdparty/opus/silk/VAD.c
      neobae/src/thirdparty/opus/silk/control_audio_bandwidth.c
      neobae/src/thirdparty/opus/silk/quant_LTP_gains.c
      neobae/src/thirdparty/opus/silk/VQ_WMat_EC.c
      neobae/src/thirdparty/opus/silk/HP_variable_cutoff.c
      neobae/src/thirdparty/opus/silk/NLSF_encode.c
      neobae/src/thirdparty/opus/silk/NLSF_VQ.c
      neobae/src/thirdparty/opus/silk/NLSF_unpack.c
      neobae/src/thirdparty/opus/silk/NLSF_del_dec_quant.c
      neobae/src/thirdparty/opus/silk/process_NLSFs.c
      neobae/src/thirdparty/opus/silk/stereo_LR_to_MS.c
      neobae/src/thirdparty/opus/silk/stereo_MS_to_LR.c
      neobae/src/thirdparty/opus/silk/check_control_input.c
      neobae/src/thirdparty/opus/silk/control_SNR.c
      neobae/src/thirdparty/opus/silk/init_encoder.c
      neobae/src/thirdparty/opus/silk/control_codec.c
      neobae/src/thirdparty/opus/silk/A2NLSF.c
      neobae/src/thirdparty/opus/silk/ana_filt_bank_1.c
      neobae/src/thirdparty/opus/silk/biquad_alt.c
      neobae/src/thirdparty/opus/silk/bwexpander_32.c
      neobae/src/thirdparty/opus/silk/bwexpander.c
      neobae/src/thirdparty/opus/silk/debug.c
      neobae/src/thirdparty/opus/silk/decode_pitch.c
      neobae/src/thirdparty/opus/silk/inner_prod_aligned.c
      neobae/src/thirdparty/opus/silk/lin2log.c
      neobae/src/thirdparty/opus/silk/log2lin.c
      neobae/src/thirdparty/opus/silk/LPC_analysis_filter.c
      neobae/src/thirdparty/opus/silk/LPC_inv_pred_gain.c
      neobae/src/thirdparty/opus/silk/table_LSF_cos.c
      neobae/src/thirdparty/opus/silk/NLSF2A.c
      neobae/src/thirdparty/opus/silk/NLSF_stabilize.c
      neobae/src/thirdparty/opus/silk/NLSF_VQ_weights_laroia.c
      neobae/src/thirdparty/opus/silk/pitch_est_tables.c
      neobae/src/thirdparty/opus/silk/resampler.c
      neobae/src/thirdparty/opus/silk/resampler_down2_3.c
      neobae/src/thirdparty/opus/silk/resampler_down2.c
      neobae/src/thirdparty/opus/silk/resampler_private_AR2.c
      neobae/src/thirdparty/opus/silk/resampler_private_down_FIR.c
      neobae/src/thirdparty/opus/silk/resampler_private_IIR_FIR.c
      neobae/src/thirdparty/opus/silk/resampler_private_up2_HQ.c
      neobae/src/thirdparty/opus/silk/resampler_rom.c
      neobae/src/thirdparty/opus/silk/sigm_Q15.c
      neobae/src/thirdparty/opus/silk/sort.c
      neobae/src/thirdparty/opus/silk/sum_sqr_shift.c
      neobae/src/thirdparty/opus/silk/stereo_decode_pred.c
      neobae/src/thirdparty/opus/silk/stereo_encode_pred.c
      neobae/src/thirdparty/opus/silk/stereo_find_predictor.c
      neobae/src/thirdparty/opus/silk/stereo_quant_pred.c
      neobae/src/thirdparty/opus/silk/LPC_fit.c
      neobae/src/thirdparty/opus/silk/float/apply_sine_window_FLP.c
      neobae/src/thirdparty/opus/silk/float/corrMatrix_FLP.c
      neobae/src/thirdparty/opus/silk/float/encode_frame_FLP.c
      neobae/src/thirdparty/opus/silk/float/find_LPC_FLP.c
      neobae/src/thirdparty/opus/silk/float/find_LTP_FLP.c
      neobae/src/thirdparty/opus/silk/float/find_pitch_lags_FLP.c
      neobae/src/thirdparty/opus/silk/float/find_pred_coefs_FLP.c
      neobae/src/thirdparty/opus/silk/float/LPC_analysis_filter_FLP.c
      neobae/src/thirdparty/opus/silk/float/LTP_analysis_filter_FLP.c
      neobae/src/thirdparty/opus/silk/float/LTP_scale_ctrl_FLP.c
      neobae/src/thirdparty/opus/silk/float/noise_shape_analysis_FLP.c
      neobae/src/thirdparty/opus/silk/float/process_gains_FLP.c
      neobae/src/thirdparty/opus/silk/float/regularize_correlations_FLP.c
      neobae/src/thirdparty/opus/silk/float/residual_energy_FLP.c
      neobae/src/thirdparty/opus/silk/float/warped_autocorrelation_FLP.c
      neobae/src/thirdparty/opus/silk/float/wrappers_FLP.c
      neobae/src/thirdparty/opus/silk/float/autocorrelation_FLP.c
      neobae/src/thirdparty/opus/silk/float/burg_modified_FLP.c
      neobae/src/thirdparty/opus/silk/float/bwexpander_FLP.c
      neobae/src/thirdparty/opus/silk/float/energy_FLP.c
      neobae/src/thirdparty/opus/silk/float/inner_product_FLP.c
      neobae/src/thirdparty/opus/silk/float/k2a_FLP.c
      neobae/src/thirdparty/opus/silk/float/LPC_inv_pred_gain_FLP.c
      neobae/src/thirdparty/opus/silk/float/pitch_analysis_core_FLP.c
      neobae/src/thirdparty/opus/silk/float/scale_copy_vector_FLP.c
      neobae/src/thirdparty/opus/silk/float/scale_vector_FLP.c
      neobae/src/thirdparty/opus/silk/float/schur_FLP.c
      neobae/src/thirdparty/opus/silk/float/sort_FLP.c
  )
  endif()
  if(BAE_ENABLE_OPUS_DECODER AND NOT NEOBAE_EXTERNAL_CODECS)
    list(APPEND NEOBAE_SOURCES
      neobae/src/thirdparty/opusfile/src/info.c
      neobae/src/thirdparty/opusfile/src/internal.c
      neobae/src/thirdparty/opusfile/src/opusfile.c
      neobae/src/thirdparty/opusfile/src/stream.c
    )
  endif()
endif()


# ABI soversion for shared libneobae (matches Windows RC VERSION_MAJOR).
set(NEOBAE_SOVERSION 2)

if(NEOBAE_SHARED_LIBNEOBAE)
  add_library(neobae SHARED ${NEOBAE_SOURCES})
  set_target_properties(neobae PROPERTIES
    VERSION "${NEOBAE_SOVERSION}.0.0"
    SOVERSION "${NEOBAE_SOVERSION}"
  )
  if(MSVC)
    # MSVC does not add a lib prefix; keep the historical libneobae.dll / .lib names.
    # MinGW already prefixes with "lib", so OUTPUT_NAME stays "neobae" → libneobae.dll.
    set_target_properties(neobae PROPERTIES OUTPUT_NAME "libneobae")
  endif()
else()
  add_library(neobae STATIC ${NEOBAE_SOURCES})
endif()

if((MINGW OR MSVC) AND NEOBAE_RC_ENABLED)
  neobae_configure_rc(neobae NEOBAE_VERSION_RC "${VERSION_RC_IN}" "NeoBAE Audio Engine" "libneobae.dll" "2" NO)
  target_sources(neobae PRIVATE "${NEOBAE_VERSION_RC}")
endif()

set(NEOBAE_DLL_DEF_FILE "")
set(NEOBAE_VCLIB_OUTPUT "")

# Shared libneobae already DT_NEEDEDs its deps; keep them out of the exported
# INTERFACE so install/CMake packages stay relocatable. Static libneobae still
# propagates link deps to consumers.
if(NEOBAE_SHARED_LIBNEOBAE)
  set(NEOBAE_DEP_LINK_VISIBILITY PRIVATE)
else()
  set(NEOBAE_DEP_LINK_VISIBILITY PUBLIC)
endif()

set(NEOBAE_STATIC_DEP_LINK_PREFIX "")
set(NEOBAE_STATIC_DEP_LINK_SUFFIX "")
set(NEOBAE_WINPTHREAD_LINK_ITEM winpthread)
if(NEOBAE_STATIC AND NEOBAE_SHARED_LIBNEOBAE AND MINGW)
  # Force static resolution for third-party -l<name> dependencies.
  set(NEOBAE_STATIC_DEP_LINK_PREFIX "-Wl,-Bstatic")
  set(NEOBAE_STATIC_DEP_LINK_SUFFIX "-Wl,-Bdynamic")

  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=libwinpthread.a
    OUTPUT_VARIABLE NEOBAE_WINPTHREAD_STATIC_ARCHIVE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NEOBAE_WINPTHREAD_STATIC_ARCHIVE AND
     NOT NEOBAE_WINPTHREAD_STATIC_ARCHIVE STREQUAL "libwinpthread.a" AND
     EXISTS "${NEOBAE_WINPTHREAD_STATIC_ARCHIVE}")
    set(NEOBAE_WINPTHREAD_LINK_ITEM "${NEOBAE_WINPTHREAD_STATIC_ARCHIVE}")
  endif()
endif()

if(NEOBAE_STATIC AND NEOBAE_SHARED_LIBNEOBAE AND MINGW)
  # Prefer static archives when resolving -l<name> while linking libneobae.dll.
  # This keeps dependency code inside libneobae.dll instead of importing sibling DLLs.
  set_target_properties(neobae PROPERTIES
    LINK_SEARCH_START_STATIC ON
    LINK_SEARCH_END_STATIC ON
  )
endif()

if(NEOBAE_SHARED_LIBNEOBAE AND WIN32)
  # Without exports MSVC will not emit an import library (.lib), which foo_neobae needs to link.
  # MinGW also needs this (plus --export-all-symbols below).
  set_target_properties(neobae PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
  )
endif()

if(NEOBAE_SHARED_LIBNEOBAE AND MINGW)
  # Extra MinGW export + .def for optional MSVC-style import lib via dlltool.
  set(NEOBAE_DLL_DEF_FILE "${CMAKE_CURRENT_BINARY_DIR}/bin/libneobae.def")
  target_link_options(neobae PRIVATE
    -Wl,--export-all-symbols
    -Wl,--output-def,${NEOBAE_DLL_DEF_FILE}
  )

  if(NEOBAE_BUILD_VCLIB)
    set(NEOBAE_DLLTOOL "${CMAKE_DLLTOOL}")
    if(NOT NEOBAE_DLLTOOL)
      execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-prog-name=dlltool
        OUTPUT_VARIABLE NEOBAE_DLLTOOL_CANDIDATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(NEOBAE_DLLTOOL_CANDIDATE AND NOT NEOBAE_DLLTOOL_CANDIDATE STREQUAL "dlltool")
        set(NEOBAE_DLLTOOL "${NEOBAE_DLLTOOL_CANDIDATE}")
      endif()
    endif()
    if(NOT NEOBAE_DLLTOOL)
      find_program(NEOBAE_DLLTOOL NAMES dlltool llvm-dlltool)
    endif()

    if(NEOBAE_DLLTOOL)
      set(NEOBAE_VCLIB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/libneobae.lib")
      add_custom_command(
        OUTPUT ${NEOBAE_VCLIB_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${NEOBAE_VCLIB_OUTPUT}
        COMMAND ${NEOBAE_DLLTOOL} -d ${NEOBAE_DLL_DEF_FILE} -D $<TARGET_FILE_NAME:neobae> -l ${NEOBAE_VCLIB_OUTPUT}
        DEPENDS neobae ${NEOBAE_DLL_DEF_FILE}
        VERBATIM
        COMMENT "Generating MSVC-style import library for libneobae.dll"
      )
      add_custom_target(neobae_vclib ALL DEPENDS ${NEOBAE_VCLIB_OUTPUT})
      add_dependencies(neobae_vclib neobae)
    else()
      message(WARNING "NEOBAE_BUILD_VCLIB is ON, but no dlltool executable was found. libneobae.lib will not be generated.")
    endif()
  endif()
endif()

if(NEOBAE_BUILD_VCLIB AND NOT (NEOBAE_SHARED_LIBNEOBAE AND MINGW))
  message(STATUS "NEOBAE_BUILD_VCLIB is only used for MinGW shared libneobae.dll builds.")
endif()

if(NEOBAE_STATIC)
  target_compile_definitions(neobae PUBLIC NEOBAE_STATIC=1)
endif()

if(BAE_EMBED_PATCHES_EFFECTIVE)
  set(BAE_EMBED_PATCH_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(BAE_EMBED_PATCH_HEADER "${BAE_EMBED_PATCH_HEADER_DIR}/BAEPatches.h")
  add_custom_command(
    OUTPUT ${BAE_EMBED_PATCH_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BAE_EMBED_PATCH_HEADER_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_patches_h.py ${BAE_EMBED_PATCH_FILE} ${BAE_EMBED_PATCH_HEADER}
    DEPENDS ${BAE_EMBED_PATCH_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_patches_h.py
    VERBATIM
    COMMENT "Embedding built-in patch bank into BAEPatches.h"
  )
  add_custom_target(neobae_embedded_patches DEPENDS ${BAE_EMBED_PATCH_HEADER})
  add_dependencies(neobae neobae_embedded_patches)
  target_include_directories(neobae PRIVATE ${BAE_EMBED_PATCH_HEADER_DIR})
endif()

# Public install surface is only the curated headers under include/neobae.
# In-tree / third-party includes stay BUILD_INTERFACE so they are not exported.
target_include_directories(neobae
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Platform>
    $<INSTALL_INTERFACE:include/neobae>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/adx2wav>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/mthc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/config>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/qoa>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/adpcm-xq>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lzma-26.00/C>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/mod2rmf>
)

if(BAE_PLATFORM STREQUAL "foobar2000")
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/foobar2000>
  )
endif()

if(BAE_ENABLE_WMA_SUPPORT)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/libwma>
  )
endif()

if(BAE_ENABLE_MP3_ENCODER OR BAE_ENABLE_MP3_DECODER)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_MPEG_Source_II>
  )
endif()

if(BAE_ENABLE_MP3_ENCODER AND NOT NEOBAE_EXTERNAL_CODECS)
    target_include_directories(neobae PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lame-3.100-slim/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lame-3.100-slim/libmp3lame>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/lame-3.100-slim/mpglib>
    )
endif()

if(BAE_ENABLE_MP3_DECODER)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/minimp3>
  )
endif()

if((BAE_ENABLE_FLAC_ENCODER OR BAE_ENABLE_FLAC_DECODER) AND NOT NEOBAE_EXTERNAL_CODECS)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/flac/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/flac/src/libFLAC/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/flac/src>
  )
endif()

if(BAE_ENABLE_VORBIS_ENCODER OR BAE_ENABLE_VORBIS_DECODER)
  if(NOT NEOBAE_EXTERNAL_CODECS)
    target_include_directories(neobae PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libvorbis/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libvorbis/lib>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libogg/include>
    )
  endif()
  if(BAE_VORBIS_INCLUDE_DIR)
    target_include_directories(neobae PUBLIC $<BUILD_INTERFACE:${BAE_VORBIS_INCLUDE_DIR}>)
  endif()
endif()

if((BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER) AND NOT NEOBAE_EXTERNAL_CODECS)
  target_include_directories(neobae BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/celt>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/silk>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opus/silk/float>
  )
endif()

if(BAE_ENABLE_OPUS_DECODER AND NOT NEOBAE_EXTERNAL_CODECS)
  target_include_directories(neobae BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opusfile/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/opusfile/src>
  )
endif()

if(BAE_ENABLE_SF2_CONVERTER)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/mod2rmf>
  )
endif()


if(BAE_ENABLE_FLAC_ENCODER OR BAE_ENABLE_FLAC_DECODER OR BAE_ENABLE_VORBIS_ENCODER OR BAE_ENABLE_VORBIS_DECODER OR BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER)
  if(NOT NEOBAE_EXTERNAL_CODECS)
    target_include_directories(neobae PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libogg/include>
    )
  endif()
  if(BAE_OGG_INCLUDE_DIR)
    target_include_directories(neobae PUBLIC $<BUILD_INTERFACE:${BAE_OGG_INCLUDE_DIR}>)
  endif()
endif()

if(NEOBAE_EXTERNAL_CODECS AND BAE_EXTERNAL_CODEC_INCLUDE_DIRS)
  foreach(_inc ${BAE_EXTERNAL_CODEC_INCLUDE_DIRS})
    target_include_directories(neobae SYSTEM PUBLIC $<BUILD_INTERFACE:${_inc}>)
  endforeach()
endif()

if(BAE_ENABLE_SF2_CONVERTER)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/sf2-hsb>
  )
endif()

if(BAE_ENABLE_BAESCRIPT)
  target_include_directories(neobae PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/script>
  )
endif()

if(BAE_ENABLE_SF2_SUPPORT AND BAE_ENABLE_FLUIDLITE AND BAE_FLUIDLITE_INCLUDE_DIR)
  target_include_directories(neobae PUBLIC $<BUILD_INTERFACE:${BAE_FLUIDLITE_INCLUDE_DIR}>)
endif()

if(BAE_PLATFORM STREQUAL "SDL2" AND BAE_SDL_BACKEND_AVAILABLE)
  target_include_directories(neobae PUBLIC $<BUILD_INTERFACE:${BAE_SDL2_INCLUDE_DIR}>)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY}
    ${NEOBAE_STATIC_DEP_LINK_PREFIX} ${BAE_SDL2_LIBRARY} ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
endif()

if(BAE_PLATFORM STREQUAL "SDL3" AND BAE_SDL_BACKEND_AVAILABLE)
  target_include_directories(neobae PUBLIC $<BUILD_INTERFACE:${BAE_SDL3_INCLUDE_DIR}>)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY}
    ${NEOBAE_STATIC_DEP_LINK_PREFIX} ${BAE_SDL3_LIBRARY} ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
endif()

if(NEOBAE_STATIC AND MINGW)
  # Keep the core/static-neobae dependency set aligned with mingw-static non-GUI makefiles.
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY}
    mingw32
    winmm
    shlwapi
    ole32
    uuid
    dsound
    dxguid
  )
  if(BAE_SDL_BACKEND_AVAILABLE)
    # Extra Win32 libs required when SDL is linked statically into libneobae.dll.
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY}
      version
      setupapi
      imm32
      cfgmgr32
      gdi32
      user32
      shell32
      oleaut32
      advapi32
      dinput8
    )
  endif()
endif()

# zlib for XMF/MXMF inflate (GenXMF.c). MinGW uses toolchain libs; MSVC uses
# find_package or a FetchContent fallback so headers/libs are always available.
# NEOBAE_ZLIB_TARGET is reused by standalone tools (xmfutil) that include zlib.h.
set(NEOBAE_ZLIB_TARGET "")
if(BAE_ENABLE_XMF_SUPPORT)
  if(MINGW)
    if(NEOBAE_STATIC)
      set(NEOBAE_ZLIB_TARGET zs)
    else()
      set(NEOBAE_ZLIB_TARGET z)
    endif()
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} ${NEOBAE_ZLIB_TARGET} ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  else()
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
      set(NEOBAE_ZLIB_TARGET ZLIB::ZLIB)
      target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_ZLIB_TARGET})
      message(STATUS "XMF: linking system zlib (${ZLIB_VERSION_STRING})")
    else()
      include(FetchContent)
      set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(neobae_zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
      )
      FetchContent_MakeAvailable(neobae_zlib)
      if(TARGET zlibstatic)
        set(NEOBAE_ZLIB_TARGET zlibstatic)
        message(STATUS "XMF: using FetchContent zlibstatic (v1.3.1)")
      elseif(TARGET zlib)
        set(NEOBAE_ZLIB_TARGET zlib)
        message(STATUS "XMF: using FetchContent zlib (v1.3.1)")
      else()
        message(FATAL_ERROR "XMF support requires zlib, but neither ZLIB package nor FetchContent zlib targets were found.")
      endif()
      target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_ZLIB_TARGET})
    endif()
  endif()
endif()



if((BAE_ENABLE_FLAC_ENCODER OR BAE_ENABLE_FLAC_DECODER) AND NEOBAE_EXTERNAL_CODECS)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} FLAC ogg ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
endif()

if((BAE_ENABLE_VORBIS_ENCODER OR BAE_ENABLE_VORBIS_DECODER) AND NEOBAE_EXTERNAL_CODECS)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} vorbis vorbisfile ogg ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  if(BAE_ENABLE_VORBIS_ENCODER AND NEOBAE_EXTERNAL_CODECS)
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} vorbisenc ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  endif()
endif()

if((BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER) AND NEOBAE_EXTERNAL_CODECS)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} opus ogg ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  if(BAE_ENABLE_OPUS_DECODER AND NEOBAE_EXTERNAL_CODECS)
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} opusfile ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  endif()
endif()

if(BAE_ENABLE_MP3_ENCODER AND NEOBAE_EXTERNAL_CODECS)
  # Some libmp3lame builds (e.g. MSYS2/llvm-mingw static) use libmpg123 for
  # decoding, so a static link needs it after mp3lame. Link it only if present.
  find_library(BAE_MPG123_LIBRARY NAMES mpg123 libmpg123)
  if(BAE_MPG123_LIBRARY)
    message(STATUS "LAME: also linking libmpg123 (${BAE_MPG123_LIBRARY})")
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} mp3lame mpg123 ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  else()
    message(STATUS "LAME: libmpg123 not found; if linking fails with undefined mpg123_* symbols, install it in the sysroot")
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} mp3lame ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  endif()
endif()


function(bae_apply_compile_definitions target)
    target_compile_definitions(${target} PUBLIC
      X_PLATFORM=${BAE_X_PLATFORM_DEFINE}
      FLAC__NO_DLL=1
      # Keep TRUE/FALSE defined for `#if FOO == TRUE` checks. If TRUE is
      # undefined, the preprocessor treats it as 0, so `USE_SDL2 == TRUE`
      # becomes `0 == 0` and wrongly selects the SDL2 branch under MSVC.
      TRUE=1
      FALSE=0
      USE_SDL2=$<IF:$<BOOL:${BAE_USING_SDL2}>,1,0>
      USE_ZMF_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_ZMF_SUPPORT}>,1,0>
      USE_LZMA_COMPRESSION=$<IF:$<BOOL:${BAE_ENABLE_LZMA}>,1,0>
      Z7_ST=$<IF:$<BOOL:${BAE_ENABLE_LZMA}>,1,0>
      USE_MPEG_ENCODER=$<IF:$<BOOL:${BAE_ENABLE_MP3_ENCODER}>,1,0>
      USE_MPEG_DECODER=$<IF:$<BOOL:${BAE_ENABLE_MP3_DECODER}>,1,0>
      USE_FLAC_ENCODER=$<IF:$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,1,0>
      USE_FLAC_DECODER=$<IF:$<BOOL:${BAE_ENABLE_FLAC_DECODER}>,1,0>
      USE_VORBIS_ENCODER=$<IF:$<BOOL:${BAE_ENABLE_VORBIS_ENCODER}>,1,0>
      USE_VORBIS_DECODER=$<IF:$<BOOL:${BAE_ENABLE_VORBIS_DECODER}>,1,0>
      USE_OPUS_ENCODER=$<IF:$<BOOL:${BAE_ENABLE_OPUS_ENCODER}>,1,0>
      USE_OPUS_DECODER=$<IF:$<BOOL:${BAE_ENABLE_OPUS_DECODER}>,1,0>
      OPUS_BUILD=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_OPUS_ENCODER}>,$<BOOL:${BAE_ENABLE_OPUS_DECODER}>>>,1,0>
      USE_MINIMP3_WRAPPER=$<IF:$<BOOL:${BAE_ENABLE_MP3_DECODER}>,1,0>
      USE_LAME_ENCODER=$<IF:$<BOOL:${BAE_ENABLE_MP3_ENCODER}>,1,0>
      HAVE_CONFIG_H=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_MP3_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_DECODER}>>>,1,0>
      HAVE_STDINT_H=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_MP3_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_DECODER}>>>,1,0>
      HAVE_INTTYPES_H=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_MP3_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_DECODER}>>>,1,0>
      USE_OGG_FORMAT=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_DECODER}>,$<BOOL:${BAE_ENABLE_VORBIS_ENCODER}>,$<BOOL:${BAE_ENABLE_VORBIS_DECODER}>,$<BOOL:${BAE_ENABLE_OPUS_ENCODER}>,$<BOOL:${BAE_ENABLE_OPUS_DECODER}>>>,1,0>
      SUPPORT_OGG_FORMAT=$<IF:$<BOOL:$<OR:$<BOOL:${BAE_ENABLE_FLAC_ENCODER}>,$<BOOL:${BAE_ENABLE_FLAC_DECODER}>,$<BOOL:${BAE_ENABLE_VORBIS_ENCODER}>,$<BOOL:${BAE_ENABLE_VORBIS_DECODER}>,$<BOOL:${BAE_ENABLE_OPUS_ENCODER}>,$<BOOL:${BAE_ENABLE_OPUS_DECODER}>>>,1,0>
      USE_SF2_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_SF2_SUPPORT}>,1,0>
      _USING_FLUIDLITE=$<IF:$<BOOL:${BAE_ENABLE_FLUIDLITE}>,1,0>
      FLUIDLITE_STATIC=$<IF:$<BOOL:${BAE_ENABLE_FLUIDLITE}>,1,0>
      SF3_SUPPORT=$<IF:$<BOOL:$<AND:$<BOOL:${BAE_ENABLE_VORBIS_DECODER}>,$<BOOL:${BAE_ENABLE_FLUIDLITE}>>>,1,0>
      USE_NATIVE_DLS=$<IF:$<BOOL:${BAE_ENABLE_NATIVE_DLS}>,1,0>
      USE_XMF_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_XMF_SUPPORT}>,1,0>
      DISABLE_NOKIA_PATCH=$<IF:$<BOOL:$<NOT:$<BOOL:${BAE_ENABLE_NOKIA_PATCH}>>>,1,0>
      DISABLE_BEATNIK_SF2_NRPN=$<IF:$<BOOL:$<NOT:$<BOOL:${BAE_ENABLE_BEATNIK_SF2_NRPN}>>>,1,0>
      _BUILT_IN_PATCHES=$<IF:$<BOOL:${BAE_EMBED_PATCHES_EFFECTIVE}>,1,0>
      _LOAD_BUILTIN_PATCHES_FOR_SF2=$<IF:$<BOOL:$<AND:$<BOOL:${BAE_EMBED_PATCHES_EFFECTIVE}>,$<BOOL:${BAE_ENABLE_BUILTIN_PATCHES_WITH_SF2}>>>,1,0>
      _LOAD_BUILTIN_PATCHES_FOR_DLS=$<IF:$<BOOL:$<AND:$<BOOL:${BAE_EMBED_PATCHES_EFFECTIVE}>,$<BOOL:${BAE_ENABLE_BUILTIN_PATCHES_WITH_DLS}>>>,1,0>
      BAE_FIX_SPAN_DC=$<IF:$<BOOL:${BAE_ENABLE_FIX_SPAN_DC}>,1,0>
      BAE_CLASSIC_CHORUS=$<IF:$<BOOL:${BAE_ENABLE_CLASSIC_CHORUS}>,1,0>
      USE_MTHC_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_MTHC_SUPPORT}>,1,0>
      USE_ADP_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_ADP_SUPPORT}>,1,0>
      USE_ADX_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_ADX_SUPPORT}>,1,0>
      SUPPORT_KARAOKE=$<IF:$<BOOL:${BAE_ENABLE_KARAOKE}>,1,0>
      SUPPORT_BAESCRIPT=$<IF:$<BOOL:${BAE_ENABLE_BAESCRIPT}>,1,0>
      SUPPORT_PLAYLIST=$<IF:$<BOOL:${BAE_ENABLE_PLAYLIST}>,1,0>
      USE_RETRO_RINGTONE_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_RETRO_RINGTONE_SUPPORT}>,1,0>
      USE_QOA_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_QOA_SUPPORT}>,1,0>
      USE_WMA_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_WMA_SUPPORT}>,1,0>
      USE_RMI_SUPPORT=$<IF:$<BOOL:${BAE_ENABLE_RMI_SUPPORT}>,1,0>
      USE_J2ME_PATCH=$<IF:$<BOOL:${BAE_ENABLE_J2ME_PATCH}>,1,0>
      BAE_ENABLE_ROLLED_MIDI_UNROLL=$<IF:$<BOOL:${BAE_ENABLE_ROLLED_MIDI_UNROLL}>,1,0>
      GIT_DIRTY=$<IF:$<BOOL:${BAE_GIT_DIRTY}>,1,0>
      SUPPORT_MIDI_HW=$<IF:$<BOOL:${BAE_ENABLE_MIDI_HARDWARE}>,1,0>
  )
  # Opus stack scratch: MSVC has no C99 VLAs, so use alloca instead of VAR_ARRAYS.
  if(BAE_ENABLE_OPUS_ENCODER OR BAE_ENABLE_OPUS_DECODER)
    if(MSVC)
      target_compile_definitions(${target} PUBLIC USE_ALLOCA=1)
    else()
      target_compile_definitions(${target} PUBLIC VAR_ARRAYS=1)
    endif()
  endif()
  # MSVC's -Wall is /Wall (every warning); keep that off. Use -Wall only for GCC/Clang.
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wall)
  endif()
endfunction()

# Quiet third-party/engine noise so real errors stay visible.
if(MSVC)
  target_compile_options(neobae PRIVATE /W0)
else()
  target_compile_options(neobae PRIVATE -w)
endif()
target_compile_features(neobae PUBLIC c_std_99)

if(MINGW)
  # On MinGW use winpthread directly; Threads::Threads can map to -lpthread and cause unused-link warnings.
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_WINPTHREAD_LINK_ITEM})
else()
  find_package(Threads REQUIRED)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} Threads::Threads)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} m)
  if(CMAKE_DL_LIBS)
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${CMAKE_DL_LIBS})
  endif()
endif()

if(BAE_ENABLE_LZMA)
  find_library(LIBLZMA_LIBRARY NAMES liblzma lzma)
  find_path(LIBLZMA_INCLUDE_DIR lzma.h)
  if(LIBLZMA_LIBRARY)
    if(LIBLZMA_INCLUDE_DIR)
      target_include_directories(neobae PRIVATE ${LIBLZMA_INCLUDE_DIR})
    endif()
    if(WIN32)
      target_compile_definitions(neobae PRIVATE LZMA_API_STATIC)
    endif()
    target_link_libraries(neobae ${NEOBAE_DEP_LINK_VISIBILITY} ${NEOBAE_STATIC_DEP_LINK_PREFIX} ${LIBLZMA_LIBRARY} ${NEOBAE_STATIC_DEP_LINK_SUFFIX})
  endif()
endif()

if(BUILD_PLAYBAE)
  if(BAE_PLAYBAE_PLATFORM_AVAILABLE)
    add_executable(playbae neobae/src/playbae/playbae.c)
    if(BAE_ENABLE_FLUIDLITE)
      target_include_directories(playbae PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/include
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src/watcom
      )
    endif()
    target_link_libraries(playbae PRIVATE neobae)
  else()
    message(STATUS "Skipping BUILD_PLAYBAE because BAE_PLATFORM is Dummy or Ansi.")
  endif()
endif()

if(BAE_ENABLE_FLUIDLITE)
  target_include_directories(neobae PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/include
    ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src
    ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src/watcom
  )
endif()

if(BUILD_ZEFIDI)
  if(BAE_ZEFIDI_AVAILABLE)
    set(BAE_EMBED_FONT_EFFECTIVE OFF)
    unset(BAE_EMBED_TTF_FILE_RESOLVED)

    if(BAE_ENABLE_EMBED_FONT)
      if(BAE_EMBED_TTF_FILE STREQUAL "")
        message(STATUS "Disabling zefidi embedded font: BAE_EMBED_TTF_FILE is empty")
      else()
        set(_BAE_EMBED_TTF_CANDIDATE "${BAE_EMBED_TTF_FILE}")
        if(NOT IS_ABSOLUTE "${_BAE_EMBED_TTF_CANDIDATE}")
          set(_BAE_EMBED_TTF_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/${BAE_EMBED_TTF_FILE}")
        endif()
        if(EXISTS "${_BAE_EMBED_TTF_CANDIDATE}")
          set(BAE_EMBED_TTF_FILE_RESOLVED "${_BAE_EMBED_TTF_CANDIDATE}")
        else()
          set(_BAE_EMBED_TTF_ALT "${CMAKE_CURRENT_SOURCE_DIR}/neobae/${BAE_EMBED_TTF_FILE}")
          if(EXISTS "${_BAE_EMBED_TTF_ALT}")
            set(BAE_EMBED_TTF_FILE_RESOLVED "${_BAE_EMBED_TTF_ALT}")
          else()
            message(STATUS "Disabling zefidi embedded font: TTF file not found at '${BAE_EMBED_TTF_FILE}'")
          endif()
          unset(_BAE_EMBED_TTF_ALT)
        endif()
        unset(_BAE_EMBED_TTF_CANDIDATE)
      endif()

      if(BAE_EMBED_TTF_FILE_RESOLVED)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(NOT Python3_Interpreter_FOUND)
          message(STATUS "Disabling zefidi embedded font: Python3 interpreter not found")
        else()
          set(BAE_EMBED_FONT_EFFECTIVE ON)
          message(STATUS "Enabling zefidi embedded font from '${BAE_EMBED_TTF_FILE_RESOLVED}'")
        endif()
      endif()
    endif()

    set(ZEFIDI_SOURCES
      neobae/src/gui/gui_theme.c
      neobae/src/gui/gui_widgets.c
      neobae/src/gui/gui_text.c
      neobae/src/gui/gui_midi_vkbd.c
      neobae/src/gui/gui_export.c
      neobae/src/gui/gui_dialogs.c
      neobae/src/gui/gui_bae.c
      neobae/src/gui/gui_settings.c
      neobae/src/gui/gui_panels.c
      neobae/src/gui/gui_channels.c
      neobae/src/gui/gui_playback_controls.c
      neobae/src/gui/gui_transport.c
      neobae/src/gui/gui_keyboard_panel.c
      neobae/src/gui/gui_status_bank.c
      neobae/src/gui/gui_file_open.c
      neobae/src/gui/gui_main.c
    )

    if(BAE_ENABLE_MIDI_HARDWARE)
      list(APPEND ZEFIDI_SOURCES
        neobae/src/gui/gui_midi_hw.c
        neobae/src/gui/gui_midi_hw_input.c
        neobae/src/gui/gui_midi_hw_output.c
        neobae/src/thirdparty/rtmidi/rtmidi_c.cpp
        neobae/src/thirdparty/rtmidi/RtMidi.cpp
      )
    endif()

    # Always compile the debug console TU. Its body is gated on `_DEBUG == TRUE`.
    # MSVC Debug configs define `_DEBUG` automatically, which enables the calls in
    # gui_main.c; previously the .c was only added for -DDEBUG=ON and then failed to link.
    list(APPEND ZEFIDI_SOURCES neobae/src/gui/gui_debug_console.c)
    if(BAE_ENABLE_KARAOKE)
      list(APPEND ZEFIDI_SOURCES neobae/src/gui/gui_karaoke.c)
    endif()
    if(BAE_ENABLE_PLAYLIST)
      list(APPEND ZEFIDI_SOURCES neobae/src/gui/gui_playlist.c)
    endif()
    if(BAE_ENABLE_BAESCRIPT)
      list(APPEND ZEFIDI_SOURCES neobae/src/gui/gui_script_editor.c)
    endif()
    if((MINGW OR MSVC) AND NEOBAE_RC_ENABLED)
      neobae_configure_rc(zefidi ZEFIDI_VERSION_RC "${VERSION_ZEFIDI_RC_IN}" "zefidi Media Player - Powered by NeoBAE" "zefidi.exe" "1" "neobae.ico")
      list(APPEND ZEFIDI_SOURCES "${ZEFIDI_VERSION_RC}")
    endif()
    if(MINGW OR MSVC)
      add_executable(zefidi WIN32 ${ZEFIDI_SOURCES})
    else()
      add_executable(zefidi ${ZEFIDI_SOURCES})
    endif()

    if(BAE_ENABLE_FLUIDLITE)
    target_include_directories(zefidi PRIVATE 
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/include
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/fluidlite/src/watcom
    )  
    endif()


    if(BAE_EMBED_FONT_EFFECTIVE)
      set(BAE_EMBED_FONT_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
      set(BAE_EMBED_FONT_HEADER "${BAE_EMBED_FONT_HEADER_DIR}/embedded_font.h")
      add_custom_command(
        OUTPUT ${BAE_EMBED_FONT_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BAE_EMBED_FONT_HEADER_DIR}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_font_h.py ${BAE_EMBED_TTF_FILE_RESOLVED} ${BAE_EMBED_FONT_HEADER}
        DEPENDS ${BAE_EMBED_TTF_FILE_RESOLVED} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_font_h.py
        VERBATIM
        COMMENT "Embedding zefidi TTF font into embedded_font.h"
      )
      add_custom_target(zefidi_embedded_font DEPENDS ${BAE_EMBED_FONT_HEADER})
      add_dependencies(zefidi zefidi_embedded_font)
      target_compile_definitions(zefidi PRIVATE EMBED_TTF_FONT=1)
      target_include_directories(zefidi PRIVATE ${BAE_EMBED_FONT_HEADER_DIR})
    endif()

    target_compile_definitions(zefidi PRIVATE _ZEFI_GUI=1)
    # -std=c11 hides POSIX APIs (realpath/readlink/popen/strcasecmp) without this.
    target_compile_definitions(zefidi PRIVATE _DEFAULT_SOURCE=1)
    # gui_midi_hw_input.c uses <stdatomic.h> / _Static_assert (C11).
    # MSVC needs /std:c11 plus /experimental:c11atomics (still experimental).
    set_target_properties(zefidi PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS OFF
    )
    if(MSVC)
      target_compile_options(zefidi PRIVATE /experimental:c11atomics)
    endif()

    if(BAE_ENABLE_MIDI_HARDWARE)
      target_include_directories(zefidi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/rtmidi
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/gui
      )
      target_compile_definitions(zefidi PRIVATE SUPPORT_MIDI_HW=1)
      if(WIN32)
        target_compile_definitions(zefidi PRIVATE __WINDOWS_MM__)
        target_link_libraries(zefidi PRIVATE winmm)
      elseif(APPLE)
        target_compile_definitions(zefidi PRIVATE __MACOSX_CORE__)
        target_link_libraries(zefidi PRIVATE
          "-framework CoreServices"
          "-framework CoreAudio"
          "-framework CoreMIDI"
          "-framework CoreFoundation"
        )
      elseif(UNIX AND BAE_ALSA_LIBRARY)
        target_compile_definitions(zefidi PRIVATE __LINUX_ALSA__)
        target_link_libraries(zefidi PRIVATE ${BAE_ALSA_LIBRARY})
      endif()
    endif()

    target_include_directories(zefidi PRIVATE ${ZEFIDI_SDL_INCLUDE_DIR} ${ZEFIDI_SDL_TTF_INCLUDE_DIR})
    target_link_libraries(zefidi PRIVATE neobae ${ZEFIDI_SDL_LIBRARY} ${ZEFIDI_SDL_TTF_LIBRARY})

    if(NEOBAE_STATIC AND MINGW)
      target_link_libraries(zefidi PRIVATE
        winmm
        version
        setupapi
        imm32
        cfgmgr32
        gdi32
        oleaut32
        comdlg32
        comctl32
        freetype
        png
        brotlidec
        brotlicommon
        harfbuzz
        zs 
      )
    endif()

    if(MINGW OR MSVC)
      add_executable(RegisterFileTypesToZefidi WIN32
        neobae/src/gui/winreg_filetypes.c
      )
      if(NEOBAE_RC_ENABLED)
        neobae_configure_rc(RegisterFileTypesToZefidi WINREG_VERSION_RC "${VERSION_ICO_RC_IN}" "zefidi File Registration Helper" "RegisterFileTypesToZefidi.exe" "1" "BAEFile.ico")
        target_sources(RegisterFileTypesToZefidi PRIVATE "${WINREG_VERSION_RC}")
      endif()
      if(MINGW)
        target_link_libraries(RegisterFileTypesToZefidi PRIVATE
          user32
          gdi32
          advapi32
          comctl32
          shlwapi
          shell32
        )
      endif()
    endif()
  else()
    message(STATUS "Skipping BUILD_ZEFIDI because no usable SDL2/SDL3 + SDL_ttf combination was found.")
  endif()
endif()

if(BUILD_CLITOOLS)
  add_executable(songtool 
    neobae/src/songtool/songtool.c
    neobae/src/mod2rmf/mod2rmf_encoder.c
  )

  target_link_libraries(songtool PRIVATE neobae)

  add_executable(rmfutil
    neobae/src/rmfutil/rmfutil.c
  )
  target_include_directories(rmfutil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common
  )
  target_link_libraries(rmfutil PRIVATE neobae)
  # libm is a separate link on Unix; MSVC has no m.lib and fails looking for it.
  if(UNIX)
    target_link_libraries(rmfutil PRIVATE m)
  endif()

  if (BAE_ENABLE_ADP_SUPPORT)
    add_executable(adp2wav 
      neobae/src/thirdparty/libg722/g722_decode.c
      neobae/src/adp2wav/adp2wav_decode.c
      neobae/src/adp2wav/adp2wav_main.c
    )
    if(UNIX)
      target_link_libraries(adp2wav PRIVATE m)
    endif()
  endif()

  if (BAE_ENABLE_ADX_SUPPORT)
    add_executable(adx2wav
      neobae/src/adx2wav/adx2wav_decode.c
      neobae/src/adx2wav/adx2wav_main.c
    )
    if(UNIX)
      target_link_libraries(adx2wav PRIVATE m)
    endif()
  endif()

  if (BAE_ENABLE_MTHC_SUPPORT)
    add_executable(mthc_decomp
      neobae/src/mthc/mthc_decomp.c
      neobae/src/mthc/mthc_main.c
    )
    target_link_libraries(mthc_decomp PRIVATE neobae)
    if(UNIX)
      target_link_libraries(mthc_decomp PRIVATE m)
    endif()
  endif()


  add_executable(ringtone2mid neobae/src/ringtone2mid/ringtone2mid.c)
  target_link_libraries(ringtone2mid PRIVATE neobae)

  add_executable(rmiutil neobae/src/rmiutil/rmiutil.c)
  target_link_libraries(rmiutil PRIVATE)

  add_executable(bankrecomp neobae/src/bankrecomp/bankrecomp_api.c)
  target_link_libraries(bankrecomp PRIVATE neobae)

  add_executable(sf2_hsb
    neobae/src/sf2-hsb/sf2_hsb_main.c
    neobae/src/sf2-hsb/sf2_hsb_converter.c
    neobae/src/sf2-hsb/sf2_parser.c
    neobae/src/sf2-hsb/hsb_writer.c
  )
  target_include_directories(sf2_hsb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/sf2-hsb)
  target_link_libraries(sf2_hsb PRIVATE neobae)
  set_target_properties(sf2_hsb PROPERTIES OUTPUT_NAME sf2-hsb)

  if(BAE_ENABLE_XMF_SUPPORT)
    add_executable(xmfutil
      neobae/src/xmfutil/xmfutil.c
      neobae/src/BAE_Source/Common/cast5mini.c
      neobae/src/BAE_Source/Common/sha1mini.c
    )
    target_include_directories(xmfutil PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common
    )
    if(NEOBAE_ZLIB_TARGET)
      target_link_libraries(xmfutil PRIVATE ${NEOBAE_ZLIB_TARGET})
    else()
      message(FATAL_ERROR "xmfutil requires zlib, but NEOBAE_ZLIB_TARGET was not configured.")
    endif()
    if(NEOBAE_STATIC AND MINGW)
      add_link_options(-static -static-libgcc $<$<LINK_LANGUAGE:CXX>:-static-libstdc++>)
    endif()
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/include/xmp.h" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/src/lite/lite-player.c")
    add_executable(mod2rmf
      neobae/src/mod2rmf/mod2rmf_main.c
      neobae/src/mod2rmf/mod2rmf_common.c
      neobae/src/mod2rmf/mod2rmf_encoder.c
      neobae/src/mod2rmf/mod2rmf_resampler.c
      neobae/src/mod2rmf/mod2rmf_rmfcreat.c
      neobae/src/mod2rmf/mod2rmf_song.c
      neobae/src/thirdparty/libxmp/src/lite/lite-virtual.c
      neobae/src/thirdparty/libxmp/src/lite/lite-format.c
      neobae/src/thirdparty/libxmp/src/lite/lite-period.c
      neobae/src/thirdparty/libxmp/src/lite/lite-player.c
      neobae/src/thirdparty/libxmp/src/lite/lite-read_event.c
      neobae/src/thirdparty/libxmp/src/lite/lite-misc.c
      neobae/src/thirdparty/libxmp/src/lite/lite-dataio.c
      neobae/src/thirdparty/libxmp/src/lite/lite-lfo.c
      neobae/src/thirdparty/libxmp/src/lite/lite-scan.c
      neobae/src/thirdparty/libxmp/src/lite/lite-control.c
      neobae/src/thirdparty/libxmp/src/lite/lite-filter.c
      neobae/src/thirdparty/libxmp/src/lite/lite-effects.c
      neobae/src/thirdparty/libxmp/src/lite/lite-mixer.c
      neobae/src/thirdparty/libxmp/src/lite/lite-mix_all.c
      neobae/src/thirdparty/libxmp/src/lite/lite-load_helpers.c
      neobae/src/thirdparty/libxmp/src/lite/lite-load.c
      neobae/src/thirdparty/libxmp/src/lite/lite-filetype.c
      neobae/src/thirdparty/libxmp/src/lite/lite-hio.c
      neobae/src/thirdparty/libxmp/src/lite/lite-smix.c
      neobae/src/thirdparty/libxmp/src/lite/lite-memio.c
      neobae/src/thirdparty/libxmp/src/lite/lite-rng.c
      neobae/src/thirdparty/libxmp/src/lite/lite-win32.c
      neobae/src/thirdparty/libxmp/src/lite/lite-flow.c
      neobae/src/thirdparty/libxmp/src/lite/lite-common.c
      neobae/src/thirdparty/libxmp/src/lite/lite-itsex.c
      neobae/src/thirdparty/libxmp/src/lite/lite-sample.c
      neobae/src/thirdparty/libxmp/src/lite/lite-xm_load.c
      neobae/src/thirdparty/libxmp/src/lite/lite-mod_load.c
      neobae/src/thirdparty/libxmp/src/lite/lite-s3m_load.c
      neobae/src/thirdparty/libxmp/src/lite/lite-it_load.c
    )
    target_include_directories(mod2rmf PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/mod2rmf
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/src
      ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/include
    )
    target_compile_definitions(mod2rmf PRIVATE LIBXMP_STATIC=1 LIBXMP_CORE_PLAYER=1 LIBXMP_BUILD=1)
    target_link_libraries(mod2rmf PRIVATE neobae)
  else()
    message(STATUS "Skipping mod2rmf (bundled libxmp-lite sources not found).")
  endif()
endif()

if(BUILD_NBEDITOR)
  if(NOT BAE_SDL3_INCLUDE_DIR)
    find_path(BAE_SDL3_INCLUDE_DIR SDL3/SDL.h)
  endif()
  if(NOT BAE_SDL3_LIBRARY)
    find_library(BAE_SDL3_LIBRARY NAMES SDL3)
  endif()

  set(NBEDITOR_LIBXMP_LITE_AVAILABLE OFF)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/include/xmp.h" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/src/lite/lite-player.c")
    set(NBEDITOR_LIBXMP_LITE_AVAILABLE ON)
  endif()

  # nbeditor builds against SDL3 or SDL2 (neobae/src/nbeditor/nbeditor_sdl_compat.h).
  # Follow the platform backend so only one SDL ends up in the process.
  set(NBEDITOR_SDL_VERSION "")
  if(BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY AND NOT BAE_USING_SDL2 AND NOT USE_SDL2)
    set(NBEDITOR_SDL_VERSION "SDL3")
    set(NBEDITOR_SDL_INCLUDE_DIRS ${BAE_SDL3_INCLUDE_DIR})
    set(NBEDITOR_SDL_LIBRARY ${BAE_SDL3_LIBRARY})
  elseif(BAE_SDL2_INCLUDE_DIR AND BAE_SDL2_LIBRARY)
    set(NBEDITOR_SDL_VERSION "SDL2")
    # imgui_impl_sdl2.cpp includes <SDL.h> directly.
    set(NBEDITOR_SDL_INCLUDE_DIRS ${BAE_SDL2_INCLUDE_DIR} ${BAE_SDL2_INCLUDE_DIR}/SDL2)
    set(NBEDITOR_SDL_LIBRARY ${BAE_SDL2_LIBRARY})
  elseif(BAE_SDL3_INCLUDE_DIR AND BAE_SDL3_LIBRARY)
    set(NBEDITOR_SDL_VERSION "SDL3")
    set(NBEDITOR_SDL_INCLUDE_DIRS ${BAE_SDL3_INCLUDE_DIR})
    set(NBEDITOR_SDL_LIBRARY ${BAE_SDL3_LIBRARY})
  endif()

  if(NBEDITOR_SDL_VERSION)
    set(NBEDITOR_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/nbeditor/nbeditor_main.cpp)
    set(NBEDITOR_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/imgui)

    if(EXISTS ${NBEDITOR_MAIN} AND EXISTS ${NBEDITOR_IMGUI_DIR}/imgui.cpp)
      set(NBEDITOR_SOURCES
        ${NBEDITOR_MAIN}
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/nbeditor/nbeditor_crash.cpp
        ${NBEDITOR_IMGUI_DIR}/imgui.cpp
        ${NBEDITOR_IMGUI_DIR}/imgui_draw.cpp
        ${NBEDITOR_IMGUI_DIR}/imgui_tables.cpp
        ${NBEDITOR_IMGUI_DIR}/imgui_widgets.cpp
      )
      if(NBEDITOR_SDL_VERSION STREQUAL "SDL2")
        list(APPEND NBEDITOR_SOURCES
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/nbeditor/nbeditor_sdl_compat_dialog.cpp
          ${NBEDITOR_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
          ${NBEDITOR_IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
        )
      else()
        list(APPEND NBEDITOR_SOURCES
          ${NBEDITOR_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
          ${NBEDITOR_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        )
      endif()
      if(NBEDITOR_LIBXMP_LITE_AVAILABLE)
        list(APPEND NBEDITOR_SOURCES
          neobae/src/mod2rmf/mod2rmf_common.c
          neobae/src/mod2rmf/mod2rmf_encoder.c
          neobae/src/mod2rmf/mod2rmf_resampler.c
          neobae/src/mod2rmf/mod2rmf_rmfcreat.c
          neobae/src/mod2rmf/mod2rmf_song.c
          neobae/src/thirdparty/libxmp/src/lite/lite-virtual.c
          neobae/src/thirdparty/libxmp/src/lite/lite-format.c
          neobae/src/thirdparty/libxmp/src/lite/lite-period.c
          neobae/src/thirdparty/libxmp/src/lite/lite-player.c
          neobae/src/thirdparty/libxmp/src/lite/lite-read_event.c
          neobae/src/thirdparty/libxmp/src/lite/lite-misc.c
          neobae/src/thirdparty/libxmp/src/lite/lite-dataio.c
          neobae/src/thirdparty/libxmp/src/lite/lite-lfo.c
          neobae/src/thirdparty/libxmp/src/lite/lite-scan.c
          neobae/src/thirdparty/libxmp/src/lite/lite-control.c
          neobae/src/thirdparty/libxmp/src/lite/lite-filter.c
          neobae/src/thirdparty/libxmp/src/lite/lite-effects.c
          neobae/src/thirdparty/libxmp/src/lite/lite-mixer.c
          neobae/src/thirdparty/libxmp/src/lite/lite-mix_all.c
          neobae/src/thirdparty/libxmp/src/lite/lite-load_helpers.c
          neobae/src/thirdparty/libxmp/src/lite/lite-load.c
          neobae/src/thirdparty/libxmp/src/lite/lite-filetype.c
          neobae/src/thirdparty/libxmp/src/lite/lite-hio.c
          neobae/src/thirdparty/libxmp/src/lite/lite-smix.c
          neobae/src/thirdparty/libxmp/src/lite/lite-memio.c
          neobae/src/thirdparty/libxmp/src/lite/lite-rng.c
          neobae/src/thirdparty/libxmp/src/lite/lite-win32.c
          neobae/src/thirdparty/libxmp/src/lite/lite-flow.c
          neobae/src/thirdparty/libxmp/src/lite/lite-common.c
          neobae/src/thirdparty/libxmp/src/lite/lite-itsex.c
          neobae/src/thirdparty/libxmp/src/lite/lite-sample.c
          neobae/src/thirdparty/libxmp/src/lite/lite-xm_load.c
          neobae/src/thirdparty/libxmp/src/lite/lite-mod_load.c
          neobae/src/thirdparty/libxmp/src/lite/lite-s3m_load.c
          neobae/src/thirdparty/libxmp/src/lite/lite-it_load.c
        )
      endif()
      if((MINGW OR MSVC) AND NEOBAE_RC_ENABLED)
        neobae_configure_rc(nbeditor NBEDITOR_VERSION_RC "${VERSION_ICO_RC_IN}" "NeoBAE Editor" "nbeditor.exe" "1" "BAEStudio.ico")
        list(APPEND NBEDITOR_SOURCES "${NBEDITOR_VERSION_RC}")
      endif()

      if(BAE_ENABLE_MIDI_HARDWARE)
        list(APPEND NBEDITOR_SOURCES
          neobae/src/gui/gui_midi_hw_input.c
          neobae/src/thirdparty/rtmidi/rtmidi_c.cpp
          neobae/src/thirdparty/rtmidi/RtMidi.cpp
        )
      endif()

      set(BAE_NBEDITOR_LIB_HYPHEN OFF)
      if(BAE_ENABLE_LIB_HYPHEN AND
         EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/hyphen/hyphen.c" AND
         EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/hyphen/hnjalloc.c")
        set(BAE_NBEDITOR_LIB_HYPHEN ON)
        list(APPEND NBEDITOR_SOURCES
          neobae/src/thirdparty/hyphen/hyphen.c
          neobae/src/thirdparty/hyphen/hnjalloc.c
        )
      endif()

      add_executable(nbeditor ${NBEDITOR_SOURCES})
      target_include_directories(nbeditor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/nbeditor
        ${NBEDITOR_IMGUI_DIR}
        ${NBEDITOR_IMGUI_DIR}/backends
        ${NBEDITOR_SDL_INCLUDE_DIRS}
      )
      if(BAE_NBEDITOR_LIB_HYPHEN)
        target_include_directories(nbeditor PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/hyphen
        )
        target_compile_definitions(nbeditor PRIVATE USE_LIB_HYPHEN=1)
        if(WIN32)
          target_link_libraries(nbeditor PRIVATE winhttp)
        else()
          find_package(CURL)
          if(CURL_FOUND)
            target_compile_definitions(nbeditor PRIVATE NBEDITOR_HAVE_LIBCURL=1)
            target_link_libraries(nbeditor PRIVATE CURL::libcurl)
          else()
            message(STATUS "nbeditor: libcurl not found; hyphen downloads disabled")
          endif()
        endif()
        # Windows: data lives in nbeditor/ next to nbeditor.exe.
        # Linux: binary is also named nbeditor, so bundle dir would clash — use
        # system hyphen packages + config downloads; catalog installs to share/.
        if(WIN32)
          set(BAE_NBEDITOR_DATA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/neobae/data/nbeditor")
          set(BAE_NBEDITOR_DATA_DST "$<TARGET_FILE_DIR:nbeditor>/nbeditor")
          add_custom_command(TARGET nbeditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BAE_NBEDITOR_DATA_DST}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${BAE_NBEDITOR_DATA_SRC}/hyph_catalog.json"
              "${BAE_NBEDITOR_DATA_SRC}/README.md"
              "${BAE_NBEDITOR_DATA_SRC}/hyph_en_US.dic"
              "${BAE_NBEDITOR_DATA_SRC}/hyph_ru_RU.dic"
              "${BAE_NBEDITOR_DATA_DST}/"
            COMMENT "Copying nbeditor hyphen data (Windows)"
          )
        endif()
        message(STATUS "nbeditor: libhyphen syllable support enabled")
      else()
        target_compile_definitions(nbeditor PRIVATE USE_LIB_HYPHEN=0)
      endif()
      if(NBEDITOR_LIBXMP_LITE_AVAILABLE)
        target_include_directories(nbeditor PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/mod2rmf
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/src
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/libxmp/include
        )
      endif()
      target_compile_definitions(nbeditor PRIVATE
        NBEDITOR=1
        NBEDITOR_MVP=1
        NBEDITOR_PLAYER_WINDOW=1
        NBEDITOR_SAMPLE_LIST_WINDOW=1
        NBEDITOR_INSTRUMENT_LIST_WINDOW=1
        SUPPORT_MIDI_HW=$<IF:$<BOOL:${BAE_ENABLE_MIDI_HARDWARE}>,1,0>
      )
      if(NBEDITOR_LIBXMP_LITE_AVAILABLE)
        target_compile_definitions(nbeditor PRIVATE LIBXMP_STATIC=1 LIBXMP_CORE_PLAYER=1 LIBXMP_BUILD=1)
      endif()
      target_link_libraries(nbeditor PRIVATE neobae ${NBEDITOR_SDL_LIBRARY})
      if(NBEDITOR_SDL_VERSION STREQUAL "SDL2")
        target_compile_definitions(nbeditor PRIVATE NBEDITOR_USE_SDL2=1)
        find_package(Threads QUIET)
        if(TARGET Threads::Threads)
          target_link_libraries(nbeditor PRIVATE Threads::Threads)
        endif()
        if(WIN32)
          target_link_libraries(nbeditor PRIVATE comdlg32)
        endif()
      endif()
      # std::clamp and other C++17 library pieces used by nbeditor_main.cpp.
      set_target_properties(nbeditor PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
      )
      # gui_midi_hw_input.c uses <stdatomic.h> / _Static_assert (C11).
      if(BAE_ENABLE_MIDI_HARDWARE)
        set_source_files_properties(
          neobae/src/gui/gui_midi_hw_input.c
          PROPERTIES
            LANGUAGE C
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
        )
        target_include_directories(nbeditor PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/thirdparty/rtmidi
          ${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/gui
        )
        if(WIN32)
          target_compile_definitions(nbeditor PRIVATE __WINDOWS_MM__)
        elseif(APPLE)
          target_compile_definitions(nbeditor PRIVATE __MACOSX_CORE__)
          target_link_libraries(nbeditor PRIVATE
            "-framework CoreServices"
            "-framework CoreAudio"
            "-framework CoreMIDI"
            "-framework CoreFoundation"
          )
        elseif(UNIX AND BAE_ALSA_LIBRARY)
          target_compile_definitions(nbeditor PRIVATE __LINUX_ALSA__)
          target_link_libraries(nbeditor PRIVATE ${BAE_ALSA_LIBRARY})
        endif()
        if(MSVC)
          target_compile_options(nbeditor PRIVATE /experimental:c11atomics)
        endif()
        message(STATUS "nbeditor: hardware MIDI input (RtMidi) enabled.")
      else()
        message(STATUS "nbeditor: hardware MIDI input disabled.")
      endif()
      if(WIN32)
        target_link_libraries(nbeditor PRIVATE imm32 winmm setupapi cfgmgr32 version dbghelp)
      endif()
      if(UNIX AND NOT APPLE)
        target_link_options(nbeditor PRIVATE -rdynamic)
      endif()
      if(NBEDITOR_LIBXMP_LITE_AVAILABLE)
        message(STATUS "nbeditor: libxmp-lite module loading enabled.")
      else()
        message(STATUS "nbeditor: libxmp-lite not found, module loading disabled.")
      endif()

      # Embed Liberation Sans Regular + Italic for UI (no system font dependency).
      set(BAE_NBEDITOR_EMBED_FONTS_EFFECTIVE OFF)
      if(EXISTS "${BAE_NBEDITOR_FONT_REGULAR}" AND EXISTS "${BAE_NBEDITOR_FONT_ITALIC}")
        if(NOT Python3_Interpreter_FOUND)
          find_package(Python3 COMPONENTS Interpreter QUIET)
        endif()
        if(Python3_Interpreter_FOUND)
          set(BAE_NBEDITOR_EMBED_FONTS_EFFECTIVE ON)
        else()
          message(STATUS "nbeditor: Python3 not found; skipping Liberation font embed")
        endif()
      else()
        message(STATUS "nbeditor: Liberation TTF files missing; skipping font embed")
      endif()
      if(BAE_NBEDITOR_EMBED_FONTS_EFFECTIVE)
        set(BAE_NBEDITOR_FONT_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set(BAE_NBEDITOR_FONT_HEADER "${BAE_NBEDITOR_FONT_HEADER_DIR}/nbeditor_embedded_fonts.h")
        add_custom_command(
          OUTPUT ${BAE_NBEDITOR_FONT_HEADER}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${BAE_NBEDITOR_FONT_HEADER_DIR}
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_font_h.py
                  ${BAE_NBEDITOR_FONT_HEADER}
                  --font ${BAE_NBEDITOR_FONT_REGULAR} liberation_sans_regular
                  --font ${BAE_NBEDITOR_FONT_ITALIC} liberation_sans_italic
          DEPENDS
            ${BAE_NBEDITOR_FONT_REGULAR}
            ${BAE_NBEDITOR_FONT_ITALIC}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_embedded_font_h.py
          VERBATIM
          COMMENT "Embedding Liberation Sans fonts for nbeditor"
        )
        add_custom_target(nbeditor_embedded_fonts DEPENDS ${BAE_NBEDITOR_FONT_HEADER})
        add_dependencies(nbeditor nbeditor_embedded_fonts)
        target_compile_definitions(nbeditor PRIVATE NBEDITOR_EMBED_FONTS=1)
        target_include_directories(nbeditor PRIVATE ${BAE_NBEDITOR_FONT_HEADER_DIR})
        message(STATUS "nbeditor: embedding Liberation Sans Regular + Italic")
      endif()

      message(STATUS "Building nbeditor with ${NBEDITOR_SDL_VERSION} + Dear ImGui.")
    else()
      message(STATUS "Skipping BUILD_NBEDITOR (nbeditor/imgui sources not present in workspace).")
    endif()
  else()
    message(STATUS "Skipping BUILD_NBEDITOR (SDL2/SDL3 headers/library not found).")
  endif()
endif()

if((MINGW OR MSVC) AND NEOBAE_RC_ENABLED)
    if(TARGET songtool)
      neobae_configure_rc(songtool SONGTOOL_VERSION_RC "${VERSION_RC_IN}" "NeoBAE Songtool CLI" "songtool.exe" "1" NO)
      target_sources(songtool PRIVATE "${SONGTOOL_VERSION_RC}")
      if(MINGW)
        target_link_options(songtool PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(songtool PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET rmfutil)
      neobae_configure_rc(rmfutil RMFUTIL_VERSION_RC "${VERSION_RC_IN}" "NeoBAE RMF Utility" "rmfutil.exe" "1" NO)
      target_sources(rmfutil PRIVATE "${RMFUTIL_VERSION_RC}")
      if(MINGW)
        target_link_options(rmfutil PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(rmfutil PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET adp2wav)
      neobae_configure_rc(adp2wav ADP2WAV_VERSION_RC "${VERSION_RC_IN}" "NeoBAE ADP to WAV Converter" "adp2wav.exe" "1" NO)
      target_sources(adp2wav PRIVATE "${ADP2WAV_VERSION_RC}")
      if(MINGW)
        target_link_options(adp2wav PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(adp2wav PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET adx2wav)
     neobae_configure_rc(adx2wav ADX2WAV_VERSION_RC "${VERSION_RC_IN}" "NeoBAE ADX to WAV Converter" "adx2wav.exe" "1" NO)
      target_sources(adx2wav PRIVATE "${ADX2WAV_VERSION_RC}")
      if(MINGW)
        target_link_options(adx2wav PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(adx2wav PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET mthc_decomp)
      neobae_configure_rc(mthc_decomp MTHC_DECOMP_VERSION_RC "${VERSION_RC_IN}" "NeoBAE MTHC Decompressor" "mthc_decomp.exe" "1" NO)
      target_sources(mthc_decomp PRIVATE "${MTHC_DECOMP_VERSION_RC}")
      if(MINGW)
        target_link_options(mthc_decomp PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(mthc_decomp PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET ringtone2mid)
      neobae_configure_rc(ringtone2mid RINGTONE2MID_VERSION_RC "${VERSION_RC_IN}" "NeoBAE Ringtone to MIDI Converter" "ringtone2mid.exe" "1" NO)
      target_sources(ringtone2mid PRIVATE "${RINGTONE2MID_VERSION_RC}")
      if(MINGW)
        target_link_options(ringtone2mid PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(ringtone2mid PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET rmiutil)
      neobae_configure_rc(rmiutil RMIUTIL_VERSION_RC "${VERSION_RC_IN}" "NeoBAE RMI Utility" "rmiutil.exe" "1" NO)
      target_sources(rmiutil PRIVATE "${RMIUTIL_VERSION_RC}")
      if(MINGW)
        target_link_options(rmiutil PRIVATE "-Wl,--subsystem,console")
      endif()
       if(BAE_USING_SDL2)
        target_link_libraries(rmiutil PRIVATE winmm setupapi imm32 version)
      endif()
   endif()

    if(TARGET bankrecomp)
      neobae_configure_rc(bankrecomp BANKRECOMP_VERSION_RC "${VERSION_RC_IN}" "NeoBAE Bank Recompiler" "bankrecomp.exe" "1" NO)
      target_sources(bankrecomp PRIVATE "${BANKRECOMP_VERSION_RC}")
      if(MINGW)
        target_link_options(bankrecomp PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(bankrecomp PRIVATE winmm setupapi imm32 version)
      endif()
   endif()

    if(TARGET sf2_hsb)
      neobae_configure_rc(sf2_hsb SF2HSB_VERSION_RC "${VERSION_RC_IN}" "NeoBAE SF2 to HSB Converter" "sf2-hsb.exe" "1" NO)
      target_sources(sf2_hsb PRIVATE "${SF2HSB_VERSION_RC}")
      if(MINGW)
        target_link_options(sf2_hsb PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(sf2_hsb PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET xmfutil)
      neobae_configure_rc(xmfutil XMFUTIL_VERSION_RC "${VERSION_RC_IN}" "NeoBAE XMF Utility" "xmfutil.exe" "1" NO)
      target_sources(xmfutil PRIVATE "${XMFUTIL_VERSION_RC}")
      if(MINGW)
        target_link_options(xmfutil PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(xmfutil PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET mod2rmf)
      neobae_configure_rc(mod2rmf MOD2RMF_VERSION_RC "${VERSION_RC_IN}" "NeoBAE MOD to RMF Converter" "mod2rmf.exe" "1" NO)
      target_sources(mod2rmf PRIVATE "${MOD2RMF_VERSION_RC}")
      if(MINGW)
        target_link_options(mod2rmf PRIVATE "-Wl,--subsystem,console")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(mod2rmf PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET playbae)
     neobae_configure_rc(playbae PLAYBAE_VERSION_RC "${VERSION_RC_IN}" "NeoBAE CLI Player" "playbae.exe" "1" NO)
     target_sources(playbae PRIVATE "${PLAYBAE_VERSION_RC}")
     if(MINGW)
       target_link_options(playbae PRIVATE "-Wl,--subsystem,console")
     endif()
      if(BAE_USING_SDL2)
        target_link_libraries(playbae PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET zefidi)
      if(MINGW)
        target_link_options(zefidi PRIVATE "-Wl,--subsystem,windows")
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(zefidi PRIVATE winmm setupapi imm32 version)
      endif()
    endif()

    if(TARGET nbeditor)
      if(MINGW)
        if(DEBUG)
          target_link_options(nbeditor PRIVATE "-Wl,--subsystem,console")
        else()
          target_link_options(nbeditor PRIVATE "-Wl,--subsystem,windows")
        endif()
      endif()
      if(BAE_USING_SDL2)
        target_link_libraries(nbeditor PRIVATE winmm setupapi imm32 version)
      endif()
    endif() 
endif()

if (BAE_USING_SDL2)
  if (TARGET songtool)
    target_link_libraries(songtool PRIVATE SDL2main SDL2)
  endif()
  if (TARGET rmfutil)
    target_link_libraries(rmfutil PRIVATE SDL2main SDL2)
  endif()
  if (TARGET adp2wav)
    target_link_libraries(adp2wav PRIVATE SDL2main SDL2)
  endif()
  if (TARGET adx2wav)
    target_link_libraries(adx2wav PRIVATE SDL2main SDL2)
  endif()
  if (TARGET mthc_decomp)
    target_link_libraries(mthc_decomp PRIVATE SDL2main SDL2)
  endif()
  if (TARGET ringtone2mid)
    target_link_libraries(ringtone2mid PRIVATE SDL2main SDL2)
  endif()
  if (TARGET rmiutil)
    target_link_libraries(rmiutil PRIVATE SDL2main SDL2)
  endif()
  if (TARGET bankrecomp)
    target_link_libraries(bankrecomp PRIVATE SDL2main SDL2)
  endif()
  if (TARGET sf2_hsb)
    target_link_libraries(sf2_hsb PRIVATE SDL2main SDL2)
  endif()
  if (TARGET xmfutil)
    target_link_libraries(xmfutil PRIVATE SDL2main SDL2)
  endif()
  if (TARGET mod2rmf)
    target_link_libraries(mod2rmf PRIVATE SDL2main SDL2)
  endif()
  if (TARGET playbae)
    target_link_libraries(playbae PRIVATE SDL2main SDL2)
  endif()
  if (TARGET zefidi)
    target_link_libraries(zefidi PRIVATE SDL2main SDL2)
  endif()
  if (TARGET nbeditor)
    target_link_libraries(nbeditor PRIVATE SDL2main SDL2)
  endif()
endif()

bae_apply_compile_definitions(neobae)

if(TARGET zefidi)
  bae_apply_compile_definitions(zefidi)
endif()

if(TARGET playbae)
  bae_apply_compile_definitions(playbae)
endif()

include(CTest)
if(BUILD_TESTING)
  include(cmake/cmake_tests.cmake)
endif()

# Collect enabled tool/app targets for install / CPack.
set(NEOBAE_TOOL_TARGETS "")
foreach(_neobae_tool
    playbae
    zefidi
    RegisterFileTypesToZefidi
    songtool
    rmfutil
    adp2wav
    adx2wav
    mthc_decomp
    ringtone2mid
    rmiutil
    bankrecomp
    sf2_hsb
    xmfutil
    mod2rmf
    nbeditor
  )
  if(TARGET ${_neobae_tool})
    list(APPEND NEOBAE_TOOL_TARGETS ${_neobae_tool})
    # With a shared libneobae, libm is no longer pulled in transitively.
    if(UNIX AND NOT APPLE)
      target_link_libraries(${_neobae_tool} PRIVATE m)
    endif()
  endif()
endforeach()
unset(_neobae_tool)

# Emit sidecar PDBs next to linked images when BUILD_DEBUG_INFO is on.
# Compile flags are applied globally above; this only names/places the PDB.
function(neobae_emit_debug_info target)
  if(NOT BUILD_DEBUG_INFO)
    return()
  endif()
  if(NOT TARGET ${target})
    return()
  endif()
  get_target_property(_neobae_dbg_type ${target} TYPE)
  if(NOT _neobae_dbg_type STREQUAL "EXECUTABLE" AND
     NOT _neobae_dbg_type STREQUAL "SHARED_LIBRARY")
    return()
  endif()
  if(MSVC)
    set_target_properties(${target} PROPERTIES
      PDB_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
  elseif(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(${target} PRIVATE
      "-Wl,--pdb=$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.pdb")
  endif()
endfunction()

neobae_emit_debug_info(neobae)
foreach(_neobae_dbg_target IN LISTS NEOBAE_TOOL_TARGETS)
  neobae_emit_debug_info(${_neobae_dbg_target})
endforeach()
unset(_neobae_dbg_target)

include(cmake/NeoBAEInstall.cmake)
include(cmake/NeoBAECpack.cmake)