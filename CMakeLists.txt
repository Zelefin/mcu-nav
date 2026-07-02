cmake_minimum_required(VERSION 3.16)

if(DEFINED IDF_TARGET AND DEFINED ENV{IDF_PATH})
    if(DEFINED NAV_PIO_ENV)
        list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/.pio/libdeps/${NAV_PIO_ENV}/RadioLib")
    endif()
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(nav_mcu_health)
    return()
endif()

project(nav_mcu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NAV_ENABLE_WARNINGS "Enable compiler warnings for nav targets" ON)

add_library(nav_core STATIC
    core/src/nav_core.c
    core/src/nav_peer_table.c
    core/src/nav_quality.c
    core/src/nav_state_machine.c
    core/src/nav_trilateration.c
    core/src/nav_logging.c
    core/src/nav_radio_protocol.c
    core/src/nav_gnss.c
    core/src/nav_nmea.c
    core/src/nav_replay.c
    core/src/nav_mock.c
    core/src/nav_serial_json.c
    core/src/nav_telemetry.c
    core/src/nav_tdma.c
)

target_include_directories(nav_core PUBLIC core/include)
target_link_libraries(nav_core PUBLIC m)

if(NAV_ENABLE_WARNINGS)
    target_compile_options(nav_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_subdirectory(ports/posix)
add_subdirectory(tools/replay)
add_subdirectory(tools/gnss)
add_subdirectory(tests)
