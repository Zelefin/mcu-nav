add_executable(nav_nmea_dump nav_nmea_dump.c)
target_link_libraries(nav_nmea_dump PRIVATE nav_core)

if(NAV_ENABLE_WARNINGS)
    target_compile_options(nav_nmea_dump PRIVATE -Wall -Wextra -Wpedantic)
endif()
