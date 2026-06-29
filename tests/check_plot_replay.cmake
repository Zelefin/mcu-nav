if(NOT DEFINED PYTHON_EXE)
    message(FATAL_ERROR "PYTHON_EXE is required")
endif()
if(NOT DEFINED GENERATOR)
    message(FATAL_ERROR "GENERATOR is required")
endif()
if(NOT DEFINED PLOTTER)
    message(FATAL_ERROR "PLOTTER is required")
endif()
if(NOT DEFINED REPLAY_EXE)
    message(FATAL_ERROR "REPLAY_EXE is required")
endif()
if(NOT DEFINED SCENARIO)
    message(FATAL_ERROR "SCENARIO is required")
endif()
if(NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "OUT_DIR is required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
execute_process(
    COMMAND
        "${PYTHON_EXE}" "${GENERATOR}"
        --scenario "${SCENARIO}"
        --out-dir "${OUT_DIR}"
        --run-replay "${REPLAY_EXE}"
        --overwrite
        --pretty
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_stdout
    ERROR_VARIABLE generate_stderr
)
if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "scenario generation/replay failed:\n${generate_stdout}\n${generate_stderr}")
endif()

execute_process(
    COMMAND
        "${PYTHON_EXE}" "${PLOTTER}"
        --truth "${OUT_DIR}/truth.csv"
        --solution "${OUT_DIR}/replay/solution.csv"
        --peers "${OUT_DIR}/replay/peers.csv"
        --compare-report "${OUT_DIR}/replay/compare_report.json"
        --out-dir "${OUT_DIR}/plots"
        --pretty
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE plot_result
    OUTPUT_VARIABLE plot_stdout
    ERROR_VARIABLE plot_stderr
)
if(NOT plot_result EQUAL 0)
    message(FATAL_ERROR "plot generation failed:\n${plot_stdout}\n${plot_stderr}")
endif()

foreach(required trajectory_xy.png horizontal_error.png altitude_error.png residuals.png solution_quality.png)
    if(NOT EXISTS "${OUT_DIR}/plots/${required}")
        message(FATAL_ERROR "missing plot output: ${OUT_DIR}/plots/${required}")
    endif()
endforeach()
