cmake_minimum_required(VERSION 3.20)
project(sonic_nx LANGUAGES C CXX)

# Host build: the core library and the headless runner. The Switch app is
# platform/switch (its own CMake project, devkitPro toolchain).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_library(s32core STATIC
    src/core/zip.cpp
    src/core/rom_set.cpp
    src/cpu/v60/v60.cpp
    src/board/system32.cpp
    src/board/video32.cpp
    src/board/scheduler.cpp
    src/board/rf5c68.cpp
    src/cpu/z80/z80.cpp
    extern/ymfm/src/ymfm_opn.cpp
    extern/ymfm/src/ymfm_adpcm.cpp
    extern/ymfm/src/ymfm_ssg.cpp
)
target_include_directories(s32core SYSTEM PUBLIC extern/ymfm/src)
target_include_directories(s32core PUBLIC src)
target_compile_options(s32core PRIVATE -Wall -Wextra)
set_source_files_properties(src/cpu/v60/v60.cpp src/cpu/z80/z80.cpp extern/ymfm/src/ymfm_opn.cpp extern/ymfm/src/ymfm_adpcm.cpp extern/ymfm/src/ymfm_ssg.cpp PROPERTIES COMPILE_OPTIONS "-w")

add_executable(s32run tools/s32run/main.cpp)
target_link_libraries(s32run PRIVATE s32core)
