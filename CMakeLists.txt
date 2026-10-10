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
)
target_include_directories(s32core PUBLIC src)
target_compile_options(s32core PRIVATE -Wall -Wextra)
set_source_files_properties(src/cpu/v60/v60.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable")

add_executable(s32run tools/s32run/main.cpp)
target_link_libraries(s32run PRIVATE s32core)
