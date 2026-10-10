cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{DEVKITPRO})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE FILEPATH "Switch toolchain")
    else()
        set(CMAKE_TOOLCHAIN_FILE "/opt/devkitpro/cmake/Switch.cmake" CACHE FILEPATH "Switch toolchain")
    endif()
endif()

project(sonic_switch LANGUAGES C CXX)

get_filename_component(SONIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-D__SWITCH__ -O3 -DNDEBUG -Wall -Wextra -ffunction-sections -fdata-sections)

set(APP_TARGET "sonic_switch")
set(APP_VERSION "0.1.0")
set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")

add_library(switch_core STATIC
    "${SONIC_ROOT}/src/core/zip.cpp"
    "${SONIC_ROOT}/src/core/rom_set.cpp"
    "${SONIC_ROOT}/src/cpu/v60/v60.cpp"
    "${SONIC_ROOT}/src/board/system32.cpp"
    "${SONIC_ROOT}/src/board/video32.cpp"
    "${SONIC_ROOT}/src/board/scheduler.cpp"
    "${SONIC_ROOT}/src/board/rf5c68.cpp"
    "${SONIC_ROOT}/src/cpu/z80/z80.cpp"
    "${SONIC_ROOT}/extern/ymfm/src/ymfm_opn.cpp"
    "${SONIC_ROOT}/extern/ymfm/src/ymfm_adpcm.cpp"
    "${SONIC_ROOT}/extern/ymfm/src/ymfm_ssg.cpp"
)
target_include_directories(switch_core SYSTEM PUBLIC "${SONIC_ROOT}/extern/ymfm/src")
set_source_files_properties("${SONIC_ROOT}/src/cpu/v60/v60.cpp" "${SONIC_ROOT}/src/cpu/z80/z80.cpp"
    "${SONIC_ROOT}/extern/ymfm/src/ymfm_opn.cpp" "${SONIC_ROOT}/extern/ymfm/src/ymfm_adpcm.cpp"
    "${SONIC_ROOT}/extern/ymfm/src/ymfm_ssg.cpp" PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(switch_core PUBLIC "${SONIC_ROOT}/src")

add_executable(${APP_TARGET} main.cpp)
target_include_directories(${APP_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    /opt/devkitpro/portlibs/switch/include
    /opt/devkitpro/portlibs/switch/include/SDL2
    /opt/devkitpro/libnx/include
)
target_link_libraries(${APP_TARGET} PRIVATE
    switch_core
    -lSDL2
    -lEGL
    -lGLESv2
    -lglapi
    -ldrm_nouveau
    -lz
    -lnx
    -lm
)
target_link_options(${APP_TARGET} PRIVATE -Wl,--gc-sections)

set(SWITCH_NACP "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.nacp")
nx_generate_nacp(OUTPUT "${SWITCH_NACP}"
    NAME "SegaSonic the Hedgehog"
    AUTHOR "Thorhax"
    VERSION "${APP_VERSION}"
)
# No RomFS: the user's sonic.zip is read from sdmc:/switch/sonic/.
nx_create_nro(${APP_TARGET}
    NACP "${SWITCH_NACP}"
    ICON "${ICON_FILE}"
)
