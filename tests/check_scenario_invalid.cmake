if(NOT DEFINED PYTHON_EXE)
    message(FATAL_ERROR "PYTHON_EXE is required")
endif()
if(NOT DEFINED GENERATOR)
    message(FATAL_ERROR "GENERATOR is required")
endif()
if(NOT DEFINED SCENARIO)
    message(FATAL_ERROR "SCENARIO is required")
endif()
if(NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "OUT_DIR is required")
endif()
if(NOT DEFINED EXPECT_ERROR)
    message(FATAL_ERROR "EXPECT_ERROR is required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
execute_process(
    COMMAND "${PYTHON_EXE}" "${GENERATOR}" --scenario "${SCENARIO}" --out-dir "${OUT_DIR}" --overwrite
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_stdout
    ERROR_VARIABLE generate_stderr
)
if(generate_result EQUAL 0)
    message(FATAL_ERROR "invalid scenario unexpectedly succeeded:\n${generate_stdout}\n${generate_stderr}")
endif()

set(combined "${generate_stdout}\n${generate_stderr}")
string(FIND "${combined}" "${EXPECT_ERROR}" error_index)
if(error_index EQUAL -1)
    message(FATAL_ERROR "expected error text '${EXPECT_ERROR}', got:\n${combined}")
endif()
