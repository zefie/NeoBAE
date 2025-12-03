# FluidSynth CMake version configuration file

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(fluidsynth_config_path "${CMAKE_CURRENT_LIST_DIR}/../i686-w64-mingw32/lib/cmake/fluidsynth/FluidSynthConfigVersion.cmake")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(fluidsynth_config_path "${CMAKE_CURRENT_LIST_DIR}/../x86_64-w64-mingw32/lib/cmake/fluidsynth/FluidSynthConfigVersion.cmake")
else()
    set(PACKAGE_VERSION_UNSUITABLE TRUE)
    return()
endif()

if(NOT EXISTS "${fluidsynth_config_path}")
    message(WARNING "${fluidsynth_config_path} does not exist: MinGW development package is corrupted")
    set(PACKAGE_VERSION_UNSUITABLE TRUE)
    return()
endif()

include("${fluidsynth_config_path}")
