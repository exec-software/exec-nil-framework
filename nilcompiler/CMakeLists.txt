cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
set(NILCOMPILER_STANDALONE OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(NILCOMPILER_STANDALONE ON)
    project(nilcompiler VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES C CXX)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(NIL_ENABLE_LLVM "Enable the optional LLVM native backend" ON)
set(
    NIL_NATIVE_LINKER_DRIVER
    "${CMAKE_CXX_COMPILER}"
    CACHE STRING
    "Compiler driver used to link NIL native executables on non-Windows hosts"
)

set(NILCOMPILER_CORE_SOURCES
    src/analysis.cpp
    src/default_analyzer.cpp
    src/lexer.cpp
    src/parser.cpp
    src/pattern_syntax.cpp
    # Matrix front end: a second surface over the same AST, type checker and
    # NIL generator. See matrix/docs/front-end-plan.md.
    src/matrix_lexer.cpp
    src/matrix_parser.cpp
    src/matrix_diagnostics.cpp
    src/matrix_hag_bridge.cpp
    src/codegen.cpp
    src/vm.cpp
    src/preprocessor.cpp
    src/type_checker.cpp
    src/ownership_check.cpp
    src/owned_contract.cpp
    src/concurrency_contract.cpp
    src/nil_loader.cpp
    src/class_contract.cpp
    src/diagnostics.cpp
    src/context_extractor.cpp
    src/executable_context.cpp
)

# Compile the runtime once as PIC so both the portable SDK shared library and
# the static companion use the same implementation and versioned C ABI.
add_library(nilruntime_objects OBJECT
    src/native_runtime.cpp
    src/memory_profile.cpp
    src/owned_runtime.cpp
    src/secure_file_io.cpp
    src/database_runtime.cpp
    src/pattern_engine.cpp
)
target_compile_features(nilruntime_objects PUBLIC cxx_std_17)
target_link_libraries(nilruntime_objects PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(nilruntime_objects PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(nilruntime_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(nilruntime STATIC $<TARGET_OBJECTS:nilruntime_objects>)
add_library(nilruntime_shared SHARED $<TARGET_OBJECTS:nilruntime_objects>)
foreach(nil_runtime_target IN ITEMS nilruntime nilruntime_shared)
    target_compile_features(${nil_runtime_target} PUBLIC cxx_std_17)
    target_link_libraries(${nil_runtime_target} PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(${nil_runtime_target} PROPERTIES
        CXX_EXTENSIONS OFF
        OUTPUT_NAME nil
    )
endforeach()
# Keep nil.lib available for static linking on Windows. The DLL import library
# needs a distinct name because both runtime forms share the library directory.
set_target_properties(nilruntime_shared PROPERTIES
    ARCHIVE_OUTPUT_NAME nil_import
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

add_library(nilcompiler_core STATIC ${NILCOMPILER_CORE_SOURCES})
nil_apply_version(nilcompiler_core)

target_compile_features(nilcompiler_core PUBLIC cxx_std_17)
set_target_properties(nilcompiler_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(nilcompiler_core PUBLIC nilruntime)

# These headers are currently internal build headers. Keep the source directory
# available to build-tree consumers without advertising a deleted install tree.
target_include_directories(nilcompiler_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

if(MSVC)
    target_compile_options(nilcompiler_core PRIVATE /permissive)
    target_compile_definitions(nilcompiler_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NIL_ENABLE_LLVM)
    find_package(LLVM CONFIG QUIET)
    if(LLVM_FOUND)
        message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
        message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

        target_sources(nilcompiler_core PRIVATE src/llvm_backend.cpp)
        target_include_directories(nilcompiler_core SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

        # LLVM_DEFINITIONS contains command-line-style -D flags. Apply them only
        # to the compiler core instead of leaking them into sibling projects.
        if(LLVM_DEFINITIONS)
            separate_arguments(
                NILCOMPILER_LLVM_DEFINITIONS
                NATIVE_COMMAND
                "${LLVM_DEFINITIONS}"
            )
            target_compile_options(
                nilcompiler_core
                PRIVATE ${NILCOMPILER_LLVM_DEFINITIONS}
            )
        endif()

        llvm_map_components_to_libnames(
            NILCOMPILER_LLVM_LIBRARIES
            Core
            Support
            native
            OrcJIT
            ExecutionEngine
        )
        target_link_libraries(
            nilcompiler_core
            PUBLIC ${NILCOMPILER_LLVM_LIBRARIES}
        )
        target_compile_definitions(nilcompiler_core PUBLIC NY_HAS_LLVM=1)
    else()
        message(STATUS "LLVM not found - native codegen disabled")
    endif()
else()
    message(STATUS "LLVM native codegen disabled (NIL_ENABLE_LLVM=OFF)")
endif()

# The supported db2 boundary dynamically loads providers, so an installed static
# runtime does not embed build-machine library paths in native executables.
find_path(NILCOMPILER_SQLITE_INCLUDE_DIR sqlite3.h)
if(NILCOMPILER_SQLITE_INCLUDE_DIR)
    target_include_directories(nilruntime_objects PRIVATE "${NILCOMPILER_SQLITE_INCLUDE_DIR}")
    target_compile_definitions(nilruntime_objects PRIVATE NIL_DB_SQLITE=1)
endif()

# PostgreSQL headers enable the dynamically loaded POSIX db2 provider.
# The old experimental VM-only db.* dispatcher is no longer compiled.
find_path(NILCOMPILER_POSTGRESQL_INCLUDE_DIR libpq-fe.h
    PATHS
        "C:/Program Files/PostgreSQL/18/include"
        "C:/Program Files/PostgreSQL/17/include"
        "C:/Program Files/PostgreSQL/16/include"
        /usr/include/postgresql
        /usr/local/include/postgresql
        /usr/include
)
if(NILCOMPILER_POSTGRESQL_INCLUDE_DIR AND NOT WIN32)
    target_include_directories(nilruntime_objects PRIVATE "${NILCOMPILER_POSTGRESQL_INCLUDE_DIR}")
    target_compile_definitions(nilruntime_objects PRIVATE NIL_DB_POSTGRESQL=1)
endif()

add_executable(nilcompiler src/main.cpp)
nil_apply_version(nilcompiler)
target_link_libraries(nilcompiler PRIVATE nilcompiler_core)
target_compile_features(nilcompiler PRIVATE cxx_std_17)
set_target_properties(nilcompiler PROPERTIES CXX_EXTENSIONS OFF)
if(BUILD_TESTING)
    add_executable(ny_preprocessor_identity_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/preprocessor_identity_tests.cpp")
    target_link_libraries(ny_preprocessor_identity_tests PRIVATE nilcompiler_core)
    nil_apply_version(ny_preprocessor_identity_tests)
    add_test(NAME ny.preprocessor-identity COMMAND ny_preprocessor_identity_tests)
    set_tests_properties(ny.preprocessor-identity PROPERTIES LABELS "tools;release;rc" TIMEOUT 30)
endif()
target_compile_definitions(
    nilcompiler
    PRIVATE
        NIL_NATIVE_LINKER_DRIVER="${NIL_NATIVE_LINKER_DRIVER}"
        NY_NATIVE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
)

if(MSVC)
    target_compile_options(nilcompiler PRIVATE /permissive)
    target_compile_definitions(nilcompiler PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# nilcompiler_core is an implementation detail shared by the in-tree tools.
# The static libnil companion is linked automatically into native programs.
# libnil.so (or the host shared-library equivalent) exposes the same C ABI for
# embedders. SDK staging places both beside the tools before package-sdk.py
# seals them under nillib/native; conventional installs use normal host paths.
install(TARGETS nilcompiler RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(CMAKE_INSTALL_BINDIR STREQUAL ".")
    install(TARGETS nilruntime ARCHIVE DESTINATION ".")
    install(TARGETS nilruntime_shared
        # The sealed SDK ships the static archive and shared runtime only.
        # Conventional Windows installs below retain the DLL import library.
        ARCHIVE DESTINATION "." EXCLUDE_FROM_ALL
        LIBRARY DESTINATION "."
        RUNTIME DESTINATION "."
    )
else()
    install(TARGETS nilruntime nilruntime_shared
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()
