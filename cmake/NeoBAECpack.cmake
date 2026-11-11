# CPack configuration for NeoBAE.
# Windows/MinGW: suite + sdk ZIP archives.
# Linux: optional DEB packages via `cpack -G DEB` on the builder's distro.

set(CPACK_PACKAGE_NAME "neobae")
set(CPACK_PACKAGE_VENDOR "zefie")
set(CPACK_PACKAGE_CONTACT "zefie")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NeoBAE audio engine and tools")
set(CPACK_PACKAGE_DESCRIPTION "Beatnik Audio Engine revival (NeoBAE): shared library, development headers, and companion tools.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/zefie/NeoBAE")
set(CPACK_PACKAGE_VERSION "${BAE_VERSION}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
if(BUILD_DEBUG_INFO)
  set(CPACK_STRIP_FILES OFF)
else()
  set(CPACK_STRIP_FILES ON)
endif()

# Sanitize version for Debian package metadata (must start with a digit).
string(REGEX REPLACE "[^A-Za-z0-9.+~-]" "-" CPACK_DEBIAN_PACKAGE_VERSION "${BAE_VERSION}")
if(CPACK_DEBIAN_PACKAGE_VERSION STREQUAL "")
  set(CPACK_DEBIAN_PACKAGE_VERSION "0.0.0")
elseif(NOT CPACK_DEBIAN_PACKAGE_VERSION MATCHES "^[0-9]")
  set(CPACK_DEBIAN_PACKAGE_VERSION "0.${CPACK_DEBIAN_PACKAGE_VERSION}")
endif()
# Keep CPACK_PACKAGE_VERSION aligned for DEB control metadata.
if(UNIX AND NOT APPLE)
  set(CPACK_PACKAGE_VERSION "${CPACK_DEBIAN_PACKAGE_VERSION}")
endif()

# Map CMAKE_SYSTEM_PROCESSOR / compiler triple to release arch labels.
set(_neobae_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(MINGW OR CMAKE_C_COMPILER MATCHES "w64-mingw32")
  if(CMAKE_C_COMPILER MATCHES "aarch64")
    set(_neobae_pkg_arch "aarch64")
  elseif(CMAKE_C_COMPILER MATCHES "armv7|arm-")
    set(_neobae_pkg_arch "armv7")
  elseif(CMAKE_C_COMPILER MATCHES "x86_64")
    set(_neobae_pkg_arch "x86_64")
  elseif(CMAKE_C_COMPILER MATCHES "i686|i386")
    set(_neobae_pkg_arch "i686")
  endif()
else()
  if(_neobae_pkg_arch MATCHES "amd64|x86_64|AMD64")
    set(_neobae_pkg_arch "x86_64")
  elseif(_neobae_pkg_arch MATCHES "aarch64|arm64")
    set(_neobae_pkg_arch "aarch64")
  elseif(_neobae_pkg_arch MATCHES "armv7|armhf")
    set(_neobae_pkg_arch "armv7")
  elseif(_neobae_pkg_arch MATCHES "i686|i386")
    set(_neobae_pkg_arch "i686")
  endif()
endif()
set(NEOBAE_CPACK_ARCH "${_neobae_pkg_arch}")
unset(_neobae_pkg_arch)

set(CPACK_COMPONENTS_ALL Runtime Tools Development)

if(WIN32)
  # Suite ZIP = Runtime+Tools; SDK ZIP = Development
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
  set(CPACK_COMPONENT_RUNTIME_GROUP "suite")
  set(CPACK_COMPONENT_TOOLS_GROUP "suite")
  set(CPACK_COMPONENT_DEVELOPMENT_GROUP "sdk")
  set(CPACK_ARCHIVE_SUITE_FILE_NAME "neobae-suite_windows_${NEOBAE_CPACK_ARCH}_${BAE_VERSION}")
  set(CPACK_ARCHIVE_SDK_FILE_NAME "neobae-sdk_windows_${NEOBAE_CPACK_ARCH}_${BAE_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "neobae-suite_windows_${NEOBAE_CPACK_ARCH}_${BAE_VERSION}")
elseif(UNIX AND NOT APPLE)
  # One .deb per component; Depends reflect the builder's distro via shlibdeps.
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_GROUPING IGNORE)
  set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
  # Derive the DEB architecture from the target (not the host dpkg) so cross builds are labelled correctly.
  if(NEOBAE_CPACK_ARCH STREQUAL "x86_64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
  elseif(NEOBAE_CPACK_ARCH STREQUAL "aarch64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
  elseif(NEOBAE_CPACK_ARCH STREQUAL "armv7")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
  elseif(NEOBAE_CPACK_ARCH STREQUAL "i686")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
  endif()
  set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
  set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)
  # Help dpkg-shlibdeps find freshly built libneobae when packaging Tools.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_BINARY_DIR}/bin"
    "${CMAKE_BINARY_DIR}/lib"
  )

  set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "libneobae")
  set(CPACK_DEBIAN_TOOLS_PACKAGE_NAME "neobae-tools")
  set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "libneobae-dev")

  set(CPACK_DEBIAN_RUNTIME_FILE_NAME "DEB-DEFAULT")
  set(CPACK_DEBIAN_TOOLS_FILE_NAME "DEB-DEFAULT")
  set(CPACK_DEBIAN_DEVELOPMENT_FILE_NAME "DEB-DEFAULT")

  set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)
  # Tools use shlibdeps for SDL etc. (libneobae is found via PRIVATE_DIRS) and also depend on it explicitly.
  set(CPACK_DEBIAN_TOOLS_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SHLIBDEPS OFF)
  set(CPACK_DEBIAN_TOOLS_PACKAGE_DEPENDS "libneobae")
  set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS "libneobae")
  # Optional system hyphenation dictionaries for nbeditor syllable Apply-text.
  set(CPACK_DEBIAN_TOOLS_PACKAGE_SUGGESTS "hyphen-en-us, hyphen-ru")
  # File-open/save dialogs: SDL2 builds shell out to a chooser, and SDL3 falls back to
  # zenity when no desktop portal is present. zenity is the lightest option.
  set(CPACK_DEBIAN_TOOLS_PACKAGE_RECOMMENDS "zenity")

  set(CPACK_DEBIAN_RUNTIME_DESCRIPTION "NeoBAE shared library")
  set(CPACK_DEBIAN_TOOLS_DESCRIPTION "NeoBAE command-line and GUI tools")
  set(CPACK_DEBIAN_DEVELOPMENT_DESCRIPTION "NeoBAE headers and CMake package files")
else()
  # macOS / other: no default generator; install via cmake --install.
  set(CPACK_GENERATOR "")
endif()

include(CPack)

if(WIN32 AND COMMAND cpack_add_component_group)
  cpack_add_component_group(suite DISPLAY_NAME "NeoBAE Suite"
    DESCRIPTION "Runtime library and tools")
  cpack_add_component_group(sdk DISPLAY_NAME "NeoBAE SDK"
    DESCRIPTION "Headers, import libraries, and CMake package")
  cpack_add_component(Runtime DISPLAY_NAME "Runtime" GROUP suite)
  cpack_add_component(Tools DISPLAY_NAME "Tools" GROUP suite)
  cpack_add_component(Development DISPLAY_NAME "Development" GROUP sdk DEPENDS Runtime)
elseif(COMMAND cpack_add_component)
  cpack_add_component(Runtime DISPLAY_NAME "Runtime")
  cpack_add_component(Tools DISPLAY_NAME "Tools" DEPENDS Runtime)
  cpack_add_component(Development DISPLAY_NAME "Development" DEPENDS Runtime)
endif()
