add_executable(nav_posix_demo main.c)
target_link_libraries(nav_posix_demo PRIVATE nav_core)

if(NAV_ENABLE_WARNINGS)
    target_compile_options(nav_posix_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
