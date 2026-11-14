execute_process(
    COMMAND "${ARCTRA_EXECUTABLE}" track --pri --observatory nyal.ini
    WORKING_DIRECTORY "${ARCTRA_SOURCE_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "track --pri unexpectedly succeeded without a terminal")
endif()

if(NOT error MATCHES "track requires an interactive terminal")
    message(FATAL_ERROR
        "track --pri did not load priorities before opening the terminal.\n"
        "stderr: ${error}\nstdout: ${output}"
    )
endif()
