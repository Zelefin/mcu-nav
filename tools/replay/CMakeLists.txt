add_executable(nav_replay nav_replay.c)
target_link_libraries(nav_replay PRIVATE nav_core)

if(NAV_ENABLE_WARNINGS)
    target_compile_options(nav_replay PRIVATE -Wall -Wextra -Wpedantic)
endif()
