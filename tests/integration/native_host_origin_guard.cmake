if(NOT DEFINED HOST_EXECUTABLE)
    message(FATAL_ERROR "HOST_EXECUTABLE is required")
endif()

execute_process(
    COMMAND "${HOST_EXECUTABLE}" "untrusted-extension@example.invalid"
    RESULT_VARIABLE host_result
    OUTPUT_VARIABLE host_stdout
    ERROR_VARIABLE host_stderr
)

if(NOT host_result EQUAL 2)
    message(FATAL_ERROR
        "Native host must reject an untrusted origin with exit code 2; got '${host_result}'. "
        "stdout='${host_stdout}' stderr='${host_stderr}'")
endif()
