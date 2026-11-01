# Asserts that an uncaught runtime error leaves a readable .nycrash artifact,
# and that the stderr notice appears only when the report was asked for.
# Runs in a scratch directory so the artifact cannot land in the source tree.
set(scratch "${CMAKE_CURRENT_BINARY_DIR}/crash-artifact-scratch")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}")

execute_process(COMMAND "${COMPILER}" --no-bundle "${SOURCE}" -o "${scratch}/crash.nil"
                RESULT_VARIABLE compiled OUTPUT_QUIET ERROR_VARIABLE compile_err)
if(NOT compiled EQUAL 0)
    message(FATAL_ERROR "could not compile the fixture: ${compile_err}")
endif()

# 1. Automatic: the artifact appears and stderr carries no extra line.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env NIL_STRICT_BOUNDS=1
                        "${INTERPRETER}" "${scratch}/crash.nil"
                WORKING_DIRECTORY "${scratch}"
                OUTPUT_QUIET ERROR_VARIABLE quiet_err RESULT_VARIABLE quiet_rc)
if(quiet_err MATCHES "nilcrash")
    message(FATAL_ERROR "default run must not add a [nilcrash] line to stderr:\n${quiet_err}")
endif()
file(GLOB artifacts "${scratch}/*.nycrash")
if(artifacts STREQUAL "")
    message(FATAL_ERROR "no .nycrash artifact was written on an uncaught error")
endif()

list(GET artifacts 0 report)
file(READ "${report}" body)
foreach(required "format = nycrash/1" "source = interpreter_self" "[error]" "[location]" "out of bounds")
    if(NOT body MATCHES "${required}")
        message(FATAL_ERROR "crash artifact is missing '${required}':\n${body}")
    endif()
endforeach()

# 2. Asked for: the notice appears.
file(REMOVE ${artifacts})
execute_process(COMMAND "${CMAKE_COMMAND}" -E env NIL_STRICT_BOUNDS=1 NIL_CRASH_REPORT=1
                        "${INTERPRETER}" "${scratch}/crash.nil"
                WORKING_DIRECTORY "${scratch}"
                OUTPUT_QUIET ERROR_VARIABLE loud_err)
if(NOT loud_err MATCHES "nilcrash")
    message(FATAL_ERROR "NIL_CRASH_REPORT=1 should report where it wrote:\n${loud_err}")
endif()

# 3. Opt-out leaves nothing behind.
file(GLOB leftovers "${scratch}/*.nycrash")
file(REMOVE ${leftovers})
execute_process(COMMAND "${CMAKE_COMMAND}" -E env NIL_STRICT_BOUNDS=1 NIL_CRASH_REPORT=0
                        "${INTERPRETER}" "${scratch}/crash.nil"
                WORKING_DIRECTORY "${scratch}" OUTPUT_QUIET ERROR_QUIET)
file(GLOB after_optout "${scratch}/*.nycrash")
if(NOT after_optout STREQUAL "")
    message(FATAL_ERROR "NIL_CRASH_REPORT=0 must write nothing, found: ${after_optout}")
endif()
