cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NilVersion.cmake")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nilschema VERSION "${NIL_VERSION_NUMERIC}" LANGUAGES CXX)
endif()

include(GNUInstallDirs)

add_executable(nilschema src/main.cpp)
nil_apply_version(nilschema)
target_compile_features(nilschema PRIVATE cxx_std_17)

install(TARGETS nilschema RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
