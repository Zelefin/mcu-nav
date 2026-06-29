function(add_nav_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE nav_core)
    if(NAV_ENABLE_WARNINGS)
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_nav_test(test_peer_table test_peer_table.c)
add_nav_test(test_state_machine test_state_machine.c)
add_nav_test(test_quality test_quality.c)
add_nav_test(test_radio_protocol test_radio_protocol.c)
add_nav_test(test_trilateration_adapter test_trilateration_adapter.c)
add_nav_test(test_radio_navigation test_radio_navigation.c)

function(add_replay_fixture_test name fixture expect_mode expect_solution expect_source expect_reject expect_anchors)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
            -DWORK_DIR=${CMAKE_SOURCE_DIR}
            -DEVENTS=${CMAKE_SOURCE_DIR}/examples/replay/${fixture}/events.csv
            -DOUT_DIR=${CMAKE_BINARY_DIR}/replay/${fixture}
            -DEXPECT_MODE=${expect_mode}
            -DEXPECT_SOLUTION=${expect_solution}
            -DEXPECT_SOURCE=${expect_source}
            -DEXPECT_REJECT=${expect_reject}
            -DEXPECT_ANCHORS=${expect_anchors}
            -DMAX_RMS=0.01
            -P ${CMAKE_SOURCE_DIR}/tests/check_replay.cmake
    )
endfunction()

add_test(
    NAME replay_radio_3d_success
    COMMAND ${CMAKE_COMMAND}
        -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
        -DWORK_DIR=${CMAKE_SOURCE_DIR}
        -DEVENTS=${CMAKE_SOURCE_DIR}/examples/replay/radio_3d_success/events.csv
        -DOUT_DIR=${CMAKE_BINARY_DIR}/replay/radio_3d_success
        -DEXPECT_MODE=RADIO_NAV_OK
        -DEXPECT_SOLUTION=RADIO_3D
        -DEXPECT_SOURCE=RADIO_3D
        -DEXPECT_REJECT=NONE
        -DEXPECT_ANCHORS=3
        -DMAX_RMS=0.01
        -DEXPECT_COMPARE_REPORT=1
        -P ${CMAKE_SOURCE_DIR}/tests/check_replay.cmake
)

add_replay_fixture_test(
    replay_reject_not_enough_anchors
    reject_not_enough_anchors
    NO_NAV_SOLUTION
    REJECTED
    NONE
    NOT_ENOUGH_ANCHORS
    2
)

add_replay_fixture_test(
    replay_reject_missing_altitude
    reject_missing_altitude
    NO_NAV_SOLUTION
    REJECTED
    NONE
    MISSING_LOCAL_ALTITUDE
    3
)

add_test(
    NAME replay_reject_stale_range
    COMMAND ${CMAKE_COMMAND}
        -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
        -DWORK_DIR=${CMAKE_SOURCE_DIR}
        -DEVENTS=${CMAKE_SOURCE_DIR}/examples/replay/reject_stale_range/events.csv
        -DOUT_DIR=${CMAKE_BINARY_DIR}/replay/reject_stale_range
        -DEXPECT_MODE=NO_NAV_SOLUTION
        -DEXPECT_SOLUTION=REJECTED
        -DEXPECT_SOURCE=NONE
        -DEXPECT_REJECT=NOT_ENOUGH_ANCHORS
        -DEXPECT_ANCHORS=2
        -DEXPECT_LOG=reason=STALE_RANGE
        -P ${CMAKE_SOURCE_DIR}/tests/check_replay.cmake
)

add_test(
    NAME replay_reject_bad_position
    COMMAND ${CMAKE_COMMAND}
        -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
        -DWORK_DIR=${CMAKE_SOURCE_DIR}
        -DEVENTS=${CMAKE_SOURCE_DIR}/examples/replay/reject_bad_position/events.csv
        -DOUT_DIR=${CMAKE_BINARY_DIR}/replay/reject_bad_position
        -DEXPECT_MODE=NO_NAV_SOLUTION
        -DEXPECT_SOLUTION=REJECTED
        -DEXPECT_SOURCE=NONE
        -DEXPECT_REJECT=NOT_ENOUGH_ANCHORS
        -DEXPECT_ANCHORS=2
        -DEXPECT_LOG=reason=BAD_POSITION
        -P ${CMAKE_SOURCE_DIR}/tests/check_replay.cmake
)

function(add_replay_invalid_test name fixture expected_text)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
            -DWORK_DIR=${CMAKE_SOURCE_DIR}
            -DEVENTS=${CMAKE_SOURCE_DIR}/examples/replay/${fixture}/events.csv
            -DOUT_DIR=${CMAKE_BINARY_DIR}/replay/${fixture}
            -DEXPECT_ERROR=${expected_text}
            -P ${CMAKE_SOURCE_DIR}/tests/check_replay_invalid.cmake
    )
endfunction()

add_replay_invalid_test(replay_parser_unknown_event parser_invalid_unknown_event "unknown event_type")
add_replay_invalid_test(replay_parser_negative_range parser_invalid_negative_range "invalid range_mm")
add_replay_invalid_test(replay_parser_invalid_enum parser_invalid_enum "invalid nav_mode")
add_replay_invalid_test(replay_parser_missing_field parser_invalid_missing_field "missing required field: range_mm")
add_replay_invalid_test(replay_parser_malformed_integer parser_invalid_integer "invalid time_ms")
add_replay_invalid_test(replay_parser_invalid_config_unknown_key parser_invalid_config_unknown_key "unknown config key")
add_replay_invalid_test(replay_parser_invalid_config_bad_value parser_invalid_config_bad_value "invalid node_id")
add_replay_invalid_test(replay_parser_invalid_truth_bad_value parser_invalid_truth_bad_value "invalid true_lat_e7")

function(add_scenario_generate_test name scenario)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON_EXE=${Python3_EXECUTABLE}
            -DGENERATOR=${CMAKE_SOURCE_DIR}/tools/sim/generate_scenario.py
            -DWORK_DIR=${CMAKE_SOURCE_DIR}
            -DSCENARIO=${CMAKE_SOURCE_DIR}/examples/scenarios/${scenario}.json
            -DOUT_DIR=${CMAKE_BINARY_DIR}/generated/ctest/${scenario}
            -P ${CMAKE_SOURCE_DIR}/tests/check_scenario_generate.cmake
    )
endfunction()

function(add_scenario_replay_test name scenario)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON_EXE=${Python3_EXECUTABLE}
            -DGENERATOR=${CMAKE_SOURCE_DIR}/tools/sim/generate_scenario.py
            -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
            -DWORK_DIR=${CMAKE_SOURCE_DIR}
            -DSCENARIO=${CMAKE_SOURCE_DIR}/examples/scenarios/${scenario}.json
            -DOUT_DIR=${CMAKE_BINARY_DIR}/generated/ctest/${scenario}_replay
            -P ${CMAKE_SOURCE_DIR}/tests/check_scenario_generate.cmake
    )
endfunction()

add_scenario_generate_test(scenario_validate_static_anchors_success static_anchors_success)
add_scenario_generate_test(scenario_validate_moving_blind_success moving_blind_success)
add_scenario_generate_test(scenario_validate_moving_anchors_success moving_anchors_success)
add_scenario_generate_test(scenario_validate_noisy_ranges_success noisy_ranges_success)
add_scenario_generate_test(scenario_validate_biased_range_degraded_or_rejected biased_range_degraded_or_rejected)
add_scenario_generate_test(scenario_validate_packet_loss_rejection packet_loss_rejection)

add_scenario_replay_test(scenario_replay_static_anchors_success static_anchors_success)
add_scenario_replay_test(scenario_replay_moving_blind_success moving_blind_success)
add_scenario_replay_test(scenario_replay_moving_anchors_success moving_anchors_success)

add_test(
    NAME scenario_invalid_missing_nodes
    COMMAND ${CMAKE_COMMAND}
        -DPYTHON_EXE=${Python3_EXECUTABLE}
        -DGENERATOR=${CMAKE_SOURCE_DIR}/tools/sim/generate_scenario.py
        -DWORK_DIR=${CMAKE_SOURCE_DIR}
        -DSCENARIO=${CMAKE_SOURCE_DIR}/examples/scenarios/invalid_missing_nodes.json
        -DOUT_DIR=${CMAKE_BINARY_DIR}/generated/ctest/invalid_missing_nodes
        -DEXPECT_ERROR=scenario.nodes
        -P ${CMAKE_SOURCE_DIR}/tests/check_scenario_invalid.cmake
)

add_test(
    NAME plot_static_anchors_success
    COMMAND ${CMAKE_COMMAND}
        -DPYTHON_EXE=${Python3_EXECUTABLE}
        -DGENERATOR=${CMAKE_SOURCE_DIR}/tools/sim/generate_scenario.py
        -DPLOTTER=${CMAKE_SOURCE_DIR}/tools/plot/plot_replay.py
        -DREPLAY_EXE=$<TARGET_FILE:nav_replay>
        -DWORK_DIR=${CMAKE_SOURCE_DIR}
        -DSCENARIO=${CMAKE_SOURCE_DIR}/examples/scenarios/static_anchors_success.json
        -DOUT_DIR=${CMAKE_BINARY_DIR}/generated/ctest/plot_static_anchors_success
        -P ${CMAKE_SOURCE_DIR}/tests/check_plot_replay.cmake
)
