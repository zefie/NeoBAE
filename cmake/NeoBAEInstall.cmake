# Install rules, export, and CMake package config for NeoBAE.
# Included from the root CMakeLists.txt after targets are defined.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(WIN32)
  # Suite ZIP stays flat (exes + DLL at archive root). SDK uses a normal prefix tree.
  set(NEOBAE_INSTALL_RUNTIME_DIR ".")
  set(NEOBAE_INSTALL_TOOL_DIR ".")
  set(NEOBAE_INSTALL_LIBDIR "lib")
  set(NEOBAE_INSTALL_INCLUDEDIR "include")
  set(NEOBAE_INSTALL_CMAKEDIR "lib/cmake/NeoBAE")
  set(NEOBAE_INSTALL_SDK_RUNTIME_DIR "bin")
else()
  set(NEOBAE_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
  set(NEOBAE_INSTALL_TOOL_DIR "${CMAKE_INSTALL_BINDIR}")
  set(NEOBAE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
  set(NEOBAE_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
  set(NEOBAE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/NeoBAE")
  set(NEOBAE_INSTALL_SDK_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

# --- Library ---
install(TARGETS neobae
  EXPORT NeoBAETargets
  RUNTIME
    DESTINATION "${NEOBAE_INSTALL_RUNTIME_DIR}"
    COMPONENT Runtime
  LIBRARY
    DESTINATION "${NEOBAE_INSTALL_LIBDIR}"
    COMPONENT Runtime
  ARCHIVE
    DESTINATION "${NEOBAE_INSTALL_LIBDIR}"
    COMPONENT Development
  INCLUDES DESTINATION "${NEOBAE_INSTALL_INCLUDEDIR}/neobae"
)

# Windows SDK zip also needs the DLL under bin/ (suite keeps it flat via Runtime).
if(WIN32 AND NEOBAE_SHARED_LIBNEOBAE)
  install(TARGETS neobae
    RUNTIME
      DESTINATION "${NEOBAE_INSTALL_SDK_RUNTIME_DIR}"
      COMPONENT Development
  )
endif()

# Optional MinGW export artifacts for MSVC-style linking.
if(NEOBAE_DLL_DEF_FILE)
  install(FILES "${NEOBAE_DLL_DEF_FILE}"
    DESTINATION "${NEOBAE_INSTALL_LIBDIR}"
    COMPONENT Development
  )
endif()
if(NEOBAE_VCLIB_OUTPUT)
  install(FILES "${NEOBAE_VCLIB_OUTPUT}"
    DESTINATION "${NEOBAE_INSTALL_LIBDIR}"
    COMPONENT Development
  )
endif()

# --- Tools ---
if(NEOBAE_TOOL_TARGETS)
  install(TARGETS ${NEOBAE_TOOL_TARGETS}
    RUNTIME
      DESTINATION "${NEOBAE_INSTALL_TOOL_DIR}"
      COMPONENT Tools
  )
endif()

# Sidecar PDBs for crash-log symbolication (BUILD_DEBUG_INFO Windows builds).
if(BUILD_DEBUG_INFO AND WIN32)
  foreach(_neobae_pdb_tool IN LISTS NEOBAE_TOOL_TARGETS)
    install(FILES
        "$<TARGET_FILE_DIR:${_neobae_pdb_tool}>/$<TARGET_FILE_BASE_NAME:${_neobae_pdb_tool}>.pdb"
      DESTINATION "${NEOBAE_INSTALL_TOOL_DIR}"
      COMPONENT Tools
      OPTIONAL
    )
  endforeach()
  unset(_neobae_pdb_tool)
  if(NEOBAE_SHARED_LIBNEOBAE)
    install(FILES
        "$<TARGET_FILE_DIR:neobae>/$<TARGET_FILE_BASE_NAME:neobae>.pdb"
      DESTINATION "${NEOBAE_INSTALL_RUNTIME_DIR}"
      COMPONENT Runtime
      OPTIONAL
    )
    install(FILES
        "$<TARGET_FILE_DIR:neobae>/$<TARGET_FILE_BASE_NAME:neobae>.pdb"
      DESTINATION "${NEOBAE_INSTALL_SDK_RUNTIME_DIR}"
      COMPONENT Development
      OPTIONAL
    )
  endif()
endif()

# nbeditor hyphen data:
#   Windows suite: Tools/nbeditor/ (next to nbeditor.exe) with EN+RU .dic
#   Linux: catalog/README under share/neobae/nbeditor (binary name is nbeditor)
set(BAE_NBEDITOR_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/neobae/data/nbeditor")
if(EXISTS "${BAE_NBEDITOR_DATA_DIR}/hyph_catalog.json")
  if(WIN32)
    install(FILES
        "${BAE_NBEDITOR_DATA_DIR}/hyph_catalog.json"
        "${BAE_NBEDITOR_DATA_DIR}/README.md"
        "${BAE_NBEDITOR_DATA_DIR}/hyph_en_US.dic"
        "${BAE_NBEDITOR_DATA_DIR}/hyph_ru_RU.dic"
      DESTINATION "${NEOBAE_INSTALL_TOOL_DIR}/nbeditor"
      COMPONENT Tools
    )
  else()
    include(GNUInstallDirs)
    install(FILES
        "${BAE_NBEDITOR_DATA_DIR}/hyph_catalog.json"
        "${BAE_NBEDITOR_DATA_DIR}/README.md"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/neobae/nbeditor"
      COMPONENT Tools
    )
  endif()
endif()

# Linux desktop integration: menu entries + hicolor icons (zefidi, nbeditor).
if(UNIX AND NOT APPLE)
  set(NEOBAE_LINUX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/neobae/linux")
  foreach(_app zefidi nbeditor)
    if(TARGET ${_app})
      install(FILES "${NEOBAE_LINUX_DIR}/${_app}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        COMPONENT Tools
      )
      install(DIRECTORY "${NEOBAE_LINUX_DIR}/icons/hicolor"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons"
        COMPONENT Tools
        FILES_MATCHING PATTERN "${_app}.png"
      )
    endif()
  endforeach()
  unset(_app)
endif()

# --- Curated public headers ---
set(NEOBAE_PUBLIC_COMMON_HEADERS
  NeoBAE.h
  BAE_EditorAPI.h
  GenSnd.h
  GenPriv.h
  GenXMF.h
  X_API.h
  X_PackStructures.h
  X_UnpackStructures.h
)

set(NEOBAE_PUBLIC_HEADER_FILES "")
foreach(_hdr IN LISTS NEOBAE_PUBLIC_COMMON_HEADERS)
  list(APPEND NEOBAE_PUBLIC_HEADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Common/${_hdr}"
  )
endforeach()

list(APPEND NEOBAE_PUBLIC_HEADER_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Platform/BAE_API.h"
)

# Only ship the BAEBuildOptions header for the compiled-in platform.
# Cache value is "Ansi"; the header file uses uppercase "ANSI".
if(BAE_PLATFORM STREQUAL "Ansi")
  set(_neobae_buildoptions_stem "ANSI")
else()
  set(_neobae_buildoptions_stem "${BAE_PLATFORM}")
endif()
set(_neobae_buildoptions_hdr
  "${CMAKE_CURRENT_SOURCE_DIR}/neobae/src/BAE_Source/Platform/BAEBuildOptions_${_neobae_buildoptions_stem}.h"
)
if(NOT EXISTS "${_neobae_buildoptions_hdr}")
  message(FATAL_ERROR
    "No BAEBuildOptions header for BAE_PLATFORM='${BAE_PLATFORM}' "
    "(expected ${_neobae_buildoptions_hdr})")
endif()
list(APPEND NEOBAE_PUBLIC_HEADER_FILES "${_neobae_buildoptions_hdr}")
unset(_neobae_buildoptions_stem)
unset(_neobae_buildoptions_hdr)

install(FILES ${NEOBAE_PUBLIC_HEADER_FILES}
  DESTINATION "${NEOBAE_INSTALL_INCLUDEDIR}/neobae"
  COMPONENT Development
)

# --- Export / package config ---
install(EXPORT NeoBAETargets
  FILE NeoBAETargets.cmake
  NAMESPACE NeoBAE::
  DESTINATION "${NEOBAE_INSTALL_CMAKEDIR}"
  COMPONENT Development
)

# Alias so find_package consumers can use NeoBAE::neobae (export uses that name).
# Targets are exported as NeoBAE::neobae from target name "neobae".

set(NEOBAE_PACKAGE_VERSION "${BAE_VERSION}")
# ConfigVersion needs a numeric-ish version for compatibility checks; keep package
# display version as BAE_VERSION separately in NeoBAEConfig.cmake.
if(BAE_VERSION MATCHES "^[0-9]+\\.[0-9]+")
  set(NEOBAE_CMAKE_PACKAGE_VERSION "${BAE_VERSION}")
else()
  set(NEOBAE_CMAKE_PACKAGE_VERSION "${NEOBAE_SOVERSION}.0.0")
endif()

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NeoBAEConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NeoBAEConfig.cmake"
  INSTALL_DESTINATION "${NEOBAE_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NeoBAEConfigVersion.cmake"
  VERSION "${NEOBAE_CMAKE_PACKAGE_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/NeoBAEConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/NeoBAEConfigVersion.cmake"
  DESTINATION "${NEOBAE_INSTALL_CMAKEDIR}"
  COMPONENT Development
)

# Component metadata (used by CPack grouping / DEB package names).
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "NeoBAE Runtime" CACHE INTERNAL "")
set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "NeoBAE Tools" CACHE INTERNAL "")
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "NeoBAE Development" CACHE INTERNAL "")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime CACHE INTERNAL "")
