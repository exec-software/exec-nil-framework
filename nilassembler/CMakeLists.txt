cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nilassembler VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES C CXX)
    include(CTest)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler"
        "${CMAKE_CURRENT_BINARY_DIR}/nilcompiler"
    )
endif()

include(GNUInstallDirs)

add_executable(nilassembler main.cpp)
target_compile_features(nilassembler PRIVATE cxx_std_17)
target_link_libraries(nilassembler PRIVATE nilcompiler_core)
nil_apply_version(nilassembler)

if(BUILD_TESTING)
    add_executable(
        nilassembler_nil_module_reader_tests
        tests/nil_module_reader_tests.cpp
    )
    target_compile_features(
        nilassembler_nil_module_reader_tests
        PRIVATE cxx_std_17
    )
    target_link_libraries(
        nilassembler_nil_module_reader_tests
        PRIVATE nilcompiler_core
    )
    add_test(
        NAME nilassembler.nil-module-reader
        COMMAND $<TARGET_FILE:nilassembler_nil_module_reader_tests>
    )

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME nilassembler.cli
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/nilassembler_cli_tests.py"
            --assembler $<TARGET_FILE:nilassembler>
            --compiler $<TARGET_FILE:nilcompiler>
            --runtime $<TARGET_FILE:nilruntime>
            --linker-driver "${CMAKE_CXX_COMPILER}"
    )
endif()

install(TARGETS nilassembler RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
