# FluidSynth CMake configuration file:
# This file is meant to be placed in a cmake subfolder of FluidSynth-devel-3.x.y-mingw

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(fluidsynth_config_path "${CMAKE_CURRENT_LIST_DIR}/../i686-w64-mingw32/lib/cmake/FluidSynth/FluidSynthConfig.cmake")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(fluidsynth_config_path "${CMAKE_CURRENT_LIST_DIR}/../x86_64-w64-mingw32/lib/cmake/FluidSynth/FluidSynthConfig.cmake")
else()
    set(FLUIDSYNTH_FOUND FALSE)
    return()
endif()

if(NOT EXISTS "${fluidsynth_config_path}")
    message(WARNING "${fluidsynth_config_path} does not exist: MinGW development package is corrupted")
    set(FLUIDSYNTH_FOUND FALSE)
    return()
endif()

include("${fluidsynth_config_path}")
