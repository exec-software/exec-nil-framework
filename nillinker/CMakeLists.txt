cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
set(NILLINKER_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(NILLINKER_STANDALONE ON)
    project(nillinker VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
    include(CTest)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler"
        "${CMAKE_CURRENT_BINARY_DIR}/nilcompiler"
        EXCLUDE_FROM_ALL
    )
endif()

include(GNUInstallDirs)

if(NOT DEFINED NIL_NATIVE_LINKER_DRIVER)
    set(
        NIL_NATIVE_LINKER_DRIVER
        "${CMAKE_CXX_COMPILER}"
        CACHE STRING
        "Compiler driver used to link NIL native executables"
    )
endif()

if(NOT TARGET nilruntime)
    message(FATAL_ERROR "nillinker requires the nilruntime build target")
endif()

# A standalone linker build must retain the same zero-configuration runtime
# discovery contract as the repository build. Keep the archive beside the
# executable in the build tree; conventional installs place it under lib/.
if(NILLINKER_STANDALONE)
    set_target_properties(
        nilruntime nilruntime_shared
        PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()

add_executable(nillinker main.cpp process_runner.cpp)
nil_apply_version(nillinker)
target_compile_features(nillinker PRIVATE cxx_std_17)
target_include_directories(
    nillinker
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler/src"
)
target_link_libraries(nillinker PRIVATE nilruntime)
if(WIN32)
    target_link_libraries(nillinker PRIVATE shell32)
endif()
target_compile_definitions(
    nillinker
    PRIVATE
        NIL_NATIVE_LINKER_DRIVER="${NIL_NATIVE_LINKER_DRIVER}"
        NY_NATIVE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
)
if(MSVC)
    target_compile_definitions(nillinker PRIVATE NILLINKER_MSVC_DRIVER=1)
endif()

install(TARGETS nillinker RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(NILLINKER_STANDALONE)
    add_dependencies(nillinker nilruntime_shared)
    install(TARGETS nilruntime nilruntime_shared
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

if(BUILD_TESTING)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

    add_executable(
        nillinker_process_runner_tests
        tests/process_runner_tests.cpp
        process_runner.cpp
    )
    target_compile_features(nillinker_process_runner_tests PRIVATE cxx_std_17)
    target_include_directories(
        nillinker_process_runner_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME nillinker.process-deadline
        COMMAND $<TARGET_FILE:nillinker_process_runner_tests>
    )
    set_tests_properties(
        nillinker.process-deadline
        PROPERTIES LABELS "nillinker;process" TIMEOUT 15
    )

    add_executable(
        nillinker_hanging_driver
        tests/hanging_driver.cpp
    )
    target_compile_features(nillinker_hanging_driver PRIVATE cxx_std_17)

    add_executable(
        nillinker_timeout_fixture
        main.cpp
        process_runner.cpp
    )
    target_compile_features(nillinker_timeout_fixture PRIVATE cxx_std_17)
    target_include_directories(
        nillinker_timeout_fixture
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler/src"
    )
    target_link_libraries(nillinker_timeout_fixture PRIVATE nilruntime)
    if(WIN32)
        target_link_libraries(nillinker_timeout_fixture PRIVATE shell32)
    endif()
    target_compile_definitions(
        nillinker_timeout_fixture
        PRIVATE
            NIL_NATIVE_LINKER_DRIVER="$<TARGET_FILE:nillinker_hanging_driver>"
            NY_NATIVE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
    )
    nil_apply_version(nillinker_timeout_fixture)
    if(MSVC)
        target_compile_definitions(
            nillinker_timeout_fixture
            PRIVATE NILLINKER_MSVC_DRIVER=1
        )
    endif()
    add_dependencies(nillinker_timeout_fixture nillinker_hanging_driver)

    add_test(
        NAME nillinker.host-object
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/host_object_link.py"
            --linker $<TARGET_FILE:nillinker>
            --timeout-linker $<TARGET_FILE:nillinker_timeout_fixture>
            --runtime $<TARGET_FILE:nilruntime>
            --cxx "${CMAKE_CXX_COMPILER}"
            --install-libdir "${CMAKE_INSTALL_LIBDIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests"
    )
    set_tests_properties(
        nillinker.host-object
        PROPERTIES
            LABELS "nillinker;native"
            TIMEOUT 90
    )

    if(NILLINKER_STANDALONE)
        set(
            NILLINKER_TEST_INSTALL_PREFIX
            "${CMAKE_CURRENT_BINARY_DIR}/tests/install"
        )
        add_test(
            NAME nillinker.standalone-install
            COMMAND
                "${CMAKE_COMMAND}"
                --install "${CMAKE_CURRENT_BINARY_DIR}"
                --prefix "${NILLINKER_TEST_INSTALL_PREFIX}"
                --config $<CONFIG>
        )
        set_tests_properties(
            nillinker.standalone-install
            PROPERTIES
                FIXTURES_SETUP nillinker_standalone_install
                LABELS "nillinker;install"
                TIMEOUT 30
        )

        add_test(
            NAME nillinker.standalone-installed-host-object
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/host_object_link.py"
                --linker
                    "${NILLINKER_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/nillinker${CMAKE_EXECUTABLE_SUFFIX}"
                --timeout-linker $<TARGET_FILE:nillinker_timeout_fixture>
                --runtime
                    "${NILLINKER_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}nil${CMAKE_STATIC_LIBRARY_SUFFIX}"
                --cxx "${CMAKE_CXX_COMPILER}"
                --install-libdir "${CMAKE_INSTALL_LIBDIR}"
                --work-dir
                    "${CMAKE_CURRENT_BINARY_DIR}/tests/installed-host-object"
        )
        set_tests_properties(
            nillinker.standalone-installed-host-object
            PROPERTIES
                FIXTURES_REQUIRED nillinker_standalone_install
                LABELS "nillinker;install;native"
                TIMEOUT 90
        )
    endif()
endif()
