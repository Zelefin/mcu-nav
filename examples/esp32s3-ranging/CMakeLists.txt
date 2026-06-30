cmake_minimum_required(VERSION 3.16)

if(DEFINED RANGING_PIO_ENV)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/.pio/libdeps/${RANGING_PIO_ENV}/RadioLib")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_ranging)
