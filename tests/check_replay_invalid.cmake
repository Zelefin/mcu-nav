if(NOT DEFINED REPLAY_EXE)
    message(FATAL_ERROR "REPLAY_EXE is required")
endif()
if(NOT DEFINED EVENTS)
    message(FATAL_ERROR "EVENTS is required")
endif()
if(NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "OUT_DIR is required")
endif()
if(NOT DEFINED EXPECT_ERROR)
    message(FATAL_ERROR "EXPECT_ERROR is required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
execute_process(
    COMMAND "${REPLAY_EXE}" --events "${EVENTS}" --out-dir "${OUT_DIR}" --node-id 0
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE replay_result
    OUTPUT_VARIABLE replay_stdout
    ERROR_VARIABLE replay_stderr
)
if(replay_result EQUAL 0)
    message(FATAL_ERROR "nav_replay unexpectedly succeeded:\n${replay_stdout}\n${replay_stderr}")
endif()

set(combined "${replay_stdout}\n${replay_stderr}")
string(FIND "${combined}" "${EXPECT_ERROR}" error_index)
if(error_index EQUAL -1)
    message(FATAL_ERROR "expected error text '${EXPECT_ERROR}', got:\n${combined}")
endif()
