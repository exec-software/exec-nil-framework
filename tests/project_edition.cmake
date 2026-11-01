# NIL-0026 editions: the backward-compatibility guarantee, as a test.
#
# The important case is the first one. A manifest with no `edition` must keep
# its original meaning forever, with no edit — if that ever stops holding,
# every NIL program written before editions existed has silently changed.
set(scratch "${CMAKE_CURRENT_BINARY_DIR}/edition-scratch")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}/src")

file(WRITE "${scratch}/src/main.matrix"
"book ed.main\n"
"main returns override integer begin paragraph\n"
"    variable xs as array of number is [1, 2, 3]\n"
"    variable v is xs at 99\n"
"    say \"REACHED-END\"\n"
"    give 0\n"
"end paragraph\n")

function(write_manifest edition_line)
    file(WRITE "${scratch}/nil.project.nildata"
"nildata 1\nobject\n  schema is \"nil.project.v1\"\n  name is \"ed\"\n"
"${edition_line}"
"  entry is \"src/main.matrix\"\n  outputDirectory is \"build\"\nend object\n")
endfunction()

function(run_project out_rc out_err)
    execute_process(COMMAND "${NILBUILD}" run --compiler "${COMPILER}" --interpreter "${INTERPRETER}"
                    WORKING_DIRECTORY "${scratch}"
                    OUTPUT_VARIABLE captured_out ERROR_VARIABLE captured_err RESULT_VARIABLE captured_rc)
    set(${out_rc} "${captured_rc}" PARENT_SCOPE)
    set(${out_err} "${captured_out}${captured_err}" PARENT_SCOPE)
endfunction()

# 1. No edition: original semantics. Out-of-bounds stays silent.
write_manifest("")
run_project(rc output)
if(NOT output MATCHES "REACHED-END")
    message(FATAL_ERROR "a manifest with no edition must keep original semantics:\n${output}")
endif()
if(output MATCHES "out of bounds")
    message(FATAL_ERROR "the original edition must not gain bounds faults:\n${output}")
endif()

# 2. Opting in changes the semantics.
write_manifest("  edition is \"2026\"\n")
run_project(rc output)
if(NOT output MATCHES "out of bounds")
    message(FATAL_ERROR "edition 2026 should fault on out-of-bounds access:\n${output}")
endif()

# 3. An unknown edition is refused, never ignored.
write_manifest("  edition is \"2099\"\n")
execute_process(COMMAND "${NILBUILD}" describe WORKING_DIRECTORY "${scratch}"
                OUTPUT_VARIABLE d_out ERROR_VARIABLE d_err RESULT_VARIABLE d_rc)
if(d_rc EQUAL 0)
    message(FATAL_ERROR "an unknown edition must fail the build, not be ignored")
endif()
if(NOT "${d_out}${d_err}" MATCHES "unknown edition")
    message(FATAL_ERROR "an unknown edition should say so:\n${d_out}${d_err}")
endif()

# 4. describe reports the edition, so a build can state its semantics.
write_manifest("")
execute_process(COMMAND "${NILBUILD}" describe --format json WORKING_DIRECTORY "${scratch}"
                OUTPUT_VARIABLE j_out ERROR_QUIET)
if(NOT j_out MATCHES "\"edition\"")
    message(FATAL_ERROR "describe must report the edition:\n${j_out}")
endif()
if(NOT j_out MATCHES "original")
    message(FATAL_ERROR "an absent edition must describe as 'original':\n${j_out}")
endif()
