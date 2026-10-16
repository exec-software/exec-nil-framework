if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

file(READ "${INPUT_FILE}" nil_magic OFFSET 0 LIMIT 4 HEX)
string(TOLOWER "${nil_magic}" nil_magic)

if(NOT nil_magic STREQUAL "4e494c00")
    message(FATAL_ERROR
        "${INPUT_FILE} has binary magic ${nil_magic}; expected NIL\\0 (4e494c00)"
    )
endif()
