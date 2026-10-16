cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nilinterpreter VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES C CXX)
endif()

include(GNUInstallDirs)

if(NOT TARGET nilcompiler_core)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../nilcompiler"
        "${CMAKE_CURRENT_BINARY_DIR}/nilcompiler"
    )
endif()

add_executable(nilinterpreter src/main.cpp)
nil_apply_version(nilinterpreter)
target_compile_features(nilinterpreter PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(nilinterpreter PRIVATE nilcompiler_core Threads::Threads)

if(WIN32)
    target_link_libraries(nilinterpreter PRIVATE Psapi)
endif()

install(TARGETS nilinterpreter RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
