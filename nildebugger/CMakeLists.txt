cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nildebugger VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
endif()

include(GNUInstallDirs)
include(CTest)

if(NOT TARGET nilcompiler_core)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler"
        "${CMAKE_CURRENT_BINARY_DIR}/nilcompiler"
    )
endif()

add_executable(nildebugger
    main.cpp
    debugger_core.cpp
    dap.cpp
    ../nillsp/json_rpc.cpp
)
target_compile_features(nildebugger PRIVATE cxx_std_17)
nil_apply_version(nildebugger)

target_include_directories(nildebugger PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../nillsp"
)

find_package(Threads REQUIRED)
target_link_libraries(nildebugger PRIVATE nilcompiler_core Threads::Threads)

if(BUILD_TESTING)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

    add_executable(nildebugger_core_tests
        tests/debugger_core_tests.cpp
        debugger_core.cpp
        ../nillsp/json_rpc.cpp
    )
    target_compile_features(nildebugger_core_tests PRIVATE cxx_std_17)
    target_include_directories(nildebugger_core_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../nillsp"
    )
    target_link_libraries(nildebugger_core_tests PRIVATE nilcompiler_core)
    add_test(NAME nildebugger.core COMMAND nildebugger_core_tests)

    add_test(
        NAME nildebugger.json-protocol
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/json_protocol.py"
                "$<TARGET_FILE:nildebugger>"
    )
    set_tests_properties(nildebugger.json-protocol PROPERTIES TIMEOUT 20)
    add_test(
        NAME nildebugger.dap-protocol
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/dap_protocol.py"
                "$<TARGET_FILE:nildebugger>" "$<TARGET_FILE:nilcompiler>"
    )
    set_tests_properties(nildebugger.dap-protocol PROPERTIES TIMEOUT 90 LABELS "debugger;rc")
    add_executable(nildebugger_source_metadata_tests tests/source_metadata_tests.cpp)
    target_compile_features(nildebugger_source_metadata_tests PRIVATE cxx_std_17)
    target_link_libraries(nildebugger_source_metadata_tests PRIVATE nilcompiler_core)
    add_test(NAME nildebugger.source-metadata COMMAND nildebugger_source_metadata_tests)
    set_tests_properties(nildebugger.source-metadata PROPERTIES TIMEOUT 30 LABELS "debugger;rc")
endif()

install(TARGETS nildebugger RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
