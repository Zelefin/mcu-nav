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

file(REMOVE_RECURSE "${OUT_DIR}")
set(generate_cmd
    "${PYTHON_EXE}" "${GENERATOR}"
    --scenario "${SCENARIO}"
    --out-dir "${OUT_DIR}"
    --overwrite
    --pretty
)
if(DEFINED REPLAY_EXE)
    list(APPEND generate_cmd --run-replay "${REPLAY_EXE}")
endif()

execute_process(
    COMMAND ${generate_cmd}
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_stdout
    ERROR_VARIABLE generate_stderr
)
if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "scenario generation failed:\n${generate_stdout}\n${generate_stderr}")
endif()

foreach(required events.csv truth.csv replay_config.csv scenario_resolved.json)
    if(NOT EXISTS "${OUT_DIR}/${required}")
        message(FATAL_ERROR "missing generated file: ${OUT_DIR}/${required}")
    endif()
endforeach()

if(DEFINED REPLAY_EXE)
    foreach(required solution.csv peers.csv logs.txt compare_report.txt compare_report.json)
        if(NOT EXISTS "${OUT_DIR}/replay/${required}")
            message(FATAL_ERROR "missing replay output: ${OUT_DIR}/replay/${required}")
        endif()
    endforeach()
    file(READ "${OUT_DIR}/replay/compare_report.txt" report_text)
    string(FIND "${report_text}" "pass=true" pass_index)
    if(pass_index EQUAL -1)
        message(FATAL_ERROR "generated replay compare report did not pass:\n${report_text}")
    endif()
endif()
